//
// Classical heuristic evaluation for chess positions.
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
#include <optional>
#include <vector>

// The evaluator and the search split the same helpers through this interface.
using HeuristicSearch::Board;
using HeuristicSearch::evaluateBoard;
using HeuristicSearch::hasAnyLegalMove;
using HeuristicSearch::isInside;
using HeuristicSearch::signFor;

namespace {

using AttackMap = std::array<std::array<bool, 8>, 8>;
using ValueMap = std::array<std::array<int, 8>, 8>;

constexpr int MateScore = HeuristicEval::MateScore;

constexpr int MaxPhase = 24;
constexpr int NoKingRow = -1;
constexpr int NoFile = -1;
constexpr int EmptyRow = 8;
// The king is never treated as a cheap attacker: it cannot win material by
// capturing a defended piece.
constexpr int KingAttackerValue = 100000;

// The active weights, published through HeuristicEval::params(). The evaluator
// reads them through this reference, so setParams() retunes every term at once
// and the reference keeps pointing at the live object. EvalParams has a
// constant initialiser, so this storage needs no dynamic initialisation.
HeuristicEval::EvalParams activeParams;
const HeuristicEval::EvalParams &P = activeParams;

// Middle-game and end-game value of one evaluation term. Every term returns one
// of these and `evaluateBoard` interpolates the total once with the phase, so a
// term can weigh differently in the two phases without repeating the
// interpolation in each of them.
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

// A term that is worth the same in both phases.
constexpr TaperedScore bothPhases(int value) {
    return {value, value};
}

// A term that only exists in the middlegame, such as king safety.
constexpr TaperedScore middleGameOnly(int value) {
    return {value, 0};
}

constexpr TaperedScore operator*(int factor, const TaperedScore &score) {
    return {factor * score.middleGame, factor * score.endGame};
}

// Tables are indexed from the point of view of the relevant side:
// index 0 is that side's home rank and index 7 its promotion rank.
// The seventh rank matches the sixth: what a pawn about to promote is worth
// lives in the passed pawn table, which also knows whether the pawn is passed.
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

// The endgame table of each piece; the king has its own. They are written from
// the shape of the endgame rather than taken from an engine: a pawn is worth what
// it has advanced (the seventh rank matches the sixth, as in the middlegame table,
// the rest of a passed pawn's worth being the passed pawn table's), the minor
// pieces and the queen want the centre more and more the fewer pieces stand in
// the way (falling off with the distance from the middle of the board), and a
// rook only minds the edge files. Each is shifted so that it has the mean of its
// middlegame table, which leaves the material balance where it was.
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

struct Offset {
    int rowDelta;
    int columnDelta;
};

constexpr std::array<Offset, 8> KnightOffsets = {{
    {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
    {1, -2},  {1, 2},  {2, -1},  {2, 1}
}};

constexpr std::array<Offset, 8> KingOffsets = {{
    {-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
    {0, 1},   {1, -1}, {1, 0},  {1, 1}
}};

constexpr std::array<Offset, 4> BishopDirections = {{
    {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
}};

constexpr std::array<Offset, 4> RookDirections = {{
    {-1, 0}, {1, 0}, {0, -1}, {0, 1}
}};

struct PieceOnBoard {
    Rules::PieceType type;
    Rules::Color color;
    int row;
    int column;
    bool hasMoved;
};

// Everything the evaluation terms need, gathered in a single board pass.
struct BoardAnalysis {
    std::array<PieceOnBoard, 32> pieces{};
    int pieceCount = 0;
    std::array<int, 8> whitePawnCounts{};
    std::array<int, 8> blackPawnCounts{};
    // Bit `row` of a file's mask is set when a pawn of that colour stands on
    // that row, which turns "pawns ahead of this square" into a mask test.
    std::array<quint8, 8> whitePawnRows{};
    std::array<quint8, 8> blackPawnRows{};
    // Least advanced pawn per file: greatest row for White (-1 when empty),
    // smallest row for Black (8 when empty).
    std::array<int, 8> whiteRearRows{};
    std::array<int, 8> blackRearRows{};
    // Pawn totals and pawns per square colour, indexed by colour then by
    // (row + column) % 2.
    std::array<int, 2> pawnTotals{};
    std::array<std::array<int, 2>, 2> pawnsOnSquareColour{};
    std::array<int, 2> bishopCounts{};
    std::array<int, 2> knightCounts{};
    std::array<int, 2> rookCounts{};
    std::array<int, 2> queenCounts{};
    // Bishops per square colour, indexed by colour then by (row + column) % 2.
    std::array<std::array<int, 2>, 2> bishopSquareColourCounts{};
    int whiteKingRow = NoKingRow;
    int whiteKingColumn = -1;
    int blackKingRow = NoKingRow;
    int blackKingColumn = -1;
    bool whiteCanCastleKingSide = false;
    bool whiteCanCastleQueenSide = false;
    bool blackCanCastleKingSide = false;
    bool blackCanCastleQueenSide = false;
    int phase = 0;
    bool hasPawnOrMajor = false;
};

struct AttackInfo {
    AttackMap squares{};
    // Only pawn attacks, used for mobility areas and outposts.
    AttackMap pawnSquares{};
    // Cheapest attacker value per square (0 when the square is not attacked).
    ValueMap cheapest{};
    // Mobility of this colour's pieces, already weighted with the tapered
    // per-piece weights: the attack walk is the same walk that counts it.
    TaperedScore mobility{};
    // What this colour's knights, bishops, rooks and queens do to the enemy
    // king, gathered by the same walk: how many of them attack a square of the
    // king's zone, the sum of their weights, the attacks on the squares right
    // beside the king, and for each kind of piece the squares (one bit per
    // row * 8 + column) it attacks and from which it would give check.
    int kingAttackers = 0;
    int kingAttackersWeight = 0;
    int kingAdjacentAttacks = 0;
    std::array<quint64, 4> checkCandidates{};
    // The rooks of this colour with the squares each can move to, for the rook
    // that its own king shuts in.
    struct RookMobility {
        int row = 0;
        int column = 0;
        int moves = 0;
    };
    std::array<RookMobility, 10> rooks{};
    int rookCount = 0;
};

// Kinds of piece that give a check, indexing `AttackInfo::checkCandidates` and
// `KingMasks::checks`.
constexpr int KnightKind = 0;
constexpr int BishopKind = 1;
constexpr int RookKind = 2;
constexpr int QueenKind = 3;

constexpr quint64 squareBit(int row, int column) {
    return quint64{1} << (row * 8 + column);
}

// The squares that matter for an attack on a king, by colour of that king: its
// zone (the squares around it plus one more row towards the enemy, shifted
// inwards on the edge files), the squares next to it, and for each kind of piece
// the squares from which that piece would check it.
struct KingMasks {
    std::array<quint64, 2> zone{};
    std::array<quint64, 2> adjacent{};
    std::array<std::array<quint64, 4>, 2> checks{};
};


constexpr int colorIndex(Rules::Color color) {
    return color == Rules::Color::White ? 0 : 1;
}


constexpr int relativeRank(Rules::Color color, int row) {
    return color == Rules::Color::White ? 8 - row : row + 1;
}

constexpr int relativeSquare(Rules::Color color, int row, int column) {
    const int rank = color == Rules::Color::White ? 7 - row : row;
    return rank * 8 + column;
}

constexpr int forwardDirection(Rules::Color color) {
    return color == Rules::Color::White ? -1 : 1;
}

// The middlegame and the endgame table of a piece, or null tables for a piece
// that has none.
struct SquareTables {
    const std::array<int, 64> *middleGame = nullptr;
    const std::array<int, 64> *endGame = nullptr;
};

constexpr SquareTables pieceSquareTables(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return {&PawnMiddleGamePST, &PawnEndGamePST};
    case Rules::PieceType::Knight: return {&KnightMiddleGamePST, &KnightEndGamePST};
    case Rules::PieceType::Bishop: return {&BishopMiddleGamePST, &BishopEndGamePST};
    case Rules::PieceType::Rook:   return {&RookMiddleGamePST, &RookEndGamePST};
    case Rules::PieceType::Queen:  return {&QueenMiddleGamePST, &QueenEndGamePST};
    case Rules::PieceType::King:   return {&KingMiddleGamePST, &KingEndGamePST};
    case Rules::PieceType::None:   return {};
    }
    return {};
}

// Rounds to the nearest centipawn, away from zero, so that a mirrored position
// keeps scoring exactly the opposite.
constexpr int interpolate(int middleGameValue, int endGameValue, int phase) {
    const int total =
        middleGameValue * phase + endGameValue * (MaxPhase - phase);
    return total >= 0 ? (total + MaxPhase / 2) / MaxPhase
                      : -((-total + MaxPhase / 2) / MaxPhase);
}

int taperedValue(const TaperedScore &score, int phase) {
    return interpolate(score.middleGame, score.endGame, phase);
}

// Pawns and kings keep a linear mobility weight: a pawn's few moves and the king's
// safe squares are not a matter of shape.
TaperedScore mobilityWeight(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:
        return {P.mobilityPawnMG, P.mobilityPawnEG};
    case Rules::PieceType::King:
        return {P.mobilityKingMG, P.mobilityKingEG};
    case Rules::PieceType::Knight:
    case Rules::PieceType::Bishop:
    case Rules::PieceType::Rook:
    case Rules::PieceType::Queen:
    case Rules::PieceType::None:
        return {};
    }
    return {};
}

// What a knight, bishop, rook or queen is worth for the number of squares it can
// move to, in centipawns, indexed by that number. The shape is what matters: the
// first squares count for far more than the last ones, and a piece with almost
// none is heavily penalised. Each table is the saturating curve
// `A * n / (n + h)`, shifted so that a typical mobility is worth zero (knight 4,
// bishop 6, rook 7, queen 13); the `mobilityScale*` weights scale them in percent.
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
    return table[static_cast<std::size_t>(std::clamp(moves, 0,
                                                     static_cast<int>(Size) - 1))];
}

// Mobility of a knight, bishop, rook or queen with `moves` squares to go to.
TaperedScore shapedMobility(Rules::PieceType type, int moves) {
    switch (type) {
    case Rules::PieceType::Knight:
        return {tableValue(KnightMobilityMG, moves) * P.mobilityScaleKnightMG / 100,
                tableValue(KnightMobilityEG, moves) * P.mobilityScaleKnightEG / 100};
    case Rules::PieceType::Bishop:
        return {tableValue(BishopMobilityMG, moves) * P.mobilityScaleBishopMG / 100,
                tableValue(BishopMobilityEG, moves) * P.mobilityScaleBishopEG / 100};
    case Rules::PieceType::Rook:
        return {tableValue(RookMobilityMG, moves) * P.mobilityScaleRookMG / 100,
                tableValue(RookMobilityEG, moves) * P.mobilityScaleRookEG / 100};
    case Rules::PieceType::Queen:
        return {tableValue(QueenMobilityMG, moves) * P.mobilityScaleQueenMG / 100,
                tableValue(QueenMobilityEG, moves) * P.mobilityScaleQueenEG / 100};
    case Rules::PieceType::Pawn:
    case Rules::PieceType::King:
    case Rules::PieceType::None:
        return {};
    }
    return {};
}


BoardAnalysis analyzeBoard(const Board &board) {
    BoardAnalysis analysis;
    analysis.whiteRearRows.fill(NoFile);
    analysis.blackRearRows.fill(EmptyRow);

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &piece = board[row][column];
            if (!piece.has_value()) {
                continue;
            }

            analysis.pieces[analysis.pieceCount++] =
                {piece->type, piece->color, row, column, piece->hasMoved};

            const int index = colorIndex(piece->color);
            switch (piece->type) {
            case Rules::PieceType::Pawn: {
                const int squareColour = (row + column) % 2;
                ++analysis.pawnTotals[index];
                ++analysis.pawnsOnSquareColour[index][squareColour];
                if (piece->color == Rules::Color::White) {
                    ++analysis.whitePawnCounts[column];
                    analysis.whitePawnRows[column] |= static_cast<quint8>(1 << row);
                    analysis.whiteRearRows[column] =
                        std::max(analysis.whiteRearRows[column], row);
                } else {
                    ++analysis.blackPawnCounts[column];
                    analysis.blackPawnRows[column] |= static_cast<quint8>(1 << row);
                    analysis.blackRearRows[column] =
                        std::min(analysis.blackRearRows[column], row);
                }
                analysis.hasPawnOrMajor = true;
                break;
            }
            case Rules::PieceType::Bishop: {
                ++analysis.bishopCounts[index];
                analysis.bishopSquareColourCounts[index][(row + column) % 2] += 1;
                analysis.phase += 1;
                break;
            }
            case Rules::PieceType::Knight: {
                ++analysis.knightCounts[index];
                analysis.phase += 1;
                break;
            }
            case Rules::PieceType::Rook:
                ++analysis.rookCounts[index];
                analysis.phase += 2;
                analysis.hasPawnOrMajor = true;
                break;
            case Rules::PieceType::Queen:
                ++analysis.queenCounts[index];
                analysis.phase += 4;
                analysis.hasPawnOrMajor = true;
                break;
            case Rules::PieceType::King:
                if (piece->color == Rules::Color::White) {
                    analysis.whiteKingRow = row;
                    analysis.whiteKingColumn = column;
                } else {
                    analysis.blackKingRow = row;
                    analysis.blackKingColumn = column;
                }
                break;
            case Rules::PieceType::None:
                break;
            }
        }
    }

    analysis.phase = std::min(analysis.phase, MaxPhase);

    const auto homeRowFor = [](Rules::Color color) {
        return color == Rules::Color::White ? 7 : 0;
    };
    const auto canCastle = [&board, &homeRowFor](Rules::Color color,
                                                 int rookColumn) {
        const int homeRow = homeRowFor(color);
        const auto &king = board[homeRow][4];
        const auto &rook = board[homeRow][rookColumn];
        return king.has_value() && king->type == Rules::PieceType::King &&
               king->color == color && !king->hasMoved && rook.has_value() &&
               rook->type == Rules::PieceType::Rook && rook->color == color &&
               !rook->hasMoved;
    };
    analysis.whiteCanCastleKingSide = canCastle(Rules::Color::White, 7);
    analysis.whiteCanCastleQueenSide = canCastle(Rules::Color::White, 0);
    analysis.blackCanCastleKingSide = canCastle(Rules::Color::Black, 7);
    analysis.blackCanCastleQueenSide = canCastle(Rules::Color::Black, 0);

    return analysis;
}

