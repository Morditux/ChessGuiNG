#include "heuristicsearch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// The evaluator and the search split the same helpers through this interface.
using HeuristicSearch::Board;
using HeuristicSearch::MaxMoves;
using HeuristicSearch::evaluateBoard;
using HeuristicSearch::hasAnyLegalMove;
using HeuristicSearch::isInside;
using HeuristicSearch::signFor;
using HeuristicSearch::snapshotBoard;

// Mate scores sit far below the classical score range, so a searched mate is
// never confused with a large material advantage.
constexpr int MateScore = HeuristicEval::MateScore;

namespace {

// ---------------------------------------------------------------------------
// Shallow alpha-beta search with a quiescence extension.
// ---------------------------------------------------------------------------

// Mate scores sit far below the classical score range, so a searched mate is
// never confused with a large material advantage.
constexpr int SearchInfinity = MateScore * 4;
// A score this close to MateScore means a mate was found inside the horizon.
constexpr int MateThreshold = MateScore - 256;
// Classical leaf scores are capped well below the mate range so that a huge
// material advantage can never be mistaken for a searched mate.
constexpr int MaxEvalScore = MateScore - 512;
constexpr int MaxSearchPly = 64;
// Extra material swing a capture must promise before quiescence searches it.
constexpr int DeltaPruningMargin = 200;
// A quiet move at the horizon must gain at least this much over the static
// score before it is searched.
constexpr int FutilityMargin = 200;
// How much shallower the search continues after passing the turn. Two plies is
// the usual reduction: the null move only produces a bound, and the opponent
// gets a free move in exchange.
constexpr int NullMoveReduction = 2;

// Maximum number of pseudo-legal moves in any position (218 legal, plus the
// extra promotion variants).

// Bound flags of a transposition-table entry.
constexpr int TtExact = 1;
constexpr int TtLower = 2;
constexpr int TtUpper = 3;

// Mate scores depend on the distance from the search root; the table stores
// them relative to the node so that an entry is reusable at any ply.
int valueToTable(int value, int ply) {
    if (value >= MateThreshold) {
        return value + ply;
    }
    if (value <= -MateThreshold) {
        return value - ply;
    }
    return value;
}

int valueFromTable(int value, int ply) {
    if (value >= MateThreshold) {
        return value - ply;
    }
    if (value <= -MateThreshold) {
        return value + ply;
    }
    return value;
}

// One slot of the shared transposition table. Both fields are atomic so the
// root workers can share the table without a lock; a reader only trusts an
// entry when the key it read before and after the payload matches, which rules
// out a torn read.
struct TtEntry {
    std::atomic<quint64> key{0};
    std::atomic<quint64> data{0};
};

// Persistent transposition table shared by every search and every root worker.
// It grows with the deepest search it has seen and is then reused, so a search
// no longer allocates and zero-initialises a table of its own; the entries are
// keyed by the Zobrist key and a probe only trusts them up to their depth, so
// keeping them across searches is safe. Growth retires the old buffer instead
// of freeing it, because a search running on another thread may still be
// reading it; the retired buffers are bounded by the three table sizes.
class TranspositionTable {
public:
    static TranspositionTable &instance() {
        static TranspositionTable table;
        return table;
    }

    [[nodiscard]] TtEntry *data() const {
        return table_.get();
    }

    [[nodiscard]] quint64 mask() const {
        return mask_;
    }

    // Grows the table to at least 1 << bits entries. A call that asks for a
    // smaller table keeps the current one, and the new storage is zeroed by
    // its own construction.
    void ensureSize(int bits) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (table_ != nullptr && bits_ >= bits) {
            return;
        }
        std::unique_ptr<TtEntry[]> table =
            std::make_unique<TtEntry[]>(std::size_t{1} << bits);
        if (table_ != nullptr) {
            retired_.push_back(std::move(table_));
        }
        table_ = std::move(table);
        bits_ = bits;
        mask_ = (quint64{1} << bits) - 1;
    }

    // Drops every entry. Like HeuristicEval::setParams(), this must not run
    // while a search is in flight on another thread.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        table_.reset();
        bits_ = 0;
        mask_ = 0;
    }

private:
    TranspositionTable() = default;

    std::mutex mutex_;
    std::unique_ptr<TtEntry[]> table_;
    std::vector<std::unique_ptr<TtEntry[]>> retired_;
    quint64 mask_ = 0;
    int bits_ = 0;
};

// One slot of the classical-evaluation cache. The value is the score of the
// position from White's perspective, keyed by its Zobrist key; as for the
// transposition table, the key is written last so a reader that sees it also
// sees the matching payload.
struct EvalCacheEntry {
    std::atomic<quint64> key{0};
    std::atomic<qint32> value{0};
};

// Direct-mapped cache of `evaluateBoard`. The leaf evaluation is the hottest
// work the search does, and the same positions come back both through
// transpositions and through the successive searches the evaluation curve
// runs, so the score is worth keeping. The active weights are part of the
// value, which is why setParams(), resetParams() and clearSearchCache() drop
// the cache together with the transposition table.
class EvalCache {
public:
    static EvalCache &instance() {
        static EvalCache cache;
        return cache;
    }

    bool lookup(quint64 key, int *value) const {
        const EvalCacheEntry &entry = entries_[key & Mask];
        const quint64 storedKey = entry.key.load(std::memory_order_acquire);
        if (storedKey != key) {
            return false;
        }
        const qint32 storedValue = entry.value.load(std::memory_order_acquire);
        if (entry.key.load(std::memory_order_acquire) != storedKey) {
            return false;
        }
        *value = storedValue;
        return true;
    }

    void store(quint64 key, int value) {
        EvalCacheEntry &entry = entries_[key & Mask];
        entry.key.store(0, std::memory_order_release);
        entry.value.store(static_cast<qint32>(value), std::memory_order_release);
        entry.key.store(key, std::memory_order_release);
    }

    void clear() {
        for (EvalCacheEntry &entry : entries_) {
            entry.key.store(0, std::memory_order_release);
            entry.value.store(0, std::memory_order_release);
        }
    }

private:
    static constexpr quint64 Bits = 16;
    static constexpr quint64 Mask = (quint64{1} << Bits) - 1;
    std::array<EvalCacheEntry, static_cast<std::size_t>(quint64{1} << Bits)>
        entries_{};
};

