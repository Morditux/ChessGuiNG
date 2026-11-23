//
// Classical heuristic evaluation for chess positions (FastBoard bitboard engine).
//

#include "heuristiceval.h"
#include "heuristicsearch.h"
#include "kpkbitbase.h"
#include "winprobability.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

using namespace HeuristicSearch;

namespace {

constexpr int MateScore = HeuristicEval::MateScore;
constexpr int MaxPhase = 24;
constexpr int ScaleNormal = 64;

// The active weights, published through HeuristicEval::params().
HeuristicEval::EvalParams activeParams;
const HeuristicEval::EvalParams &P = activeParams;

struct TaperedScore {
    int middleGame = 0;
    int endGame = 0;

    TaperedScore &operator+=(const TaperedScore &other) {
        middleGame += other.middleGame;
        endGame += other.endGame;
        return *this;
    }

    TaperedScore &operator-=(const TaperedScore &other) {
        middleGame -= other.middleGame;
        endGame -= other.endGame;
        return *this;
    }
};

constexpr TaperedScore bothPhases(int value) {
    return {value, value};
}

constexpr TaperedScore middleGameOnly(int value) {
    return {value, 0};
}

constexpr TaperedScore operator*(int factor, const TaperedScore &score) {
    return {factor * score.middleGame, factor * score.endGame};
}

// Tables are indexed from the point of view of the relevant side:
// index 0 is that side's home rank and index 7 its promotion rank.
constexpr std::array<int, 64> PawnMiddleGamePST = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    10, 10, 20, 30, 30, 20, 10, 10,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr std::array<int, 64> KnightMiddleGamePST = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

constexpr std::array<int, 64> BishopMiddleGamePST = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};

constexpr std::array<int, 64> RookMiddleGamePST = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr std::array<int, 64> QueenMiddleGamePST = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,
     0,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
   -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};

constexpr std::array<int, 64> KingMiddleGamePST = {
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
   -10,-20,-20,-20,-20,-20,-20,-10,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30
};

constexpr std::array<int, 64> KingEndGamePST = {
   -50,-30,-20,-20,-20,-20,-30,-50,
   -30,-10, 10, 15, 15, 10,-10,-30,
   -20, 10, 25, 30, 30, 25, 10,-20,
   -20, 15, 30, 40, 40, 30, 15,-20,
   -20, 15, 30, 40, 40, 30, 15,-20,
   -20, 10, 25, 30, 30, 25, 10,-20,
   -30,-10, 10, 15, 15, 10,-10,-30,
   -50,-30,-20,-20,-20,-20,-30,-50
};

constexpr std::array<int, 64> PawnEndGamePST = {
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
      4,   4,   4,   4,   4,   4,   4,   4,
     10,  10,  10,  10,  10,  10,  10,  10,
     18,  18,  18,  18,  18,  18,  18,  18,
     28,  28,  28,  28,  28,  28,  28,  28,
     28,  28,  28,  28,  28,  28,  28,  28,
      0,   0,   0,   0,   0,   0,   0,   0
};

constexpr std::array<int, 64> KnightEndGamePST = {
    -27, -22, -19, -17, -17, -19, -22, -27,
    -22, -17, -12,  -9,  -9, -12, -17, -22,
    -19, -12,  -6,  -2,  -2,  -6, -12, -19,
    -17,  -9,  -2,   5,   5,  -2,  -9, -17,
    -17,  -9,  -2,   5,   5,  -2,  -9, -17,
    -19, -12,  -6,  -2,  -2,  -6, -12, -19,
    -22, -17, -12,  -9,  -9, -12, -17, -22,
    -27, -22, -19, -17, -17, -19, -22, -27
};

constexpr std::array<int, 64> BishopEndGamePST = {
    -10,  -7,  -5,  -4,  -4,  -5,  -7, -10,
     -7,  -4,  -1,   0,   0,  -1,  -4,  -7,
     -5,  -1,   2,   4,   4,   2,  -1,  -5,
     -4,   0,   4,   8,   8,   4,   0,  -4,
     -4,   0,   4,   8,   8,   4,   0,  -4,
     -5,  -1,   2,   4,   4,   2,  -1,  -5,
     -7,  -4,  -1,   0,   0,  -1,  -4,  -7,
    -10,  -7,  -5,  -4,  -4,  -5,  -7, -10
};

constexpr std::array<int, 64> RookEndGamePST = {
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1,
     -1,   1,   1,   1,   1,   1,   1,  -1
};

constexpr std::array<int, 64> QueenEndGamePST = {
    -12,  -9,  -6,  -5,  -5,  -6,  -9, -12,
     -9,  -5,  -2,   0,   0,  -2,  -5,  -9,
     -6,  -2,   2,   5,   5,   2,  -2,  -6,
     -5,   0,   5,   9,   9,   5,   0,  -5,
     -5,   0,   5,   9,   9,   5,   0,  -5,
     -6,  -2,   2,   5,   5,   2,  -2,  -6,
     -9,  -5,  -2,   0,   0,  -2,  -5,  -9,
    -12,  -9,  -6,  -5,  -5,  -6,  -9, -12
};

constexpr std::array<const std::array<int, 64> *, 7> MiddleGamePST = {
    nullptr, &PawnMiddleGamePST, &KnightMiddleGamePST, &BishopMiddleGamePST,
    &RookMiddleGamePST, &QueenMiddleGamePST, &KingMiddleGamePST};

constexpr std::array<const std::array<int, 64> *, 7> EndGamePST = {
    nullptr, &PawnEndGamePST, &KnightEndGamePST, &BishopEndGamePST,
    &RookEndGamePST, &QueenEndGamePST, &KingEndGamePST};

constexpr std::array<int, 9> KnightMobilityMG = {
    -27, -17, -10, -4, 0, 3, 6, 8, 10};
constexpr std::array<int, 9> KnightMobilityEG = {
    -28, -18, -10, -5, 0, 4, 7, 10, 12};
constexpr std::array<int, 14> BishopMobilityMG = {
    -39, -29, -21, -14, -9, -4, 0, 3, 6, 9, 11, 14, 15, 17};
constexpr std::array<int, 14> BishopMobilityEG = {
    -47, -35, -25, -17, -10, -5, 0, 4, 8, 11, 14, 17, 19, 21};
constexpr std::array<int, 15> RookMobilityMG = {
    -38, -28, -20, -14, -10, -6, -3, 0, 2, 4, 6, 8, 9, 10, 11};
constexpr std::array<int, 15> RookMobilityEG = {
    -65, -49, -36, -26, -18, -11, -5, 0, 4, 8, 11, 14, 17, 20, 22};
constexpr std::array<int, 28> QueenMobilityMG = {
    -28, -24, -20, -17, -14, -12, -10, -8, -6, -5, -3, -2, -1, 0,
    1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8};
constexpr std::array<int, 28> QueenMobilityEG = {
    -52, -44, -38, -32, -27, -23, -19, -15, -12, -9, -7, -4, -2, 0,
    2, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, 16, 17};

template <std::size_t Size>
constexpr int tableValue(const std::array<int, Size> &table, int moves) {
    return table[static_cast<std::size_t>(
        std::clamp(moves, 0, static_cast<int>(Size) - 1))];
}

TaperedScore shapedMobility(FastPiece type, int moves) {
    switch (type) {
    case Knight:
        return {tableValue(KnightMobilityMG, moves) * P.mobilityScaleKnightMG / 100,
                tableValue(KnightMobilityEG, moves) * P.mobilityScaleKnightEG / 100};
    case Bishop:
        return {tableValue(BishopMobilityMG, moves) * P.mobilityScaleBishopMG / 100,
                tableValue(BishopMobilityEG, moves) * P.mobilityScaleBishopEG / 100};
    case Rook:
        return {tableValue(RookMobilityMG, moves) * P.mobilityScaleRookMG / 100,
                tableValue(RookMobilityEG, moves) * P.mobilityScaleRookEG / 100};
    case Queen:
        return {tableValue(QueenMobilityMG, moves) * P.mobilityScaleQueenMG / 100,
                tableValue(QueenMobilityEG, moves) * P.mobilityScaleQueenEG / 100};
    default:
        return {};
    }
}