void addCheapestAttacker(AttackInfo &info, int row, int column, int value) {
    if (info.cheapest[row][column] == 0 || value < info.cheapest[row][column]) {
        info.cheapest[row][column] = value;
    }
}

bool occupiedByColour(const Board &board, Rules::Color color, int row, int column) {
    const auto &target = board[row][column];
    return target.has_value() && target->color == color;
}

// Weight of a piece that attacks the zone around the enemy king.
int kingAttackerWeight(int kind) {
    switch (kind) {
    case KnightKind: return P.kingAttackerWeightKnight;
    case BishopKind: return P.kingAttackerWeightBishop;
    case RookKind:   return P.kingAttackerWeightRook;
    default:         return P.kingAttackerWeightQueen;
    }
}

// The zone, the neighbouring squares and the check squares of both kings. The
// zone is the three files around the king (shifted inwards on an edge file) over
// the rows around it plus one more towards the enemy; a check square is a square
// from which a piece would attack the king: a knight's jump away, or along a
// ray up to the first piece (which counts when it belongs to the king's side,
// since a capturing check is a check too).
KingMasks buildKingMasks(const Board &board, const BoardAnalysis &analysis) {
    KingMasks masks;
    for (const Rules::Color color :
         {Rules::Color::White, Rules::Color::Black}) {
        const int index = colorIndex(color);
        const int kingRow =
            color == Rules::Color::White ? analysis.whiteKingRow
                                         : analysis.blackKingRow;
        const int kingColumn = color == Rules::Color::White
                                   ? analysis.whiteKingColumn
                                   : analysis.blackKingColumn;
        if (kingRow == NoKingRow) {
            continue;
        }

        const int firstColumn = std::clamp(kingColumn - 1, 0, 5);
        const int towardsEnemy = forwardDirection(color);
        for (int row = kingRow - 1; row <= kingRow + 1; ++row) {
            for (int column = kingColumn - 1; column <= kingColumn + 1;
                 ++column) {
                if (isInside(row, column) &&
                    (row != kingRow || column != kingColumn)) {
                    masks.adjacent[index] |= squareBit(row, column);
                }
            }
        }
        for (int row = kingRow - 1; row <= kingRow + 1; ++row) {
            for (int column = firstColumn; column <= firstColumn + 2;
                 ++column) {
                if (isInside(row, column)) {
                    masks.zone[index] |= squareBit(row, column);
                }
            }
        }
        const int extraRow = kingRow + 2 * towardsEnemy;
        for (int column = firstColumn; column <= firstColumn + 2; ++column) {
            if (isInside(extraRow, column)) {
                masks.zone[index] |= squareBit(extraRow, column);
            }
        }

        for (const auto &offset : KnightOffsets) {
            const int row = kingRow + offset.rowDelta;
            const int column = kingColumn + offset.columnDelta;
            if (isInside(row, column)) {
                masks.checks[index][KnightKind] |= squareBit(row, column);
            }
        }
        const auto ray = [&](const std::array<Offset, 4> &directions,
                             int kind) {
            for (const auto &direction : directions) {
                int row = kingRow + direction.rowDelta;
                int column = kingColumn + direction.columnDelta;
                while (isInside(row, column)) {
                    const auto &piece = board[row][column];
                    if (piece.has_value()) {
                        if (piece->color == color) {
                            masks.checks[index][kind] |= squareBit(row, column);
                        }
                        break;
                    }
                    masks.checks[index][kind] |= squareBit(row, column);
                    row += direction.rowDelta;
                    column += direction.columnDelta;
                }
            }
        };
        ray(BishopDirections, BishopKind);
        ray(RookDirections, RookKind);
        masks.checks[index][QueenKind] =
            masks.checks[index][BishopKind] | masks.checks[index][RookKind];
    }
    return masks;
}