// Heuristic move ordering state and a pointer to the table. Killers and
// history are per root worker so that their effect stays out of the result;
// the shared table only ever returns exact bounds, so the value does not
// depend on thread scheduling either.
// Everything a search needs to enforce its limits. The deadline is resolved
// once, and `limitReached` lets the root workers stop each other as soon as one
// of them spends the budget.
struct SearchControl {
    const HeuristicEval::SearchLimits *limits = nullptr;
    std::atomic<bool> *limitReached = nullptr;
    std::chrono::steady_clock::time_point deadline{};
    bool hasDeadline = false;
    // Nodes the completed iterations already spent, so that the node budget
    // covers the whole call and not each iteration on its own.
    int nodesAlreadySearched = 0;
};

struct SearchState {
    Rules::Move killers[MaxSearchPly][2]{};
    int history[2][64][64] = {};
    TtEntry *table = nullptr;
    quint64 tableMask = 0;
    // Limit tracking. The node counter always runs so that a search without
    // limits still reports the work it did; `control` is null in that case.
    const SearchControl *control = nullptr;
    int nodes = 0;
    bool stopped = false;
    // True while a null move is being searched, which forbids a second one in a
    // row: two passes would prove nothing about the position.
    bool nullMove = false;
    // Zobrist key of the position at each ply of the line being searched,
    // used to recognise a repetition inside the search. The root position is
    // seeded at index 0; quiescence is entered at the ply of the node that
    // called it, hence the extra slot.
    quint64 pathKeys[MaxSearchPly + 1]{};
};

// The clock and the cancellation callback cost more than a counter, so they are
// only consulted every this many nodes.
constexpr int LimitsPollInterval = 1024;

// Counts the node and reports whether the search has to stop now. The first
// worker to spend the budget tells the others through the shared flag.
bool limitsReached(SearchState &state) {
    if (state.stopped) {
        return true;
    }
    ++state.nodes;

    const SearchControl *control = state.control;
    if (control == nullptr) {
        return false;
    }
    if (control->limitReached != nullptr &&
        control->limitReached->load(std::memory_order_relaxed)) {
        state.stopped = true;
        return true;
    }

    const HeuristicEval::SearchLimits *limits = control->limits;
    const bool nodeBudgetSpent =
        limits != nullptr && limits->maxNodes > 0 &&
        control->nodesAlreadySearched + state.nodes >= limits->maxNodes;
    // The first node is polled too, so a cancellation that is already pending
    // stops the search before it does any work.
    const bool pollNow =
        state.nodes == 1 || (state.nodes % LimitsPollInterval) == 0;
    const bool timedOut =
        pollNow && control->hasDeadline &&
        std::chrono::steady_clock::now() >= control->deadline;
    const bool cancelled =
        pollNow && limits != nullptr && limits->shouldStop &&
        limits->shouldStop();
    if (!nodeBudgetSpent && !timedOut && !cancelled) {
        return false;
    }

    state.stopped = true;
    if (control->limitReached != nullptr) {
        control->limitReached->store(true, std::memory_order_relaxed);
    }
    return true;
}

quint64 packMove(const Rules::Move &move) {
    if (move.from.row < 0) {
        return 0;
    }
    const quint64 from = static_cast<quint64>(move.from.row * 8 + move.from.column);
    const quint64 to = static_cast<quint64>(move.to.row * 8 + move.to.column);
    const quint64 promotion = move.promotion == Rules::PieceType::None
                                  ? 0
                                  : static_cast<quint64>(move.promotion);
    return 1 + (from | (to << 6) | (promotion << 12));
}

Rules::Move unpackMove(quint64 packed) {
    Rules::Move move;
    if (packed == 0) {
        return move;
    }
    const quint64 value = packed - 1;
    const quint64 from = value & 0x3F;
    const quint64 to = (value >> 6) & 0x3F;
    const quint64 promotion = (value >> 12) & 0x7;
    move.from = {static_cast<int>(from / 8), static_cast<int>(from % 8)};
    move.to = {static_cast<int>(to / 8), static_cast<int>(to % 8)};
    move.promotion = static_cast<Rules::PieceType>(promotion);
    return move;
}

quint64 packEntry(int score, int depth, int flag, const Rules::Move &move) {
    const quint64 scoreBits = static_cast<quint64>(static_cast<quint32>(score));
    const quint64 depthBits = static_cast<quint64>(depth & 0xFF);
    const quint64 flagBits = static_cast<quint64>(flag & 0x3);
    const quint64 moveBits = packMove(move);
    return scoreBits | (depthBits << 32) | (flagBits << 40) |
           (moveBits << 42);
}

// Reads the entry for `key`. Returns true when it provides a score cutoff;
// `bestMove` and `hasBestMove` are filled for move ordering either way.
bool probeTable(const SearchState &state, quint64 key, int ply, int depth,
                int alpha, int beta, Rules::Move &bestMove, bool &hasBestMove,
                int &value) {
    if (state.table == nullptr) {
        return false;
    }
    const TtEntry &entry = state.table[key & state.tableMask];
    const quint64 storedKey = entry.key.load(std::memory_order_acquire);
    if (storedKey != key) {
        return false;
    }
    const quint64 data = entry.data.load(std::memory_order_acquire);
    if (entry.key.load(std::memory_order_acquire) != storedKey) {
        return false;
    }

    const int score = static_cast<int>(static_cast<qint32>(data & 0xFFFFFFFF));
    const int storedDepth = static_cast<int>((data >> 32) & 0xFF);
    const int flag = static_cast<int>((data >> 40) & 0x3);
    const Rules::Move storedMove = unpackMove(data >> 42);
    if (storedMove.from.row >= 0) {
        bestMove = storedMove;
        hasBestMove = true;
    }

    if (storedDepth < depth) {
        return false;
    }
    const int tableValue = valueFromTable(score, ply);
    if (flag == TtExact) {
        value = tableValue;
        return true;
    }
    if (flag == TtLower && tableValue >= beta) {
        value = tableValue;
        return true;
    }
    if (flag == TtUpper && tableValue <= alpha) {
        value = tableValue;
        return true;
    }
    return false;
}