struct EvalMasks {
    std::array<std::array<Bitboard, 64>, 2> kingZone{};
    std::array<std::array<Bitboard, 64>, 2> forwardFile{};
    std::array<std::array<Bitboard, 64>, 2> rearFile{};
    std::array<std::array<Bitboard, 64>, 2> passedPawn{};
    std::array<std::array<Bitboard, 64>, 2> supportPawn{};
    std::array<std::array<Bitboard, 64>, 2> outpostEnemy{};
    std::array<Bitboard, 8> adjacentFiles{};
    std::array<std::array<Bitboard, 8>, 2> kingShelterAhead3{};
    std::array<Bitboard, 8> kingStormFiles{};
};

constexpr EvalMasks buildEvalMasks() {
    EvalMasks m;
    for (int f = 0; f < 8; ++f) {
        if (f > 0) {
            m.adjacentFiles[f] |= FileMask[f - 1];
        }
        if (f < 7) {
            m.adjacentFiles[f] |= FileMask[f + 1];
        }
        for (int df = -2; df <= 2; ++df) {
            if (f + df >= 0 && f + df < 8) {
                m.kingStormFiles[f] |= FileMask[f + df];
            }
        }
    }

    for (int r = 0; r < 8; ++r) {
        for (int d = 1; d <= 3; ++d) {
            if (r - d >= 0) {
                m.kingShelterAhead3[White][r] |= RankMask[r - d];
            }
            if (r + d < 8) {
                m.kingShelterAhead3[Black][r] |= RankMask[r + d];
            }
        }
    }

    for (int sq = 0; sq < 64; ++sq) {
        const int r = sq >> 3;
        const int c = sq & 7;
        const int firstCol = std::clamp(c - 1, 0, 5);

        for (int cIdx = 0; cIdx < 2; ++cIdx) {
            const int forward = cIdx == White ? -1 : 1;
            Bitboard zone = 0;
            for (int rr = r - 1; rr <= r + 1; ++rr) {
                if (rr < 0 || rr >= 8) {
                    continue;
                }
                for (int cc = firstCol; cc <= firstCol + 2; ++cc) {
                    zone |= squareBB(rr * 8 + cc);
                }
            }
            const int extraRow = r + 2 * forward;
            if (extraRow >= 0 && extraRow < 8) {
                for (int cc = firstCol; cc <= firstCol + 2; ++cc) {
                    zone |= squareBB(extraRow * 8 + cc);
                }
            }
            m.kingZone[cIdx][sq] = zone;

            Bitboard aheadRanks = 0;
            Bitboard behindRanks = 0;
            for (int rr = 0; rr < 8; ++rr) {
                const bool isAhead = cIdx == White ? (rr < r) : (rr > r);
                if (isAhead) {
                    aheadRanks |= RankMask[rr];
                } else if (rr != r) {
                    behindRanks |= RankMask[rr];
                }
            }

            m.forwardFile[cIdx][sq] = FileMask[c] & aheadRanks;
            m.rearFile[cIdx][sq] = FileMask[c] & behindRanks;
            m.passedPawn[cIdx][sq] =
                (FileMask[c] | m.adjacentFiles[c]) & aheadRanks;
            m.supportPawn[cIdx][sq] =
                m.adjacentFiles[c] & (behindRanks | RankMask[r]);
            m.outpostEnemy[cIdx][sq] = m.adjacentFiles[c] & aheadRanks;
        }
    }
    return m;
}

constexpr EvalMasks EMasks = buildEvalMasks();

constexpr Bitboard WhiteSpaceZone =
    (Rank8 | Rank7 | Rank6 | Rank5) & (FileC | FileD | FileE | FileF);
constexpr Bitboard BlackSpaceZone =
    (Rank4 | Rank3 | Rank2 | Rank1) & (FileC | FileD | FileE | FileF);

constexpr Bitboard WhiteOutpostRanks = Rank6 | Rank5 | Rank4;
constexpr Bitboard BlackOutpostRanks = Rank3 | Rank4 | Rank5;

constexpr Bitboard WhiteStormRanks = Rank8 | Rank7 | Rank6 | Rank5;
constexpr Bitboard BlackStormRanks = Rank1 | Rank2 | Rank3 | Rank4;

constexpr int KnightKind = 0;
constexpr int BishopKind = 1;
constexpr int RookKind = 2;
constexpr int QueenKind = 3;

constexpr int interpolate(int middleGameValue, int endGameValue, int phase) {
    const int total =
        middleGameValue * phase + endGameValue * (MaxPhase - phase);
    return total >= 0 ? (total + MaxPhase / 2) / MaxPhase
                      : -((-total + MaxPhase / 2) / MaxPhase);
}

int taperedValue(const TaperedScore &score, int phase) {
    return interpolate(score.middleGame, score.endGame, phase);
}

int pieceTypeValue(FastPiece pt) {
    switch (pt) {
    case Pawn:   return P.pawnValue;
    case Knight: return P.knightValue;
    case Bishop: return P.bishopValue;
    case Rook:   return P.rookValue;
    case Queen:  return P.queenValue;
    default:     return 0;
    }
}

int kingAttackerWeight(int kind) {
    switch (kind) {
    case KnightKind: return P.kingAttackerWeightKnight;
    case BishopKind: return P.kingAttackerWeightBishop;
    case RookKind:   return P.kingAttackerWeightRook;
    default:         return P.kingAttackerWeightQueen;
    }
}

int computePhase(const FastBoard &pos) {
    const int phase =
        std::popcount(pos.byType[Knight] | pos.byType[Bishop]) +
        2 * std::popcount(pos.byType[Rook]) +
        4 * std::popcount(pos.byType[Queen]);
    return std::min(phase, MaxPhase);
}

bool isInsufficientMaterial(const FastBoard &pos) {
    if ((pos.byType[Pawn] | pos.byType[Rook] | pos.byType[Queen]) != 0) {
        return false;
    }
    Rules::MaterialCounts counts;
    counts.hasPawnOrMajor = false;
    for (int c = 0; c < 2; ++c) {
        const FastColor color = static_cast<FastColor>(c);
        counts.knights[c] = std::popcount(pos.pieces(color, Knight));
        const Bitboard bishops = pos.pieces(color, Bishop);
        counts.evenSquaredBishops[c] =
            std::popcount(bishops & EvenSquaresMask);
        counts.oddSquaredBishops[c] =
            std::popcount(bishops & OddSquaresMask);
    }
    return Rules::classifyMaterialDraw(counts).unforceable;
}

bool isWrongColouredRookPawnEnding(const FastBoard &pos) {
    if ((pos.byType[Knight] | pos.byType[Rook] | pos.byType[Queen]) != 0) {
        return false;
    }
    for (int c = 0; c < 2; ++c) {
        const FastColor strong = static_cast<FastColor>(c);
        const FastColor weak = oppositeColor(strong);
        const Bitboard strongBishops = pos.pieces(strong, Bishop);
        if (std::popcount(strongBishops) != 1) {
            continue;
        }
        if ((pos.byColor[weak] & ~pos.byType[King]) != 0) {
            continue;
        }
        const Bitboard strongPawns = pos.pieces(strong, Pawn);
        if (strongPawns == 0 || (strongPawns & ~(FileA | FileH)) != 0) {
            continue;
        }
        const bool pawnsOnA = (strongPawns & FileA) != 0;
        const bool pawnsOnH = (strongPawns & FileH) != 0;
        if (pawnsOnA == pawnsOnH) {
            continue;
        }
        const bool bishopEven = (strongBishops & EvenSquaresMask) != 0;
        if (bishopEven != pawnsOnA) {
            return true;
        }
    }
    return false;
}

