//
// Classical heuristic evaluation for chess positions.
//

#include "heuristiceval.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {

using Board = std::array<std::array<std::optional<Rules::Piece>, 8>, 8>;
using AttackMap = std::array<std::array<bool, 8>, 8>;

constexpr int PawnValue = 100;
constexpr int KnightValue = 320;
constexpr int BishopValue = 330;
constexpr int RookValue = 500;
constexpr int QueenValue = 900;

constexpr int MaxPhase = 24;
constexpr int TempoBonus = 10;
constexpr int InCheckPenalty = 50;
constexpr int NoKingRow = -1;

// Tables are indexed from the point of view of the relevant side:
// index 0 is that side's home rank and index 7 its promotion rank.
constexpr std::array<int, 64> PawnPST = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
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
    std::array<int, 2> bishopCounts{};
    std::array<int, 2> minorCounts{};
    int whiteKingRow = NoKingRow;
    int whiteKingColumn = -1;
    int blackKingRow = NoKingRow;
    int blackKingColumn = -1;
    int phase = 0;
    bool hasPawnOrMajor = false;
};

constexpr int signFor(Rules::Color color) {
    return color == Rules::Color::White ? 1 : -1;
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

constexpr int pieceValue(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return PawnValue;
    case Rules::PieceType::Knight: return KnightValue;
    case Rules::PieceType::Bishop: return BishopValue;
    case Rules::PieceType::Rook:   return RookValue;
    case Rules::PieceType::Queen:  return QueenValue;
    case Rules::PieceType::King:
    case Rules::PieceType::None:   return 0;
    }
    return 0;
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

constexpr int mobilityWeight(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return 1;
    case Rules::PieceType::Knight: return 4;
    case Rules::PieceType::Bishop: return 4;
    case Rules::PieceType::Rook:   return 2;
    case Rules::PieceType::Queen:  return 1;
    case Rules::PieceType::King:   return 2;
    case Rules::PieceType::None:   return 0;
    }
    return 0;
}

constexpr int interpolate(int middleGameValue, int endGameValue, int phase) {
    return (middleGameValue * phase + endGameValue * (MaxPhase - phase)) / MaxPhase;
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
    analysis.whiteRearRows.fill(-1);
    analysis.blackRearRows.fill(8);

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &piece = board[row][column];
            if (!piece.has_value()) {
                continue;
            }

            analysis.pieces[analysis.pieceCount++] =
                {piece->type, piece->color, row, column};

            switch (piece->type) {
            case Rules::PieceType::Pawn:
                if (piece->color == Rules::Color::White) {
                    ++analysis.whitePawnCounts[column];
                    analysis.whiteRearRows[column] =
                        std::max(analysis.whiteRearRows[column], row);
                } else {
                    ++analysis.blackPawnCounts[column];
                    analysis.blackRearRows[column] =
                        std::min(analysis.blackRearRows[column], row);
                }
                analysis.hasPawnOrMajor = true;
                break;
            case Rules::PieceType::Bishop: {
                const int index = piece->color == Rules::Color::White ? 0 : 1;
                ++analysis.bishopCounts[index];
                ++analysis.minorCounts[index];
                break;
            }
            case Rules::PieceType::Knight: {
                const int index = piece->color == Rules::Color::White ? 0 : 1;
                ++analysis.minorCounts[index];
                break;
            }
            case Rules::PieceType::Rook:
                analysis.phase += 2;
                analysis.hasPawnOrMajor = true;
                break;
            case Rules::PieceType::Queen:
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
    return analysis;
}

void markSlidingAttacks(AttackMap &map, const Board &board, int row, int column,
                        const std::array<Offset, 4> &directions) {
    for (const auto &direction : directions) {
        int targetRow = row + direction.rowDelta;
        int targetColumn = column + direction.columnDelta;
        while (isInside(targetRow, targetColumn)) {
            map[targetRow][targetColumn] = true;
            if (board[targetRow][targetColumn].has_value()) {
                break;
            }
            targetRow += direction.rowDelta;
            targetColumn += direction.columnDelta;
        }
    }
}

AttackMap buildAttackMap(const Board &board, Rules::Color color) {
    AttackMap map{};
    const int pawnDirection = color == Rules::Color::White ? -1 : 1;

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &piece = board[row][column];
            if (!piece.has_value() || piece->color != color) {
                continue;
            }

            switch (piece->type) {
            case Rules::PieceType::Pawn:
                for (const int columnDelta : {-1, 1}) {
                    const int targetRow = row + pawnDirection;
                    const int targetColumn = column + columnDelta;
                    if (isInside(targetRow, targetColumn)) {
                        map[targetRow][targetColumn] = true;
                    }
                }
                break;
            case Rules::PieceType::Knight:
            case Rules::PieceType::King: {
                const auto &offsets = piece->type == Rules::PieceType::Knight
                                          ? KnightOffsets
                                          : KingOffsets;
                for (const auto &offset : offsets) {
                    const int targetRow = row + offset.rowDelta;
                    const int targetColumn = column + offset.columnDelta;
                    if (isInside(targetRow, targetColumn)) {
                        map[targetRow][targetColumn] = true;
                    }
                }
                break;
            }
            case Rules::PieceType::Bishop:
                markSlidingAttacks(map, board, row, column, BishopDirections);
                break;
            case Rules::PieceType::Rook:
                markSlidingAttacks(map, board, row, column, RookDirections);
                break;
            case Rules::PieceType::Queen:
                markSlidingAttacks(map, board, row, column, BishopDirections);
                markSlidingAttacks(map, board, row, column, RookDirections);
                break;
            case Rules::PieceType::None:
                break;
            }
        }
    }
    return map;
}