void storeTable(SearchState &state, quint64 key, int depth, int score, int flag,
                const Rules::Move &move) {
    if (state.table == nullptr) {
        return;
    }
    TtEntry &entry = state.table[key & state.tableMask];

    // Depth-preferred replacement: a shallow entry must not evict a deeper one
    // from the slot, since the deeper bound is the valuable one. The key is
    // read twice around the payload so that a torn write by another worker is
    // treated as an empty slot rather than as a depth.
    const quint64 storedKey = entry.key.load(std::memory_order_acquire);
    if (storedKey == key) {
        const quint64 storedData = entry.data.load(std::memory_order_acquire);
        if (entry.key.load(std::memory_order_acquire) == storedKey) {
            const int storedDepth =
                static_cast<int>((storedData >> 32) & 0xFF);
            if (storedDepth > depth) {
                return;
            }
        }
    }

    // Store the payload before the key so that a reader which sees the new key
    // also sees the matching payload.
    entry.key.store(0, std::memory_order_release);
    entry.data.store(packEntry(score, depth, flag, move),
                     std::memory_order_release);
    entry.key.store(key, std::memory_order_release);
}

// Everything the search needs to know about a move besides its legality:
// ordering score, whether it is a capture or promotion (and so worth searching
// in the quiescence) and the material it wins outright for delta pruning. The
// two squares are inspected once, where the old code looked them up again for
// each of those questions.
struct MoveOrdering {
    int score = 0;
    bool tactical = false;
    int gain = 0;
};

MoveOrdering computeMoveOrdering(const Rules &rules, const Rules::Move &move,
                                 const SearchState &state, int ply,
                                 const Rules::Move &tableMove,
                                 bool hasTableMove) {
    MoveOrdering ordering;

    const auto target = rules.pieceAt(move.to);
    const auto from = rules.pieceAt(move.from);
    // En passant: a pawn moving diagonally onto an otherwise empty square.
    const bool enPassant = !target.has_value() && from.has_value() &&
                           from->type == Rules::PieceType::Pawn &&
                           move.from.column != move.to.column;

    ordering.tactical = move.promotion != Rules::PieceType::None ||
                        target.has_value() || enPassant;
    if (target.has_value()) {
        ordering.gain += HeuristicEval::pieceValue(target->type);
    } else if (enPassant) {
        ordering.gain += HeuristicEval::params().pawnValue;
    }
    if (move.promotion != Rules::PieceType::None) {
        ordering.gain += HeuristicEval::pieceValue(move.promotion);
    }

    if (hasTableMove && move == tableMove) {
        ordering.score = 2'000'000;
    } else if (target.has_value()) {
        ordering.score =
            1'000'000 + 10 * HeuristicEval::pieceValue(target->type);
        if (from.has_value()) {
            ordering.score -= HeuristicEval::pieceValue(from->type);
        }
    } else if (move.promotion != Rules::PieceType::None) {
        ordering.score = 900'000 + HeuristicEval::pieceValue(move.promotion);
    } else if (ply < MaxSearchPly && move == state.killers[ply][0]) {
        ordering.score = 800'000;
    } else if (ply < MaxSearchPly && move == state.killers[ply][1]) {
        ordering.score = 799'000;
    } else {
        const int color = rules.currentPlayer() == Rules::Color::White ? 0 : 1;
        ordering.score =
            state.history[color][move.from.row * 8 + move.from.column]
                         [move.to.row * 8 + move.to.column];
    }
    return ordering;
}

// Pieces the swap below may trade before it gives up, which bounds the loop.
constexpr int MaxSeePlies = 32;

// True when the piece sitting on `from` attacks `to` on this board, which is a
// plain copy the caller owns, so no pin or turn rule is consulted.
bool pieceAttacks(const Board &board, Rules::Position from, Rules::Position to) {
    const auto piece = board[from.row][from.column];
    if (!piece.has_value()) {
        return false;
    }
    const int rowStep = to.row - from.row;
    const int columnStep = to.column - from.column;
    const int rowDistance = std::abs(rowStep);
    const int columnDistance = std::abs(columnStep);
    switch (piece->type) {
    case Rules::PieceType::Pawn:
        // A pawn captures one step diagonally forward, and "forward" follows
        // its colour.
        return columnDistance == 1 &&
               rowStep == (piece->color == Rules::Color::White ? -1 : 1);
    case Rules::PieceType::Knight:
        return (rowDistance == 2 && columnDistance == 1) ||
               (rowDistance == 1 && columnDistance == 2);
    case Rules::PieceType::King:
        return rowDistance <= 1 && columnDistance <= 1 &&
               (rowDistance != 0 || columnDistance != 0);
    case Rules::PieceType::Bishop:
        if (rowDistance != columnDistance) {
            return false;
        }
        break;
    case Rules::PieceType::Rook:
        if (rowStep != 0 && columnStep != 0) {
            return false;
        }
        break;
    case Rules::PieceType::Queen:
        if (rowDistance != columnDistance && rowStep != 0 && columnStep != 0) {
            return false;
        }
        break;
    case Rules::PieceType::None:
        return false;
    }

    // A sliding piece needs an empty path: the walk stops on the first occupied
    // square, which is the target when nothing stands in between.
    const int rowDirection = (rowStep > 0) - (rowStep < 0);
    const int columnDirection = (columnStep > 0) - (columnStep < 0);
    int row = from.row + rowDirection;
    int column = from.column + columnDirection;
    while (row != to.row || column != to.column) {
        if (!isInside(row, column) || board[row][column].has_value()) {
            return false;
        }
        row += rowDirection;
        column += columnDirection;
    }
    return true;
}

// Cheapest piece of `side` that attacks `to`, kings left out: a king taking
// back on a defended square is not a legal exchange, and counting it would make
// every defended piece look worthless.
std::optional<Rules::Position> leastValuableAttacker(const Board &board,
                                                     Rules::Position to,
                                                     Rules::Color side) {
    std::optional<Rules::Position> best;
    int bestValue = 0;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &square = board[row][column];
            if (!square.has_value() || square->color != side ||
                square->type == Rules::PieceType::King) {
                continue;
            }
            const int value = HeuristicEval::pieceValue(square->type);
            if (best.has_value() && value >= bestValue) {
                continue;
            }
            if (pieceAttacks(board, {row, column}, to)) {
                best = Rules::Position{row, column};
                bestValue = value;
            }
        }
    }
    return best;
}

