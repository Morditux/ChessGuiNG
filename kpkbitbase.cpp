//
// Exact result of king and pawn against king.
//

#include "kpkbitbase.h"

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

// The pawn is always white and moves up the board, on the files a-d: the other
// colour is a flip of the ranks and the other wing a mirror of the files, and
// neither symmetry changes the result. Squares are `rank * 8 + file`.
constexpr int PawnFiles = 4;
constexpr int PawnRanks = 6; // ranks 2 to 7
constexpr int PawnSquares = PawnFiles * PawnRanks;
constexpr std::size_t PositionCount =
    static_cast<std::size_t>(2) * PawnSquares * 64 * 64;

enum class Verdict : std::uint8_t { Invalid, Unknown, Draw, Win };

constexpr int rankOf(int square) {
    return square >> 3;
}

constexpr int fileOf(int square) {
    return square & 7;
}

constexpr int distance(int first, int second) {
    return std::max(std::abs(rankOf(first) - rankOf(second)),
                    std::abs(fileOf(first) - fileOf(second)));
}

// Index of a position; `attackerToMove` is zero when the side with the pawn is
// on the move.
constexpr std::size_t indexOf(int attackerToMove, int pawn, int attackerKing,
                              int defenderKing) {
    const int pawnIndex = (rankOf(pawn) - 1) * PawnFiles + fileOf(pawn);
    return ((static_cast<std::size_t>(attackerToMove) * PawnSquares +
             static_cast<std::size_t>(pawnIndex)) * 64 +
            static_cast<std::size_t>(attackerKing)) * 64 +
           static_cast<std::size_t>(defenderKing);
}

// Squares a white pawn on `pawn` attacks.
constexpr bool pawnAttacks(int pawn, int target) {
    return rankOf(target) == rankOf(pawn) + 1 &&
           std::abs(fileOf(target) - fileOf(pawn)) == 1;
}

constexpr bool onBoard(int rank, int file) {
    return rank >= 0 && rank < 8 && file >= 0 && file < 8;
}

class Table {
public:
    Table() {
        std::vector<Verdict> verdicts(PositionCount, Verdict::Invalid);
        classify(verdicts);
        solve(verdicts);
        for (std::size_t index = 0; index < PositionCount; ++index) {
            wins_[index] = verdicts[index] == Verdict::Win;
        }
    }

    [[nodiscard]] bool win(std::size_t index) const {
        return wins_[index];
    }

private:
    // Marks the illegal positions, the ones whose result is already known
    // without looking at a move, and leaves the others unknown.
    static void classify(std::vector<Verdict> &verdicts) {
        for (int attackerToMove = 0; attackerToMove < 2; ++attackerToMove) {
            for (int rank = 1; rank <= PawnRanks; ++rank) {
                for (int file = 0; file < PawnFiles; ++file) {
                    const int pawn = rank * 8 + file;
                    for (int attackerKing = 0; attackerKing < 64;
                         ++attackerKing) {
                        for (int defenderKing = 0; defenderKing < 64;
                             ++defenderKing) {
                            verdicts[indexOf(attackerToMove, pawn, attackerKing,
                                             defenderKing)] =
                                initialVerdict(attackerToMove, pawn,
                                               attackerKing, defenderKing);
                        }
                    }
                }
            }
        }
    }

    static Verdict initialVerdict(int attackerToMove, int pawn,
                                  int attackerKing, int defenderKing) {
        if (attackerKing == defenderKing || attackerKing == pawn ||
            defenderKing == pawn || distance(attackerKing, defenderKing) <= 1) {
            return Verdict::Invalid;
        }
        const bool defenderInCheck = pawnAttacks(pawn, defenderKing);
        if (attackerToMove == 0) {
            // The defender cannot have been left in check by its own move.
            if (defenderInCheck) {
                return Verdict::Invalid;
            }
            // A pawn on the seventh rank that can promote without losing the
            // new queen at once (the defender's king is too far to take it, or
            // the attacker's king protects the square) wins.
            const int promotion = pawn + 8;
            if (rankOf(pawn) == 6 && promotion != attackerKing &&
                promotion != defenderKing &&
                (distance(defenderKing, promotion) > 1 ||
                 distance(attackerKing, promotion) == 1)) {
                return Verdict::Win;
            }
            return Verdict::Unknown;
        }

        // The defender to move: no legal move is mate or stalemate, and taking
        // the undefended pawn is an immediate draw.
        int moves = 0;
        for (int rankStep = -1; rankStep <= 1; ++rankStep) {
            for (int fileStep = -1; fileStep <= 1; ++fileStep) {
                const int rank = rankOf(defenderKing) + rankStep;
                const int file = fileOf(defenderKing) + fileStep;
                if ((rankStep == 0 && fileStep == 0) || !onBoard(rank, file)) {
                    continue;
                }
                const int target = rank * 8 + file;
                if (distance(target, attackerKing) <= 1 ||
                    pawnAttacks(pawn, target)) {
                    continue;
                }
                if (target == pawn) {
                    return Verdict::Draw;
                }
                ++moves;
            }
        }
        if (moves == 0) {
            return defenderInCheck ? Verdict::Win : Verdict::Draw;
        }
        return Verdict::Unknown;
    }