// Marks the attacks of both colours and, in the same walk, counts how many
// squares each piece can move to. Walking the rays once for both is worth it:
// the attack map and the mobility area ask almost the same question.
void buildAttackInfo(const Board &board, const KingMasks &masks,
                     AttackInfo &white, AttackInfo &black) {
    // The pawns come first: their attacks define the area every other piece
    // excludes, and their own mobility does not depend on it.
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &piece = board[row][column];
            if (!piece.has_value() ||
                piece->type != Rules::PieceType::Pawn) {
                continue;
            }

            const bool isWhite = piece->color == Rules::Color::White;
            AttackInfo &info = isWhite ? white : black;
            const int pawnDirection = forwardDirection(piece->color);
            for (const int columnDelta : {-1, 1}) {
                const int targetRow = row + pawnDirection;
                const int targetColumn = column + columnDelta;
                if (isInside(targetRow, targetColumn)) {
                    info.squares[targetRow][targetColumn] = true;
                    info.pawnSquares[targetRow][targetColumn] = true;
                    addCheapestAttacker(info, targetRow, targetColumn,
                                        P.pawnValue);
                }
            }

            const int startingRow = isWhite ? 6 : 1;
            const int forwardRow = row + pawnDirection;
            int moves = 0;
            if (isInside(forwardRow, column) &&
                !board[forwardRow][column].has_value()) {
                ++moves;
                const int doubleRow = row + 2 * pawnDirection;
                if (row == startingRow &&
                    !board[doubleRow][column].has_value()) {
                    ++moves;
                }
            }
            for (const int columnDelta : {-1, 1}) {
                const int targetColumn = column + columnDelta;
                if (!isInside(forwardRow, targetColumn)) {
                    continue;
                }
                const auto &target = board[forwardRow][targetColumn];
                if (target.has_value() && target->color != piece->color) {
                    ++moves;
                }
            }
            info.mobility += moves * mobilityWeight(piece->type);
        }
    }

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &piece = board[row][column];
            if (!piece.has_value() ||
                piece->type == Rules::PieceType::Pawn) {
                continue;
            }

            const bool isWhite = piece->color == Rules::Color::White;
            AttackInfo &info = isWhite ? white : black;
            const AttackMap &enemyPawnAttacks =
                isWhite ? black.pawnSquares : white.pawnSquares;
            int moves = 0;

            // What this piece does to the enemy king: whether it attacks a
            // square of the king's zone, how many of the squares beside the king
            // it attacks, and the squares it attacks from which it would check.
            const int enemyIndex = isWhite ? 1 : 0;
            const int kind = piece->type == Rules::PieceType::Knight ? KnightKind
                             : piece->type == Rules::PieceType::Bishop
                                 ? BishopKind
                             : piece->type == Rules::PieceType::Rook
                                 ? RookKind
                                 : QueenKind;
            const bool tracksKing = piece->type != Rules::PieceType::King;
            bool touchesZone = false;
            int adjacentHits = 0;
            quint64 candidates = 0;
            const auto noteTarget = [&](int targetRow, int targetColumn) {
                if (!tracksKing) {
                    return;
                }
                const quint64 bit = squareBit(targetRow, targetColumn);
                touchesZone = touchesZone || (masks.zone[enemyIndex] & bit) != 0;
                adjacentHits += (masks.adjacent[enemyIndex] & bit) != 0;
                const quint64 checkSquare = masks.checks[enemyIndex][kind] & bit;
                if (checkSquare != 0 &&
                    !occupiedByColour(board, piece->color, targetRow,
                                      targetColumn)) {
                    candidates |= checkSquare;
                }
            };

            if (piece->type == Rules::PieceType::Knight ||
                piece->type == Rules::PieceType::King) {
                const bool knight = piece->type == Rules::PieceType::Knight;
                const auto &offsets = knight ? KnightOffsets : KingOffsets;
                const int value =
                    knight ? P.knightValue : KingAttackerValue;
                for (const auto &offset : offsets) {
                    const int targetRow = row + offset.rowDelta;
                    const int targetColumn = column + offset.columnDelta;
                    if (!isInside(targetRow, targetColumn)) {
                        continue;
                    }
                    info.squares[targetRow][targetColumn] = true;
                    addCheapestAttacker(info, targetRow, targetColumn, value);
                    noteTarget(targetRow, targetColumn);
                    if (!enemyPawnAttacks[targetRow][targetColumn] &&
                        !occupiedByColour(board, piece->color, targetRow,
                                          targetColumn)) {
                        ++moves;
                    }
                }
            } else {
                const bool diagonal =
                    piece->type == Rules::PieceType::Bishop ||
                    piece->type == Rules::PieceType::Queen;
                const bool straight =
                    piece->type == Rules::PieceType::Rook ||
                    piece->type == Rules::PieceType::Queen;
                const int value = piece->type == Rules::PieceType::Bishop
                                      ? P.bishopValue
                                  : piece->type == Rules::PieceType::Rook
                                      ? P.rookValue
                                      : P.queenValue;

                const auto walk = [&](const std::array<Offset, 4> &directions) {
                    for (const auto &direction : directions) {
                        int targetRow = row + direction.rowDelta;
                        int targetColumn = column + direction.columnDelta;
                        while (isInside(targetRow, targetColumn)) {
                            info.squares[targetRow][targetColumn] = true;
                            addCheapestAttacker(info, targetRow, targetColumn,
                                                value);
                            noteTarget(targetRow, targetColumn);
                            const auto &target = board[targetRow][targetColumn];
                            if (!target.has_value()) {
                                if (!enemyPawnAttacks[targetRow][targetColumn]) {
                                    ++moves;
                                }
                                targetRow += direction.rowDelta;
                                targetColumn += direction.columnDelta;
                                continue;
                            }
                            if (target->color != piece->color &&
                                !enemyPawnAttacks[targetRow][targetColumn]) {
                                ++moves;
                            }
                            break;
                        }
                    }
                };

                if (diagonal) {
                    walk(BishopDirections);
                }
                if (straight) {
                    walk(RookDirections);
                }
            }

            // The king's mobility is not counted here: its legal moves depend on
            // the enemy attacks, which are only complete once both maps are
            // built. `kingMobility` adds it afterwards.
            if (piece->type != Rules::PieceType::King) {
                info.mobility += shapedMobility(piece->type, moves);
            }
            if (piece->type == Rules::PieceType::Rook &&
                info.rookCount < static_cast<int>(info.rooks.size())) {
                info.rooks[info.rookCount++] = {row, column, moves};
            }
            if (touchesZone) {
                ++info.kingAttackers;
                info.kingAttackersWeight += kingAttackerWeight(kind);
            }
            info.kingAdjacentAttacks += adjacentHits;
            info.checkCandidates[kind] |= candidates;
        }
    }
}


// Material and placement of every piece, in a single pass: they read the same
// piece list and the same square.
void evaluateMaterialAndPlacement(const BoardAnalysis &analysis,
                                  TaperedScore *material,
                                  TaperedScore *placement) {
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        const int sign = signFor(piece.color);
        *material += sign * bothPhases(HeuristicEval::pieceValue(piece.type));

        const int square = relativeSquare(piece.color, piece.row, piece.column);
        if (const SquareTables tables = pieceSquareTables(piece.type);
            tables.middleGame != nullptr) {
            *placement += sign * TaperedScore{(*tables.middleGame)[square],
                                              (*tables.endGame)[square]};
        }

        if (piece.type == Rules::PieceType::Rook) {
            const auto &ownPawns =
                piece.color == Rules::Color::White ? analysis.whitePawnCounts
                                                   : analysis.blackPawnCounts;
            const auto &enemyPawns =
                piece.color == Rules::Color::White ? analysis.blackPawnCounts
                                                   : analysis.whitePawnCounts;
            if (ownPawns[piece.column] == 0) {
                *placement += sign * (enemyPawns[piece.column] == 0
                                          ? TaperedScore{P.rookOpenFileMG,
                                                         P.rookOpenFileEG}
                                          : TaperedScore{P.rookSemiOpenFileMG,
                                                         P.rookSemiOpenFileEG});
            }
            // A rook on the seventh rank only earns its bonus when it has
            // something to attack there: the enemy king still on its back rank,
            // or enemy pawns that have not left their seventh rank.
            if (relativeRank(piece.color, piece.row) == 7) {
                const bool white = piece.color == Rules::Color::White;
                const int enemyKingRow =
                    white ? analysis.blackKingRow : analysis.whiteKingRow;
                const auto &enemyRows =
                    white ? analysis.blackPawnRows : analysis.whitePawnRows;
                const quint8 enemySeventh =
                    static_cast<quint8>(1 << (white ? 1 : 6));
                bool pawnsOnSeventh = false;
                for (const quint8 rows : enemyRows) {
                    pawnsOnSeventh = pawnsOnSeventh || (rows & enemySeventh) != 0;
                }
                if (enemyKingRow == (white ? 0 : 7) || pawnsOnSeventh) {
                    *placement += sign * TaperedScore{P.rookSeventhRankMG,
                                                      P.rookSeventhRankEG};
                }
            }
        }
    }

    const TaperedScore bishopPair{P.bishopPairMG, P.bishopPairEG};
    if (analysis.bishopCounts[0] >= 2) {
        *placement += bishopPair;
    }
    if (analysis.bishopCounts[1] >= 2) {
        *placement -= bishopPair;
    }
}

// Rows strictly ahead of `row` in the direction of play, as a bit mask: bit
// `r` is set when row `r` lies further along than `row` (smaller rows for
// White, larger rows for Black).
constexpr quint8 rowsAhead(int row, int direction) {
    if (direction < 0) {
        return row == 0 ? 0 : static_cast<quint8>((1u << row) - 1);
    }
    return row >= 7 ? 0
                    : static_cast<quint8>(0xFF & ~((1u << (row + 1)) - 1));
}

bool isPassedPawn(const BoardAnalysis &analysis, const PieceOnBoard &pawn) {
    // A pawn with a friendly pawn still ahead on its own file is not passed:
    // the leading pawn of a doubled pair is the passed one.
    const auto &ownRows = pawn.color == Rules::Color::White
                              ? analysis.whitePawnRows
                              : analysis.blackPawnRows;
    if (ownRows[pawn.column] &
        rowsAhead(pawn.row, forwardDirection(pawn.color))) {
        return false;
    }

    for (int file = std::max(0, pawn.column - 1);
         file <= std::min(7, pawn.column + 1); ++file) {
        if (pawn.color == Rules::Color::White) {
            if (analysis.blackRearRows[file] < pawn.row) {
                return false;
            }
        } else {
            if (analysis.whiteRearRows[file] > pawn.row) {
                return false;
            }
        }
    }
    return true;
}

// Whether a rook of `colour` stands behind `pawn` on its file: the square the
// pawn came from and every square further back.
bool hasRookBehindPawn(const Board &board, const PieceOnBoard &pawn,
                       Rules::Color colour) {
    const int direction = forwardDirection(pawn.color);
    for (int row = pawn.row - direction; isInside(row, pawn.column);
         row -= direction) {
        const auto &piece = board[row][pawn.column];
        if (piece.has_value() && piece->type == Rules::PieceType::Rook &&
            piece->color == colour) {
            return true;
        }
    }
    return false;
}

// Bonus of a passed pawn by relative rank (2 to 7).
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

constexpr Rules::Color opposite(Rules::Color color) {
    return color == Rules::Color::White ? Rules::Color::Black
                                        : Rules::Color::White;
}

