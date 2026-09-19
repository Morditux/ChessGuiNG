//
// Classical heuristic evaluation for chess positions.
//

#include "heuristiceval.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {

using Board = std::array<std::array<std::optional<Rules::Piece>, 8>, 8>;
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
    // Least advanced pawn per file: greatest row for White (-1 when empty),
    // smallest row for Black (8 when empty).
    std::array<int, 8> whiteRearRows{};
    std::array<int, 8> blackRearRows{};
    // Most advanced pawn per file: smallest row for White (8 when empty),
    // greatest row for Black (-1 when empty).
    std::array<int, 8> whiteFrontRows{};
    std::array<int, 8> blackFrontRows{};
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
};

constexpr int signFor(Rules::Color color) {
    return color == Rules::Color::White ? 1 : -1;
}

constexpr int colorIndex(Rules::Color color) {
    return color == Rules::Color::White ? 0 : 1;
}

constexpr bool isInside(int row, int column) {
    return row >= 0 && row < 8 && column >= 0 && column < 8;
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

Board snapshotBoard(const Rules &rules) {
    Board board{};
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            board[row][column] = rules.pieceAt({row, column});
        }
    }
    return board;
}

BoardAnalysis analyzeBoard(const Board &board) {
    BoardAnalysis analysis;
    analysis.whiteRearRows.fill(NoFile);
    analysis.blackRearRows.fill(EmptyRow);
    analysis.whiteFrontRows.fill(EmptyRow);
    analysis.blackFrontRows.fill(NoFile);

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
                    analysis.whiteRearRows[column] =
                        std::max(analysis.whiteRearRows[column], row);
                    analysis.whiteFrontRows[column] =
                        std::min(analysis.whiteFrontRows[column], row);
                } else {
                    ++analysis.blackPawnCounts[column];
                    analysis.blackRearRows[column] =
                        std::min(analysis.blackRearRows[column], row);
                    analysis.blackFrontRows[column] =
                        std::max(analysis.blackFrontRows[column], row);
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

void markSlidingAttacks(AttackInfo &info, const Board &board, int row,
                        int column, const std::array<Offset, 4> &directions,
                        int value) {
    for (const auto &direction : directions) {
        int targetRow = row + direction.rowDelta;
        int targetColumn = column + direction.columnDelta;
        while (isInside(targetRow, targetColumn)) {
            info.squares[targetRow][targetColumn] = true;
            if (info.cheapest[targetRow][targetColumn] == 0 ||
                value < info.cheapest[targetRow][targetColumn]) {
                info.cheapest[targetRow][targetColumn] = value;
            }
            if (board[targetRow][targetColumn].has_value()) {
                break;
            }
            targetRow += direction.rowDelta;
            targetColumn += direction.columnDelta;
        }
    }
}

// Fills the attack maps of both colours in a single board pass. Attacks never
// cross colours, so the interleaving order does not change the result.
void buildAttackInfo(const Board &board, AttackInfo &white, AttackInfo &black) {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &piece = board[row][column];
            if (!piece.has_value()) {
                continue;
            }

            const bool isWhite = piece->color == Rules::Color::White;
            AttackInfo &info = isWhite ? white : black;

            switch (piece->type) {
            case Rules::PieceType::Pawn: {
                const int pawnDirection = forwardDirection(piece->color);
                for (const int columnDelta : {-1, 1}) {
                    const int targetRow = row + pawnDirection;
                    const int targetColumn = column + columnDelta;
                    if (isInside(targetRow, targetColumn)) {
                        info.squares[targetRow][targetColumn] = true;
                        info.pawnSquares[targetRow][targetColumn] = true;
                        if (info.cheapest[targetRow][targetColumn] == 0 ||
                            P.pawnValue < info.cheapest[targetRow][targetColumn]) {
                            info.cheapest[targetRow][targetColumn] = P.pawnValue;
                        }
                    }
                }
                break;
            }
            case Rules::PieceType::Knight:
            case Rules::PieceType::King: {
                const bool knight = piece->type == Rules::PieceType::Knight;
                const auto &offsets = knight ? KnightOffsets : KingOffsets;
                const int value = knight ? P.knightValue : KingAttackerValue;
                for (const auto &offset : offsets) {
                    const int targetRow = row + offset.rowDelta;
                    const int targetColumn = column + offset.columnDelta;
                    if (isInside(targetRow, targetColumn)) {
                        info.squares[targetRow][targetColumn] = true;
                        if (info.cheapest[targetRow][targetColumn] == 0 ||
                            value < info.cheapest[targetRow][targetColumn]) {
                            info.cheapest[targetRow][targetColumn] = value;
                        }
                    }
                }
                break;
            }
            case Rules::PieceType::Bishop:
                markSlidingAttacks(info, board, row, column, BishopDirections,
                                   P.bishopValue);
                break;
            case Rules::PieceType::Rook:
                markSlidingAttacks(info, board, row, column, RookDirections,
                                   P.rookValue);
                break;
            case Rules::PieceType::Queen:
                markSlidingAttacks(info, board, row, column, BishopDirections,
                                   P.queenValue);
                markSlidingAttacks(info, board, row, column, RookDirections,
                                   P.queenValue);
                break;
            case Rules::PieceType::None:
                break;
            }
        }
    }
}

