#include "heuristicsearch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace HeuristicSearch;

constexpr int MateScore = HeuristicEval::MateScore;

namespace {

constexpr int SearchInfinity = MateScore * 4;
constexpr int MateThreshold = MateScore - 256;
constexpr int MaxEvalScore = MateScore - 512;
constexpr int MaxSearchPly = 64;
constexpr int DeltaPruningMargin = 200;
constexpr int FutilityMargin = 200;
constexpr int NullMoveReduction = 2;

constexpr int TtExact = 1;
constexpr int TtLower = 2;
constexpr int TtUpper = 3;

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

struct TtEntry {
    std::atomic<quint64> keyWord{0};
    std::atomic<quint64> data{0};
};

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

struct EvalCacheEntry {
    std::atomic<quint64> word{0};
};

class EvalCache {
public:
    static EvalCache &instance() {
        static EvalCache cache;
        return cache;
    }

    bool lookup(quint64 key, int *value) const {
        const quint64 word =
            entries_[key & Mask].word.load(std::memory_order_relaxed);
        const quint64 tag = (key >> 32) | 1ULL;
        if ((word >> 32) != tag) {
            return false;
        }
        *value = static_cast<qint32>(static_cast<quint32>(word));
        return true;
    }

    void store(quint64 key, int value) {
        const quint64 tag = (key >> 32) | 1ULL;
        const quint64 word =
            (tag << 32) | static_cast<quint32>(static_cast<qint32>(value));
        entries_[key & Mask].word.store(word, std::memory_order_relaxed);
    }