// Moves the fastest unstoppable passed pawn of `color` still needs, or zero when
// it has none. Only a defender with no piece left can be out-run this way: the
// pawn is unstoppable when the enemy king, on the move if it is its turn, cannot
// reach the promotion square in time (the rule of the square). The pawn gets one
// move less from its starting rank, where it can advance two squares, and one
// more when its own king stands in front of it on its file.
int unstoppablePasserMoves(const BoardAnalysis &analysis, Rules::Color color,
                           Rules::Color sideToMove) {
    const Rules::Color defender = opposite(color);
    const int defenderIndex = colorIndex(defender);
    if (analysis.knightCounts[defenderIndex] +
            analysis.bishopCounts[defenderIndex] +
            analysis.rookCounts[defenderIndex] +
            analysis.queenCounts[defenderIndex] != 0) {
        return 0;
    }
    const bool white = color == Rules::Color::White;
    const int ownKingRow = white ? analysis.whiteKingRow : analysis.blackKingRow;
    const int ownKingColumn =
        white ? analysis.whiteKingColumn : analysis.blackKingColumn;
    const int defenderRow = white ? analysis.blackKingRow : analysis.whiteKingRow;
    const int defenderColumn =
        white ? analysis.blackKingColumn : analysis.whiteKingColumn;
    if (ownKingRow == NoKingRow || defenderRow == NoKingRow) {
        return 0;
    }

    const int direction = forwardDirection(color);
    const int promotionRow = white ? 0 : 7;
    int best = 0;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &pawn = analysis.pieces[index];
        if (pawn.type != Rules::PieceType::Pawn || pawn.color != color ||
            !isPassedPawn(analysis, pawn)) {
            continue;
        }
        const int rank = relativeRank(color, pawn.row);
        int moves = 8 - rank;
        if (rank == 2) {
            --moves;
        }
        if (ownKingColumn == pawn.column &&
            (ownKingRow - pawn.row) * direction > 0) {
            ++moves;
        }
        const int kingMoves =
            std::max(std::abs(defenderRow - promotionRow),
                     std::abs(defenderColumn - pawn.column)) -
            (sideToMove == defender ? 1 : 0);
        if (moves < kingMoves && (best == 0 || moves < best)) {
            best = moves;
        }
    }
    return best;
}

// Space: the safe central squares a side's pawns control in the enemy half. A
// square counts when a friendly pawn attacks it, no enemy pawn does, and it
// lies on one of the four central files past the middle of the board. More
// space means the pieces behind the pawns have more room to operate.
TaperedScore evaluateSpace(const AttackMap &whitePawnAttacks,
                           const AttackMap &blackPawnAttacks) {
    int white = 0;
    int black = 0;
    for (int row = 0; row < 8; ++row) {
        for (int column = 2; column <= 5; ++column) {
            if (row <= 3) {
                if (whitePawnAttacks[row][column] &&
                    !blackPawnAttacks[row][column]) {
                    ++white;
                }
            } else if (blackPawnAttacks[row][column] &&
                       !whitePawnAttacks[row][column]) {
                ++black;
            }
        }
    }
    const int advantage = white - black;
    return {advantage * P.spaceBonusMG, advantage * P.spaceBonusEG};
}

// What the pawns alone determine: it is a function of where the pawns stand, so
// it is kept by the pawn key and shared by every position with the same pawns,
// whatever the pieces do.
struct PawnEntry {
    // Doubled, isolated, supported, phalanx, backward and candidate pawns.
    TaperedScore structure;
    // The safe central squares the pawns control.
    TaperedScore space;
    // One bit per square (row * 8 + column) with a passed pawn on it.
    quint64 passed = 0;
};

PawnEntry computePawnEntry(const BoardAnalysis &analysis,
                           const AttackMap &whitePawnAttacks,
                           const AttackMap &blackPawnAttacks) {
    PawnEntry entry;
    TaperedScore &score = entry.structure;

    for (const Rules::Color color :
         {Rules::Color::White, Rules::Color::Black}) {
        const int sign = signFor(color);
        const auto &counts = color == Rules::Color::White
                                 ? analysis.whitePawnCounts
                                 : analysis.blackPawnCounts;
        for (int file = 0; file < 8; ++file) {
            if (counts[file] > 1) {
                score -= sign * (counts[file] - 1) *
                         TaperedScore{P.doubledPawnPenaltyMG,
                                      P.doubledPawnPenaltyEG};
            }
            const bool hasAdjacentPawns =
                (file > 0 && counts[file - 1] > 0) ||
                (file < 7 && counts[file + 1] > 0);
            if (!hasAdjacentPawns) {
                score -= sign * counts[file] *
                         TaperedScore{P.isolatedPawnPenaltyMG,
                                      P.isolatedPawnPenaltyEG};
            }
        }
    }

    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type != Rules::PieceType::Pawn) {
            continue;
        }

        const int sign = signFor(piece.color);
        const int direction = forwardDirection(piece.color);

        // A friendly pawn attacks the square the pawn stands on exactly when
        // it is defended by one.
        const AttackMap &friendlyPawnAttacks =
            piece.color == Rules::Color::White ? whitePawnAttacks
                                               : blackPawnAttacks;
        const bool supportedByPawn =
            friendlyPawnAttacks[piece.row][piece.column];
        if (supportedByPawn) {
            score += sign * TaperedScore{P.supportedPawnBonusMG,
                                         P.supportedPawnBonusEG};
        }

        const auto &ownCounts = piece.color == Rules::Color::White
                                    ? analysis.whitePawnCounts
                                    : analysis.blackPawnCounts;
        const bool isolated =
            (piece.column == 0 || ownCounts[piece.column - 1] == 0) &&
            (piece.column == 7 || ownCounts[piece.column + 1] == 0);

        const bool passed = isPassedPawn(analysis, piece);
        if (passed) {
            entry.passed |= quint64{1} << (piece.row * 8 + piece.column);
        }

        const auto &enemyPawnRows = piece.color == Rules::Color::White
                                        ? analysis.blackPawnRows
                                        : analysis.whitePawnRows;
        const auto &ownPawnRows = piece.color == Rules::Color::White
                                      ? analysis.whitePawnRows
                                      : analysis.blackPawnRows;
        const quint8 rowsInFront = rowsAhead(piece.row, direction);
        // Rows a friendly pawn could still come from to support this one: its
        // own row (a neighbour beside it steps up to defend it) and the rows
        // behind.
        const quint8 rowsBehindOrLevel = static_cast<quint8>(~rowsInFront);

        // A pawn with a friendly pawn beside it on the same rank (a phalanx)
        // is worth more the further it has come, and half as much when an enemy
        // pawn faces it on its file.
        const quint8 sameRow = static_cast<quint8>(1 << piece.row);
        const bool inPhalanx =
            (piece.column > 0 && (ownPawnRows[piece.column - 1] & sameRow)) ||
            (piece.column < 7 && (ownPawnRows[piece.column + 1] & sameRow));
        if (inPhalanx) {
            const int advance = relativeRank(piece.color, piece.row) - 1;
            const TaperedScore bonus =
                advance * TaperedScore{P.phalanxPawnBonusMG,
                                       P.phalanxPawnBonusEG};
            const bool opposed = enemyPawnRows[piece.column] & rowsInFront;
            score += sign * (opposed ? TaperedScore{bonus.middleGame / 2,
                                                    bonus.endGame / 2}
                                     : bonus);
        }

        if (passed) {
            continue;
        }

        // A backward pawn is not isolated, passed or supported, has no friendly
        // pawn on an adjacent file level with it or behind it that could ever
        // come to its support, and cannot advance safely: the square in front
        // of it is covered by an enemy pawn. A pair of neighbours on the same
        // row therefore is not backward, and a distant enemy pawn no longer
        // makes a pawn backward by itself.
        if (!isolated && !supportedByPawn) {
            int supporters = 0;
            for (const int columnDelta : {-1, 1}) {
                const int file = piece.column + columnDelta;
                if (file >= 0 && file < 8) {
                    supporters += std::popcount(static_cast<unsigned>(
                        ownPawnRows[file] & rowsBehindOrLevel));
                }
            }
            const AttackMap &enemyPawnAttacks =
                piece.color == Rules::Color::White ? blackPawnAttacks
                                                   : whitePawnAttacks;
            const int stopRow = piece.row + direction;
            if (supporters == 0 && isInside(stopRow, piece.column) &&
                enemyPawnAttacks[stopRow][piece.column]) {
                score -= sign * TaperedScore{P.backwardPawnPenaltyMG,
                                             P.backwardPawnPenaltyEG};
            }
        }

        // A candidate is a pawn on a file the enemy has no pawn ahead on, that
        // is not passed only because enemy pawns guard its path from the
        // adjacent files, and that has at least as many friendly pawns beside
        // it or behind it on those files (helpers) as enemy pawns ahead of it
        // (sentinels). A pawn blocked on its own file is no candidate, and
        // neither is the rear pawn of a doubled pair.
        if (!(enemyPawnRows[piece.column] & rowsInFront) &&
            !(ownPawnRows[piece.column] & rowsInFront)) {
            int helpers = 0;
            int sentinels = 0;
            for (const int columnDelta : {-1, 1}) {
                const int file = piece.column + columnDelta;
                if (file < 0 || file > 7) {
                    continue;
                }
                helpers += std::popcount(static_cast<unsigned>(
                    ownPawnRows[file] & rowsBehindOrLevel));
                sentinels += std::popcount(static_cast<unsigned>(
                    enemyPawnRows[file] & rowsInFront));
            }
            if (helpers >= sentinels) {
                const int rank = relativeRank(piece.color, piece.row);
                score += sign * TaperedScore{
                                    P.candidatePawnBonusMG +
                                        rank * P.candidatePawnRankBonusMG,
                                    P.candidatePawnBonusEG +
                                        rank * P.candidatePawnRankBonusEG};
            }
        }
    }

    entry.space = evaluateSpace(whitePawnAttacks, blackPawnAttacks);
    return entry;
}