int kpkScore(const FastBoard &pos) {
    const int pawnSq = std::countr_zero(pos.byType[Pawn]);
    const FastColor pawnColor = pieceByteColor(pos.board[pawnSq]);
    const FastColor bareColor = oppositeColor(pawnColor);
    const int pawnKingSq = pos.kingSq[pawnColor];
    const int bareKingSq = pos.kingSq[bareColor];
    const Rules::Color rPawnColor =
        pawnColor == White ? Rules::Color::White : Rules::Color::Black;
    const Rules::Color rSideToMove =
        pos.sideToMove == White ? Rules::Color::White : Rules::Color::Black;
    if (!KpkBitbase::isWin(
            rPawnColor, rSideToMove, {squareRow(pawnSq), squareCol(pawnSq)},
            {squareRow(pawnKingSq), squareCol(pawnKingSq)},
            {squareRow(bareKingSq), squareCol(bareKingSq)})) {
        return 0;
    }
    return signFor(pawnColor) *
           (P.knownWinScore +
            P.knownWinRankBonus * relativeRank(pawnColor, pawnSq));
}

struct AttackState {
    // Index 0 = all attacks by this color, indices 1..6 = attacks per piece type.
    std::array<std::array<Bitboard, 7>, 2> by{};
    std::array<TaperedScore, 2> mobility{};
    std::array<int, 2> kingAttackers{};
    std::array<int, 2> kingAttackersWeight{};
    std::array<int, 2> kingAdjacentAttacks{};
    std::array<std::array<Bitboard, 4>, 2> checkCandidates{};
    TaperedScore trappedRooks{};
};

void collectAttacksAndMobility(const FastBoard &pos, AttackState &att) {
    const Bitboard occ = pos.occupancy();
    const auto &t = attackTables();

    // Build check-square masks for each king.
    std::array<std::array<Bitboard, 4>, 2> kingChecks{};
    for (int c = 0; c < 2; ++c) {
        const int kSq = pos.kingSq[c];
        if (kSq >= 64) {
            continue;
        }
        const Bitboard notOwn = ~pos.byColor[c];
        // Note: `checks` in original code included empty squares and friendly
        // pieces of the king's side (`notEnemy` from attacker's POV = `~byColor[enemy]`).
        // Wait: `buildKingMasks` added squares occupied by `color` (the king's side)
        // and broke on enemy pieces (which are `~byColor[color]`).
        const Bitboard bRays = bishopAttacks(kSq, occ) &
                               (~occ | pos.byColor[c]);
        const Bitboard rRays = rookAttacks(kSq, occ) &
                               (~occ | pos.byColor[c]);
        kingChecks[c][KnightKind] = t.knight[kSq];
        kingChecks[c][BishopKind] = bRays;
        kingChecks[c][RookKind] = rRays;
        kingChecks[c][QueenKind] = bRays | rRays;
        (void)notOwn;
    }

    // 1. Pawns: attacks and mobility.
    for (int c = 0; c < 2; ++c) {
        const FastColor color = static_cast<FastColor>(c);
        const FastColor enemy = oppositeColor(color);
        const Bitboard pawns = pos.pieces(color, Pawn);
        att.by[c][Pawn] = pawnAttacksBB(color, pawns);

        const Bitboard empty = ~occ;
        const Bitboard singlePush =
            (color == White ? (pawns >> 8) : (pawns << 8)) & empty;
        const Bitboard doublePush =
            (color == White
                 ? (((pawns & Rank2) >> 8) & empty) >> 8
                 : (((pawns & Rank7) << 8) & empty) << 8) &
            empty;
        const Bitboard capLeft =
            (color == White ? ((pawns & ~FileA) >> 9)
                            : ((pawns & ~FileA) << 7)) &
            pos.byColor[enemy];
        const Bitboard capRight =
            (color == White ? ((pawns & ~FileH) >> 7)
                            : ((pawns & ~FileH) << 9)) &
            pos.byColor[enemy];
        const int pawnMoves = std::popcount(singlePush) +
                              std::popcount(doublePush) +
                              std::popcount(capLeft) +
                              std::popcount(capRight);
        const TaperedScore w = {P.mobilityPawnMG, P.mobilityPawnEG};
        att.mobility[c] += pawnMoves * w;
    }

    // 2. Knights, Bishops, Rooks, Queens.
    for (int c = 0; c < 2; ++c) {
        const FastColor color = static_cast<FastColor>(c);
        const FastColor enemy = oppositeColor(color);
        const Bitboard mobilityArea = ~(pos.byColor[c] | att.by[enemy][Pawn]);
        const int enemyKingSq = pos.kingSq[enemy];
        const Bitboard enemyZone =
            enemyKingSq < 64 ? EMasks.kingZone[enemy][enemyKingSq] : 0;
        const Bitboard enemyAdj =
            enemyKingSq < 64 ? t.king[enemyKingSq] : 0;
        const Bitboard notOwn = ~pos.byColor[c];

        const auto processPieces = [&](FastPiece pt, int kind, auto &&attackFn) {
            Bitboard bb = pos.pieces(color, pt);
            while (bb != 0) {
                const int sq = popLsb(bb);
                const Bitboard pieceAtt = attackFn(sq);
                att.by[c][pt] |= pieceAtt;
                const int moves = std::popcount(pieceAtt & mobilityArea);
                att.mobility[c] += shapedMobility(pt, moves);

                if (enemyKingSq < 64) {
                    if ((pieceAtt & enemyZone) != 0) {
                        ++att.kingAttackers[c];
                        att.kingAttackersWeight[c] += kingAttackerWeight(kind);
                    }
                    att.kingAdjacentAttacks[c] +=
                        std::popcount(pieceAtt & enemyAdj);
                    att.checkCandidates[c][kind] |=
                        pieceAtt & kingChecks[enemy][kind] & notOwn;
                }

                if (pt == Rook && moves <= 3) {
                    const int homeRow = color == White ? 7 : 0;
                    const int ownKingSq = pos.kingSq[c];
                    if (ownKingSq < 64 && squareRow(ownKingSq) == homeRow &&
                        squareRow(sq) == homeRow) {
                        const int kCol = squareCol(ownKingSq);
                        const int rCol = squareCol(sq);
                        const bool outsideKing =
                            kCol >= 4 ? (rCol > kCol) : (rCol < kCol);
                        const bool ksRight =
                            (pos.castling &
                             (color == White ? WhiteKingSide : BlackKingSide)) != 0;
                        const bool qsRight =
                            (pos.castling &
                             (color == White ? WhiteQueenSide : BlackQueenSide)) != 0;
                        const bool canCastleSide =
                            rCol > kCol ? ksRight : qsRight;
                        if (outsideKing && !canCastleSide) {
                            att.trappedRooks -=
                                signFor(color) *
                                ((ksRight || qsRight) ? 1 : 2) *
                                middleGameOnly(P.trappedRookPenalty);
                        }
                    }
                }
            }
        };

        processPieces(Knight, KnightKind, [&](int sq) { return t.knight[sq]; });
        processPieces(Bishop, BishopKind,
                      [&](int sq) { return bishopAttacks(sq, occ); });
        processPieces(Rook, RookKind,
                      [&](int sq) { return rookAttacks(sq, occ); });
        processPieces(Queen, QueenKind,
                      [&](int sq) { return queenAttacks(sq, occ); });
    }

    // 3. Kings & union of all attacks.
    for (int c = 0; c < 2; ++c) {
        if (pos.kingSq[c] < 64) {
            att.by[c][King] = t.king[pos.kingSq[c]];
        }
        att.by[c][0] = att.by[c][Pawn] | att.by[c][Knight] |
                       att.by[c][Bishop] | att.by[c][Rook] |
                       att.by[c][Queen] | att.by[c][King];
    }

    // 4. King mobility (requires enemy's complete attack map).
    const TaperedScore kingMobWeight{P.mobilityKingMG, P.mobilityKingEG};
    for (int c = 0; c < 2; ++c) {
        if (pos.kingSq[c] < 64) {
            const int enemy = 1 - c;
            const int moves = std::popcount(
                att.by[c][King] & ~(pos.byColor[c] | att.by[enemy][0]));
            att.mobility[c] += moves * kingMobWeight;
        }
    }
}