int countSliderMoves(const Board &board, const PieceOnBoard &piece,
                     const std::array<Offset, 4> &directions,
                     const AttackMap &enemyPawnAttacks) {
    int moves = 0;
    for (const auto &direction : directions) {
        int targetRow = piece.row + direction.rowDelta;
        int targetColumn = piece.column + direction.columnDelta;
        while (isInside(targetRow, targetColumn)) {
            const bool attackedByPawn = enemyPawnAttacks[targetRow][targetColumn];
            const auto &target = board[targetRow][targetColumn];
            if (!target.has_value()) {
                if (!attackedByPawn) {
                    ++moves;
                }
            } else {
                if (target->color != piece.color && !attackedByPawn) {
                    ++moves;
                }
                break;
            }
            targetRow += direction.rowDelta;
            targetColumn += direction.columnDelta;
        }
    }
    return moves;
}

int pieceMobility(const Board &board, const PieceOnBoard &piece,
                  const AttackMap &enemyPawnAttacks) {
    const auto isOwnPiece = [&board, &piece](int row, int column) {
        const auto &target = board[row][column];
        return target.has_value() && target->color == piece.color;
    };

    int moves = 0;
    switch (piece.type) {
    case Rules::PieceType::Knight:
        for (const auto &offset : KnightOffsets) {
            const int row = piece.row + offset.rowDelta;
            const int column = piece.column + offset.columnDelta;
            if (isInside(row, column) && !isOwnPiece(row, column) &&
                !enemyPawnAttacks[row][column]) {
                ++moves;
            }
        }
        break;
    case Rules::PieceType::King:
        for (const auto &offset : KingOffsets) {
            const int row = piece.row + offset.rowDelta;
            const int column = piece.column + offset.columnDelta;
            if (isInside(row, column) && !isOwnPiece(row, column) &&
                !enemyPawnAttacks[row][column]) {
                ++moves;
            }
        }
        break;
    case Rules::PieceType::Bishop:
        moves += countSliderMoves(board, piece, BishopDirections,
                                  enemyPawnAttacks);
        break;
    case Rules::PieceType::Rook:
        moves += countSliderMoves(board, piece, RookDirections,
                                  enemyPawnAttacks);
        break;
    case Rules::PieceType::Queen:
        moves += countSliderMoves(board, piece, BishopDirections,
                                  enemyPawnAttacks);
        moves += countSliderMoves(board, piece, RookDirections,
                                  enemyPawnAttacks);
        break;
    case Rules::PieceType::Pawn: {
        const int direction = forwardDirection(piece.color);
        const int startingRow = piece.color == Rules::Color::White ? 6 : 1;
        const int forwardRow = piece.row + direction;
        if (isInside(forwardRow, piece.column) &&
            !board[forwardRow][piece.column].has_value()) {
            ++moves;
            const int doubleRow = piece.row + 2 * direction;
            if (piece.row == startingRow &&
                !board[doubleRow][piece.column].has_value()) {
                ++moves;
            }
        }
        for (const int columnDelta : {-1, 1}) {
            const int column = piece.column + columnDelta;
            if (!isInside(forwardRow, column)) {
                continue;
            }
            const auto &target = board[forwardRow][column];
            if (target.has_value() && target->color != piece.color) {
                ++moves;
            }
        }
        break;
    }
    case Rules::PieceType::None:
        break;
    }
    return moves;
}