int countSliderMoves(const Board &board, const PieceOnBoard &piece,
                     const std::array<Offset, 4> &directions) {
    int moves = 0;
    for (const auto &direction : directions) {
        int targetRow = piece.row + direction.rowDelta;
        int targetColumn = piece.column + direction.columnDelta;
        while (isInside(targetRow, targetColumn)) {
            const auto &target = board[targetRow][targetColumn];
            if (!target.has_value() || target->color != piece.color) {
                ++moves;
            }
            if (target.has_value()) {
                break;
            }
            targetRow += direction.rowDelta;
            targetColumn += direction.columnDelta;
        }
    }
    return moves;
}

int pieceMobility(const Board &board, const PieceOnBoard &piece,
                  const AttackMap &enemyAttacks) {
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
            if (isInside(row, column) && !isOwnPiece(row, column)) {
                ++moves;
            }
        }
        break;
    case Rules::PieceType::King:
        // King mobility is the one case where attacked destination squares
        // should not be counted as useful mobility.
        for (const auto &offset : KingOffsets) {
            const int row = piece.row + offset.rowDelta;
            const int column = piece.column + offset.columnDelta;
            if (isInside(row, column) && !isOwnPiece(row, column) &&
                !enemyAttacks[row][column]) {
                ++moves;
            }
        }
        break;
    case Rules::PieceType::Bishop:
        moves += countSliderMoves(board, piece, BishopDirections);
        break;
    case Rules::PieceType::Rook:
        moves += countSliderMoves(board, piece, RookDirections);
        break;
    case Rules::PieceType::Queen:
        moves += countSliderMoves(board, piece, BishopDirections);
        moves += countSliderMoves(board, piece, RookDirections);
        break;
    case Rules::PieceType::Pawn: {
        const int direction = piece.color == Rules::Color::White ? -1 : 1;
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
    return moves * mobilityWeight(piece.type);
}

int evaluateMaterialAndPlacement(const BoardAnalysis &analysis) {
    int score = 0;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        const int sign = signFor(piece.color);
        score += sign * pieceValue(piece.type);

        const int square = relativeSquare(piece.color, piece.row, piece.column);
        if (piece.type == Rules::PieceType::King) {
            score += sign * interpolate(KingMiddleGamePST[square],
                                        KingEndGamePST[square], analysis.phase);
        } else if (const auto *pst = pieceSquareTable(piece.type)) {
            score += sign * (*pst)[square];
        }

        if (piece.type == Rules::PieceType::Rook) {
            const auto &ownPawns =
                piece.color == Rules::Color::White ? analysis.whitePawnCounts
                                                   : analysis.blackPawnCounts;
            const auto &enemyPawns =
                piece.color == Rules::Color::White ? analysis.blackPawnCounts
                                                   : analysis.whitePawnCounts;
            if (ownPawns[piece.column] == 0) {
                score += sign * (enemyPawns[piece.column] == 0 ? 20 : 10);
            }
            if (relativeRank(piece.color, piece.row) == 7) {
                score += sign * 20;
            }
        }
    }

    if (analysis.bishopCounts[0] >= 2) {
        score += 30;
    }
    if (analysis.bishopCounts[1] >= 2) {
        score -= 30;
    }

    return score;
}

int evaluatePawnStructure(const BoardAnalysis &analysis) {
    int score = 0;

    for (const Rules::Color color :
         {Rules::Color::White, Rules::Color::Black}) {
        const int sign = signFor(color);
        const auto &counts = color == Rules::Color::White
                                 ? analysis.whitePawnCounts
                                 : analysis.blackPawnCounts;
        for (int file = 0; file < 8; ++file) {
            if (counts[file] > 1) {
                score += sign * (-(counts[file] - 1) * 12);
            }
            const bool hasAdjacentPawns =
                (file > 0 && counts[file - 1] > 0) ||
                (file < 7 && counts[file + 1] > 0);
            if (!hasAdjacentPawns) {
                score += sign * (-15 * counts[file]);
            }
        }
    }

    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        if (piece.type != Rules::PieceType::Pawn) {
            continue;
        }

        bool isPassed = true;
        for (int file = std::max(0, piece.column - 1);
             file <= std::min(7, piece.column + 1); ++file) {
            if (piece.color == Rules::Color::White) {
                if (analysis.blackRearRows[file] < piece.row) {
                    isPassed = false;
                    break;
                }
            } else {
                if (analysis.whiteRearRows[file] > piece.row) {
                    isPassed = false;
                    break;
                }
            }
        }
        if (isPassed) {
            score += signFor(piece.color) *
                     (12 + relativeRank(piece.color, piece.row) * 6);
        }
    }
    return score;
}