void evaluateMaterialAndPlacement(const FastBoard &pos, TaperedScore *material,
                                  TaperedScore *placement) {
    const Bitboard whitePawns = pos.pieces(White, Pawn);
    const Bitboard blackPawns = pos.pieces(Black, Pawn);

    for (int ptIdx = Pawn; ptIdx <= King; ++ptIdx) {
        const FastPiece pt = static_cast<FastPiece>(ptIdx);
        const int val = pieceTypeValue(pt);
        const auto *mgTable = MiddleGamePST[ptIdx];
        const auto *egTable = EndGamePST[ptIdx];

        for (int c = 0; c < 2; ++c) {
            const FastColor color = static_cast<FastColor>(c);
            const int sign = signFor(color);
            Bitboard bb = pos.pieces(color, pt);
            const int count = std::popcount(bb);
            if (val != 0 && count != 0) {
                *material += sign * count * bothPhases(val);
            }
            while (bb != 0) {
                const int sq = popLsb(bb);
                const int relSq = relativeSquare(color, sq);
                *placement += sign * TaperedScore{(*mgTable)[relSq],
                                                  (*egTable)[relSq]};

                if (pt == Rook) {
                    const Bitboard file = FileMask[squareCol(sq)];
                    const Bitboard ownPawns = c == White ? whitePawns : blackPawns;
                    const Bitboard enemyPawns = c == White ? blackPawns : whitePawns;
                    if ((ownPawns & file) == 0) {
                        *placement += sign * ((enemyPawns & file) == 0
                                                  ? TaperedScore{P.rookOpenFileMG,
                                                                 P.rookOpenFileEG}
                                                  : TaperedScore{P.rookSemiOpenFileMG,
                                                                 P.rookSemiOpenFileEG});
                    }
                    if (relativeRank(color, sq) == 7) {
                        const int enemy = 1 - c;
                        const int enemyKingSq = pos.kingSq[enemy];
                        const int enemyBackRow = c == White ? 0 : 7;
                        const Bitboard enemySeventhRank =
                            c == White ? Rank7 : Rank2;
                        if ((enemyKingSq < 64 &&
                             squareRow(enemyKingSq) == enemyBackRow) ||
                            (enemyPawns & enemySeventhRank) != 0) {
                            *placement += sign * TaperedScore{P.rookSeventhRankMG,
                                                              P.rookSeventhRankEG};
                        }
                    }
                }
            }
        }
    }

    const TaperedScore bishopPair{P.bishopPairMG, P.bishopPairEG};
    if (std::popcount(pos.pieces(White, Bishop)) >= 2) {
        *placement += bishopPair;
    }
    if (std::popcount(pos.pieces(Black, Bishop)) >= 2) {
        *placement -= bishopPair;
    }
}

struct PawnEntry {
    TaperedScore structure;
    TaperedScore space;
    Bitboard passed = 0;
};

class PawnCache {
public:
    static PawnCache &instance() {
        static PawnCache cache;
        return cache;
    }

    bool lookup(quint64 key, PawnEntry *entry) const {
        const Slot &slot = slots_[key & Mask];
        const quint64 structure = slot.structure.load(std::memory_order_relaxed);
        const quint64 space = slot.space.load(std::memory_order_relaxed);
        const quint64 passed = slot.passed.load(std::memory_order_relaxed);
        const quint64 keyWord = slot.keyWord.load(std::memory_order_relaxed);
        if ((keyWord ^ structure ^ space ^ passed) != key) {
            return false;
        }
        *entry = unpack(structure, space, passed);
        return true;
    }

    void store(quint64 key, const PawnEntry &entry) {
        Slot &slot = slots_[key & Mask];
        const quint64 structure = pack(entry.structure);
        const quint64 space = pack(entry.space);
        slot.structure.store(structure, std::memory_order_relaxed);
        slot.space.store(space, std::memory_order_relaxed);
        slot.passed.store(entry.passed, std::memory_order_relaxed);
        slot.keyWord.store(key ^ structure ^ space ^ entry.passed,
                           std::memory_order_relaxed);
    }

    void clear() {
        for (Slot &slot : slots_) {
            slot.structure.store(0, std::memory_order_relaxed);
            slot.space.store(0, std::memory_order_relaxed);
            slot.passed.store(0, std::memory_order_relaxed);
            slot.keyWord.store(0, std::memory_order_relaxed);
        }
    }

private:
    struct Slot {
        std::atomic<quint64> keyWord{0};
        std::atomic<quint64> structure{0};
        std::atomic<quint64> space{0};
        std::atomic<quint64> passed{0};
    };

    static quint64 pack(const TaperedScore &score) {
        return static_cast<quint64>(static_cast<quint32>(score.middleGame)) |
               (static_cast<quint64>(static_cast<quint32>(score.endGame)) << 32);
    }

    static PawnEntry unpack(quint64 structure, quint64 space, quint64 passed) {
        const auto score = [](quint64 word) {
            return TaperedScore{
                static_cast<qint32>(static_cast<quint32>(word & 0xFFFFFFFFu)),
                static_cast<qint32>(static_cast<quint32>(word >> 32))};
        };
        return PawnEntry{score(structure), score(space), passed};
    }

    static constexpr quint64 Bits = 14;
    static constexpr quint64 Mask = (quint64{1} << Bits) - 1;
    std::array<Slot, static_cast<std::size_t>(quint64{1} << Bits)> slots_{};
};