// The passed pawns, whose worth depends on the pieces around them: what blocks
// or guards the road, the rooks behind them and the kings, plus the pawn no
// defender can catch.
TaperedScore evaluatePassedPawns(const Board &board,
                                 const BoardAnalysis &analysis,
                                 const AttackInfo &whiteAttacks,
                                 const AttackInfo &blackAttacks,
                                 Rules::Color sideToMove, quint64 passed) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount && passed != 0; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type != Rules::PieceType::Pawn ||
            !(passed & (quint64{1} << (piece.row * 8 + piece.column)))) {
            continue;
        }

        const int sign = signFor(piece.color);
        const int direction = forwardDirection(piece.color);
        const int rank = relativeRank(piece.color, piece.row);
        TaperedScore bonus = passedPawnRankBonus(rank);

        const AttackMap &enemyAttacks =
            piece.color == Rules::Color::White ? blackAttacks.squares
                                               : whiteAttacks.squares;
        const int aheadRow = piece.row + direction;
        if (isInside(aheadRow, piece.column)) {
            if (board[aheadRow][piece.column].has_value()) {
                bonus -= TaperedScore{P.passedPawnBlockedPenaltyMG,
                                      P.passedPawnBlockedPenaltyEG};
            } else if (enemyAttacks[aheadRow][piece.column]) {
                // The advance square is empty but controlled by an enemy
                // piece, so the pawn cannot step forward safely.
                bonus -= TaperedScore{P.passedPawnControlledPenaltyMG,
                                      P.passedPawnControlledPenaltyEG};
            }
        }

        // A rook belongs behind a passed pawn: the owner's rook escorts it
        // to promotion, an enemy rook blockades it from behind.
        if (hasRookBehindPawn(board, piece, piece.color)) {
            bonus += TaperedScore{P.passedPawnRookBehindMG,
                                  P.passedPawnRookBehindEG};
        }
        if (hasRookBehindPawn(board, piece, opposite(piece.color))) {
            bonus -= TaperedScore{P.passedPawnEnemyRookBehindMG,
                                  P.passedPawnEnemyRookBehindEG};
        }

        // The road to promotion: free of pieces and of enemy attacks, or at
        // least a safe stop square. Only advanced pawns get it, in
        // proportion to how far past the third rank they are.
        const int advance = std::max(0, rank - 3);
        if (advance > 0) {
            bool pathFree = true;
            bool stopSafe = true;
            for (int row = piece.row + direction; isInside(row, piece.column);
                 row += direction) {
                const bool blocked = board[row][piece.column].has_value() ||
                                     enemyAttacks[row][piece.column];
                if (blocked) {
                    pathFree = false;
                    stopSafe = stopSafe && row != piece.row + direction;
                }
            }
            if (pathFree) {
                bonus += advance * TaperedScore{P.passedPawnFreePathMG,
                                                P.passedPawnFreePathEG};
            } else if (stopSafe) {
                bonus += advance * TaperedScore{P.passedPawnSafeStopMG,
                                                P.passedPawnSafeStopEG};
            }
        }

        score += sign * TaperedScore{std::max(bonus.middleGame, 0),
                                     std::max(bonus.endGame, 0)};

        // Kings decide a passed pawn in the endgame: the enemy king is better
        // away from the square in front of it, the own king near.
        if (advance > 0) {
            const bool white = piece.color == Rules::Color::White;
            const int ownKingRow =
                white ? analysis.whiteKingRow : analysis.blackKingRow;
            const int ownKingColumn =
                white ? analysis.whiteKingColumn : analysis.blackKingColumn;
            const int enemyKingRow =
                white ? analysis.blackKingRow : analysis.whiteKingRow;
            const int enemyKingColumn =
                white ? analysis.blackKingColumn : analysis.whiteKingColumn;
            if (ownKingRow != NoKingRow && enemyKingRow != NoKingRow) {
                const auto steps = [&](int kingRow, int kingColumn) {
                    return std::min(5, std::max(std::abs(kingRow - aheadRow),
                                                std::abs(kingColumn -
                                                         piece.column)));
                };
                score += sign *
                         TaperedScore{0, advance *
                                             (P.passedKingDistanceEnemy *
                                                  steps(enemyKingRow,
                                                        enemyKingColumn) -
                                              P.passedKingDistanceOwn *
                                                  steps(ownKingRow,
                                                        ownKingColumn))};
            }
        }
    }

    // A passed pawn the defender cannot catch decides the game: the side whose
    // such pawn promotes first is paid for it, and equal pawns cancel.
    if (passed != 0) {
        const int whiteMoves =
            unstoppablePasserMoves(analysis, Rules::Color::White, sideToMove);
        const int blackMoves =
            unstoppablePasserMoves(analysis, Rules::Color::Black, sideToMove);
        const auto unstoppableBonus = [](int moves) {
            return TaperedScore{0, std::max(0, P.unstoppablePasserBonus -
                                                   P.unstoppablePasserPerMove *
                                                       moves)};
        };
        if (whiteMoves != 0 && (blackMoves == 0 || whiteMoves < blackMoves)) {
            score += unstoppableBonus(whiteMoves);
        } else if (blackMoves != 0 &&
                   (whiteMoves == 0 || blackMoves < whiteMoves)) {
            score -= unstoppableBonus(blackMoves);
        }
    }
    return score;
}


// Direct-mapped cache of the pawn entries, keyed by `Rules::pawnKey()`. The
// pawn structure changes only when a pawn moves or is captured, so the same
// entry serves the many positions of a search that differ by pieces alone. It
// is filled by any thread without a lock: an entry is three payload words and a
// key word holding the key XOR the payload, so a slot two threads wrote at once
// fails the check instead of being read as someone else's entry. The active
// weights are part of the entry, so it is dropped with the other caches
// whenever they change.
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

// Whether a square is an outpost for `color`: on the fourth to sixth rank, held by
// a friendly pawn and out of reach of every enemy pawn for good, that is with no
// enemy pawn on an adjacent file that is still ahead of it and could advance to
// attack it.
bool isOutpost(const BoardAnalysis &analysis, const AttackInfo &own,
               Rules::Color color, int row, int column) {
    const int rank = relativeRank(color, row);
    if (rank < 4 || rank > 6 || !own.pawnSquares[row][column]) {
        return false;
    }
    const auto &enemyRows = color == Rules::Color::White ? analysis.blackPawnRows
                                                         : analysis.whitePawnRows;
    const quint8 ahead = rowsAhead(row, forwardDirection(color));
    return !((column > 0 && (enemyRows[column - 1] & ahead)) ||
             (column < 7 && (enemyRows[column + 1] & ahead)));
}

// Threats, outposts and the bad-bishop penalty in one pass over the pieces:
// they all ask a question about the piece that is already in hand.
void evaluatePieceTerms(const Board &board, const BoardAnalysis &analysis,
                        const AttackInfo &white, const AttackInfo &black,
                        TaperedScore *threats,
                        TaperedScore *outposts, TaperedScore *badBishops) {
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type == Rules::PieceType::None) {
            continue;
        }

        const bool isWhite = piece.color == Rules::Color::White;
        const AttackInfo &enemy = isWhite ? black : white;
        const AttackInfo &own = isWhite ? white : black;
        const int sign = signFor(piece.color);

        if (piece.type == Rules::PieceType::Bishop) {
            const int colour = colorIndex(piece.color);
            const int squareColour = (piece.row + piece.column) % 2;
            const int blockers =
                analysis.pawnsOnSquareColour[colour][squareColour];
            const int penalty =
                std::min(P.badBishopCap, blockers * P.badBishopPenalty);
            *badBishops -= sign * bothPhases(penalty);
        }

        if (piece.type != Rules::PieceType::King &&
            enemy.squares[piece.row][piece.column]) {
            const int value = HeuristicEval::pieceValue(piece.type);
            if (!own.squares[piece.row][piece.column]) {
                // Attacked and undefended: the opponent can simply take it.
                *threats -= sign * bothPhases(value / P.hangingPieceDivisor);
            } else {
                const int cheapest = enemy.cheapest[piece.row][piece.column];
                if (cheapest != 0 && cheapest + P.cheapAttackerMargin < value) {
                    *threats -= sign * bothPhases((value - cheapest) /
                                                  P.cheapAttackerDivisor);
                }
            }
        }

        if (piece.type == Rules::PieceType::Knight ||
            piece.type == Rules::PieceType::Bishop) {
            if (isOutpost(analysis, own, piece.color, piece.row, piece.column)) {
                *outposts += sign * (piece.type == Rules::PieceType::Knight
                                         ? TaperedScore{P.outpostKnightMG,
                                                        P.outpostKnightEG}
                                         : TaperedScore{P.outpostBishopMG,
                                                        P.outpostBishopEG});
            } else if (piece.type == Rules::PieceType::Knight) {
                // A knight one jump from an outpost is worth a smaller bonus.
                for (const Offset &offset : KnightOffsets) {
                    const int row = piece.row + offset.rowDelta;
                    const int column = piece.column + offset.columnDelta;
                    if (isInside(row, column) &&
                        !occupiedByColour(board, piece.color, row, column) &&
                        isOutpost(analysis, own, piece.color, row, column)) {
                        *outposts += sign * TaperedScore{P.outpostReachableKnightMG,
                                                         P.outpostReachableKnightEG};
                        break;
                    }
                }
            }
        }
    }
}

bool hasClearPath(const Board &board, const PieceOnBoard &first,
                  const PieceOnBoard &second) {
    if (first.row == second.row) {
        const int from = std::min(first.column, second.column) + 1;
        const int to = std::max(first.column, second.column);
        for (int column = from; column < to; ++column) {
            if (board[first.row][column].has_value()) {
                return false;
            }
        }
        return true;
    }
    if (first.column == second.column) {
        const int from = std::min(first.row, second.row) + 1;
        const int to = std::max(first.row, second.row);
        for (int row = from; row < to; ++row) {
            if (board[row][first.column].has_value()) {
                return false;
            }
        }
        return true;
    }
    return false;
}

TaperedScore evaluateConnectedRooks(const Board &board,
                                   const BoardAnalysis &analysis) {
    TaperedScore score;
    for (const Rules::Color color :
         {Rules::Color::White, Rules::Color::Black}) {
        if (analysis.rookCounts[colorIndex(color)] != 2) {
            continue;
        }
        const PieceOnBoard *first = nullptr;
        const PieceOnBoard *second = nullptr;
        for (int index = 0; index < analysis.pieceCount; ++index) {
            const PieceOnBoard &piece = analysis.pieces[index];
            if (piece.type != Rules::PieceType::Rook || piece.color != color) {
                continue;
            }
            if (first == nullptr) {
                first = &piece;
            } else {
                second = &piece;
            }
        }
        if (first != nullptr && second != nullptr &&
            hasClearPath(board, *first, *second)) {
            score += signFor(color) *
                     TaperedScore{P.connectedRooksMG, P.connectedRooksEG};
        }
    }
    return score;
}