int evaluateMobility(const Board &board, const BoardAnalysis &analysis,
                     const AttackMap &attacksByWhite,
                     const AttackMap &attacksByBlack) {
    int score = 0;
    for (int index = 0; index < analysis.pieceCount; ++index) {
        const PieceOnBoard &piece = analysis.pieces[index];
        const AttackMap &enemyAttacks =
            piece.color == Rules::Color::White ? attacksByBlack : attacksByWhite;
        score += signFor(piece.color) *
                 pieceMobility(board, piece, enemyAttacks);
    }
    return score;
}

int kingSafety(const Board &board, const BoardAnalysis &analysis,
               Rules::Color color, const AttackMap &enemyAttacks) {
    const int kingRow =
        color == Rules::Color::White ? analysis.whiteKingRow : analysis.blackKingRow;
    const int kingColumn = color == Rules::Color::White
                               ? analysis.whiteKingColumn
                               : analysis.blackKingColumn;
    if (kingRow == NoKingRow) {
        return 0;
    }

    int score = 0;
    const int homeRow = color == Rules::Color::White ? 7 : 0;
    if (kingRow == homeRow && (kingColumn == 2 || kingColumn == 6)) {
        score += 15;
    }

    for (int row = kingRow - 1; row <= kingRow + 1; ++row) {
        for (int column = kingColumn - 1; column <= kingColumn + 1; ++column) {
            if (isInside(row, column) && enemyAttacks[row][column]) {
                score -= 12;
            }
        }
    }

    // Pawns directly in front of the king form its shelter.
    const int shelterRow = color == Rules::Color::White ? kingRow - 1
                                                        : kingRow + 1;
    if (isInside(shelterRow, kingColumn)) {
        for (int column = std::max(0, kingColumn - 1);
             column <= std::min(7, kingColumn + 1); ++column) {
            const auto &piece = board[shelterRow][column];
            if (piece.has_value() && piece->color == color &&
                piece->type == Rules::PieceType::Pawn) {
                score += 8;
            }
        }
    }

    return score;
}

int evaluateKingSafety(const Board &board, const BoardAnalysis &analysis,
                       const AttackMap &attacksByWhite,
                       const AttackMap &attacksByBlack) {
    return kingSafety(board, analysis, Rules::Color::White, attacksByBlack) -
           kingSafety(board, analysis, Rules::Color::Black, attacksByWhite);
}

bool isInsufficientMaterial(const BoardAnalysis &analysis) {
    // King plus at most one minor piece per side cannot force mate.
    return !analysis.hasPawnOrMajor && analysis.minorCounts[0] <= 1 &&
           analysis.minorCounts[1] <= 1;
}

} // namespace

int HeuristicEval::evaluateCentipawns(const Rules &rules) {
    // Only the side to move can legally be checkmated or stalemated.
    const Rules::Color sideToMove = rules.currentPlayer();
    if (rules.isCheckmate(sideToMove)) {
        return sideToMove == Rules::Color::White ? -MateScore : MateScore;
    }
    if (rules.isStalemate(sideToMove)) {
        return 0;
    }

    const Board board = snapshotBoard(rules);
    const BoardAnalysis analysis = analyzeBoard(board);
    if (isInsufficientMaterial(analysis)) {
        return 0;
    }

    const AttackMap attacksByWhite = buildAttackMap(board, Rules::Color::White);
    const AttackMap attacksByBlack = buildAttackMap(board, Rules::Color::Black);

    int score = evaluateMaterialAndPlacement(analysis);
    score += evaluatePawnStructure(analysis);
    score += evaluateMobility(board, analysis, attacksByWhite, attacksByBlack);
    score += evaluateKingSafety(board, analysis, attacksByWhite, attacksByBlack);
    score += sideToMove == Rules::Color::White ? TempoBonus : -TempoBonus;

    // Only the side to move can legally be in check.
    if (rules.isInCheck(sideToMove)) {
        score -= signFor(sideToMove) * InCheckPenalty;
    }

    // Reserve MateScore exclusively for actual checkmates.
    return std::clamp(score, -MateScore + 1, MateScore - 1);
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

double HeuristicEval::evaluate(const Rules &rules) {
    return evaluateDisplayPercentage(rules);
}