TaperedScore evaluateMaterial(const BoardAnalysis &analysis) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        score += signFor(piece.color) *
                 bothPhases(HeuristicEval::pieceValue(piece.type));
    }
    return score;
}

TaperedScore evaluatePlacement(const BoardAnalysis &analysis) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        const int sign = signFor(piece.color);
        const int square = relativeSquare(piece.color, piece.row, piece.column);
        if (piece.type == Rules::PieceType::King) {
            score += sign * TaperedScore{KingMiddleGamePST[square],
                                         KingEndGamePST[square]};
        } else if (const auto *pst = pieceSquareTable(piece.type)) {
            score += sign * bothPhases((*pst)[square]);
        }

        if (piece.type == Rules::PieceType::Rook) {
            const auto &ownPawns =
                piece.color == Rules::Color::White ? analysis.whitePawnCounts
                                                   : analysis.blackPawnCounts;
            const auto &enemyPawns =
                piece.color == Rules::Color::White ? analysis.blackPawnCounts
                                                   : analysis.whitePawnCounts;
            if (ownPawns[piece.column] == 0) {
                score += sign * (enemyPawns[piece.column] == 0
                                     ? TaperedScore{P.rookOpenFileMG,
                                                    P.rookOpenFileEG}
                                     : TaperedScore{P.rookSemiOpenFileMG,
                                                    P.rookSemiOpenFileEG});
            }
            if (relativeRank(piece.color, piece.row) == 7) {
                score += sign * TaperedScore{P.rookSeventhRankMG,
                                             P.rookSeventhRankEG};
            }
        }
    }

    const TaperedScore bishopPair{P.bishopPairMG, P.bishopPairEG};
    if (analysis.bishopCounts[0] >= 2) {
        score += bishopPair;
    }
    if (analysis.bishopCounts[1] >= 2) {
        score -= bishopPair;
    }

    return score;
}