// Danger of the attack on the king of `color`, from what the enemy pieces
// gathered while their attacks were walked (`enemy`) and what the king's own
// side defends (`own`). Every piece that attacks the zone around the king
// counts, and the total grows with the number of attackers as well as with their
// weight, because a lone attacker does little and coordinated ones are decisive;
// the attacks right beside the king, the checks that could be given from a
// square the defence does not cover and the files without a friendly pawn add to
// it, and it falls sharply when the attacker has no queen. The penalty is
// quadratic in the danger, so it stays small until an attack is really brewing.
TaperedScore kingDanger(const BoardAnalysis &analysis, Rules::Color color,
                        const AttackInfo &own, const AttackInfo &enemy,
                        int shelterFilesMissing) {
    const int enemyIndex = 1 - colorIndex(color);
    const bool enemyHasQueen = analysis.queenCounts[enemyIndex] > 0;
    if (enemy.kingAttackers == 0 ||
        (enemy.kingAttackers < 2 && !enemyHasQueen)) {
        return {};
    }

    int danger = enemy.kingAttackers * enemy.kingAttackersWeight +
                 enemy.kingAdjacentAttacks * P.kingAdjacentAttackWeight +
                 shelterFilesMissing * P.kingDangerOpenFile;

    // A check square the defence does not cover (pawns, knights, bishops and
    // rooks count as defenders; the king and the queen do not, since taking
    // with them may not be possible).
    const int safeCheckWeights[4] = {P.safeCheckKnight, P.safeCheckBishop,
                                     P.safeCheckRook, P.safeCheckQueen};
    for (int kind = 0; kind < 4; ++kind) {
        quint64 candidates = enemy.checkCandidates[kind];
        while (candidates != 0) {
            const int square = std::countr_zero(candidates);
            candidates &= candidates - 1;
            const int cheapest = own.cheapest[square / 8][square % 8];
            if (cheapest == 0 || cheapest >= P.queenValue) {
                danger += safeCheckWeights[kind];
            }
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

TaperedScore kingSafety(const Board &board, const BoardAnalysis &analysis,
                        Rules::Color color, const AttackInfo &own,
                        const AttackInfo &enemy) {
    const int kingRow = color == Rules::Color::White ? analysis.whiteKingRow
                                                     : analysis.blackKingRow;
    const int kingColumn = color == Rules::Color::White
                               ? analysis.whiteKingColumn
                               : analysis.blackKingColumn;
    if (kingRow == NoKingRow) {
        return {};
    }

    TaperedScore score;
    const int homeRow = color == Rules::Color::White ? 7 : 0;
    if (kingRow == homeRow && (kingColumn == 2 || kingColumn == 6)) {
        score += middleGameOnly(P.castledBonus);
    }

    const bool canCastle = color == Rules::Color::White
                               ? (analysis.whiteCanCastleKingSide ||
                                  analysis.whiteCanCastleQueenSide)
                               : (analysis.blackCanCastleKingSide ||
                                  analysis.blackCanCastleQueenSide);
    if (canCastle) {
        score += middleGameOnly(P.castlingRightsBonus);
    }

    const int ownIndex = colorIndex(color);
    const int enemyIndex = 1 - ownIndex;
    // An open file only matters to an opponent who still has a rook or a queen
    // to use it.
    const bool enemyHasHeavyPieces =
        analysis.rookCounts[enemyIndex] + analysis.queenCounts[enemyIndex] > 0;

    // The pawns in front of the king form its shelter; the closer they are,
    // the better. The shelter keeps a reduced endgame value: even with few
    // pieces left the king prefers pawns in front of it.
    const TaperedScore shelter{
        P.kingShelterBonus,
        P.kingShelterBonus * P.kingShelterEndgamePercent / 100};
    const int forward = forwardDirection(color);
    for (int file = std::max(0, kingColumn - 1);
         file <= std::min(7, kingColumn + 1); ++file) {
        int distance = 0;
        bool found = false;
        for (int row = kingRow + forward; isInside(row, file);
             row += forward) {
            ++distance;
            const auto &piece = board[row][file];
            if (piece.has_value() && piece->color == color &&
                piece->type == Rules::PieceType::Pawn) {
                found = true;
                break;
            }
            if (distance >= 3) {
                break;
            }
        }
        if (found) {
            const int percent = distance == 1
                                    ? 100
                                    : (distance == 2
                                           ? P.kingShelterSecondRankPercent
                                           : P.kingShelterThirdRankPercent);
            // In the middlegame the pawn directly in front of the king shields
            // it best and the adjacent files count only a share of the bonus.
            // In the endgame the file matters less, so the reduced shelter is
            // not split further.
            const int filePercent = file == kingColumn
                                        ? 100
                                        : P.kingShelterAdjacentFilePercent;
            score += TaperedScore{
                shelter.middleGame * percent * filePercent / 10000,
                shelter.endGame * percent / 100};
        }
    }

    // Files with no friendly pawn near the king give attacking pieces a way in:
    // a file with no pawn at all is open, one that still holds an enemy pawn is
    // only half open and costs a share of the penalty.
    const auto &ownCounts = color == Rules::Color::White
                                ? analysis.whitePawnCounts
                                : analysis.blackPawnCounts;
    const auto &enemyCounts = color == Rules::Color::White
                                  ? analysis.blackPawnCounts
                                  : analysis.whitePawnCounts;
    int filesMissing = 0;
    for (int file = std::max(0, kingColumn - 1);
         file <= std::min(7, kingColumn + 1); ++file) {
        if (ownCounts[file] != 0) {
            continue;
        }
        ++filesMissing;
        if (enemyHasHeavyPieces) {
            const int penalty = enemyCounts[file] == 0
                                    ? P.openFileNearKingPenalty
                                    : P.openFileNearKingPenalty *
                                          P.kingSemiOpenFilePercent / 100;
            score -= middleGameOnly(penalty);
        }
    }

    // A king that is still in the centre, cannot castle any more and has a file
    // open beside it is exposed to the heavy pieces.
    if ((kingColumn == 3 || kingColumn == 4) && !canCastle &&
        filesMissing > 0 && enemyHasHeavyPieces) {
        score -= middleGameOnly(P.kingCentreOpenFilePenalty);
    }

    // Enemy pawns advancing towards the king are a storm, less dangerous when a
    // friendly pawn stands right in front of the advancing one and blocks it.
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type != Rules::PieceType::Pawn ||
            piece.color == color) {
            continue;
        }
        if (std::abs(piece.column - kingColumn) > 2) {
            continue;
        }
        const int rank = relativeRank(piece.color, piece.row);
        if (rank >= 5) {
            const int frontRow = piece.row + forwardDirection(piece.color);
            const bool blocked =
                isInside(frontRow, piece.column) &&
                board[frontRow][piece.column].has_value() &&
                board[frontRow][piece.column]->color == color &&
                board[frontRow][piece.column]->type == Rules::PieceType::Pawn;
            const int penalty = blocked ? P.kingPawnStormPenalty *
                                              P.kingStormBlockedPercent / 100
                                        : P.kingPawnStormPenalty;
            score -= (rank - 4) * middleGameOnly(penalty);
        }
    }

    score -= kingDanger(analysis, color, own, enemy, filesMissing);
    return score;
}

// A rook that its own king shuts in: on the first rank, outside the king (on the
// wing the king has stepped towards), with three squares or fewer to move to and
// no castling to free it. The penalty is doubled when the side has lost every
// castling right.
TaperedScore evaluateTrappedRooks(const BoardAnalysis &analysis,
                                  const AttackInfo &white,
                                  const AttackInfo &black) {
    TaperedScore score;
    for (const Rules::Color color :
         {Rules::Color::White, Rules::Color::Black}) {
        const bool isWhite = color == Rules::Color::White;
        const AttackInfo &info = isWhite ? white : black;
        const int kingRow = isWhite ? analysis.whiteKingRow : analysis.blackKingRow;
        const int kingColumn =
            isWhite ? analysis.whiteKingColumn : analysis.blackKingColumn;
        const int homeRow = isWhite ? 7 : 0;
        if (kingRow != homeRow) {
            continue;
        }
        const bool kingSide = isWhite ? analysis.whiteCanCastleKingSide
                                      : analysis.blackCanCastleKingSide;
        const bool queenSide = isWhite ? analysis.whiteCanCastleQueenSide
                                       : analysis.blackCanCastleQueenSide;
        for (int index = 0; index < info.rookCount; ++index) {
            const AttackInfo::RookMobility &rook = info.rooks[index];
            if (rook.row != homeRow || rook.moves > 3) {
                continue;
            }
            const bool outsideKing =
                kingColumn >= 4 ? rook.column > kingColumn
                                : rook.column < kingColumn;
            const bool canStillCastle = rook.column > kingColumn ? kingSide
                                                                 : queenSide;
            if (outsideKing && !canStillCastle) {
                score -= signFor(color) * (kingSide || queenSide ? 1 : 2) *
                         middleGameOnly(P.trappedRookPenalty);
            }
        }
    }
    return score;
}

TaperedScore evaluateKingSafety(const Board &board,
                                const BoardAnalysis &analysis,
                                const AttackInfo &whiteAttacks,
                                const AttackInfo &blackAttacks) {
    TaperedScore score = kingSafety(board, analysis, Rules::Color::White,
                                    whiteAttacks, blackAttacks);
    score -= kingSafety(board, analysis, Rules::Color::Black, blackAttacks,
                        whiteAttacks);
    return score;
}

// Mobility of a king, counted against the enemy's full attack map: a king
// cannot step onto a square an enemy piece controls, so those squares are not
// real moves. The attack walk cannot count this itself, because the enemy map
// is only complete once every piece has been walked.
TaperedScore kingMobility(const Board &board, const BoardAnalysis &analysis,
                          Rules::Color color, const AttackMap &enemyAttacks) {
    const int kingRow = color == Rules::Color::White ? analysis.whiteKingRow
                                                     : analysis.blackKingRow;
    const int kingColumn = color == Rules::Color::White
                               ? analysis.whiteKingColumn
                               : analysis.blackKingColumn;
    if (kingRow == NoKingRow) {
        return {};
    }

    int moves = 0;
    for (const Offset &offset : KingOffsets) {
        const int row = kingRow + offset.rowDelta;
        const int column = kingColumn + offset.columnDelta;
        if (!isInside(row, column) || enemyAttacks[row][column] ||
            occupiedByColour(board, color, row, column)) {
            continue;
        }
        ++moves;
    }
    return moves * mobilityWeight(Rules::PieceType::King);
}

// Chessboard distance from the centre: zero in the middle, three on an edge or
// in a corner. The mop-up uses it to push the enemy king outwards.
int centreDistance(int row, int column) {
    const int rowDistance = std::max({0, 3 - row, row - 4});
    const int columnDistance = std::max({0, 3 - column, column - 4});
    return std::max(rowDistance, columnDistance);
}

int manhattanDistance(int firstRow, int firstColumn, int secondRow,
                      int secondColumn) {
    return std::abs(firstRow - secondRow) + std::abs(firstColumn - secondColumn);
}

// Mop-up for the stronger side: drive the enemy king to the edge and bring the
// own king closer. Only the endgame half of the score is filled in, so the term
// fades away while pieces are still on the board.
TaperedScore mopUpFor(Rules::Color strong, const BoardAnalysis &analysis) {
    const bool strongIsWhite = strong == Rules::Color::White;
    const int strongRow =
        strongIsWhite ? analysis.whiteKingRow : analysis.blackKingRow;
    const int strongColumn =
        strongIsWhite ? analysis.whiteKingColumn : analysis.blackKingColumn;
    const int weakRow =
        strongIsWhite ? analysis.blackKingRow : analysis.whiteKingRow;
    const int weakColumn =
        strongIsWhite ? analysis.blackKingColumn : analysis.whiteKingColumn;
    if (strongRow == NoKingRow || weakRow == NoKingRow) {
        return {};
    }

    // The kings are at most fourteen Manhattan steps apart.
    const int proximity =
        14 - manhattanDistance(strongRow, strongColumn, weakRow, weakColumn);
    const int strongIndex = colorIndex(strong);
    const int weakIndex = 1 - strongIndex;
    const bool bishopAndKnightOnly =
        analysis.bishopCounts[strongIndex] == 1 &&
        analysis.knightCounts[strongIndex] == 1 &&
        analysis.rookCounts[strongIndex] == 0 &&
        analysis.queenCounts[strongIndex] == 0 &&
        analysis.pawnTotals[strongIndex] == 0 &&
        analysis.pawnTotals[weakIndex] == 0 &&
        analysis.knightCounts[weakIndex] == 0 &&
        analysis.bishopCounts[weakIndex] == 0 &&
        analysis.rookCounts[weakIndex] == 0 &&
        analysis.queenCounts[weakIndex] == 0;
    if (bishopAndKnightOnly) {
        // Bishop and knight only mate in a corner of the bishop's colour: the
        // king is driven to the nearest such corner, not to any edge.
        const int bishopColour =
            analysis.bishopSquareColourCounts[strongIndex][0] == 1 ? 0 : 1;
        // Corners are (0, 0), (7, 7) for even squares and (0, 7), (7, 0) for odd.
        const int firstColumn = bishopColour == 0 ? 0 : 7;
        const int secondColumn = 7 - firstColumn;
        const int cornerDistance = std::min(
            manhattanDistance(weakRow, weakColumn, 0, firstColumn),
            manhattanDistance(weakRow, weakColumn, 7, secondColumn));
        return {0, (14 - cornerDistance) * P.mopUpBishopKnightCornerBonus +
                       proximity * P.mopUpKingProximityBonus};
    }

    const int edge = centreDistance(weakRow, weakColumn);
    return {0, edge * P.mopUpEdgeBonus + proximity * P.mopUpKingProximityBonus};
}

// Sum of the material on the board from White's point of view.
int materialBalance(const BoardAnalysis &analysis) {
    int balance = 0;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        balance += signFor(piece.color) * HeuristicEval::pieceValue(piece.type);
    }
    return balance;
}