PawnEntry computePawnEntry(const FastBoard &pos, Bitboard whitePawnAtt,
                           Bitboard blackPawnAtt) {
    PawnEntry entry;
    TaperedScore &score = entry.structure;

    const std::array<Bitboard, 2> pawns = {pos.pieces(White, Pawn),
                                           pos.pieces(Black, Pawn)};
    const std::array<Bitboard, 2> pawnAtt = {whitePawnAtt, blackPawnAtt};

    for (int c = 0; c < 2; ++c) {
        const FastColor color = static_cast<FastColor>(c);
        const int sign = signFor(color);
        const Bitboard ownPawns = pawns[c];
        const Bitboard enemyPawns = pawns[1 - c];
        const Bitboard ownAtt = pawnAtt[c];
        const Bitboard enemyAtt = pawnAtt[1 - c];

        for (int file = 0; file < 8; ++file) {
            const int count = std::popcount(ownPawns & FileMask[file]);
            if (count > 1) {
                score -= sign * (count - 1) *
                         TaperedScore{P.doubledPawnPenaltyMG,
                                      P.doubledPawnPenaltyEG};
            }
            if (count > 0 && (ownPawns & EMasks.adjacentFiles[file]) == 0) {
                score -= sign * count *
                         TaperedScore{P.isolatedPawnPenaltyMG,
                                      P.isolatedPawnPenaltyEG};
            }
        }

        const int supportedCount = std::popcount(ownPawns & ownAtt);
        if (supportedCount > 0) {
            score += sign * supportedCount *
                     TaperedScore{P.supportedPawnBonusMG,
                                  P.supportedPawnBonusEG};
        }

        const Bitboard phalanxMask =
            ((ownPawns & ~FileA) >> 1) | ((ownPawns & ~FileH) << 1);
        const int forwardDelta = color == White ? -8 : 8;

        Bitboard bb = ownPawns;
        while (bb != 0) {
            const int sq = popLsb(bb);
            const Bitboard sqBit = squareBB(sq);
            const int col = squareCol(sq);

            const bool inPhalanx = (phalanxMask & sqBit) != 0;
            if (inPhalanx) {
                const int advance = relativeRank(color, sq) - 1;
                const TaperedScore bonus =
                    advance * TaperedScore{P.phalanxPawnBonusMG,
                                           P.phalanxPawnBonusEG};
                const bool opposed =
                    (enemyPawns & EMasks.forwardFile[c][sq]) != 0;
                score += sign * (opposed ? TaperedScore{bonus.middleGame / 2,
                                                        bonus.endGame / 2}
                                         : bonus);
            }

            const bool passed =
                (ownPawns & EMasks.forwardFile[c][sq]) == 0 &&
                (enemyPawns & EMasks.passedPawn[c][sq]) == 0;
            if (passed) {
                entry.passed |= sqBit;
                continue;
            }

            const bool isolated = (ownPawns & EMasks.adjacentFiles[col]) == 0;
            const bool supported = (ownAtt & sqBit) != 0;
            if (!isolated && !supported) {
                const int supporters =
                    std::popcount(ownPawns & EMasks.supportPawn[c][sq]);
                const int stopSq = sq + forwardDelta;
                if (supporters == 0 && stopSq >= 0 && stopSq < 64 &&
                    (enemyAtt & squareBB(stopSq)) != 0) {
                    score -= sign * TaperedScore{P.backwardPawnPenaltyMG,
                                                 P.backwardPawnPenaltyEG};
                }
            }

            if ((enemyPawns & EMasks.forwardFile[c][sq]) == 0 &&
                (ownPawns & EMasks.forwardFile[c][sq]) == 0) {
                const int helpers =
                    std::popcount(ownPawns & EMasks.supportPawn[c][sq]);
                const int sentinels =
                    std::popcount(enemyPawns & EMasks.outpostEnemy[c][sq]);
                if (helpers >= sentinels) {
                    const int rank = relativeRank(color, sq);
                    score += sign *
                             TaperedScore{
                                 P.candidatePawnBonusMG +
                                     rank * P.candidatePawnRankBonusMG,
                                 P.candidatePawnBonusEG +
                                     rank * P.candidatePawnRankBonusEG};
                }
            }
        }
    }

    const int whiteSpace =
        std::popcount(WhiteSpaceZone & whitePawnAtt & ~blackPawnAtt);
    const int blackSpace =
        std::popcount(BlackSpaceZone & blackPawnAtt & ~whitePawnAtt);
    const int advantage = whiteSpace - blackSpace;
    entry.space = {advantage * P.spaceBonusMG, advantage * P.spaceBonusEG};
    return entry;
}

TaperedScore passedPawnRankBonus(int rank) {
    using Params = HeuristicEval::EvalParams;
    static constexpr int Params::*MiddleGame[6] = {
        &Params::passedPawnRank2MG, &Params::passedPawnRank3MG,
        &Params::passedPawnRank4MG, &Params::passedPawnRank5MG,
        &Params::passedPawnRank6MG, &Params::passedPawnRank7MG};
    static constexpr int Params::*EndGame[6] = {
        &Params::passedPawnRank2EG, &Params::passedPawnRank3EG,
        &Params::passedPawnRank4EG, &Params::passedPawnRank5EG,
        &Params::passedPawnRank6EG, &Params::passedPawnRank7EG};
    const int index = std::clamp(rank - 2, 0, 5);
    return {P.*MiddleGame[index], P.*EndGame[index]};
}

int unstoppablePasserMoves(const FastBoard &pos, FastColor color,
                           Bitboard passedBB) {
    const FastColor defender = oppositeColor(color);
    if (pos.nonPawnCount[defender] != 0) {
        return 0;
    }
    const int ownKingSq = pos.kingSq[color];
    const int defKingSq = pos.kingSq[defender];
    if (ownKingSq >= 64 || defKingSq >= 64) {
        return 0;
    }
    const int ownKingRow = squareRow(ownKingSq);
    const int ownKingCol = squareCol(ownKingSq);
    const int defKingRow = squareRow(defKingSq);
    const int defKingCol = squareCol(defKingSq);
    const int promoRow = color == White ? 0 : 7;

    int best = 0;
    Bitboard bb = passedBB & pos.pieces(color, Pawn);
    while (bb != 0) {
        const int sq = popLsb(bb);
        const int rank = relativeRank(color, sq);
        int moves = 8 - rank;
        if (rank == 2) {
            --moves;
        }
        const int pawnRow = squareRow(sq);
        const int pawnCol = squareCol(sq);
        if (ownKingCol == pawnCol &&
            (color == White ? (ownKingRow < pawnRow) : (ownKingRow > pawnRow))) {
            ++moves;
        }
        const int kingMoves =
            std::max(std::abs(defKingRow - promoRow),
                     std::abs(defKingCol - pawnCol)) -
            (pos.sideToMove == defender ? 1 : 0);
        if (moves < kingMoves && (best == 0 || moves < best)) {
            best = moves;
        }
    }
    return best;
}

TaperedScore evaluatePassedPawns(const FastBoard &pos, const AttackState &att,
                                 Bitboard passed) {
    if (passed == 0) {
        return {};
    }
    const Bitboard occ = pos.occupancy();
    TaperedScore score;
    Bitboard bb = passed;
    while (bb != 0) {
        const int sq = popLsb(bb);
        const FastColor color = pieceByteColor(pos.board[sq]);
        const FastColor enemy = oppositeColor(color);
        const int sign = signFor(color);
        const int rank = relativeRank(color, sq);
        TaperedScore bonus = passedPawnRankBonus(rank);

        const int forward = color == White ? -8 : 8;
        const int aheadSq = sq + forward;
        const Bitboard enemyAtt = att.by[enemy][0];
        if (aheadSq >= 0 && aheadSq < 64) {
            const Bitboard aheadBit = squareBB(aheadSq);
            if ((occ & aheadBit) != 0) {
                bonus -= TaperedScore{P.passedPawnBlockedPenaltyMG,
                                      P.passedPawnBlockedPenaltyEG};
            } else if ((enemyAtt & aheadBit) != 0) {
                bonus -= TaperedScore{P.passedPawnControlledPenaltyMG,
                                      P.passedPawnControlledPenaltyEG};
            }
        }

        const Bitboard rear = EMasks.rearFile[color][sq];
        if ((rear & pos.pieces(color, Rook)) != 0) {
            bonus += TaperedScore{P.passedPawnRookBehindMG,
                                  P.passedPawnRookBehindEG};
        }
        if ((rear & pos.pieces(enemy, Rook)) != 0) {
            bonus -= TaperedScore{P.passedPawnEnemyRookBehindMG,
                                  P.passedPawnEnemyRookBehindEG};
        }

        const int advance = std::max(0, rank - 3);
        if (advance > 0) {
            const Bitboard road = EMasks.forwardFile[color][sq];
            const Bitboard blocked = road & (occ | enemyAtt);
            if (blocked == 0) {
                bonus += advance * TaperedScore{P.passedPawnFreePathMG,
                                                P.passedPawnFreePathEG};
            } else if (aheadSq >= 0 && aheadSq < 64 &&
                       (blocked & squareBB(aheadSq)) == 0) {
                bonus += advance * TaperedScore{P.passedPawnSafeStopMG,
                                                P.passedPawnSafeStopEG};
            }
        }

        score += sign * TaperedScore{std::max(bonus.middleGame, 0),
                                     std::max(bonus.endGame, 0)};

        if (advance > 0) {
            const int ownKingSq = pos.kingSq[color];
            const int enemyKingSq = pos.kingSq[enemy];
            if (ownKingSq < 64 && enemyKingSq < 64 &&
                aheadSq >= 0 && aheadSq < 64) {
                const int aheadRow = squareRow(aheadSq);
                const int aheadCol = squareCol(aheadSq);
                const auto steps = [&](int kingSq) {
                    return std::min(5,
                                    std::max(std::abs(squareRow(kingSq) - aheadRow),
                                             std::abs(squareCol(kingSq) - aheadCol)));
                };
                score += sign *
                         TaperedScore{0, advance *
                                             (P.passedKingDistanceEnemy *
                                                  steps(enemyKingSq) -
                                              P.passedKingDistanceOwn *
                                                  steps(ownKingSq))};
            }
        }
    }

    const int whiteMoves = unstoppablePasserMoves(pos, White, passed);
    const int blackMoves = unstoppablePasserMoves(pos, Black, passed);
    const auto unstoppableBonus = [](int moves) {
        return TaperedScore{
            0, std::max(0, P.unstoppablePasserBonus -
                               P.unstoppablePasserPerMove * moves)};
    };
    if (whiteMoves != 0 && (blackMoves == 0 || whiteMoves < blackMoves)) {
        score += unstoppableBonus(whiteMoves);
    } else if (blackMoves != 0 &&
               (whiteMoves == 0 || blackMoves < whiteMoves)) {
        score -= unstoppableBonus(blackMoves);
    }
    return score;
}