// Material the exchange started by `move` is worth for the mover, by the classic
// swap algorithm: the cheapest available piece always takes back, and the
// balance is folded back so that either side may stop the exchange. The board is
// a local copy, so the caller's position is never touched; kings never take
// part, which is the usual approximation, and en passant removes the pawn that
// is not on the target square.
bool moveCaptures(const Rules &rules, const Rules::Move &move) {
    if (rules.pieceAt(move.to).has_value()) {
        return true;
    }
    const auto from = rules.pieceAt(move.from);
    return from.has_value() && from->type == Rules::PieceType::Pawn &&
           move.from.column != move.to.column;
}

int seeValue(const Rules &rules, const Rules::Move &move) {
    Board board = snapshotBoard(rules);

    const auto &victim = board[move.to.row][move.to.column];
    const auto &mover = board[move.from.row][move.from.column];
    const bool enPassant = !victim.has_value() && mover.has_value() &&
                           mover->type == Rules::PieceType::Pawn &&
                           move.from.column != move.to.column;
    const int victimValue = victim.has_value()
                                ? HeuristicEval::pieceValue(victim->type)
                                : (enPassant ? HeuristicEval::pieceValue(
                                                   Rules::PieceType::Pawn)
                                             : 0);

    // Play the capture on the local board, promoting the pawn if it does.
    Rules::Piece landing = mover.value_or(Rules::Piece{});
    if (move.promotion != Rules::PieceType::None) {
        landing.type = move.promotion;
    } else if (landing.type == Rules::PieceType::Pawn &&
               (move.to.row == 0 || move.to.row == 7)) {
        landing.type = Rules::PieceType::Queen;
    }
    board[move.from.row][move.from.column] = std::nullopt;
    board[move.to.row][move.to.column] = landing;
    if (enPassant) {
        board[move.from.row][move.to.column] = std::nullopt;
    }

    int gain[MaxSeePlies + 1] = {};
    gain[0] = victimValue;
    int depth = 0;
    Rules::Color side = rules.currentPlayer() == Rules::Color::White
                            ? Rules::Color::Black
                            : Rules::Color::White;
    while (depth < MaxSeePlies) {
        const auto attacker = leastValuableAttacker(board, move.to, side);
        if (!attacker.has_value()) {
            break;
        }
        ++depth;
        const int onSquare =
            HeuristicEval::pieceValue(board[move.to.row][move.to.column]->type);
        gain[depth] = onSquare - gain[depth - 1];
        board[move.to.row][move.to.column] =
            board[attacker->row][attacker->column];
        board[attacker->row][attacker->column] = std::nullopt;
        side = side == Rules::Color::White ? Rules::Color::Black
                                           : Rules::Color::White;
    }
    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }
    return gain[0];
}

// Classical evaluation seen from the side to move, without the terminal checks
// the search performs itself, capped below the mate range. The score is a
// function of the position alone (the caller's `inCheck` is that of the side to
// move, which the key already covers), so it is read from the cache and only
// computed on a miss.
int leafScore(const Rules &rules, bool inCheck) {
    const Rules::Color side = rules.currentPlayer();
    const quint64 key = rules.zobristKey();
    int white = 0;
    if (!EvalCache::instance().lookup(key, &white)) {
        const Board board = snapshotBoard(rules);
        white = evaluateBoard(board, side, inCheck, nullptr);
        EvalCache::instance().store(key, white);
    }
    return std::clamp(signFor(side) * white, -MaxEvalScore, MaxEvalScore);
}

// Defined further down; the draw test needs it to tell a checkmate from a
// position that is merely in check when the fifty-move clock has run out.

// Null-move pruning is unsound in a position that can only get worse by being
// forced to move, so it is limited to sides that still hold a piece next to
// their pawns and their king.
bool hasNonPawnMaterial(const Rules &rules, Rules::Color side) {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto piece = rules.pieceAt({row, column});
            if (!piece.has_value() || piece->color != side) {
                continue;
            }
            if (piece->type == Rules::PieceType::Knight ||
                piece->type == Rules::PieceType::Bishop ||
                piece->type == Rules::PieceType::Rook ||
                piece->type == Rules::PieceType::Queen) {
                return true;
            }
        }
    }
    return false;
}

// Draws that depend on the line that reached the position: the fifty-move rule
// and a repetition of a position already met by the same side to move earlier
// in the search. Such a node returns a draw score and is never stored in the
// transposition table, because the value does not belong to the position
// alone. The repetition scan uses the Zobrist keys of the current line, so the
// game history before the search root is not consulted.
bool isPathDraw(const Rules &rules, SearchState &state, int ply, bool inCheck) {
    const quint64 key = rules.zobristKey();
    state.pathKeys[ply] = key;

    // A side that is checkmated is not rescued by the fifty-move rule, so the
    // terminal test keeps priority: only a position with a legal move is a
    // draw here.
    if (rules.isFiftyMoveRule() && (!inCheck || hasAnyLegalMove(rules))) {
        return true;
    }

    for (int previous = ply - 2; previous >= 0; previous -= 2) {
        if (state.pathKeys[previous] == key) {
            return true;
        }
    }
    return false;
}

// Selects the not-yet-searched move with the highest ordering score and swaps
// it into position `index`, keeping the parallel tactical/gain arrays aligned.
void selectNextMove(Rules::Move *moves, int *scores, int index, int count,
                    bool *tactical = nullptr, int *gain = nullptr) {
    int best = index;
    for (int j = index + 1; j < count; ++j) {
        if (scores[j] > scores[best]) {
            best = j;
        }
    }
    std::swap(moves[index], moves[best]);
    std::swap(scores[index], scores[best]);
    if (tactical != nullptr) {
        std::swap(tactical[index], tactical[best]);
    }
    if (gain != nullptr) {
        std::swap(gain[index], gain[best]);
    }
}

