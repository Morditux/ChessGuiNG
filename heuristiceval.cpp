//
// Classical heuristic evaluation for chess positions.
//

#include "heuristiceval.h"
#include "heuristicsearch.h"
#include "winprobability.h"

#include <algorithm>
#include <array>
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
using HeuristicSearch::snapshotBoard;

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
constexpr std::array<int, 64> PawnPST = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    70, 70, 70, 90, 90, 70, 70, 70,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr std::array<int, 64> KnightPST = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

constexpr std::array<int, 64> BishopPST = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};

constexpr std::array<int, 64> RookPST = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr std::array<int, 64> QueenPST = {
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
    // Strongest (most valuable) attacker on each square, used to weigh a king
    // attack by the piece that delivers it. `None` when nothing attacks it.
    std::array<std::array<Rules::PieceType, 8>, 8> strongestType{};
    // Mobility of this colour's pieces, already weighted with the tapered
    // per-piece weights: the attack walk is the same walk that counts it.
    TaperedScore mobility{};
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

constexpr const std::array<int, 64> *pieceSquareTable(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return &PawnPST;
    case Rules::PieceType::Knight: return &KnightPST;
    case Rules::PieceType::Bishop: return &BishopPST;
    case Rules::PieceType::Rook:   return &RookPST;
    case Rules::PieceType::Queen:  return &QueenPST;
    case Rules::PieceType::King:
    case Rules::PieceType::None:   return nullptr;
    }
    return nullptr;
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

// Mobility weights are tapered so that rooks and queens matter more once the
// board opens up.
TaperedScore mobilityWeight(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:
        return {P.mobilityPawnMG, P.mobilityPawnEG};
    case Rules::PieceType::Knight:
        return {P.mobilityKnightMG, P.mobilityKnightEG};
    case Rules::PieceType::Bishop:
        return {P.mobilityBishopMG, P.mobilityBishopEG};
    case Rules::PieceType::Rook:
        return {P.mobilityRookMG, P.mobilityRookEG};
    case Rules::PieceType::Queen:
        return {P.mobilityQueenMG, P.mobilityQueenEG};
    case Rules::PieceType::King:
        return {P.mobilityKingMG, P.mobilityKingEG};
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

// Keeps the most valuable attacker seen on a square. Kings are never recorded:
// their value is zero, so a square only a king attacks stays `None`.
void addStrongestAttacker(AttackInfo &info, int row, int column,
                          Rules::PieceType type) {
    const int value = HeuristicEval::pieceValue(type);
    if (value > HeuristicEval::pieceValue(info.strongestType[row][column])) {
        info.strongestType[row][column] = type;
    }
}

bool occupiedByColour(const Board &board, Rules::Color color, int row, int column) {
    const auto &target = board[row][column];
    return target.has_value() && target->color == color;
}

// Marks the attacks of both colours and, in the same walk, counts how many
// squares each piece can move to. Walking the rays once for both is worth it:
// the attack map and the mobility area ask almost the same question.
void buildAttackInfo(const Board &board, AttackInfo &white, AttackInfo &black) {
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
                    addStrongestAttacker(info, targetRow, targetColumn,
                                         Rules::PieceType::Pawn);
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
                    addStrongestAttacker(info, targetRow, targetColumn,
                                         piece->type);
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
                            addStrongestAttacker(info, targetRow, targetColumn,
                                                 piece->type);
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
                info.mobility += moves * mobilityWeight(piece->type);
            }
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
        if (piece.type == Rules::PieceType::King) {
            *placement += sign * TaperedScore{KingMiddleGamePST[square],
                                              KingEndGamePST[square]};
        } else if (const auto *pst = pieceSquareTable(piece.type)) {
            *placement += sign * bothPhases((*pst)[square]);
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
            if (relativeRank(piece.color, piece.row) == 7) {
                *placement += sign * TaperedScore{P.rookSeventhRankMG,
                                                  P.rookSeventhRankEG};
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

// Counts pawns of `color` standing in the span (own and adjacent files) that
// lies ahead of `row` in the given direction.
int countPawnsAhead(const BoardAnalysis &analysis, Rules::Color color, int row,
                    int column, int direction) {
    const auto &pawnRows =
        color == Rules::Color::White ? analysis.whitePawnRows
                                     : analysis.blackPawnRows;
    const quint8 ahead = rowsAhead(row, direction);
    int count = 0;
    for (int file = std::max(0, column - 1); file <= std::min(7, column + 1);
         ++file) {
        count += std::popcount(
            static_cast<unsigned>(pawnRows[file] & ahead));
    }
    return count;
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

TaperedScore evaluatePawnStructure(const Board &board,
                                   const BoardAnalysis &analysis,
                                   const AttackInfo &whiteAttacks,
                                   const AttackInfo &blackAttacks) {
    TaperedScore score;

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
            piece.color == Rules::Color::White ? whiteAttacks.pawnSquares
                                               : blackAttacks.pawnSquares;
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

        // A backward pawn is not isolated, has no friendly pawn on an adjacent
        // file that could ever support it from behind, and faces an enemy pawn
        // on an adjacent file ahead of it.
        if (!isolated && !supportedByPawn && !passed) {
            bool canBeSupported = false;
            for (const int columnDelta : {-1, 1}) {
                const int file = piece.column + columnDelta;
                if (!isInside(0, file)) {
                    continue;
                }
                if (piece.color == Rules::Color::White) {
                    if (analysis.whiteRearRows[file] > piece.row) {
                        canBeSupported = true;
                    }
                } else {
                    if (analysis.blackRearRows[file] < piece.row) {
                        canBeSupported = true;
                    }
                }
            }
            bool enemyAhead = false;
            for (const int columnDelta : {-1, 1}) {
                const int file = piece.column + columnDelta;
                if (!isInside(0, file)) {
                    continue;
                }
                if (piece.color == Rules::Color::White) {
                    if (analysis.blackRearRows[file] < piece.row) {
                        enemyAhead = true;
                    }
                } else {
                    if (analysis.whiteRearRows[file] > piece.row) {
                        enemyAhead = true;
                    }
                }
            }
            if (!canBeSupported && enemyAhead) {
                score -= sign * TaperedScore{P.backwardPawnPenaltyMG,
                                             P.backwardPawnPenaltyEG};
            }
        }

        if (passed) {
            const int rank = relativeRank(piece.color, piece.row);
            TaperedScore bonus{
                P.passedPawnBaseMG + rank * P.passedPawnRankBonusMG,
                P.passedPawnBaseEG + rank * P.passedPawnRankBonusEG};

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
            if (hasRookBehindPawn(
                    board, piece,
                    piece.color == Rules::Color::White ? Rules::Color::Black
                                                       : Rules::Color::White)) {
                bonus -= TaperedScore{P.passedPawnEnemyRookBehindMG,
                                      P.passedPawnEnemyRookBehindEG};
            }

            score += sign *
                     TaperedScore{std::max(bonus.middleGame, 0),
                                  std::max(bonus.endGame, 0)};
        } else {
            // A candidate is a non-passed pawn whose span contains at least as
            // many friendly pawns as enemy ones.
            const int ownAhead = countPawnsAhead(analysis, piece.color,
                                                 piece.row, piece.column,
                                                 direction);
            const int enemyAhead =
                countPawnsAhead(analysis,
                                piece.color == Rules::Color::White
                                    ? Rules::Color::Black
                                    : Rules::Color::White,
                                piece.row, piece.column, direction);
            if (supportedByPawn && ownAhead > 0 && ownAhead >= enemyAhead) {
                const int rank = relativeRank(piece.color, piece.row);
                score += sign * TaperedScore{
                                    P.candidatePawnBonusMG +
                                        rank * P.candidatePawnRankBonusMG,
                                    P.candidatePawnBonusEG +
                                        rank * P.candidatePawnRankBonusEG};
            }
        }
    }
    return score;
}


// Threats, outposts and the bad-bishop penalty in one pass over the pieces:
// they all ask a question about the piece that is already in hand.
void evaluatePieceTerms(const BoardAnalysis &analysis, const AttackInfo &white,
                        const AttackInfo &black, TaperedScore *threats,
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

        if ((piece.type == Rules::PieceType::Knight ||
             piece.type == Rules::PieceType::Bishop) &&
            relativeRank(piece.color, piece.row) >= 4 &&
            own.pawnSquares[piece.row][piece.column] &&
            !enemy.pawnSquares[piece.row][piece.column]) {
            const TaperedScore bonus =
                piece.type == Rules::PieceType::Knight
                    ? TaperedScore{P.outpostKnightMG, P.outpostKnightEG}
                    : TaperedScore{P.outpostBishopMG, P.outpostBishopEG};
            *outposts += sign * bonus;
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

// How much of `kingAttackPenalty` one attacked king-zone square carries, given
// the strongest enemy piece attacking it. A knight or bishop keeps the full
// penalty (100), a pawn is lighter and a queen heavier.
int kingAttackPercent(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return P.kingAttackPawnPercent;
    case Rules::PieceType::Knight: return P.kingAttackKnightPercent;
    case Rules::PieceType::Bishop: return P.kingAttackBishopPercent;
    case Rules::PieceType::Rook:   return P.kingAttackRookPercent;
    case Rules::PieceType::Queen:  return P.kingAttackQueenPercent;
    case Rules::PieceType::King:
    case Rules::PieceType::None:   return 0;
    }
    return 0;
}

TaperedScore kingSafety(const Board &board, const BoardAnalysis &analysis,
                        Rules::Color color, const AttackInfo &enemy) {
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

    // Attacked squares in the king's neighbourhood expose it to tactics, and
    // each one is weighted by the strongest enemy piece attacking it: a queen
    // bearing down on the king is far more dangerous than a pawn.
    int attackWeight = 0;
    for (int row = kingRow - 1; row <= kingRow + 1; ++row) {
        for (int column = kingColumn - 1; column <= kingColumn + 1; ++column) {
            if (isInside(row, column) && enemy.squares[row][column]) {
                attackWeight +=
                    kingAttackPercent(enemy.strongestType[row][column]);
            }
        }
    }
    score -= middleGameOnly(P.kingAttackPenalty * attackWeight / 100);

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

    // Files with no friendly pawn near the king give attacking pieces a way in.
    for (int file = std::max(0, kingColumn - 1);
         file <= std::min(7, kingColumn + 1); ++file) {
        bool hasOwnPawn = false;
        for (int row = 0; row < 8; ++row) {
            const auto &piece = board[row][file];
            if (piece.has_value() && piece->type == Rules::PieceType::Pawn &&
                piece->color == color) {
                hasOwnPawn = true;
                break;
            }
        }
        if (!hasOwnPawn) {
            score -= middleGameOnly(P.openFileNearKingPenalty);
        }
    }

    // Enemy pawns advancing towards the king are a storm.
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
            score -= (rank - 4) * middleGameOnly(P.kingPawnStormPenalty);
        }
    }

    return score;
}

TaperedScore evaluateKingSafety(const Board &board,
                                const BoardAnalysis &analysis,
                                const AttackInfo &whiteAttacks,
                                const AttackInfo &blackAttacks) {
    TaperedScore score =
        kingSafety(board, analysis, Rules::Color::White, blackAttacks);
    score -= kingSafety(board, analysis, Rules::Color::Black, whiteAttacks);
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

    const int edge = centreDistance(weakRow, weakColumn);
    // The kings are at most fourteen Manhattan steps apart.
    const int proximity =
        14 - manhattanDistance(strongRow, strongColumn, weakRow, weakColumn);
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
bool isOppositeColourBishopEnding(const BoardAnalysis &analysis, int *num,
                                  int *den) {
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
    if (analysis.knightCounts[0] != 0 || analysis.knightCounts[1] != 0 ||
        analysis.rookCounts[0] != 0 || analysis.rookCounts[1] != 0 ||
        analysis.queenCounts[0] != 0 || analysis.queenCounts[1] != 0) {
        return false;
    }
    if (analysis.pawnTotals[0] == 0 && analysis.pawnTotals[1] == 0) {
        *num = 1;
        *den = 8;
    } else {
        *num = 1;
        *den = 2;
    }
    return true;
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

// Classical score of a position from White's perspective. It performs no
// terminal test (checkmate, stalemate, insufficient material beyond the quick
// board check): the search handles terminals itself, so a leaf can skip the
// expensive legal-move generation they would need.
} // namespace

namespace HeuristicSearch {

Board snapshotBoard(const Rules &rules) {
    Board board{};
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            board[row][column] = rules.pieceAt({row, column});
        }
    }
    return board;
}

int evaluateBoard(const Board &board, Rules::Color sideToMove, bool inCheck,
                  HeuristicEval::EvalBreakdown *breakdown) {
    const BoardAnalysis analysis = analyzeBoard(board);
    if (isInsufficientMaterial(analysis)) {
        return 0;
    }
    if (isWrongColouredRookPawnEnding(analysis)) {
        return 0;
    }

    AttackInfo attacksByWhite;
    AttackInfo attacksByBlack;
    buildAttackInfo(board, attacksByWhite, attacksByBlack);

    // Every term is computed once, both for the total and for the breakdown,
    // and tapered at the end so that the terms weigh in the phase together.
    TaperedScore material;
    TaperedScore placement;
    evaluateMaterialAndPlacement(analysis, &material, &placement);
    const TaperedScore pawns =
        evaluatePawnStructure(board, analysis, attacksByWhite, attacksByBlack);
    // Collected by the attack walk, from each colour's point of view. The king
    // is added here because its safe moves need both attack maps.
    TaperedScore mobility = attacksByWhite.mobility;
    mobility -= attacksByBlack.mobility;
    mobility += kingMobility(board, analysis, Rules::Color::White,
                             attacksByBlack.squares);
    mobility -= kingMobility(board, analysis, Rules::Color::Black,
                             attacksByWhite.squares);
    const TaperedScore kingSafety =
        evaluateKingSafety(board, analysis, attacksByWhite, attacksByBlack);
    TaperedScore threats;
    TaperedScore outposts;
    TaperedScore badBishops;
    evaluatePieceTerms(analysis, attacksByWhite, attacksByBlack, &threats,
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
    score += mobility;
    score += kingSafety;
    score += threats;
    score += outposts;
    score += badBishops;
    score += connectedRooks;
    score += mopUp;
    score += tempo;
    score += inCheckPenalty;

    int value = taperedValue(score, analysis.phase);

    if (breakdown != nullptr) {
        breakdown->material = taperedValue(material, analysis.phase);
        breakdown->placement = taperedValue(placement, analysis.phase);
        breakdown->pawns = taperedValue(pawns, analysis.phase);
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

    int scaleNum = 1;
    int scaleDen = 1;
    if (isOppositeColourBishopEnding(analysis, &scaleNum, &scaleDen)) {
        value = value * scaleNum / scaleDen;
    }

    if (breakdown != nullptr) {
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

    const Board board = snapshotBoard(rules);
    evaluateBoard(board, sideToMove, inCheck, &breakdown);

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