void evaluatePieceTerms(const FastBoard &pos, const AttackState &att,
                        TaperedScore *threats, TaperedScore *outposts,
                        TaperedScore *badBishops) {
    const auto &t = attackTables();

    for (int c = 0; c < 2; ++c) {
        const FastColor color = static_cast<FastColor>(c);
        const FastColor enemy = oppositeColor(color);
        const int sign = signFor(color);
        const Bitboard ownPawns = pos.pieces(color, Pawn);
        const Bitboard enemyPawns = pos.pieces(enemy, Pawn);
        const Bitboard ownPawnAtt = att.by[c][Pawn];
        const Bitboard outpostRanks =
            color == White ? WhiteOutpostRanks : BlackOutpostRanks;

        // 1. Bad bishops.
        Bitboard bishops = pos.pieces(color, Bishop);
        while (bishops != 0) {
            const int sq = popLsb(bishops);
            const Bitboard sqColorMask =
                (squareBB(sq) & EvenSquaresMask) != 0 ? EvenSquaresMask
                                                      : OddSquaresMask;
            const int blockers = std::popcount(ownPawns & sqColorMask);
            const int penalty =
                std::min(P.badBishopCap, blockers * P.badBishopPenalty);
            *badBishops -= sign * bothPhases(penalty);
        }

        // 2. Threats (only pieces attacked by the enemy, excluding kings).
        Bitboard attacked =
            pos.byColor[c] & ~pos.byType[King] & att.by[enemy][0];
        while (attacked != 0) {
            const int sq = popLsb(attacked);
            const Bitboard sqBit = squareBB(sq);
            const int value = pieceTypeValue(pieceByteType(pos.board[sq]));
            if ((att.by[c][0] & sqBit) == 0) {
                *threats -= sign * bothPhases(value / P.hangingPieceDivisor);
            } else {
                int cheapest = std::numeric_limits<int>::max();
                bool found = false;
                for (const FastPiece pt : {Pawn, Knight, Bishop, Rook, Queen}) {
                    if ((att.by[enemy][pt] & sqBit) != 0) {
                        const int v = pieceTypeValue(pt);
                        if (v < cheapest) {
                            cheapest = v;
                            found = true;
                        }
                    }
                }
                if (found && cheapest + P.cheapAttackerMargin < value) {
                    *threats -= sign * bothPhases((value - cheapest) /
                                                  P.cheapAttackerDivisor);
                }
            }
        }

        // 3. Outposts (knights and bishops).
        const Bitboard candidateOutpostSquares = outpostRanks & ownPawnAtt;
        const auto isOutpostSq = [&](int sq) {
            return (squareBB(sq) & candidateOutpostSquares) != 0 &&
                   (enemyPawns & EMasks.outpostEnemy[c][sq]) == 0;
        };

        Bitboard knights = pos.pieces(color, Knight);
        while (knights != 0) {
            const int sq = popLsb(knights);
            if (isOutpostSq(sq)) {
                *outposts += sign * TaperedScore{P.outpostKnightMG,
                                                 P.outpostKnightEG};
            } else {
                Bitboard jumps = t.knight[sq] & ~pos.byColor[c] &
                                 candidateOutpostSquares;
                while (jumps != 0) {
                    const int target = popLsb(jumps);
                    if ((enemyPawns & EMasks.outpostEnemy[c][target]) == 0) {
                        *outposts += sign *
                                     TaperedScore{P.outpostReachableKnightMG,
                                                  P.outpostReachableKnightEG};
                        break;
                    }
                }
            }
        }

        bishops = pos.pieces(color, Bishop);
        while (bishops != 0) {
            const int sq = popLsb(bishops);
            if (isOutpostSq(sq)) {
                *outposts += sign * TaperedScore{P.outpostBishopMG,
                                                 P.outpostBishopEG};
            }
        }
    }
}

TaperedScore evaluateConnectedRooks(const FastBoard &pos) {
    const Bitboard occ = pos.occupancy();
    TaperedScore score;
    for (int c = 0; c < 2; ++c) {
        Bitboard rooks = pos.pieces(static_cast<FastColor>(c), Rook);
        if (std::popcount(rooks) != 2) {
            continue;
        }
        const int first = popLsb(rooks);
        const int second = popLsb(rooks);
        if ((rookAttacks(first, occ) & squareBB(second)) != 0) {
            score += signFor(static_cast<FastColor>(c)) *
                     TaperedScore{P.connectedRooksMG, P.connectedRooksEG};
        }
    }
    return score;
}

TaperedScore kingDanger(const FastBoard &pos, FastColor color,
                        const AttackState &att, int shelterFilesMissing) {
    const int own = color;
    const int enemy = 1 - own;
    const bool enemyHasQueen =
        pos.pieces(static_cast<FastColor>(enemy), Queen) != 0;
    if (att.kingAttackers[enemy] == 0 ||
        (att.kingAttackers[enemy] < 2 && !enemyHasQueen)) {
        return {};
    }

    int danger = att.kingAttackers[enemy] * att.kingAttackersWeight[enemy] +
                 att.kingAdjacentAttacks[enemy] * P.kingAdjacentAttackWeight +
                 shelterFilesMissing * P.kingDangerOpenFile;

    Bitboard defendedByNonQueen = 0;
    if (P.pawnValue < P.queenValue) {
        defendedByNonQueen |= att.by[own][Pawn];
    }
    if (P.knightValue < P.queenValue) {
        defendedByNonQueen |= att.by[own][Knight];
    }
    if (P.bishopValue < P.queenValue) {
        defendedByNonQueen |= att.by[own][Bishop];
    }
    if (P.rookValue < P.queenValue) {
        defendedByNonQueen |= att.by[own][Rook];
    }
    const Bitboard safeMask = ~defendedByNonQueen;
    const int safeCheckWeights[4] = {P.safeCheckKnight, P.safeCheckBishop,
                                     P.safeCheckRook, P.safeCheckQueen};
    for (int kind = 0; kind < 4; ++kind) {
        const Bitboard safeChecks = att.checkCandidates[enemy][kind] & safeMask;
        if (safeChecks != 0) {
            danger += std::popcount(safeChecks) * safeCheckWeights[kind];
        }
    }

    if (!enemyHasQueen) {
        danger -= P.queenlessDangerReduction;
    }
    if (danger <= 0) {
        return {};
    }
    return TaperedScore{danger * danger / std::max(1, P.kingDangerDivisor),
                        danger / 16};
}