int quiescence(Rules &rules, int depth, int alpha, int beta, int ply,
               SearchState &state) {
    if (limitsReached(state)) {
        return 0;
    }

    const Rules::Color side = rules.currentPlayer();
    const bool inCheck = rules.isInCheck(side);

    if (isPathDraw(rules, state, ply, inCheck)) {
        return 0;
    }

    int best = -SearchInfinity;
    if (!inCheck) {
        const int standPat = leafScore(rules, false);
        if (standPat >= beta) {
            return standPat;
        }
        if (standPat > alpha) {
            alpha = standPat;
        }
        best = standPat;
    }

    if (depth <= 0 || ply >= MaxSearchPly) {
        return inCheck ? leafScore(rules, true) : best;
    }

    Rules::Move moves[MaxMoves];
    int scores[MaxMoves];
    bool tactical[MaxMoves];
    int gains[MaxMoves];
    int count = rules.generatePseudoLegalMoves(moves, MaxMoves);
    for (int i = 0; i < count; ++i) {
        const MoveOrdering ordering =
            computeMoveOrdering(rules, moves[i], state, ply, Rules::Move{},
                                false);
        scores[i] = ordering.score;
        tactical[i] = ordering.tactical;
        gains[i] = ordering.gain;
    }
    // Out of check every quiet move is searched anyway, so drop them before
    // ordering and searching rather than skipping them one by one.
    if (!inCheck) {
        int kept = 0;
        for (int i = 0; i < count; ++i) {
            if (!tactical[i]) {
                continue;
            }
            moves[kept] = moves[i];
            scores[kept] = scores[i];
            gains[kept] = gains[i];
            ++kept;
        }
        count = kept;
    }

    bool anyLegal = false;
    for (int i = 0; i < count; ++i) {
        selectNextMove(moves, scores, i, count, tactical, gains);
        const Rules::Move move = moves[i];
        if (!inCheck) {
            // Delta pruning: a capture that cannot raise the score even in the
            // best case is not worth searching.
            if (best + gains[i] + DeltaPruningMargin <= alpha) {
                continue;
            }
            // Static exchange evaluation: when the swap on the target square
            // loses material, the capture is not forced and searching it only
            // grows the tree.
            if (gains[i] > 0 && moveCaptures(rules, move) &&
                seeValue(rules, move) < 0) {
                continue;
            }
        }

        Rules::Undo undo;
        if (!rules.makeMove(move, undo)) {
            continue;
        }
        anyLegal = true;
        const int score =
            -quiescence(rules, depth - 1, -beta, -alpha, ply + 1, state);
        rules.unmakeMove(move, undo);

        if (score >= beta) {
            return score;
        }
        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    if (!anyLegal && inCheck) {
        return -MateScore + ply;
    }
    return best;
}

int negamax(Rules &rules, int depth, int quiescenceDepth, int alpha, int beta,
            int ply, SearchState &state) {
    if (depth <= 0 || ply >= MaxSearchPly) {
        return quiescence(rules, quiescenceDepth, alpha, beta, ply, state);
    }

    if (limitsReached(state)) {
        return 0;
    }

    const Rules::Color side = rules.currentPlayer();
    const bool inCheck = rules.isInCheck(side);

    // A draw is worth nothing even when the classical score is huge, and its
    // value is path dependent, so it never reaches the transposition table.
    if (isPathDraw(rules, state, ply, inCheck)) {
        return 0;
    }

    const bool useTable = state.table != nullptr;
    const quint64 key = useTable ? rules.zobristKey() : 0;
    Rules::Move tableMove{};
    bool hasTableMove = false;
    int tableValue = 0;
    if (probeTable(state, key, ply, depth, alpha, beta, tableMove,
                   hasTableMove, tableValue)) {
        return tableValue;
    }

    // Null-move pruning: when the opponent cannot reach beta even after being
    // handed a free move, every real move keeps this position at least as good,
    // so the node is cut without searching any of them. It is skipped in check,
    // in a position with nothing but pawns left (where being forced to move is
    // the whole problem) and near the mate range, where a bound would be a lie.
    if (!inCheck && depth >= 3 && std::abs(beta) < MateThreshold &&
        !state.nullMove && hasNonPawnMaterial(rules, side)) {
        Rules::NullUndo nullUndo;
        if (rules.makeNullMove(nullUndo)) {
            state.nullMove = true;
            const int score = -negamax(rules, depth - 1 - NullMoveReduction,
                                       quiescenceDepth, -beta, -beta + 1,
                                       ply + 1, state);
            state.nullMove = false;
            rules.unmakeNullMove(nullUndo);
            if (state.stopped) {
                return 0;
            }
            if (score >= beta) {
                return beta;
            }
        }
    }

    const int alphaOriginal = alpha;
    Rules::Move moves[MaxMoves];
    int scores[MaxMoves];
    bool tacticalFlags[MaxMoves];
    int gains[MaxMoves];
    const int count = rules.generatePseudoLegalMoves(moves, MaxMoves);
    for (int i = 0; i < count; ++i) {
        const MoveOrdering ordering =
            computeMoveOrdering(rules, moves[i], state, ply, tableMove,
                                hasTableMove);
        scores[i] = ordering.score;
        tacticalFlags[i] = ordering.tactical;
        gains[i] = ordering.gain;
    }

    bool anyLegal = false;
    const bool futileNode = depth == 1 && !inCheck;
    int staticEval = 0;
    if (futileNode) {
        staticEval = leafScore(rules, inCheck);
    }
    int best = futileNode ? staticEval : -SearchInfinity;
    Rules::Move bestMove{};
    for (int i = 0; i < count; ++i) {
        selectNextMove(moves, scores, i, count, tacticalFlags, gains);
        const Rules::Move move = moves[i];
        const bool tactical = tacticalFlags[i];

        // Futility pruning: at the horizon a quiet move that cannot lift the
        // score past alpha is not worth searching.
        if (futileNode && !tactical && std::abs(alpha) < MateThreshold &&
            staticEval + FutilityMargin <= alpha) {
            continue;
        }

        // A capture that loses material is not worth a full search at the
        // horizon either: the quiescence search would never play it.
        if (futileNode && tactical && !inCheck && gains[i] > 0 &&
            moveCaptures(rules, move) && seeValue(rules, move) < 0) {
            continue;
        }

        Rules::Undo undo;
        if (!rules.makeMove(move, undo)) {
            continue;
        }
        // The first move searched gets the full window; every later one is only
        // scouted with a null window and re-searched when it beats alpha, which
        // is what makes alpha-beta cut more of the tree.
        const bool firstMove = !anyLegal;
        anyLegal = true;
        // A move that gives check may start a forcing line, so it is searched one
        // ply deeper and is never reduced. A check played while already in check
        // is only an escape, and leaving those unextended is what keeps a series
        // of checks from holding the remaining depth constant.
        const bool givesCheck = rules.isInCheck(rules.currentPlayer());
        int childDepth = depth - 1;
        int reduction = 0;
        // Late move reductions: quiet moves far down the list are searched a ply
        // shallower and only re-searched in full when they beat alpha. The check
        // test is shared with the extension below, so it is not paid twice.
        if (depth >= 3 && !tactical && !inCheck && i >= 3 && !givesCheck &&
            alpha > -MateThreshold && beta < MateThreshold) {
            reduction = i >= 8 ? 2 : 1;
            if (reduction > childDepth) {
                reduction = childDepth;
            }
            childDepth -= reduction;
        }
        const int extension = givesCheck && !inCheck ? 1 : 0;
        childDepth += extension;

        int score = 0;
        if (firstMove) {
            score = -negamax(rules, childDepth, quiescenceDepth, -beta, -alpha,
                             ply + 1, state);
        } else {
            score = -negamax(rules, childDepth, quiescenceDepth, -alpha - 1,
                             -alpha, ply + 1, state);
        }
        if (reduction > 0 && score > alpha) {
            score = -negamax(rules, depth - 1 + extension, quiescenceDepth,
                             -alpha - 1, -alpha, ply + 1, state);
        }
        if (!firstMove && score > alpha && score < beta) {
            score = -negamax(rules, depth - 1 + extension, quiescenceDepth,
                             -beta, -alpha, ply + 1, state);
        }
        rules.unmakeMove(move, undo);

        if (score > best) {
            best = score;
            bestMove = move;
        }
        if (score >= beta) {
            if (!tactical && ply < MaxSearchPly) {
                if (!(state.killers[ply][0] == move)) {
                    state.killers[ply][1] = state.killers[ply][0];
                    state.killers[ply][0] = move;
                }
                const int color = side == Rules::Color::White ? 0 : 1;
                state.history[color][move.from.row * 8 + move.from.column]
                             [move.to.row * 8 + move.to.column] += depth * depth;
            }
            break;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    if (!anyLegal) {
        if (inCheck) {
            return -MateScore + ply;
        }
        // Every quiet move was pruned at the horizon; report the static score
        // rather than a stalemate, since the moves were never tested.
        return futileNode ? staticEval : 0;
    }

    // A value computed after a limit interrupted the subtree is not a value of
    // this position, so it never reaches the table.
    if (useTable && !state.stopped) {
        const int flag = best <= alphaOriginal
                             ? TtUpper
                             : (best >= beta ? TtLower : TtExact);
        storeTable(state, key, depth, valueToTable(best, ply), flag, bestMove);
    }
    return best;
}

// Searches a fixed subset of the root moves. Threads share a best-so-far alpha
// so that a good move found by one thread prunes the others. Alpha-beta only
// ever returns values bounded by the true score, and the best move is never
// cut off before it has raised alpha to the exact minimax value, so the final
// maximum does not depend on thread scheduling.
struct RootResult {
    int score = -SearchInfinity;
    int nodes = 0;
    bool aborted = false;
    Rules::Move move{};
};

RootResult searchRootChunk(const Rules &rules, const Rules::Move *moves,
                           const std::vector<int> &indices, int depth,
                           int quiescenceDepth, int alpha, int beta,
                           std::atomic<int> &sharedAlpha,
                           std::atomic<bool> &stop, TtEntry *table,
                           quint64 tableMask, const SearchControl *control) {
    Rules root = rules.detachedCopy();
    SearchState state;
    state.table = table;
    state.tableMask = tableMask;
    state.control = control;
    // The root position opens the repetition line every worker walks.
    state.pathKeys[0] = root.zobristKey();

    RootResult result;
    for (const int index : indices) {
        if (stop.load(std::memory_order_relaxed) || state.stopped) {
            break;
        }
        const Rules::Move move = moves[index];
        Rules::Undo undo;
        if (!root.makeMove(move, undo)) {
            continue;
        }
        const int windowAlpha = sharedAlpha.load(std::memory_order_relaxed);
        const int score = -negamax(root, depth - 1, quiescenceDepth, -beta,
                                   -windowAlpha, 1, state);
        root.unmakeMove(move, undo);

        if (score > result.score) {
            result.score = score;
            result.move = move;
        }
        int current = sharedAlpha.load(std::memory_order_relaxed);
        while (score > current &&
               !sharedAlpha.compare_exchange_weak(
                   current, score, std::memory_order_relaxed)) {
        }
        if (score >= beta) {
            // The root fails high: the aspiration window has to be widened.
            stop.store(true, std::memory_order_relaxed);
            break;
        }
    }
    result.nodes = state.nodes;
    result.aborted = state.stopped;
    return result;
}

// Upper bound on the number of root workers. Zero means "derive it from the
// hardware and the environment"; a positive value is the explicit cap set by
// HeuristicEval::setSearchThreads(), which is what makes a search deterministic
// regardless of the pool's actual size.
std::atomic<int> searchThreadOverride{0};

// Threads a search may use by default. CHESSGUI_EVAL_THREADS is read on every
// call, so the value can be changed between searches.
int hardwareThreads() {
    unsigned count = std::thread::hardware_concurrency();
    if (const char *overrideThreads = std::getenv("CHESSGUI_EVAL_THREADS")) {
        const int requested = std::atoi(overrideThreads);
        if (requested > 0) {
            count = static_cast<unsigned>(requested);
        }
    }
    return static_cast<int>(std::min(count == 0 ? 1u : count, 16u));
}

int rootWorkerLimit() {
    const int requested = searchThreadOverride.load(std::memory_order_relaxed);
    if (requested > 0) {
        return std::min(requested, 16);
    }
    return hardwareThreads();
}

// A small persistent pool. Iterative deepening runs several searches in a row,
// so creating and joining threads per iteration used to dominate the shallow
// ones; the pool is created once and reused. It is created on the first search
// that actually wants more than one worker, so a shallow evaluation never pays
// for the threads.
class SearchPool {
public:
    static SearchPool &instance() {
        static SearchPool pool;
        return pool;
    }

    [[nodiscard]] int size() const {
        return static_cast<int>(threads_.size());
    }

    // Runs `jobCount` independent jobs (indexed 0..jobCount-1) and blocks
    // until they all finish. Calls are serialised so the pool can be shared.
    void run(int jobCount, const std::function<void(int)> &job) {
        std::lock_guard<std::mutex> serialise(runMutex_);
        if (jobCount <= 0) {
            return;
        }
        if (size() <= 1) {
            for (int index = 0; index < jobCount; ++index) {
                job(index);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_ = &job;
            jobCount_ = jobCount;
            next_ = 0;
            remaining_ = jobCount;
            ++generation_;
        }
        wakeWorkers_.notify_all();

        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return remaining_ == 0; });
    }

private:
    SearchPool() {
        const unsigned count = static_cast<unsigned>(hardwareThreads());
        threads_.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            threads_.emplace_back([this] { workerLoop(); });
        }
    }

    ~SearchPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
            ++generation_;
        }
        wakeWorkers_.notify_all();
        for (std::thread &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void workerLoop() {
        int seenGeneration = 0;
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            wakeWorkers_.wait(lock, [this, seenGeneration] {
                return generation_ != seenGeneration;
            });
            if (quit_) {
                return;
            }
            seenGeneration = generation_;
            const std::function<void(int)> *job = job_;
            const int count = jobCount_;
            lock.unlock();

            while (true) {
                int index;
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    if (next_ >= count) {
                        break;
                    }
                    index = next_++;
                }
                (*job)(index);

                std::lock_guard<std::mutex> guard(mutex_);
                if (--remaining_ == 0) {
                    done_.notify_one();
                }
            }
            lock.lock();
        }
    }

    std::vector<std::thread> threads_;
    std::mutex runMutex_;
    std::mutex mutex_;
    std::condition_variable wakeWorkers_;
    std::condition_variable done_;
    const std::function<void(int)> *job_ = nullptr;
    int jobCount_ = 0;
    int next_ = 0;
    int remaining_ = 0;
    int generation_ = 0;
    bool quit_ = false;
};