TaperedScore evaluateEndgameMopUp(const BoardAnalysis &analysis) {
    const int balance = materialBalance(analysis);
    if (balance >= P.mopUpMaterialThreshold) {
        return mopUpFor(Rules::Color::White, analysis);
    }
    if (balance <= -P.mopUpMaterialThreshold) {
        const TaperedScore score = mopUpFor(Rules::Color::Black, analysis);
        return {-score.middleGame, -score.endGame};
    }
    return {};
}

// Delegates the material-draw decision to the rules engine so the score the UI
// shows and the draws the game adjudicates can never drift apart. The board
// pass already counted the material, so the rules engine only has to classify
// it; `unforceable` covers the dead positions plus the material that cannot
// force mate (a lone minor on each side, two knights against a bare king).
bool isInsufficientMaterial(const BoardAnalysis &analysis) {
    Rules::MaterialCounts counts;
    for (int colour = 0; colour < 2; ++colour) {
        counts.knights[colour] = analysis.knightCounts[colour];
        counts.evenSquaredBishops[colour] =
            analysis.bishopSquareColourCounts[colour][0];
        counts.oddSquaredBishops[colour] =
            analysis.bishopSquareColourCounts[colour][1];
    }
    counts.hasPawnOrMajor = analysis.hasPawnOrMajor;
    return Rules::classifyMaterialDraw(counts).unforceable;
}

// Returns true when opposite-coloured bishops with no other non-pawn material
// make a position drawish.
bool isOppositeColourBishopEnding(const BoardAnalysis &analysis) {
    if (analysis.bishopCounts[0] != 1 || analysis.bishopCounts[1] != 1) {
        return false;
    }
    const bool whiteOnEvenSquares =
        analysis.bishopSquareColourCounts[0][0] == 1;
    const bool blackOnEvenSquares =
        analysis.bishopSquareColourCounts[1][0] == 1;
    if (whiteOnEvenSquares == blackOnEvenSquares) {
        return false;
    }
    return analysis.knightCounts[0] == 0 && analysis.knightCounts[1] == 0 &&
           analysis.rookCounts[0] == 0 && analysis.rookCounts[1] == 0 &&
           analysis.queenCounts[0] == 0 && analysis.queenCounts[1] == 0;
}

// Non-pawn material of one colour, with the active piece values.
int nonPawnMaterial(const BoardAnalysis &analysis, int colour) {
    return analysis.knightCounts[colour] * P.knightValue +
           analysis.bishopCounts[colour] * P.bishopValue +
           analysis.rookCounts[colour] * P.rookValue +
           analysis.queenCounts[colour] * P.queenValue;
}

// Share out of `ScaleNormal` of the two halves of the score that survives when
// an advantage cannot be converted. Material that cannot win by itself scales
// the whole score; a shortage of pawns only scales the endgame half, because
// with many pieces on the board the position still holds its middlegame chances.
constexpr int ScaleNormal = 64;

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

// The pawns of `colour` all stand on one wing (files a-d or e-h), and on one only.
bool pawnsOnOneFlank(const BoardAnalysis &analysis, int colour) {
    const auto &counts =
        colour == 0 ? analysis.whitePawnCounts : analysis.blackPawnCounts;
    int queenSide = 0;
    int kingSide = 0;
    for (int file = 0; file < 8; ++file) {
        (file < 4 ? queenSide : kingSide) += counts[file];
    }
    return (queenSide > 0) != (kingSide > 0);
}

// How much of an advantage `strong` can hope to convert. The cases are the
// classic ones: opposite-coloured bishops, a side without pawns that leads by no
// more than a piece, few pawns in general, and the rook ending with the pawns on
// one wing that is nearly always drawn.
EndgameScale endgameScale(const BoardAnalysis &analysis, Rules::Color strong) {
    EndgameScale scale;
    const int strongIndex = colorIndex(strong);
    const int weakIndex = 1 - strongIndex;
    const int strongPawns = analysis.pawnTotals[strongIndex];
    const int strongMaterial = nonPawnMaterial(analysis, strongIndex);
    const int weakMaterial = nonPawnMaterial(analysis, weakIndex);

    if (isOppositeColourBishopEnding(analysis)) {
        scale.limitBoth(analysis.pawnTotals[0] == 0 && analysis.pawnTotals[1] == 0
                            ? P.oppositeBishopsNoPawnsScale
                            : P.oppositeBishopsScale);
    }

    if (strongPawns == 0 && strongMaterial - weakMaterial <= P.bishopValue) {
        scale.limitBoth(strongMaterial < P.rookValue ? P.noPawnsMinorScale
                        : weakMaterial <= P.bishopValue
                            ? P.noPawnsRookVsMinorScale
                            : P.noPawnsRookScale);
    }

    // A bare king has nothing to defend with: the mop-up positions keep their
    // full value.
    const bool weakIsBareKing =
        weakMaterial == 0 && analysis.pawnTotals[weakIndex] == 0;
    if (!weakIsBareKing) {
        scale.limitEndGame(P.generalScaleBase +
                           P.generalScalePerPawn * strongPawns);
    }

    // One rook each, pawns of the stronger side on one wing, a lead of at most
    // a pawn, and the weaker king next to one of its own pawns.
    if (analysis.rookCounts[0] == 1 && analysis.rookCounts[1] == 1 &&
        strongMaterial == P.rookValue && weakMaterial == P.rookValue &&
        strongPawns - analysis.pawnTotals[weakIndex] <= 1 &&
        pawnsOnOneFlank(analysis, strongIndex)) {
        const int kingRow =
            strong == Rules::Color::White ? analysis.blackKingRow
                                          : analysis.whiteKingRow;
        const int kingColumn =
            strong == Rules::Color::White ? analysis.blackKingColumn
                                          : analysis.whiteKingColumn;
        bool kingBesidePawn = false;
        for (int index = 0; index < analysis.pieceCount; ++index) {
            const PieceOnBoard &piece = analysis.pieces[index];
            if (piece.type == Rules::PieceType::Pawn &&
                piece.color == opposite(strong) && kingRow != NoKingRow &&
                std::abs(piece.row - kingRow) <= 1 &&
                std::abs(piece.column - kingColumn) <= 1) {
                kingBesidePawn = true;
                break;
            }
        }
        if (kingBesidePawn) {
            scale.limitEndGame(P.rookEndgameOneFlankScale);
        }
    }
    return scale;
}