TaperedScore kingSafetyFor(const FastBoard &pos, FastColor color,
                           const AttackState &att) {
    const int kSq = pos.kingSq[color];
    if (kSq >= 64) {
        return {};
    }
    const int kRow = squareRow(kSq);
    const int kCol = squareCol(kSq);
    const FastColor enemy = oppositeColor(color);

    TaperedScore score;
    const int homeRow = color == White ? 7 : 0;
    if (kRow == homeRow && (kCol == 2 || kCol == 6)) {
        score += middleGameOnly(P.castledBonus);
    }

    const uint8_t sideCastleMask =
        color == White ? (WhiteKingSide | WhiteQueenSide)
                       : (BlackKingSide | BlackQueenSide);
    const bool canCastle = (pos.castling & sideCastleMask) != 0;
    if (canCastle) {
        score += middleGameOnly(P.castlingRightsBonus);
    }

    const bool enemyHasHeavyPieces =
        (pos.pieces(enemy, Rook) | pos.pieces(enemy, Queen)) != 0;
    const Bitboard ownPawns = pos.pieces(color, Pawn);
    const Bitboard enemyPawns = pos.pieces(enemy, Pawn);

    // Pawn shelter (up to 3 squares ahead on adjacent files).
    const TaperedScore shelter{
        P.kingShelterBonus,
        P.kingShelterBonus * P.kingShelterEndgamePercent / 100};
    const Bitboard ahead3 = EMasks.kingShelterAhead3[color][kRow];
    const int firstFile = std::max(0, kCol - 1);
    const int lastFile = std::min(7, kCol + 1);
    for (int file = firstFile; file <= lastFile; ++file) {
        const Bitboard filePawns = ownPawns & FileMask[file] & ahead3;
        if (filePawns == 0) {
            continue;
        }
        const int pawnSq = color == White ? (63 - std::countl_zero(filePawns))
                                          : std::countr_zero(filePawns);
        const int distance = std::abs(squareRow(pawnSq) - kRow);
        const int percent = distance == 1
                                ? 100
                                : (distance == 2 ? P.kingShelterSecondRankPercent
                                                 : P.kingShelterThirdRankPercent);
        const int filePercent =
            file == kCol ? 100 : P.kingShelterAdjacentFilePercent;
        score += TaperedScore{
            shelter.middleGame * percent * filePercent / 10000,
            shelter.endGame * percent / 100};
    }

    int filesMissing = 0;
    for (int file = firstFile; file <= lastFile; ++file) {
        if ((ownPawns & FileMask[file]) != 0) {
            continue;
        }
        ++filesMissing;
        if (enemyHasHeavyPieces) {
            const int penalty = (enemyPawns & FileMask[file]) == 0
                                    ? P.openFileNearKingPenalty
                                    : P.openFileNearKingPenalty *
                                          P.kingSemiOpenFilePercent / 100;
            score -= middleGameOnly(penalty);
        }
    }

    if ((kCol == 3 || kCol == 4) && !canCastle && filesMissing > 0 &&
        enemyHasHeavyPieces) {
        score -= middleGameOnly(P.kingCentreOpenFilePenalty);
    }

    // Enemy pawn storm.
    const Bitboard stormRanks =
        enemy == White ? WhiteStormRanks : BlackStormRanks;
    Bitboard stormPawns =
        enemyPawns & EMasks.kingStormFiles[kCol] & stormRanks;
    const int enemyForward = enemy == White ? -8 : 8;
    while (stormPawns != 0) {
        const int sq = popLsb(stormPawns);
        const int rank = relativeRank(enemy, sq);
        const int frontSq = sq + enemyForward;
        const bool blocked = frontSq >= 0 && frontSq < 64 &&
                             (ownPawns & squareBB(frontSq)) != 0;
        const int penalty = blocked ? P.kingPawnStormPenalty *
                                          P.kingStormBlockedPercent / 100
                                    : P.kingPawnStormPenalty;
        score -= (rank - 4) * middleGameOnly(penalty);
    }

    score -= kingDanger(pos, color, att, filesMissing);
    return score;
}

TaperedScore evaluateKingSafety(const FastBoard &pos, const AttackState &att) {
    TaperedScore score = kingSafetyFor(pos, White, att);
    score -= kingSafetyFor(pos, Black, att);
    return score;
}

int centreDistance(int row, int col) {
    const int rowDistance = std::max({0, 3 - row, row - 4});
    const int colDistance = std::max({0, 3 - col, col - 4});
    return std::max(rowDistance, colDistance);
}

int manhattanDistance(int r1, int c1, int r2, int c2) {
    return std::abs(r1 - r2) + std::abs(c1 - c2);
}

TaperedScore mopUpFor(FastColor strong, const FastBoard &pos) {
    const FastColor weak = oppositeColor(strong);
    const int strongKingSq = pos.kingSq[strong];
    const int weakKingSq = pos.kingSq[weak];
    if (strongKingSq >= 64 || weakKingSq >= 64) {
        return {};
    }
    const int strongRow = squareRow(strongKingSq);
    const int strongCol = squareCol(strongKingSq);
    const int weakRow = squareRow(weakKingSq);
    const int weakCol = squareCol(weakKingSq);

    const int proximity =
        14 - manhattanDistance(strongRow, strongCol, weakRow, weakCol);
    const bool bishopAndKnightOnly =
        (pos.byColor[weak] & ~pos.byType[King]) == 0 &&
        (pos.pieces(strong, Pawn) | pos.pieces(strong, Rook) |
         pos.pieces(strong, Queen)) == 0 &&
        std::popcount(pos.pieces(strong, Bishop)) == 1 &&
        std::popcount(pos.pieces(strong, Knight)) == 1;
    if (bishopAndKnightOnly) {
        const bool bishopEven =
            (pos.pieces(strong, Bishop) & EvenSquaresMask) != 0;
        const int firstCol = bishopEven ? 0 : 7;
        const int secondCol = 7 - firstCol;
        const int cornerDistance = std::min(
            manhattanDistance(weakRow, weakCol, 0, firstCol),
            manhattanDistance(weakRow, weakCol, 7, secondCol));
        return {0, (14 - cornerDistance) * P.mopUpBishopKnightCornerBonus +
                       proximity * P.mopUpKingProximityBonus};
    }

    const int edge = centreDistance(weakRow, weakCol);
    return {0, edge * P.mopUpEdgeBonus + proximity * P.mopUpKingProximityBonus};
}

TaperedScore evaluateEndgameMopUp(const FastBoard &pos, int materialBalance) {
    if (materialBalance >= P.mopUpMaterialThreshold) {
        return mopUpFor(White, pos);
    }
    if (materialBalance <= -P.mopUpMaterialThreshold) {
        const TaperedScore score = mopUpFor(Black, pos);
        return {-score.middleGame, -score.endGame};
    }
    return {};
}

bool isOppositeColourBishopEnding(const FastBoard &pos) {
    if ((pos.byType[Knight] | pos.byType[Rook] | pos.byType[Queen]) != 0) {
        return false;
    }
    const Bitboard wBishops = pos.pieces(White, Bishop);
    const Bitboard bBishops = pos.pieces(Black, Bishop);
    if (std::popcount(wBishops) != 1 || std::popcount(bBishops) != 1) {
        return false;
    }
    return ((wBishops & EvenSquaresMask) != 0) !=
           ((bBishops & EvenSquaresMask) != 0);
}

int nonPawnMaterial(const FastBoard &pos, FastColor color) {
    return std::popcount(pos.pieces(color, Knight)) * P.knightValue +
           std::popcount(pos.pieces(color, Bishop)) * P.bishopValue +
           std::popcount(pos.pieces(color, Rook)) * P.rookValue +
           std::popcount(pos.pieces(color, Queen)) * P.queenValue;
}

struct EndgameScale {
    int middleGame = ScaleNormal;
    int endGame = ScaleNormal;

    void limitBoth(int factor) {
        middleGame = std::min(middleGame, factor);
        endGame = std::min(endGame, factor);
    }
    void limitEndGame(int factor) {
        endGame = std::min(endGame, factor);
    }
};