bool isPassedPawn(const BoardAnalysis &analysis, const PieceOnBoard &pawn) {
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
int countPawnsAhead(const Board &board, Rules::Color color, int row,
                    int column, int direction) {
    int count = 0;
    for (int file = std::max(0, column - 1); file <= std::min(7, column + 1);
         ++file) {
        for (int r = row + direction; isInside(r, file); r += direction) {
            const auto &piece = board[r][file];
            if (piece.has_value() && piece->type == Rules::PieceType::Pawn &&
                piece->color == color) {
                ++count;
            }
        }
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
                                   const BoardAnalysis &analysis) {
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
        const int supportRow = piece.row - direction;

        bool supportedByPawn = false;
        if (isInside(supportRow, piece.column)) {
            for (const int columnDelta : {-1, 1}) {
                const int column = piece.column + columnDelta;
                if (!isInside(supportRow, column)) {
                    continue;
                }
                const auto &neighbour = board[supportRow][column];
                if (neighbour.has_value() &&
                    neighbour->type == Rules::PieceType::Pawn &&
                    neighbour->color == piece.color) {
                    supportedByPawn = true;
                }
            }
        }
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

            const int aheadRow = piece.row + direction;
            if (isInside(aheadRow, piece.column)) {
                if (board[aheadRow][piece.column].has_value()) {
                    bonus -= TaperedScore{P.passedPawnBlockedPenaltyMG,
                                          P.passedPawnBlockedPenaltyEG};
                } else if ((piece.color == Rules::Color::White
                                ? analysis.blackPawnCounts
                                : analysis.whitePawnCounts)[piece.column] > 0) {
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
            const int ownAhead = countPawnsAhead(board, piece.color, piece.row,
                                                 piece.column, direction);
            const int enemyAhead =
                countPawnsAhead(board,
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

TaperedScore evaluateMobility(const Board &board, const BoardAnalysis &analysis,
                              const AttackMap &whitePawnAttacks,
                              const AttackMap &blackPawnAttacks) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        const AttackMap &enemyPawnAttacks =
            piece.color == Rules::Color::White ? blackPawnAttacks
                                               : whitePawnAttacks;
        score += signFor(piece.color) *
                 (pieceMobility(board, piece, enemyPawnAttacks) *
                  mobilityWeight(piece.type));
    }
    return score;
}

TaperedScore evaluateThreats(const BoardAnalysis &analysis,
                            const AttackInfo &white, const AttackInfo &black) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type == Rules::PieceType::King ||
            piece.type == Rules::PieceType::None) {
            continue;
        }

        const bool isWhite = piece.color == Rules::Color::White;
        const AttackInfo &enemy = isWhite ? black : white;
        const AttackInfo &own = isWhite ? white : black;
        const int sign = signFor(piece.color);

        if (!enemy.squares[piece.row][piece.column]) {
            continue;
        }

        const int value = HeuristicEval::pieceValue(piece.type);
        if (!own.squares[piece.row][piece.column]) {
            // Attacked and undefended: the opponent can simply take it.
            score -= sign * bothPhases(value / P.hangingPieceDivisor);
        } else {
            const int cheapest = enemy.cheapest[piece.row][piece.column];
            if (cheapest != 0 && cheapest + P.cheapAttackerMargin < value) {
                score -=
                    sign * bothPhases((value - cheapest) / P.cheapAttackerDivisor);
            }
        }
    }
    return score;
}

TaperedScore evaluateOutposts(const BoardAnalysis &analysis,
                             const AttackInfo &white,
                             const AttackInfo &black) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type != Rules::PieceType::Knight &&
            piece.type != Rules::PieceType::Bishop) {
            continue;
        }

        const bool isWhite = piece.color == Rules::Color::White;
        const AttackInfo &own = isWhite ? white : black;
        const AttackInfo &enemy = isWhite ? black : white;
        if (relativeRank(piece.color, piece.row) < 4) {
            continue;
        }
        if (!own.pawnSquares[piece.row][piece.column] ||
            enemy.pawnSquares[piece.row][piece.column]) {
            continue;
        }

        const TaperedScore bonus =
            piece.type == Rules::PieceType::Knight
                ? TaperedScore{P.outpostKnightMG, P.outpostKnightEG}
                : TaperedScore{P.outpostBishopMG, P.outpostBishopEG};
        score += signFor(piece.color) * bonus;
    }
    return score;
}

TaperedScore evaluateBadBishops(const BoardAnalysis &analysis) {
    TaperedScore score;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type != Rules::PieceType::Bishop) {
            continue;
        }
        const int colour = colorIndex(piece.color);
        const int squareColour = (piece.row + piece.column) % 2;
        const int blockers =
            analysis.pawnsOnSquareColour[colour][squareColour];
        const int penalty =
            std::min(P.badBishopCap, blockers * P.badBishopPenalty);
        score -= signFor(piece.color) * bothPhases(penalty);
    }
    return score;
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