    // Repeats until nothing changes: an unknown position with a winning reply
    // (attacker to move), or with only winning replies (defender to move), is a
    // win, and one whose replies are all draws, or that has a drawing reply, is
    // a draw. Whatever stays unknown is a draw by repetition.
    static void solve(std::vector<Verdict> &verdicts) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int attackerToMove = 0; attackerToMove < 2; ++attackerToMove) {
                for (int rank = 1; rank <= PawnRanks; ++rank) {
                    for (int file = 0; file < PawnFiles; ++file) {
                        const int pawn = rank * 8 + file;
                        for (int attackerKing = 0; attackerKing < 64;
                             ++attackerKing) {
                            for (int defenderKing = 0; defenderKing < 64;
                                 ++defenderKing) {
                                Verdict &verdict = verdicts[indexOf(
                                    attackerToMove, pawn, attackerKing,
                                    defenderKing)];
                                if (verdict != Verdict::Unknown) {
                                    continue;
                                }
                                const Verdict result = attackerToMove == 0
                                    ? attackerVerdict(verdicts, pawn,
                                                      attackerKing, defenderKing)
                                    : defenderVerdict(verdicts, pawn,
                                                      attackerKing, defenderKing);
                                if (result != Verdict::Unknown) {
                                    verdict = result;
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        for (Verdict &verdict : verdicts) {
            if (verdict == Verdict::Unknown) {
                verdict = Verdict::Draw;
            }
        }
    }

    static Verdict attackerVerdict(const std::vector<Verdict> &verdicts,
                                   int pawn, int attackerKing,
                                   int defenderKing) {
        bool anyMove = false;
        bool allDraw = true;
        const auto consider = [&](int newPawn, int newAttackerKing) {
            anyMove = true;
            const Verdict child = verdicts[indexOf(1, newPawn, newAttackerKing,
                                                   defenderKing)];
            if (child == Verdict::Win) {
                return true;
            }
            if (child != Verdict::Draw) {
                allDraw = false;
            }
            return false;
        };

        for (int rankStep = -1; rankStep <= 1; ++rankStep) {
            for (int fileStep = -1; fileStep <= 1; ++fileStep) {
                const int rank = rankOf(attackerKing) + rankStep;
                const int file = fileOf(attackerKing) + fileStep;
                if ((rankStep == 0 && fileStep == 0) || !onBoard(rank, file)) {
                    continue;
                }
                const int target = rank * 8 + file;
                if (target == pawn || distance(target, defenderKing) <= 1) {
                    continue;
                }
                if (consider(pawn, target)) {
                    return Verdict::Win;
                }
            }
        }
        // Pawn moves. A push to the last rank is the promotion the initial
        // classification already judged.
        const int push = pawn + 8;
        if (rankOf(pawn) < 6 && push != attackerKing && push != defenderKing) {
            if (consider(push, attackerKing)) {
                return Verdict::Win;
            }
            const int doublePush = pawn + 16;
            if (rankOf(pawn) == 1 && doublePush != attackerKing &&
                doublePush != defenderKing && consider(doublePush, attackerKing)) {
                return Verdict::Win;
            }
        }
        if (!anyMove) {
            return Verdict::Draw; // stalemate
        }
        return allDraw ? Verdict::Draw : Verdict::Unknown;
    }

    static Verdict defenderVerdict(const std::vector<Verdict> &verdicts,
                                   int pawn, int attackerKing,
                                   int defenderKing) {
        bool allWin = true;
        for (int rankStep = -1; rankStep <= 1; ++rankStep) {
            for (int fileStep = -1; fileStep <= 1; ++fileStep) {
                const int rank = rankOf(defenderKing) + rankStep;
                const int file = fileOf(defenderKing) + fileStep;
                if ((rankStep == 0 && fileStep == 0) || !onBoard(rank, file)) {
                    continue;
                }
                const int target = rank * 8 + file;
                if (target == pawn || distance(target, attackerKing) <= 1 ||
                    pawnAttacks(pawn, target)) {
                    continue;
                }
                const Verdict child = verdicts[indexOf(0, pawn, attackerKing,
                                                       target)];
                if (child == Verdict::Draw) {
                    return Verdict::Draw;
                }
                if (child != Verdict::Win) {
                    allWin = false;
                }
            }
        }
        return allWin ? Verdict::Win : Verdict::Unknown;
    }

    std::bitset<PositionCount> wins_;
};

const Table &table() {
    // Initialised once, on first use, by the language's thread-safe static.
    static const Table instance;
    return instance;
}

} // namespace

namespace KpkBitbase {

bool isWin(Rules::Color pawnColor, Rules::Color sideToMove,
           Rules::Position pawn, Rules::Position pawnKing,
           Rules::Position bareKing) {
    const bool flipRanks = pawnColor == Rules::Color::Black;
    const bool mirrorFiles = pawn.column > 3;
    const auto square = [&](Rules::Position position) {
        const int rank = flipRanks ? position.row : 7 - position.row;
        const int file = mirrorFiles ? 7 - position.column : position.column;
        return rank * 8 + file;
    };
    const int attackerToMove = sideToMove == pawnColor ? 0 : 1;
    return table().win(
        indexOf(attackerToMove, square(pawn), square(pawnKing), square(bareKing)));
}

} // namespace KpkBitbase