EndgameScale endgameScale(const FastBoard &pos, FastColor strong) {
    EndgameScale scale;
    const FastColor weak = oppositeColor(strong);
    const Bitboard strongPawnsBB = pos.pieces(strong, Pawn);
    const Bitboard weakPawnsBB = pos.pieces(weak, Pawn);
    const int strongPawns = std::popcount(strongPawnsBB);
    const int weakPawns = std::popcount(weakPawnsBB);
    const int strongMaterial = nonPawnMaterial(pos, strong);
    const int weakMaterial = nonPawnMaterial(pos, weak);

    if (isOppositeColourBishopEnding(pos)) {
        scale.limitBoth(pos.byType[Pawn] == 0 ? P.oppositeBishopsNoPawnsScale
                                              : P.oppositeBishopsScale);
    }

    if (strongPawns == 0 && strongMaterial - weakMaterial <= P.bishopValue) {
        scale.limitBoth(strongMaterial < P.rookValue ? P.noPawnsMinorScale
                        : weakMaterial <= P.bishopValue
                            ? P.noPawnsRookVsMinorScale
                            : P.noPawnsRookScale);
    }

    const bool weakIsBareKing = weakMaterial == 0 && weakPawns == 0;
    if (!weakIsBareKing) {
        scale.limitEndGame(P.generalScaleBase +
                           P.generalScalePerPawn * strongPawns);
    }

    if (std::popcount(pos.pieces(White, Rook)) == 1 &&
        std::popcount(pos.pieces(Black, Rook)) == 1 &&
        strongMaterial == P.rookValue && weakMaterial == P.rookValue &&
        strongPawns - weakPawns <= 1) {
        const bool oneFlank = ((strongPawnsBB & QueensideMask) != 0) !=
                              ((strongPawnsBB & KingsideMask) != 0);
        const int weakKingSq = pos.kingSq[weak];
        if (oneFlank && weakKingSq < 64 &&
            (attackTables().king[weakKingSq] & weakPawnsBB) != 0) {
            scale.limitEndGame(P.rookEndgameOneFlankScale);
        }
    }
    return scale;
}

} // namespace

namespace HeuristicSearch {

void clearPawnCache() {
    PawnCache::instance().clear();
}

int evaluateBoard(const FastBoard &pos, bool inCheck,
                  HeuristicEval::EvalBreakdown *breakdown) {
    if (isInsufficientMaterial(pos) || isWrongColouredRookPawnEnding(pos)) {
        return 0;
    }

    if (std::popcount(pos.occupancy()) == 3 &&
        std::popcount(pos.byType[Pawn]) == 1 &&
        pos.kingSq[White] < 64 && pos.kingSq[Black] < 64) {
        const int value = kpkScore(pos);
        if (breakdown != nullptr) {
            breakdown->total = value;
        }
        return value;
    }

    AttackState att;
    collectAttacksAndMobility(pos, att);

    TaperedScore material;
    TaperedScore placement;
    evaluateMaterialAndPlacement(pos, &material, &placement);

    PawnEntry pawnEntry;
    if (!PawnCache::instance().lookup(pos.pawnHash, &pawnEntry)) {
        pawnEntry = computePawnEntry(pos, att.by[White][Pawn], att.by[Black][Pawn]);
        PawnCache::instance().store(pos.pawnHash, pawnEntry);
    }
    TaperedScore pawns = pawnEntry.structure;
    pawns += evaluatePassedPawns(pos, att, pawnEntry.passed);

    TaperedScore mobility = att.mobility[White];
    mobility -= att.mobility[Black];
    mobility += att.trappedRooks;

    const TaperedScore kingSafety = evaluateKingSafety(pos, att);
    const TaperedScore space = pawnEntry.space;
    TaperedScore threats;
    TaperedScore outposts;
    TaperedScore badBishops;
    evaluatePieceTerms(pos, att, &threats, &outposts, &badBishops);
    const TaperedScore connectedRooks = evaluateConnectedRooks(pos);
    const TaperedScore mopUp = evaluateEndgameMopUp(pos, material.middleGame);
    const FastColor side = static_cast<FastColor>(pos.sideToMove);
    const TaperedScore tempo = signFor(side) * bothPhases(P.tempoBonus);
    const TaperedScore inCheckPenalty =
        inCheck ? -signFor(side) * bothPhases(P.inCheckPenalty)
                : TaperedScore{};

    TaperedScore score = material;
    score += placement;
    score += pawns;
    score += space;
    score += mobility;
    score += kingSafety;
    score += threats;
    score += outposts;
    score += badBishops;
    score += connectedRooks;
    score += mopUp;
    score += tempo;
    score += inCheckPenalty;

    const int phase = computePhase(pos);
    const int lead = score.endGame != 0 ? score.endGame : score.middleGame;
    const EndgameScale scale = endgameScale(pos, lead >= 0 ? White : Black);
    const int value = interpolate(
        score.middleGame * scale.middleGame / ScaleNormal,
        score.endGame * scale.endGame / ScaleNormal, phase);

    if (breakdown != nullptr) {
        breakdown->material = taperedValue(material, phase);
        breakdown->placement = taperedValue(placement, phase);
        breakdown->pawns = taperedValue(pawns, phase);
        breakdown->space = taperedValue(space, phase);
        breakdown->mobility = taperedValue(mobility, phase);
        breakdown->kingSafety = taperedValue(kingSafety, phase);
        breakdown->threats = taperedValue(threats, phase);
        breakdown->outposts = taperedValue(outposts, phase);
        breakdown->badBishops = taperedValue(badBishops, phase);
        breakdown->connectedRooks = taperedValue(connectedRooks, phase);
        breakdown->mopUp = taperedValue(mopUp, phase);
        breakdown->tempo = taperedValue(tempo, phase);
        breakdown->inCheck = taperedValue(inCheckPenalty, phase);
        breakdown->scaleFactor = scale.endGame;
        breakdown->total = value;
    }
    return value;
}

bool hasAnyLegalMove(const Rules &rules) {
    return FastBoard::fromRules(rules).hasAnyLegalMove();
}

} // namespace HeuristicSearch

int HeuristicEval::pieceValue(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return P.pawnValue;
    case Rules::PieceType::Knight: return P.knightValue;
    case Rules::PieceType::Bishop: return P.bishopValue;
    case Rules::PieceType::Rook:   return P.rookValue;
    case Rules::PieceType::Queen:  return P.queenValue;
    case Rules::PieceType::King:
    case Rules::PieceType::None:   return 0;
    }
    return 0;
}

const HeuristicEval::EvalParams &HeuristicEval::params() {
    return activeParams;
}

void HeuristicEval::setParams(const EvalParams &params) {
    activeParams = params;
    HeuristicSearch::clearCaches();
}

void HeuristicEval::resetParams() {
    activeParams = EvalParams{};
    HeuristicSearch::clearCaches();
}

int HeuristicEval::evaluateCentipawns(const Rules &rules) {
    return evaluateBreakdown(rules).total;
}

HeuristicEval::EvalBreakdown HeuristicEval::evaluateBreakdown(const Rules &rules) {
    EvalBreakdown breakdown;
    const FastBoard pos = FastBoard::fromRules(rules);
    const FastColor sideToMove = static_cast<FastColor>(pos.sideToMove);
    const bool inCheck = pos.isInCheck(sideToMove);
    if (!pos.hasAnyLegalMove()) {
        if (inCheck) {
            breakdown.total = sideToMove == White ? -MateScore : MateScore;
        }
        return breakdown;
    }

    if (rules.isFiftyMoveRule() || rules.isThreefoldRepetition()) {
        return breakdown;
    }

    evaluateBoard(pos, inCheck, &breakdown);
    breakdown.total =
        std::clamp(breakdown.total, -MateScore + 1, MateScore - 1);
    return breakdown;
}

double HeuristicEval::centipawnsToPercentage(double cp) {
    return WinProbability::fromCentipawns(cp);
}

double HeuristicEval::evaluateDisplayPercentage(const Rules &rules) {
    return centipawnsToPercentage(static_cast<double>(evaluateCentipawns(rules)));
}