RootResult parallelRootSearch(const Rules &rules, const Rules::Move *moves,
                              int moveCount, int depth, int quiescenceDepth,
                              int alpha, int beta, TtEntry *table,
                              quint64 tableMask, const SearchControl *control) {
    // The worker budget is decided before the pool exists, so a search that
    // does not need threads never creates any.
    const int workers = std::min(rootWorkerLimit(), std::min(moveCount, 16));

    std::atomic<int> sharedAlpha{alpha};
    std::atomic<bool> stop{false};

    if (depth < 3 || workers <= 1) {
        std::vector<int> indices(static_cast<std::size_t>(moveCount));
        for (int i = 0; i < moveCount; ++i) {
            indices[static_cast<std::size_t>(i)] = i;
        }
        return searchRootChunk(rules, moves, indices, depth, quiescenceDepth,
                               alpha, beta, sharedAlpha, stop, table,
                               tableMask, control);
    }

    SearchPool &pool = SearchPool::instance();
    std::vector<std::vector<int>> chunks(static_cast<std::size_t>(workers));
    for (int i = 0; i < moveCount; ++i) {
        chunks[static_cast<std::size_t>(i % workers)].push_back(i);
    }
    std::vector<RootResult> results(static_cast<std::size_t>(workers));

    pool.run(workers, [&rules, moves, &chunks, &results, depth, quiescenceDepth,
                       alpha, beta, &sharedAlpha, &stop, table, tableMask,
                       control](int worker) {
        results[static_cast<std::size_t>(worker)] = searchRootChunk(
            rules, moves, chunks[static_cast<std::size_t>(worker)], depth,
            quiescenceDepth, alpha, beta, sharedAlpha, stop, table, tableMask,
            control);
    });

    RootResult best;
    for (const RootResult &result : results) {
        best.nodes += result.nodes;
        best.aborted = best.aborted || result.aborted;
        if (result.score > best.score) {
            best.score = result.score;
            best.move = result.move;
        }
    }
    return best;
}