    void clear() {
        for (EvalCacheEntry &entry : entries_) {
            entry.word.store(0, std::memory_order_relaxed);
        }
    }

private:
    static constexpr quint64 Bits = 16;
    static constexpr quint64 Mask = (quint64{1} << Bits) - 1;
    std::array<EvalCacheEntry, static_cast<std::size_t>(quint64{1} << Bits)>
        entries_{};
};

struct SearchControl {
    const HeuristicEval::SearchLimits *limits = nullptr;
    std::atomic<bool> *limitReached = nullptr;
    std::chrono::steady_clock::time_point deadline{};
    bool hasDeadline = false;
    int nodesAlreadySearched = 0;
};

struct SearchState {
    FastMove killers[MaxSearchPly][2]{};
    int history[2][64][64] = {};
    TtEntry *table = nullptr;
    quint64 tableMask = 0;
    const SearchControl *control = nullptr;
    int nodes = 0;
    bool stopped = false;
    bool nullMove = false;
    int lastNullPly = -1;
    quint64 pathKeys[MaxSearchPly + 1]{};
};

constexpr int LimitsPollInterval = 1024;

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

constexpr quint64 packMove(FastMove move) {
    return move == NoFastMove ? 0ULL : (1ULL + static_cast<quint64>(move));
}

constexpr FastMove unpackMove(quint64 packed) {
    return packed == 0ULL ? NoFastMove : static_cast<FastMove>(packed - 1ULL);
}

quint64 packEntry(int score, int depth, int flag, FastMove move) {
    const quint64 scoreBits = static_cast<quint64>(static_cast<quint32>(score));
    const quint64 depthBits = static_cast<quint64>(depth & 0xFF);
    const quint64 flagBits = static_cast<quint64>(flag & 0x3);
    const quint64 moveBits = packMove(move);
    return scoreBits | (depthBits << 32) | (flagBits << 40) |
           (moveBits << 42);
}

bool probeTable(const SearchState &state, quint64 key, int ply, int depth,
                int alpha, int beta, FastMove &bestMove, bool &hasBestMove,
                int &value) {
    if (state.table == nullptr) {
        return false;
    }
    const TtEntry &entry = state.table[key & state.tableMask];
    const quint64 data = entry.data.load(std::memory_order_relaxed);
    const quint64 keyWord = entry.keyWord.load(std::memory_order_relaxed);
    if ((keyWord ^ data) != key) {
        return false;
    }

    const int score = static_cast<int>(static_cast<qint32>(data & 0xFFFFFFFF));
    const int storedDepth = static_cast<int>((data >> 32) & 0xFF);
    const int flag = static_cast<int>((data >> 40) & 0x3);
    const FastMove storedMove = unpackMove(data >> 42);
    if (storedMove != NoFastMove) {
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
                FastMove move) {
    if (state.table == nullptr) {
        return;
    }
    TtEntry &entry = state.table[key & state.tableMask];

    FastMove moveToStore = move;
    const quint64 storedData = entry.data.load(std::memory_order_relaxed);
    const quint64 storedKeyWord = entry.keyWord.load(std::memory_order_relaxed);
    if ((storedKeyWord ^ storedData) == key) {
        const int storedDepth = static_cast<int>((storedData >> 32) & 0xFF);
        if (storedDepth > depth) {
            return;
        }
        if (moveToStore == NoFastMove) {
            const FastMove existingMove = unpackMove(storedData >> 42);
            if (existingMove != NoFastMove) {
                moveToStore = existingMove;
            }
        }
    }

    const quint64 data = packEntry(score, depth, flag, moveToStore);
    entry.data.store(data, std::memory_order_relaxed);
    entry.keyWord.store(key ^ data, std::memory_order_relaxed);
}

int pieceTypeValue(FastPiece pt) {
    switch (pt) {
    case Pawn:   return HeuristicEval::pieceValue(Rules::PieceType::Pawn);
    case Knight: return HeuristicEval::pieceValue(Rules::PieceType::Knight);
    case Bishop: return HeuristicEval::pieceValue(Rules::PieceType::Bishop);
    case Rook:   return HeuristicEval::pieceValue(Rules::PieceType::Rook);
    case Queen:  return HeuristicEval::pieceValue(Rules::PieceType::Queen);
    default:     return 0;
    }
}

struct MoveOrdering {
    int score = 0;
    bool tactical = false;
    bool losingCapture = false;
    int gain = 0;
};

MoveOrdering computeMoveOrdering(const FastBoard &pos, FastMove move,
                                 const SearchState &state, int ply,
                                 FastMove tableMove, bool hasTableMove) {
    MoveOrdering ordering;
    const int from = moveFrom(move);
    const int to = moveTo(move);
    const uint8_t fromByte = pos.board[from];
    const uint8_t toByte = pos.board[to];
    const FastPiece fromPt = pieceByteType(fromByte);
    const FastPiece toPt = pieceByteType(toByte);
    const FastPiece promoPt = movePromo(move);

    const bool enPassant = toByte == 0 && fromPt == Pawn &&
                           (from & 7) != (to & 7);
    const bool isCapture = toByte != 0 || enPassant;
    const bool isPromotion = promoPt != NoPiece;

    ordering.tactical = isPromotion || isCapture;
    if (toByte != 0) {
        ordering.gain += pieceTypeValue(toPt);
    } else if (enPassant) {
        ordering.gain += HeuristicEval::params().pawnValue;
    }
    if (isPromotion) {
        ordering.gain += pieceTypeValue(promoPt);
    }

    if (hasTableMove && move == tableMove) {
        ordering.score = 2'000'000;
        if (isCapture && ordering.gain > 0) {
            const int targetValue = toByte != 0 ? pieceTypeValue(toPt)
                                                : HeuristicEval::params().pawnValue;
            const int fromValue = fromByte != 0 ? pieceTypeValue(fromPt) : 0;
            if (targetValue < fromValue && seeValueFast(pos, move) < 0) {
                ordering.losingCapture = true;
            }
        }
    } else if (isCapture) {
        const int targetValue = toByte != 0 ? pieceTypeValue(toPt)
                                            : HeuristicEval::params().pawnValue;
        const int fromValue = fromByte != 0 ? pieceTypeValue(fromPt) : 0;
        const int promoBonus = isPromotion ? pieceTypeValue(promoPt) : 0;

        if (targetValue >= fromValue) {
            ordering.score =
                1'000'000 + 10 * targetValue - fromValue + promoBonus;
        } else {
            const int see = seeValueFast(pos, move);
            if (see >= 0) {
                ordering.score =
                    1'000'000 + 10 * targetValue - fromValue + promoBonus;
            } else {
                ordering.score = -100'000 + see;
                ordering.losingCapture = true;
            }
        }
    } else if (isPromotion) {
        ordering.score = 900'000 + pieceTypeValue(promoPt);
    } else if (ply < MaxSearchPly && move == state.killers[ply][0]) {
        ordering.score = 800'000;
    } else if (ply < MaxSearchPly && move == state.killers[ply][1]) {
        ordering.score = 799'000;
    } else {
        ordering.score = state.history[pos.sideToMove][from][to];
    }
    return ordering;
}

int leafScore(const FastBoard &pos, bool inCheck) {
    const FastColor side = static_cast<FastColor>(pos.sideToMove);
    const quint64 key = pos.hash;
    int white = 0;
    if (!EvalCache::instance().lookup(key, &white)) {
        white = evaluateBoard(pos, inCheck, nullptr);
        EvalCache::instance().store(key, white);
    }
    return std::clamp(signFor(side) * white, -MaxEvalScore, MaxEvalScore);
}

bool isPathDraw(const FastBoard &pos, SearchState &state, int ply, bool inCheck) {
    const quint64 key = pos.hash;
    state.pathKeys[ply] = key;

    if (pos.halfmoveClock >= 100 && (!inCheck || pos.hasAnyLegalMove())) {
        return true;
    }

    for (int previous = ply - 2; previous > state.lastNullPly; previous -= 2) {
        if (state.pathKeys[previous] == key) {
            return true;
        }
    }
    return false;
}

void selectNextMove(FastMove *moves, int *scores, int index, int count,
                    bool *tactical = nullptr, bool *losing = nullptr,
                    int *gain = nullptr) {
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
    if (losing != nullptr) {
        std::swap(losing[index], losing[best]);
    }
    if (gain != nullptr) {
        std::swap(gain[index], gain[best]);
    }
}

int quiescence(FastBoard &pos, int depth, int alpha, int beta, int ply,
               SearchState &state) {
    if (limitsReached(state)) {
        return 0;
    }

    const FastColor side = static_cast<FastColor>(pos.sideToMove);
    const bool inCheck = pos.isInCheck(side);

    if (isPathDraw(pos, state, ply, inCheck)) {
        return 0;
    }

    int best = -SearchInfinity;
    if (!inCheck) {
        const int standPat = leafScore(pos, false);
        if (standPat >= beta) {
            return standPat;
        }
        if (standPat > alpha) {
            alpha = standPat;
        }
        best = standPat;
    }

    if (depth <= 0 || ply >= MaxSearchPly) {
        return inCheck ? leafScore(pos, true) : best;
    }

    FastMove moves[MaxMoves];
    int scores[MaxMoves];
    bool losing[MaxMoves];
    int gains[MaxMoves];
    const int count = inCheck ? pos.generatePseudoLegalMoves(moves)
                              : pos.generateTacticalMoves(moves);
    for (int i = 0; i < count; ++i) {
        const MoveOrdering ordering =
            computeMoveOrdering(pos, moves[i], state, ply, NoFastMove, false);
        scores[i] = ordering.score;
        losing[i] = ordering.losingCapture;
        gains[i] = ordering.gain;
    }

    bool anyLegal = false;
    for (int i = 0; i < count; ++i) {
        selectNextMove(moves, scores, i, count, nullptr, losing, gains);
        const FastMove move = moves[i];
        if (!inCheck) {
            if (best + gains[i] + DeltaPruningMargin <= alpha) {
                continue;
            }
            if (losing[i]) {
                continue;
            }
        }

        FastUndo undo;
        if (!pos.makeMove(move, undo)) {
            continue;
        }
        anyLegal = true;
        const int score =
            -quiescence(pos, depth - 1, -beta, -alpha, ply + 1, state);
        pos.unmakeMove(move, undo);

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

int negamax(FastBoard &pos, int depth, int quiescenceDepth, int alpha, int beta,
            int ply, SearchState &state) {
    if (depth <= 0 || ply >= MaxSearchPly) {
        return quiescence(pos, quiescenceDepth, alpha, beta, ply, state);
    }

    if (limitsReached(state)) {
        return 0;
    }

    const FastColor side = static_cast<FastColor>(pos.sideToMove);
    const bool inCheck = pos.isInCheck(side);

    if (isPathDraw(pos, state, ply, inCheck)) {
        return 0;
    }

    const bool useTable = state.table != nullptr;
    const quint64 key = useTable ? pos.hash : 0;
    FastMove tableMove = NoFastMove;
    bool hasTableMove = false;
    int tableValue = 0;
    if (probeTable(state, key, ply, depth, alpha, beta, tableMove,
                   hasTableMove, tableValue)) {
        return tableValue;
    }

    if (!inCheck && depth >= 3 && std::abs(beta) < MateThreshold &&
        !state.nullMove && pos.hasNonPawnMaterial(side)) {
        FastNullUndo nullUndo;
        if (pos.makeNullMove(nullUndo)) {
            const int previousNullPly = state.lastNullPly;
            state.nullMove = true;
            state.lastNullPly = ply;
            const int score = -negamax(pos, depth - 1 - NullMoveReduction,
                                       quiescenceDepth, -beta, -beta + 1,
                                       ply + 1, state);
            state.nullMove = false;
            state.lastNullPly = previousNullPly;
            pos.unmakeNullMove(nullUndo);
            if (state.stopped) {
                return 0;
            }
            if (score >= beta) {
                return beta;
            }
        }
    }

    const int alphaOriginal = alpha;
    FastMove moves[MaxMoves];
    int scores[MaxMoves];
    bool tacticalFlags[MaxMoves];
    bool losingFlags[MaxMoves];
    const int count = pos.generatePseudoLegalMoves(moves);
    for (int i = 0; i < count; ++i) {
        const MoveOrdering ordering =
            computeMoveOrdering(pos, moves[i], state, ply, tableMove,
                                hasTableMove);
        scores[i] = ordering.score;
        tacticalFlags[i] = ordering.tactical;
        losingFlags[i] = ordering.losingCapture;
    }

    bool anyLegal = false;
    const bool futileNode = depth == 1 && !inCheck;
    int staticEval = 0;
    if (futileNode) {
        staticEval = leafScore(pos, inCheck);
    }
    int best = futileNode ? staticEval : -SearchInfinity;
    FastMove bestMove = NoFastMove;
    for (int i = 0; i < count; ++i) {
        selectNextMove(moves, scores, i, count, tacticalFlags, losingFlags);
        const FastMove move = moves[i];
        const bool tactical = tacticalFlags[i];

        if (futileNode && !tactical && std::abs(alpha) < MateThreshold &&
            staticEval + FutilityMargin <= alpha) {
            continue;
        }

        if (futileNode && losingFlags[i]) {
            continue;
        }

        FastUndo undo;
        if (!pos.makeMove(move, undo)) {
            continue;
        }
        const bool firstMove = !anyLegal;
        anyLegal = true;
        const bool givesCheck = pos.isInCheck(static_cast<FastColor>(pos.sideToMove));
        int childDepth = depth - 1;
        int reduction = 0;
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
            score = -negamax(pos, childDepth, quiescenceDepth, -beta, -alpha,
                             ply + 1, state);
        } else {
            score = -negamax(pos, childDepth, quiescenceDepth, -alpha - 1,
                             -alpha, ply + 1, state);
        }
        if (reduction > 0 && score > alpha) {
            score = -negamax(pos, depth - 1 + extension, quiescenceDepth,
                             -alpha - 1, -alpha, ply + 1, state);
        }
        if (!firstMove && score > alpha && score < beta) {
            score = -negamax(pos, depth - 1 + extension, quiescenceDepth,
                             -beta, -alpha, ply + 1, state);
        }
        pos.unmakeMove(move, undo);

        if (score > best) {
            best = score;
            bestMove = move;
        }
        if (score >= beta) {
            if (!tactical && ply < MaxSearchPly) {
                if (state.killers[ply][0] != move) {
                    state.killers[ply][1] = state.killers[ply][0];
                    state.killers[ply][0] = move;
                }
                state.history[side][moveFrom(move)][moveTo(move)] +=
                    depth * depth;
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
        return futileNode ? staticEval : 0;
    }

    if (useTable && !state.stopped) {
        const int flag = best <= alphaOriginal
                             ? TtUpper
                             : (best >= beta ? TtLower : TtExact);
        storeTable(state, key, depth, valueToTable(best, ply), flag, bestMove);
    }
    return best;
}

struct RootResult {
    int score = -SearchInfinity;
    int nodes = 0;
    bool aborted = false;
    FastMove move = NoFastMove;
};

RootResult searchRootChunk(const FastBoard &rootBoard, const FastMove *moves,
                           const std::vector<int> &indices, int depth,
                           int quiescenceDepth, int alpha, int beta,
                           std::atomic<int> &sharedAlpha,
                           std::atomic<bool> &stop, TtEntry *table,
                           quint64 tableMask, const SearchControl *control) {
    FastBoard root = rootBoard;
    SearchState state;
    state.table = table;
    state.tableMask = tableMask;
    state.control = control;
    state.pathKeys[0] = root.hash;

    RootResult result;
    for (const int index : indices) {
        if (stop.load(std::memory_order_relaxed) || state.stopped) {
            break;
        }
        const FastMove move = moves[index];
        FastUndo undo;
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
            stop.store(true, std::memory_order_relaxed);
            break;
        }
    }
    result.nodes = state.nodes;
    result.aborted = state.stopped;
    return result;
}

std::atomic<int> searchThreadOverride{0};

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

class SearchPool {
public:
    static SearchPool &instance() {
        static SearchPool pool;
        return pool;
    }

    [[nodiscard]] int size() const {
        return static_cast<int>(threads_.size());
    }

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

RootResult parallelRootSearch(const FastBoard &rootBoard, const FastMove *moves,
                              int moveCount, int depth, int quiescenceDepth,
                              int alpha, int beta, TtEntry *table,
                              quint64 tableMask, const SearchControl *control) {
    const int workers = std::min(rootWorkerLimit(), std::min(moveCount, 16));

    std::atomic<int> sharedAlpha{alpha};
    std::atomic<bool> stop{false};

    if (depth < 3 || workers <= 1) {
        std::vector<int> indices(static_cast<std::size_t>(moveCount));
        for (int i = 0; i < moveCount; ++i) {
            indices[static_cast<std::size_t>(i)] = i;
        }
        return searchRootChunk(rootBoard, moves, indices, depth,
                               quiescenceDepth, alpha, beta, sharedAlpha, stop,
                               table, tableMask, control);
    }

    SearchPool &pool = SearchPool::instance();
    std::vector<std::vector<int>> chunks(static_cast<std::size_t>(workers));
    for (int i = 0; i < moveCount; ++i) {
        chunks[static_cast<std::size_t>(i % workers)].push_back(i);
    }
    std::vector<RootResult> results(static_cast<std::size_t>(workers));

    pool.run(workers, [&rootBoard, moves, &chunks, &results, depth,
                       quiescenceDepth, alpha, beta, &sharedAlpha, &stop, table,
                       tableMask, control](int worker) {
        results[static_cast<std::size_t>(worker)] = searchRootChunk(
            rootBoard, moves, chunks[static_cast<std::size_t>(worker)], depth,
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

constexpr int MaxPvPlies = 16;

std::vector<Rules::Move> extractPrincipalVariation(const FastBoard &rootBoard,
                                                   FastMove firstMove,
                                                   TtEntry *table,
                                                   quint64 tableMask) {
    std::vector<Rules::Move> line;
    if (firstMove == NoFastMove) {
        return line;
    }
    line.push_back(toRulesMove(firstMove));
    if (table == nullptr) {
        return line;
    }

    FastBoard position = rootBoard;
    FastUndo undo;
    if (!position.makeMove(firstMove, undo)) {
        return line;
    }

    SearchState state;
    state.table = table;
    state.tableMask = tableMask;
    for (int ply = 1; ply < MaxPvPlies; ++ply) {
        FastMove move = NoFastMove;
        bool hasMove = false;
        int value = 0;
        probeTable(state, position.hash, ply, 0, -SearchInfinity,
                   SearchInfinity, move, hasMove, value);
        if (!hasMove) {
            break;
        }
        FastMove legalMoves[MaxMoves];
        const int count = position.generatePseudoLegalMoves(legalMoves);
        bool found = false;
        for (int i = 0; i < count; ++i) {
            if (legalMoves[i] == move) {
                found = true;
                break;
            }
        }
        if (!found || !position.makeMove(move, undo)) {
            break;
        }
        line.push_back(toRulesMove(move));
    }
    return line;
}

} // namespace

namespace HeuristicSearch {

int seeValue(const Rules &rules, const Rules::Move &move) {
    return seeValueFast(FastBoard::fromRules(rules), toFastMove(move));
}

int moveOrderingScore(const Rules &rules, const Rules::Move &move,
                      const Rules::Move &tableMove,
                      const Rules::Move &killer0,
                      const Rules::Move &killer1) {
    const FastBoard pos = FastBoard::fromRules(rules);
    SearchState state;
    state.killers[0][0] = toFastMove(killer0);
    state.killers[0][1] = toFastMove(killer1);
    const FastMove fastTableMove = toFastMove(tableMove);
    return computeMoveOrdering(pos, toFastMove(move), state, 0, fastTableMove,
                               fastTableMove != NoFastMove)
        .score;
}

void storeTransposition(quint64 key, int depth, int score, int flag,
                        const Rules::Move &move) {
    TranspositionTable &table = TranspositionTable::instance();
    table.ensureSize(15);
    SearchState state;
    state.table = table.data();
    state.tableMask = table.mask();
    storeTable(state, key, depth, score, flag, toFastMove(move));
}

void storeTornTransposition(quint64 key, int depth, int score, int flag,
                            const Rules::Move &move, int otherDepth,
                            int otherScore, int otherFlag,
                            const Rules::Move &otherMove) {
    TranspositionTable &table = TranspositionTable::instance();
    table.ensureSize(15);
    const quint64 keyData =
        packEntry(score, depth, flag, toFastMove(move));
    const quint64 otherData =
        packEntry(otherScore, otherDepth, otherFlag, toFastMove(otherMove));
    TtEntry &entry = table.data()[key & table.mask()];
    entry.keyWord.store(key ^ keyData, std::memory_order_relaxed);
    entry.data.store(otherData, std::memory_order_relaxed);
}

bool probeTranspositionMove(quint64 key, Rules::Move &outMove) {
    TranspositionTable &table = TranspositionTable::instance();
    if (table.data() == nullptr) {
        return false;
    }
    SearchState state;
    state.table = table.data();
    state.tableMask = table.mask();
    bool hasBestMove = false;
    int value = 0;
    FastMove bestMove = NoFastMove;
    probeTable(state, key, 0, 0, -SearchInfinity, SearchInfinity, bestMove,
               hasBestMove, value);
    if (hasBestMove) {
        outMove = toRulesMove(bestMove);
        return true;
    }
    return false;
}

void clearCaches() {
    TranspositionTable::instance().clear();
    EvalCache::instance().clear();
    clearPawnCache();
}

} // namespace HeuristicSearch

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

    const FastBoard rootBoard = FastBoard::fromRules(rules);
    const FastColor side = static_cast<FastColor>(rootBoard.sideToMove);
    depth = std::clamp(depth, 0, MaxSearchPly);
    quiescenceDepth = std::clamp(quiescenceDepth, 0, MaxSearchPly);

    FastMove moves[MaxMoves];
    const int moveCount = rootBoard.generatePseudoLegalMoves(moves);
    if (moveCount == 0 || !rootBoard.hasAnyLegalMove()) {
        if (rootBoard.isInCheck(side)) {
            result.centipawns = side == White ? -MateScore : MateScore;
            result.mateIn = 0;
        }
        return result;
    }
    if (rules.isInsufficientMaterial()) {
        return result;
    }
    if (rules.isFiftyMoveRule() || rules.isThreefoldRepetition()) {
        return result;
    }

    SearchState orderingState;
    int scores[MaxMoves];
    for (int i = 0; i < moveCount; ++i) {
        scores[i] = computeMoveOrdering(rootBoard, moves[i], orderingState, 0,
                                        NoFastMove, false)
                        .score;
    }
    for (int i = 0; i < moveCount; ++i) {
        selectNextMove(moves, scores, i, moveCount);
    }

    TtEntry *table = nullptr;
    quint64 tableMask = 0;
    if (depth >= 3) {
        const int bits = depth >= 5 ? 18 : (depth >= 4 ? 16 : 15);
        TranspositionTable &shared = TranspositionTable::instance();
        shared.ensureSize(bits);
        table = shared.data();
        tableMask = shared.mask();
    }

    std::atomic<bool> limitReached{false};
    SearchControl controlStorage;
    const SearchControl *control = nullptr;
    if (limits.maxNodes > 0 || limits.maxMilliseconds > 0 ||
        limits.shouldStop) {
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

    constexpr int AspirationDelta = 200;
    int best = 0;
    bool completed = false;
    FastMove bestMove = NoFastMove;
    const int targetDepth = std::max(0, depth);
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
            if (bestMove != NoFastMove) {
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
            rootResult = parallelRootSearch(
                rootBoard, moves, moveCount, iteration, quiescenceDepth, alpha,
                beta, table, tableMask, control);
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
        return result;
    }

    if (best >= MateThreshold) {
        const int pliesToMate = MateScore - best;
        const int mateMoves = (pliesToMate + 1) / 2;
        result.mateIn = signFor(side) > 0 ? mateMoves : -mateMoves;
        result.centipawns = side == White ? MateScore : -MateScore;
    } else if (best <= -MateThreshold) {
        const int pliesToMate = MateScore + best;
        const int mateMoves = (pliesToMate + 1) / 2;
        result.mateIn = signFor(side) > 0 ? -mateMoves : mateMoves;
        result.centipawns = side == White ? -MateScore : MateScore;
    } else {
        const int whiteScore = signFor(side) * best;
        result.centipawns = std::clamp(whiteScore, -MaxEvalScore, MaxEvalScore);
    }

    if (bestMove != NoFastMove) {
        result.bestMove = toRulesMove(bestMove);
        result.principalVariation =
            extractPrincipalVariation(rootBoard, bestMove, table, tableMask);
    }
    return result;
}