// A lone bishop with only rook pawns of its wrong colour cannot promote
// against a bare king: the defender walks to the corner the bishop does not
// control. The position is drawn whatever the material says, so the evaluator
// scores it as such.
bool isWrongColouredRookPawnEnding(const BoardAnalysis &analysis) {
    for (int colour = 0; colour < 2; ++colour) {
        const int other = 1 - colour;
        if (analysis.bishopCounts[colour] != 1 ||
            analysis.knightCounts[colour] != 0 ||
            analysis.rookCounts[colour] != 0 ||
            analysis.queenCounts[colour] != 0) {
            continue;
        }
        // The defender must be a bare king for the corner to be a safe haven.
        if (analysis.knightCounts[other] != 0 ||
            analysis.bishopCounts[other] != 0 ||
            analysis.rookCounts[other] != 0 ||
            analysis.queenCounts[other] != 0 ||
            analysis.pawnTotals[other] != 0) {
            continue;
        }

        const auto &pawnCounts = colour == 0 ? analysis.whitePawnCounts
                                             : analysis.blackPawnCounts;
        if (analysis.pawnTotals[colour] == 0) {
            continue;
        }
        int rookPawns = 0;
        for (int file = 1; file < 7; ++file) {
            rookPawns += pawnCounts[file];
        }
        if (rookPawns != 0) {
            continue;
        }
        // One of the two rook-pawn files is always playable, so only a single
        // file can be wrong-coloured.
        const bool pawnsOnAFile = pawnCounts[0] > 0;
        const bool pawnsOnHFile = pawnCounts[7] > 0;
        if (pawnsOnAFile == pawnsOnHFile) {
            continue;
        }

        // The a-pawn promotes on an even square, the h-pawn on an odd one.
        const bool bishopOnEvenSquares =
            analysis.bishopSquareColourCounts[colour][0] == 1;
        const bool promotionOnEvenSquares = pawnsOnAFile;
        if (bishopOnEvenSquares != promotionOnEvenSquares) {
            return true;
        }
    }
    return false;
}

// Exact score of king and pawn against king: zero for a draw, a known win
// (steeper as the pawn advances) for the side with the pawn otherwise.
int kpkScore(const BoardAnalysis &analysis, Rules::Color sideToMove) {
    const PieceOnBoard *pawn = nullptr;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        if (analysis.pieces[index].type == Rules::PieceType::Pawn) {
            pawn = &analysis.pieces[index];
        }
    }
    const bool white = pawn->color == Rules::Color::White;
    const Rules::Position pawnKing =
        white ? Rules::Position{analysis.whiteKingRow, analysis.whiteKingColumn}
              : Rules::Position{analysis.blackKingRow, analysis.blackKingColumn};
    const Rules::Position bareKing =
        white ? Rules::Position{analysis.blackKingRow, analysis.blackKingColumn}
              : Rules::Position{analysis.whiteKingRow, analysis.whiteKingColumn};
    if (!KpkBitbase::isWin(pawn->color, sideToMove, {pawn->row, pawn->column},
                           pawnKing, bareKing)) {
        return 0;
    }
    return signFor(pawn->color) *
           (P.knownWinScore +
            P.knownWinRankBonus * relativeRank(pawn->color, pawn->row));
}

// Classical score of a position from White's perspective. It performs no
// terminal test (checkmate, stalemate, insufficient material beyond the quick
// board check): the search handles terminals itself, so a leaf can skip the
// expensive legal-move generation they would need.
} // namespace

namespace HeuristicSearch {

void clearPawnCache() {
    PawnCache::instance().clear();
}

int evaluateBoard(const Board &board, Rules::Color sideToMove, bool inCheck,
                  quint64 pawnKey, HeuristicEval::EvalBreakdown *breakdown) {
    const BoardAnalysis analysis = analyzeBoard(board);
    if (isInsufficientMaterial(analysis)) {
        return 0;
    }
    if (isWrongColouredRookPawnEnding(analysis)) {
        return 0;
    }
    // King and pawn against king has an exact answer, and no term can improve
    // on it.
    if (analysis.pieceCount == 3 &&
        analysis.pawnTotals[0] + analysis.pawnTotals[1] == 1 &&
        analysis.whiteKingRow != NoKingRow &&
        analysis.blackKingRow != NoKingRow) {
        const int value = kpkScore(analysis, sideToMove);
        if (breakdown != nullptr) {
            breakdown->total = value;
        }
        return value;
    }

    AttackInfo attacksByWhite;
    AttackInfo attacksByBlack;
    const KingMasks kingMasks = buildKingMasks(board, analysis);
    buildAttackInfo(board, kingMasks, attacksByWhite, attacksByBlack);

    // Every term is computed once, both for the total and for the breakdown,
    // and tapered at the end so that the terms weigh in the phase together.
    TaperedScore material;
    TaperedScore placement;
    evaluateMaterialAndPlacement(analysis, &material, &placement);
    // The pawn-only terms come from the cache when the pawns have been seen
    // before; only what depends on the pieces is computed every time.
    PawnEntry pawnEntry;
    if (!PawnCache::instance().lookup(pawnKey, &pawnEntry)) {
        pawnEntry = computePawnEntry(analysis, attacksByWhite.pawnSquares,
                                     attacksByBlack.pawnSquares);
        PawnCache::instance().store(pawnKey, pawnEntry);
    }
    TaperedScore pawns = pawnEntry.structure;
    pawns += evaluatePassedPawns(board, analysis, attacksByWhite, attacksByBlack,
                                 sideToMove, pawnEntry.passed);
    // Collected by the attack walk, from each colour's point of view. The king
    // is added here because its safe moves need both attack maps.
    TaperedScore mobility = attacksByWhite.mobility;
    mobility -= attacksByBlack.mobility;
    mobility += evaluateTrappedRooks(analysis, attacksByWhite, attacksByBlack);
    mobility += kingMobility(board, analysis, Rules::Color::White,
                             attacksByBlack.squares);
    mobility -= kingMobility(board, analysis, Rules::Color::Black,
                             attacksByWhite.squares);
    const TaperedScore kingSafety =
        evaluateKingSafety(board, analysis, attacksByWhite, attacksByBlack);
    const TaperedScore space = pawnEntry.space;
    TaperedScore threats;
    TaperedScore outposts;
    TaperedScore badBishops;
    evaluatePieceTerms(board, analysis, attacksByWhite, attacksByBlack, &threats,
                       &outposts, &badBishops);
    const TaperedScore connectedRooks = evaluateConnectedRooks(board, analysis);
    const TaperedScore mopUp = evaluateEndgameMopUp(analysis);
    const TaperedScore tempo =
        (sideToMove == Rules::Color::White ? 1 : -1) * bothPhases(P.tempoBonus);
    const TaperedScore inCheckPenalty =
        inCheck ? -signFor(sideToMove) * bothPhases(P.inCheckPenalty)
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

    // The stronger side is the one the endgame half of the score favours; its
    // advantage may be worth less than it looks.
    const int lead = score.endGame != 0 ? score.endGame : score.middleGame;
    const EndgameScale scale =
        endgameScale(analysis, lead >= 0 ? Rules::Color::White
                                         : Rules::Color::Black);
    // C++ division truncates towards zero, so a mirrored position still scores
    // exactly the opposite.
    const int value = interpolate(
        score.middleGame * scale.middleGame / ScaleNormal,
        score.endGame * scale.endGame / ScaleNormal, analysis.phase);

    if (breakdown != nullptr) {
        breakdown->material = taperedValue(material, analysis.phase);
        breakdown->placement = taperedValue(placement, analysis.phase);
        breakdown->pawns = taperedValue(pawns, analysis.phase);
        breakdown->space = taperedValue(space, analysis.phase);
        breakdown->mobility = taperedValue(mobility, analysis.phase);
        breakdown->kingSafety = taperedValue(kingSafety, analysis.phase);
        breakdown->threats = taperedValue(threats, analysis.phase);
        breakdown->outposts = taperedValue(outposts, analysis.phase);
        breakdown->badBishops = taperedValue(badBishops, analysis.phase);
        breakdown->connectedRooks =
            taperedValue(connectedRooks, analysis.phase);
        breakdown->mopUp = taperedValue(mopUp, analysis.phase);
        breakdown->tempo = taperedValue(tempo, analysis.phase);
        breakdown->inCheck = taperedValue(inCheckPenalty, analysis.phase);
    }

    if (breakdown != nullptr) {
        breakdown->scaleFactor = scale.endGame;
        breakdown->total = value;
    }
    return value;
}

// Tests whether the side to move has at least one legal move. This drives the
// checkmate/stalemate tests at the top level, where Rules::hasLegalMove would
// scan every piece against every target square; the search primitives decide
// legality move by move and stop at the first legal one.
bool hasAnyLegalMove(const Rules &rules) {
    Rules probe = rules.detachedCopy();
    Rules::Move moves[MaxMoves];
    const int count = probe.generatePseudoLegalMoves(moves, MaxMoves);
    for (int i = 0; i < count; ++i) {
        Rules::Undo undo;
        if (probe.makeMove(moves[i], undo)) {
            probe.unmakeMove(moves[i], undo);
            return true;
        }
    }
    return false;
}

} // namespace

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
    // Every cached score was computed with the previous weights.
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

    // Only the side to move can legally be checkmated or stalemated. One check
    // test and one legal-move scan replace the two isInCheck calls and the full
    // hasLegalMove of isCheckmate()+isStalemate().
    const Rules::Color sideToMove = rules.currentPlayer();
    const bool inCheck = rules.isInCheck(sideToMove);
    if (!hasAnyLegalMove(rules)) {
        if (inCheck) {
            breakdown.total =
                sideToMove == Rules::Color::White ? -MateScore : MateScore;
        }
        return breakdown; // Stalemate leaves the total at zero.
    }

    // A claimable draw scores exactly like a drawn position, whatever material
    // still stands on the board. Checkmate was handled above and keeps
    // priority, so a mate delivered on the last half-move is still a mate.
    if (rules.isFiftyMoveRule() || rules.isThreefoldRepetition()) {
        return breakdown;
    }

    evaluateBoard(rules.board(), sideToMove, inCheck, rules.pawnKey(), &breakdown);

    // Reserve MateScore exclusively for actual checkmates.
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