// Longest line read back from the transposition table.
constexpr int MaxPvPlies = 16;

// Best line the transposition table knows after `firstMove`. The table stores
// the best move of every position it searched, so the line is read back move by
// move from the root; a move that is not legal in the position ends the line.
// Without a table the caller keeps the root move alone.
std::vector<Rules::Move> extractPrincipalVariation(const Rules &rules,
                                                   const Rules::Move &firstMove,
                                                   TtEntry *table,
                                                   quint64 tableMask) {
    std::vector<Rules::Move> line;
    if (firstMove.from.row < 0) {
        return line;
    }
    line.push_back(firstMove);
    if (table == nullptr) {
        return line;
    }

    Rules position = rules.detachedCopy();
    Rules::Undo undo;
    if (!position.makeMove(firstMove, undo)) {
        return line;
    }

    SearchState state;
    state.table = table;
    state.tableMask = tableMask;
    for (int ply = 1; ply < MaxPvPlies; ++ply) {
        Rules::Move move{};
        bool hasMove = false;
        int value = 0;
        // A depth of zero never cuts off, so the probe only fills the move.
        probeTable(state, position.zobristKey(), ply, 0, -SearchInfinity,
                   SearchInfinity, move, hasMove, value);
        if (!hasMove || !position.isValidMove(move)) {
            break;
        }
        if (!position.makeMove(move, undo)) {
            break;
        }
        line.push_back(move);
    }
    return line;
}

} // namespace

namespace HeuristicSearch {

void clearCaches() {
    TranspositionTable::instance().clear();
    EvalCache::instance().clear();
}

} // namespace

void HeuristicEval::clearSearchCache() {
    HeuristicSearch::clearCaches();
}

void HeuristicEval::setSearchThreads(int threads) {
    searchThreadOverride.store(threads > 0 ? std::min(threads, 16) : 0,
                               std::memory_order_relaxed);
}

int HeuristicEval::searchThreads() {
    return rootWorkerLimit();
}