TaperedScore kingSafety(const Board &board, const BoardAnalysis &analysis,
                        Rules::Color color, const AttackMap &enemyAttacks) {
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

    // Attacked squares in the king's neighbourhood expose it to tactics.
    int attackedSquares = 0;
    for (int row = kingRow - 1; row <= kingRow + 1; ++row) {
        for (int column = kingColumn - 1; column <= kingColumn + 1; ++column) {
            if (isInside(row, column) && enemyAttacks[row][column]) {
                ++attackedSquares;
            }
        }
    }
    score -= attackedSquares * middleGameOnly(P.kingAttackPenalty);

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
            score += TaperedScore{shelter.middleGame * percent / 100,
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
                                const AttackMap &whiteAttacks,
                                const AttackMap &blackAttacks) {
    TaperedScore score =
        kingSafety(board, analysis, Rules::Color::White, blackAttacks);
    score -= kingSafety(board, analysis, Rules::Color::Black, whiteAttacks);
    return score;
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
    const TaperedScore material = evaluateMaterial(analysis);
    const TaperedScore placement = evaluatePlacement(analysis);
    const TaperedScore pawns = evaluatePawnStructure(board, analysis);
    const TaperedScore mobility =
        evaluateMobility(board, analysis, attacksByWhite.pawnSquares,
                         attacksByBlack.pawnSquares);
    const TaperedScore kingSafety =
        evaluateKingSafety(board, analysis, attacksByWhite.squares,
                           attacksByBlack.squares);
    const TaperedScore threats =
        evaluateThreats(analysis, attacksByWhite, attacksByBlack);
    const TaperedScore outposts =
        evaluateOutposts(analysis, attacksByWhite, attacksByBlack);
    const TaperedScore badBishops = evaluateBadBishops(analysis);
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

// Maximum number of pseudo-legal moves in any position (218 legal, plus the
// extra promotion variants).
constexpr int MaxMoves = 256;

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
        ordering.gain += P.pawnValue;
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
bool hasAnyLegalMove(const Rules &rules);

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

        Rules::Undo undo;
        if (!rules.makeMove(move, undo)) {
            continue;
        }
        anyLegal = true;
        // A check may start a forcing line, so it is never reduced.
        const bool givesCheck = rules.isInCheck(rules.currentPlayer());

        // Late move reductions: quiet moves far down the list are searched a
        // ply shallower and only re-searched in full when they beat alpha.
        int childDepth = depth - 1;
        int reduction = 0;
        if (depth >= 3 && !tactical && !inCheck && !givesCheck && i >= 3 &&
            alpha > -MateThreshold && beta < MateThreshold) {
            reduction = i >= 8 ? 2 : 1;
            if (reduction > childDepth) {
                reduction = childDepth;
            }
            childDepth -= reduction;
        }

        int score = -negamax(rules, childDepth, quiescenceDepth, -beta, -alpha,
                             ply + 1, state);
        if (reduction > 0 && score > alpha) {
            score = -negamax(rules, depth - 1, quiescenceDepth, -beta, -alpha,
                             ply + 1, state);
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
    TranspositionTable::instance().clear();
    EvalCache::instance().clear();
}

void HeuristicEval::resetParams() {
    activeParams = EvalParams{};
    TranspositionTable::instance().clear();
    EvalCache::instance().clear();
}

void HeuristicEval::setSearchThreads(int threads) {
    searchThreadOverride.store(threads > 0 ? std::min(threads, 16) : 0,
                               std::memory_order_relaxed);
}

int HeuristicEval::searchThreads() {
    return rootWorkerLimit();
}

void HeuristicEval::clearSearchCache() {
    TranspositionTable::instance().clear();
    EvalCache::instance().clear();
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

HeuristicEval::SearchResult HeuristicEval::search(const Rules &rules, int depth,
                                                 int quiescenceDepth) {
    return search(rules, depth, quiescenceDepth, SearchLimits{});
}

HeuristicEval::SearchResult HeuristicEval::search(const Rules &rules, int depth,
                                                 int quiescenceDepth,
                                                 const SearchLimits &limits) {
    SearchResult result;

    const Rules::Color side = rules.currentPlayer();
    depth = std::max(0, depth);
    quiescenceDepth = std::max(0, quiescenceDepth);

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

double HeuristicEval::centipawnsToPercentage(double cp) {
    if (cp >= MateScore) {
        return 100.0;
    }
    if (cp <= -MateScore) {
        return 0.0;
    }

    const double exponent = -cp / 400.0;
    const double percentage = 100.0 / (1.0 + std::pow(10.0, exponent));
    return std::clamp(percentage, 0.0, 100.0);
}

double HeuristicEval::evaluateDisplayPercentage(const Rules &rules) {
    return centipawnsToPercentage(static_cast<double>(evaluateCentipawns(rules)));
}