HeuristicEval::SearchResult HeuristicEval::search(const Rules &rules, int depth,
                                                 int quiescenceDepth) {
    return search(rules, depth, quiescenceDepth, SearchLimits{});
}

HeuristicEval::SearchResult HeuristicEval::search(const Rules &rules, int depth,
                                                 int quiescenceDepth,
                                                 const SearchLimits &limits) {
    SearchResult result;

    const Rules::Color side = rules.currentPlayer();
    // The horizon caps the search whatever the caller asks for, so a depth
    // beyond it is clamped instead of silently stopping at `MaxSearchPly`.
    depth = std::clamp(depth, 0, MaxSearchPly);
    quiescenceDepth = std::clamp(quiescenceDepth, 0, MaxSearchPly);

    Rules::Move moves[MaxMoves];
    const int moveCount = rules.generatePseudoLegalMoves(moves, MaxMoves);
    // A single legal-move scan decides checkmate and stalemate; the old
    // isCheckmate()+isStalemate() pair each ran their own full generation.
    if (moveCount == 0 || !hasAnyLegalMove(rules)) {
        if (rules.isInCheck(side)) {
            result.centipawns =
                side == Rules::Color::White ? -MateScore : MateScore;
            result.mateIn = 0;
        }
        return result;
    }
    if (rules.isInsufficientMaterial()) {
        return result;
    }
    // The root itself is drawn: every continuation keeps the draw, so there is
    // nothing to search.
    if (rules.isFiftyMoveRule() || rules.isThreefoldRepetition()) {
        return result;
    }

    SearchState orderingState;
    int scores[MaxMoves];
    for (int i = 0; i < moveCount; ++i) {
        scores[i] =
            computeMoveOrdering(rules, moves[i], orderingState, 0, Rules::Move{},
                                false)
                .score;
    }
    for (int i = 0; i < moveCount; ++i) {
        selectNextMove(moves, scores, i, moveCount);
    }

    // One table is shared by every iteration, every root worker and every
    // search. It is grown to the size the target depth asks for and then kept,
    // so only the first deep search pays for the storage.
    TtEntry *table = nullptr;
    quint64 tableMask = 0;
    if (depth >= 3) {
        const int bits = depth >= 5 ? 18 : (depth >= 4 ? 16 : 15);
        TranspositionTable &shared = TranspositionTable::instance();
        shared.ensureSize(bits);
        table = shared.data();
        tableMask = shared.mask();
    }

    // A control block is only built when there is a limit to enforce, so an
    // unlimited search pays nothing for the feature beyond the node counter.
    std::atomic<bool> limitReached{false};
    SearchControl controlStorage;
    const SearchControl *control = nullptr;
    if (limits.maxNodes > 0 || limits.maxMilliseconds > 0 || limits.shouldStop) {
        controlStorage.limits = &limits;
        controlStorage.limitReached = &limitReached;
        if (limits.maxMilliseconds > 0) {
            controlStorage.hasDeadline = true;
            controlStorage.deadline =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(limits.maxMilliseconds);
        }
        control = &controlStorage;
    }

    // Iterative deepening: each iteration reorders the root moves with the
    // previous best and fills the table for the next one. A narrow aspiration
    // window around the previous score is widened until it contains the value.
    constexpr int AspirationDelta = 200;
    int best = 0;
    bool completed = false;
    Rules::Move bestMove{};
    const int targetDepth = std::max(0, depth);
    // Re-searching every shallower depth only pays for itself from depth 4 on;
    // below that a single full-window search is cheaper.
    const int firstIteration = targetDepth < 4 ? targetDepth : 1;
    for (int iteration = firstIteration; iteration <= targetDepth;
         ++iteration) {
        int alpha = -SearchInfinity;
        int beta = SearchInfinity;
        int delta = AspirationDelta;
        if (iteration > firstIteration && std::abs(best) < MateThreshold) {
            alpha = best - delta;
            beta = best + delta;
        }

        RootResult rootResult;
        while (true) {
            if (bestMove.from.row >= 0) {
                for (int i = 0; i < moveCount; ++i) {
                    if (moves[i] == bestMove) {
                        std::swap(moves[0], moves[i]);
                        break;
                    }
                }
            }
            if (control != nullptr) {
                controlStorage.nodesAlreadySearched = result.nodes;
            }
            rootResult = parallelRootSearch(rules, moves, moveCount, iteration,
                                            quiescenceDepth, alpha, beta,
                                            table, tableMask, control);
            result.nodes += rootResult.nodes;
            if (rootResult.aborted) {
                result.aborted = true;
                break;
            }
            if (rootResult.score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(rootResult.score - delta, -SearchInfinity);
                delta += delta / 4 + 1;
                continue;
            }
            if (rootResult.score >= beta) {
                beta = std::min(rootResult.score + delta, SearchInfinity);
                delta += delta / 4 + 1;
                continue;
            }
            break;
        }

        if (result.aborted) {
            break;
        }

        best = rootResult.score;
        bestMove = rootResult.move;
        completed = true;
        result.depth = iteration;
    }

    if (!completed) {
        // A limit stopped even the first iteration: there is no score and no
        // move to report, so the caller only learns that it was cut short.
        return result;
    }

    if (best >= MateThreshold) {
        const int pliesToMate = MateScore - best;
        const int mateMoves = (pliesToMate + 1) / 2;
        result.mateIn = signFor(side) > 0 ? mateMoves : -mateMoves;
        result.centipawns =
            side == Rules::Color::White ? MateScore : -MateScore;
    } else if (best <= -MateThreshold) {
        const int pliesToMate = MateScore + best;
        const int mateMoves = (pliesToMate + 1) / 2;
        result.mateIn = signFor(side) > 0 ? -mateMoves : mateMoves;
        result.centipawns =
            side == Rules::Color::White ? -MateScore : MateScore;
    } else {
        const int whiteScore = signFor(side) * best;
        result.centipawns = std::clamp(whiteScore, -MaxEvalScore, MaxEvalScore);
    }

    if (bestMove.from.row >= 0) {
        result.bestMove = bestMove;
        result.principalVariation =
            extractPrincipalVariation(rules, bestMove, table, tableMask);
    }
    return result;
}
