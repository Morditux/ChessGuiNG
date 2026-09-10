//
// Chess rules engine.
//

#include "rules.h"

#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>

Rules::Rules() {
    reset();
}

void Rules::reset() {
    for (auto &row : board_) {
        row.fill(std::nullopt);
    }

    constexpr std::array<PieceType, 8> backRank = {
        PieceType::Rook,
        PieceType::Knight,
        PieceType::Bishop,
        PieceType::Queen,
        PieceType::King,
        PieceType::Bishop,
        PieceType::Knight,
        PieceType::Rook
    };

    for (int column = 0; column < 8; ++column) {
        board_[0][column] = Piece{backRank[column], Color::Black, false};
        board_[1][column] = Piece{PieceType::Pawn, Color::Black, false};
        board_[6][column] = Piece{PieceType::Pawn, Color::White, false};
        board_[7][column] = Piece{backRank[column], Color::White, false};
    }

    currentPlayer_ = Color::White;
    lastMove_.reset();
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
    repetitionHistory_.clear();
    repetitionHistory_.push_back(positionKey());
}

std::optional<Rules::Piece> Rules::pieceAt(Position position) const {
    if (!isInside(position)) {
        return std::nullopt;
    }

    return board_[position.row][position.column];
}

Rules::Color Rules::currentPlayer() const {
    return currentPlayer_;
}

std::optional<Rules::Move> Rules::lastMove() const {
    return lastMove_;
}

bool Rules::isValidMove(Position from, Position to) const {
    return isValidMove(Move{from, to, PieceType::None});
}

bool Rules::isValidMove(const Move &move) const {
    if (!isPseudoLegalMove(move)) {
        return false;
    }

    const Piece movingPiece = *board_[move.from.row][move.from.column];
    Rules candidate = *this;
    candidate.applyMoveUnchecked(move);

    return !candidate.isInCheck(movingPiece.color);
}

bool Rules::tryMove(Position from, Position to, PieceType promotion) {
    return tryMove(Move{from, to, promotion});
}

bool Rules::tryMove(const Move &move) {
    if (!isValidMove(move)) {
        return false;
    }

    // Copy before applyMoveUnchecked destroys the source square contents.
    const Piece movingPiece = *board_[move.from.row][move.from.column];
    const bool isCapture = board_[move.to.row][move.to.column].has_value();

    applyMoveUnchecked(move);
    lastMove_ = move;

    // A pawn move or a capture resets the halfmove clock; an en passant
    // capture is always a pawn move and is covered by the first condition.
    if (movingPiece.type == PieceType::Pawn || isCapture) {
        halfmoveClock_ = 0;
    } else {
        ++halfmoveClock_;
    }

    if (currentPlayer_ == Color::Black) {
        ++fullmoveNumber_;
    }
    currentPlayer_ = opposite(currentPlayer_);
    repetitionHistory_.push_back(positionKey());
    return true;
}

std::vector<Rules::Position> Rules::legalMoves(Position from) const {
    std::vector<Position> moves;

    if (!isInside(from) || !board_[from.row][from.column].has_value() ||
        board_[from.row][from.column]->color != currentPlayer_) {
        return moves;
    }

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const Position to{row, column};
            if (isValidMove(from, to)) {
                moves.push_back(to);
            }
        }
    }

    return moves;
}

bool Rules::isInCheck(Color color) const {
    Position kingPosition{-1, -1};
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &square = board_[row][column];
            if (square.has_value() && square->type == PieceType::King &&
                square->color == color) {
                kingPosition = Position{row, column};
                break;
            }
        }
        if (kingPosition.row != -1) {
            break;
        }
    }

    if (kingPosition.row == -1) {
        return false;
    }

    return isSquareAttacked(kingPosition, opposite(color));
}

bool Rules::isCheckmate(Color color) const {
    return isInCheck(color) && !hasLegalMove(color);
}

bool Rules::isStalemate(Color color) const {
    return !isInCheck(color) && !hasLegalMove(color);
}

bool Rules::isGameOver() const {
    return isCheckmate(Color::White) || isCheckmate(Color::Black) || isDraw();
}

bool Rules::isThreefoldRepetition() const {
    if (repetitionHistory_.size() < 3) {
        return false;
    }

    const QString currentKey = positionKey();
    return std::count(repetitionHistory_.begin(), repetitionHistory_.end(),
                      currentKey) >= 3;
}

bool Rules::isFiftyMoveRule() const {
    return halfmoveClock_ >= 100;
}

bool Rules::isDraw() const {
    // A checkmate ends the game before any claimable draw can apply.
    if (isCheckmate(Color::White) || isCheckmate(Color::Black)) {
        return false;
    }
    return isStalemate(currentPlayer_) || isInsufficientMaterial() ||
           isThreefoldRepetition() || isFiftyMoveRule();
}

bool Rules::isInsufficientMaterial() const {
    int knights = 0;
    int evenSquaredBishops = 0;
    int oddSquaredBishops = 0;

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &square = board_[row][column];
            if (!square.has_value() || square->type == PieceType::King) {
                continue;
            }

            switch (square->type) {
            case PieceType::Knight:
                ++knights;
                break;
            case PieceType::Bishop:
                if ((row + column) % 2 == 0) {
                    ++evenSquaredBishops;
                } else {
                    ++oddSquaredBishops;
                }
                break;
            default:
                // Pawns, rooks, and queens always allow checkmate.
                return false;
            }
        }
    }

    if (knights == 0 && evenSquaredBishops == 0 && oddSquaredBishops == 0) {
        return true; // King versus king.
    }
    if (knights == 1 && evenSquaredBishops == 0 && oddSquaredBishops == 0) {
        return true; // King and knight versus king.
    }
    if (knights == 0 && evenSquaredBishops + oddSquaredBishops == 1) {
        return true; // King and bishop versus king.
    }
    if (knights == 0 &&
        evenSquaredBishops + oddSquaredBishops == 2 &&
        (evenSquaredBishops == 2 || oddSquaredBishops == 2)) {
        return true; // Only bishops confined to same-colored squares.
    }

    return false;
}

QString Rules::positionKey() const {
    QStringList fields = toFen().split(QChar(' '));
    if (fields.size() < 4) {
        return toFen();
    }

    // FIDE repetition compares the same legal moves: an en passant target is
    // only part of the position when the capture is actually available.
    if (!enPassantCaptureAvailable()) {
        fields[3] = QStringLiteral("-");
    }
    return fields.mid(0, 4).join(QChar(' '));
}

bool Rules::enPassantCaptureAvailable() const {
    if (!lastMove_.has_value()) {
        return false;
    }

    const Move &last = *lastMove_;
    const auto movedPiece = pieceAt(last.to);
    if (!movedPiece.has_value() || movedPiece->type != PieceType::Pawn ||
        std::abs(last.to.row - last.from.row) != 2) {
        return false;
    }

    const int targetRow = (last.from.row + last.to.row) / 2;
    const int pawnRow = last.to.row;
    const Position target{targetRow, last.to.column};

    for (const int columnOffset : {-1, 1}) {
        const int column = last.to.column + columnOffset;
        if (column < 0 || column > 7) {
            continue;
        }

        const Position from{pawnRow, column};
        const auto pawn = pieceAt(from);
        if (pawn.has_value() && pawn->type == PieceType::Pawn &&
            pawn->color == currentPlayer_ && isValidMove(from, target)) {
            return true;
        }
    }

    return false;
}

bool Rules::isInside(Position position) {
    return position.row >= 0 && position.row < 8 &&
           position.column >= 0 && position.column < 8;
}

QString Rules::toUci(Position from, Position to, PieceType promotion) {
    QString uci = QString(QChar('a' + from.column)) + QString::number(8 - from.row) +
                  QString(QChar('a' + to.column)) + QString::number(8 - to.row);
    if (promotion != PieceType::None) {
        char pChar = 'q';
        switch (promotion) {
        case PieceType::Rook:   pChar = 'r'; break;
        case PieceType::Bishop: pChar = 'b'; break;
        case PieceType::Knight: pChar = 'n'; break;
        case PieceType::Queen:  pChar = 'q'; break;
        default: pChar = 'q'; break;
        }
        uci += QChar(pChar);
    }
    return uci;
}

QString Rules::toUci(const Move &move) {
    return toUci(move.from, move.to, move.promotion);
}

Rules::Color Rules::opposite(Color color) {
    return color == Color::White ? Color::Black : Color::White;
}

bool Rules::isPromotionPiece(PieceType type) {
    return type == PieceType::Queen ||
           type == PieceType::Rook ||
           type == PieceType::Bishop ||
           type == PieceType::Knight;
}

bool Rules::isPseudoLegalMove(const Move &move) const {
    if (!isInside(move.from) || !isInside(move.to) || move.from == move.to) {
        return false;
    }

    const auto &sourceSquare = board_[move.from.row][move.from.column];
    if (!sourceSquare.has_value() || sourceSquare->color != currentPlayer_) {
        return false;
    }

    const Piece movingPiece = *sourceSquare;
    const auto &targetSquare = board_[move.to.row][move.to.column];

    if (targetSquare.has_value() && targetSquare->color == movingPiece.color) {
        return false;
    }

    // Kings are never captured.
    if (targetSquare.has_value() && targetSquare->type == PieceType::King) {
        return false;
    }

    const bool reachesPromotion = movingPiece.type == PieceType::Pawn &&
                                  (move.to.row == 0 || move.to.row == 7);
    if (move.promotion != PieceType::None &&
        (!isPromotionPiece(move.promotion) || !reachesPromotion)) {
        return false;
    }

    const int rowDelta = move.to.row - move.from.row;
    const int columnDelta = move.to.column - move.from.column;
    const int absoluteRowDelta = std::abs(rowDelta);
    const int absoluteColumnDelta = std::abs(columnDelta);

    switch (movingPiece.type) {
    case PieceType::Pawn: {
        const int direction = movingPiece.color == Color::White ? -1 : 1;
        const int startingRow = movingPiece.color == Color::White ? 6 : 1;
        const bool targetIsEmpty = !targetSquare.has_value();

        if (columnDelta == 0 && targetIsEmpty) {
            if (rowDelta == direction) {
                return true;
            }

            return rowDelta == 2 * direction && move.from.row == startingRow &&
                   !board_[move.from.row + direction][move.from.column].has_value();
        }

        if (absoluteColumnDelta == 1 && rowDelta == direction) {
            if (targetSquare.has_value() && targetSquare->color != movingPiece.color) {
                return true;
            }

            if (targetIsEmpty && lastMove_.has_value()) {
                const Move &previousMove = *lastMove_;
                const auto &previousPiece = board_[previousMove.to.row][previousMove.to.column];
                const bool previousPawn = previousPiece.has_value() &&
                                           previousPiece->type == PieceType::Pawn &&
                                           previousPiece->color != movingPiece.color;
                const bool previousDoubleStep =
                    std::abs(previousMove.to.row - previousMove.from.row) == 2;
                const bool adjacentPawn = previousMove.to.row == move.from.row &&
                                          previousMove.to.column == move.to.column;

                return previousPawn && previousDoubleStep && adjacentPawn;
            }
        }

        return false;
    }

    case PieceType::Knight:
        return (absoluteRowDelta == 2 && absoluteColumnDelta == 1) ||
               (absoluteRowDelta == 1 && absoluteColumnDelta == 2);

    case PieceType::Bishop:
        return absoluteRowDelta == absoluteColumnDelta &&
               isPathClear(move.from, move.to);

    case PieceType::Rook:
        return (rowDelta == 0 || columnDelta == 0) &&
               isPathClear(move.from, move.to);

    case PieceType::Queen:
        return (absoluteRowDelta == absoluteColumnDelta ||
                rowDelta == 0 || columnDelta == 0) &&
               isPathClear(move.from, move.to);

    case PieceType::King: {
        if (absoluteRowDelta <= 1 && absoluteColumnDelta <= 1) {
            return true;
        }

        if (rowDelta != 0 || absoluteColumnDelta != 2 || movingPiece.hasMoved) {
            return false;
        }

        const int homeRow = movingPiece.color == Color::White ? 7 : 0;
        if (move.from.row != homeRow || move.from.column != 4 ||
            (move.to.column != 2 && move.to.column != 6) ||
            isInCheck(movingPiece.color)) {
            return false;
        }

        const int rookColumn = move.to.column > move.from.column ? 7 : 0;
        const auto &rookSquare = board_[homeRow][rookColumn];
        if (!rookSquare.has_value() || rookSquare->color != movingPiece.color ||
            rookSquare->type != PieceType::Rook || rookSquare->hasMoved) {
            return false;
        }

        const int step = move.to.column > move.from.column ? 1 : -1;
        for (int column = move.from.column + step; column != rookColumn; column += step) {
            if (board_[homeRow][column].has_value()) {
                return false;
            }
        }

        // The king may not cross an attacked square while castling.
        Rules crossingPosition = *this;
        crossingPosition.applyMoveUnchecked(
            Move{move.from, {homeRow, move.from.column + step}, PieceType::None});
        return !crossingPosition.isInCheck(movingPiece.color);
    }

    case PieceType::None:
        return false;
    }

    return false;
}

bool Rules::isPathClear(Position from, Position to) const {
    const int rowStep = (to.row > from.row) - (to.row < from.row);
    const int columnStep = (to.column > from.column) - (to.column < from.column);

    Position current{from.row + rowStep, from.column + columnStep};
    while (current != to) {
        if (board_[current.row][current.column].has_value()) {
            return false;
        }
        current.row += rowStep;
        current.column += columnStep;
    }

    return true;
}

bool Rules::isSquareAttacked(Position position, Color byColor) const {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const auto &square = board_[row][column];
            if (!square.has_value() || square->color != byColor) {
                continue;
            }

            const Position from{row, column};
            const int rowDelta = position.row - from.row;
            const int columnDelta = position.column - from.column;
            const int absoluteRowDelta = std::abs(rowDelta);
            const int absoluteColumnDelta = std::abs(columnDelta);

            switch (square->type) {
            case PieceType::Pawn: {
                const int direction = byColor == Color::White ? -1 : 1;
                if (rowDelta == direction && absoluteColumnDelta == 1) {
                    return true;
                }
                break;
            }
            case PieceType::Knight:
                if ((absoluteRowDelta == 2 && absoluteColumnDelta == 1) ||
                    (absoluteRowDelta == 1 && absoluteColumnDelta == 2)) {
                    return true;
                }
                break;
            case PieceType::Bishop:
                if (absoluteRowDelta == absoluteColumnDelta &&
                    isPathClear(from, position)) {
                    return true;
                }
                break;
            case PieceType::Rook:
                if ((rowDelta == 0 || columnDelta == 0) &&
                    isPathClear(from, position)) {
                    return true;
                }
                break;
            case PieceType::Queen:
                if ((absoluteRowDelta == absoluteColumnDelta ||
                     rowDelta == 0 || columnDelta == 0) &&
                    isPathClear(from, position)) {
                    return true;
                }
                break;
            case PieceType::King:
                if (absoluteRowDelta <= 1 && absoluteColumnDelta <= 1 &&
                    (absoluteRowDelta != 0 || absoluteColumnDelta != 0)) {
                    return true;
                }
                break;
            case PieceType::None:
                break;
            }
        }
    }

    return false;
}

bool Rules::hasLegalMove(Color color) const {
    Rules position = *this;
    position.currentPlayer_ = color;

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const Position from{row, column};
            if (!position.board_[row][column].has_value() ||
                position.board_[row][column]->color != color) {
                continue;
            }

            if (!position.legalMoves(from).empty()) {
                return true;
            }
        }
    }

    return false;
}

void Rules::applyMoveUnchecked(const Move &move) {
    Piece movingPiece = *board_[move.from.row][move.from.column];
    board_[move.from.row][move.from.column].reset();

    const bool castling = movingPiece.type == PieceType::King &&
                          std::abs(move.to.column - move.from.column) == 2;
    const bool enPassant = movingPiece.type == PieceType::Pawn &&
                           move.from.column != move.to.column &&
                           !board_[move.to.row][move.to.column].has_value();

    if (enPassant) {
        board_[move.from.row][move.to.column].reset();
    }

    if (castling) {
        const int rookFromColumn = move.to.column > move.from.column ? 7 : 0;
        const int rookToColumn = move.to.column > move.from.column ? 5 : 3;
        Piece rook = *board_[move.from.row][rookFromColumn];
        board_[move.from.row][rookFromColumn].reset();
        rook.hasMoved = true;
        board_[move.from.row][rookToColumn] = rook;
    }

    movingPiece.hasMoved = true;
    if (movingPiece.type == PieceType::Pawn &&
        (move.to.row == 0 || move.to.row == 7)) {
        movingPiece.type = move.promotion == PieceType::None
                                ? PieceType::Queen
                                : move.promotion;
    }

    board_[move.to.row][move.to.column] = movingPiece;
}

bool Rules::loadFen(const QString &fen) {
    const QString trimmed = fen.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
    if (parts.size() != 6) {
        return false;
    }

    const QStringList ranks = parts[0].split(QChar('/'));
    if (ranks.size() != 8) {
        return false;
    }

    Board newBoard{};
    for (auto &row : newBoard) {
        row.fill(std::nullopt);
    }

    std::array<int, 2> pieceCounts{};
    std::array<int, 2> pawnCounts{};
    std::array<int, 2> kingCounts{};

    for (int row = 0; row < 8; ++row) {
        const QString &rankStr = ranks[row];
        int col = 0;
        for (const QChar &ch : rankStr) {
            if (ch >= QLatin1Char('1') && ch <= QLatin1Char('8')) {
                const int count = ch.digitValue();
                if (col + count > 8) {
                    return false;
                }
                col += count;
            } else {
                if (col >= 8) {
                    return false;
                }
                const char latin = ch.toLatin1();
                const bool isWhite = latin >= 'A' && latin <= 'Z';
                const bool isBlack = latin >= 'a' && latin <= 'z';
                if (!isWhite && !isBlack) {
                    return false;
                }
                const char lower = static_cast<char>(std::tolower(latin));
                PieceType type = PieceType::None;
                switch (lower) {
                case 'p': type = PieceType::Pawn; break;
                case 'n': type = PieceType::Knight; break;
                case 'b': type = PieceType::Bishop; break;
                case 'r': type = PieceType::Rook; break;
                case 'q': type = PieceType::Queen; break;
                case 'k': type = PieceType::King; break;
                default: return false;
                }
                newBoard[row][col] = Piece{type, isWhite ? Color::White : Color::Black, true};
                const int colorIndex = isWhite ? 0 : 1;
                ++pieceCounts[colorIndex];
                if (type == PieceType::Pawn) {
                    if (row == 0 || row == 7) {
                        return false;
                    }
                    ++pawnCounts[colorIndex];
                } else if (type == PieceType::King) {
                    ++kingCounts[colorIndex];
                }
                ++col;
            }
        }
        if (col != 8) {
            return false;
        }
    }

    // A legal chess position has exactly one king per side, no more than
    // sixteen pieces per side, and no more than eight pawns per side.
    if (kingCounts[0] != 1 || kingCounts[1] != 1 ||
        pieceCounts[0] > 16 || pieceCounts[1] > 16 ||
        pawnCounts[0] > 8 || pawnCounts[1] > 8) {
        return false;
    }

    // Active color
    Color newPlayer;
    if (parts[1] == QStringLiteral("b")) {
        newPlayer = Color::Black;
    } else if (parts[1] == QStringLiteral("w")) {
        newPlayer = Color::White;
    } else {
        return false;
    }

    // Castling rights
    const QString &castling = parts[2];
    if (castling.isEmpty()) {
        return false;
    }
    if (castling != QStringLiteral("-")) {
        for (const QChar right : castling) {
            if (!QStringLiteral("KQkq").contains(right) ||
                castling.count(right) != 1) {
                return false;
            }
        }

        const auto isPiece = [&](int row, int column, PieceType type, Color color) {
            const auto &piece = newBoard[row][column];
            return piece.has_value() && piece->type == type && piece->color == color;
        };

        if ((castling.contains(QChar('K')) &&
             (!isPiece(7, 4, PieceType::King, Color::White) ||
              !isPiece(7, 7, PieceType::Rook, Color::White))) ||
            (castling.contains(QChar('Q')) &&
             (!isPiece(7, 4, PieceType::King, Color::White) ||
              !isPiece(7, 0, PieceType::Rook, Color::White))) ||
            (castling.contains(QChar('k')) &&
             (!isPiece(0, 4, PieceType::King, Color::Black) ||
              !isPiece(0, 7, PieceType::Rook, Color::Black))) ||
            (castling.contains(QChar('q')) &&
             (!isPiece(0, 4, PieceType::King, Color::Black) ||
              !isPiece(0, 0, PieceType::Rook, Color::Black)))) {
            return false;
        }
    }
    const bool wK = castling.contains(QChar('K'));
    const bool wQ = castling.contains(QChar('Q'));
    const bool bK = castling.contains(QChar('k'));
    const bool bQ = castling.contains(QChar('q'));

    const auto kingPosition = [&](Color color) {
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                const auto &piece = newBoard[row][column];
                if (piece.has_value() && piece->type == PieceType::King &&
                    piece->color == color) {
                    return Position{row, column};
                }
            }
        }
        return Position{-1, -1};
    };

    const Position whiteKing = kingPosition(Color::White);
    const Position blackKing = kingPosition(Color::Black);
    if (std::abs(whiteKing.row - blackKing.row) <= 1 &&
        std::abs(whiteKing.column - blackKing.column) <= 1) {
        return false;
    }

    // Set hasMoved for Kings and Rooks based on castling rights
    if (newBoard[7][4].has_value() && newBoard[7][4]->type == PieceType::King && newBoard[7][4]->color == Color::White) {
        if (wK || wQ) {
            newBoard[7][4]->hasMoved = false;
        }
    }
    if (wK && newBoard[7][7].has_value() && newBoard[7][7]->type == PieceType::Rook && newBoard[7][7]->color == Color::White) {
        newBoard[7][7]->hasMoved = false;
    }
    if (wQ && newBoard[7][0].has_value() && newBoard[7][0]->type == PieceType::Rook && newBoard[7][0]->color == Color::White) {
        newBoard[7][0]->hasMoved = false;
    }

    if (newBoard[0][4].has_value() && newBoard[0][4]->type == PieceType::King && newBoard[0][4]->color == Color::Black) {
        if (bK || bQ) {
            newBoard[0][4]->hasMoved = false;
        }
    }
    if (bK && newBoard[0][7].has_value() && newBoard[0][7]->type == PieceType::Rook && newBoard[0][7]->color == Color::Black) {
        newBoard[0][7]->hasMoved = false;
    }
    if (bQ && newBoard[0][0].has_value() && newBoard[0][0]->type == PieceType::Rook && newBoard[0][0]->color == Color::Black) {
        newBoard[0][0]->hasMoved = false;
    }

    // En passant target square
    std::optional<Move> newLastMove;
    const QString &ep = parts[3];
    if (ep != QStringLiteral("-")) {
        if (ep.size() != 2 || ep[0] < QLatin1Char('a') ||
            ep[0] > QLatin1Char('h') ||
            (ep[1] != QLatin1Char('3') && ep[1] != QLatin1Char('6'))) {
            return false;
        }

        const int epCol = ep[0].toLatin1() - 'a';
        const int epRank = ep[1].toLatin1() - '0';
        const int epRow = 8 - epRank;
        const bool whitePawnJustMoved = epRank == 3;
        if ((newPlayer == Color::Black) != whitePawnJustMoved) {
            return false;
        }

        const int pawnRow = whitePawnJustMoved ? epRow - 1 : epRow + 1;
        const int originRow = whitePawnJustMoved ? epRow + 1 : epRow - 1;
        const Color pawnColor = whitePawnJustMoved ? Color::White : Color::Black;
        const auto &pawn = newBoard[pawnRow][epCol];
        if (newBoard[epRow][epCol].has_value() ||
            !pawn.has_value() || pawn->type != PieceType::Pawn ||
            pawn->color != pawnColor || newBoard[originRow][epCol].has_value()) {
            return false;
        }

        if (whitePawnJustMoved) {
            newLastMove = Move{{6, epCol}, {4, epCol}, PieceType::None};
        } else {
            newLastMove = Move{{1, epCol}, {3, epCol}, PieceType::None};
        }
    }

    // The side that just moved may not still be in check.  This catches FENs
    // that are structurally complete but cannot result from a legal move.
    Rules candidate;
    candidate.board_ = newBoard;
    candidate.currentPlayer_ = newPlayer;
    candidate.lastMove_ = newLastMove;
    if (candidate.isInCheck(opposite(newPlayer))) {
        return false;
    }

    const auto isUnsignedInteger = [](const QString &value, bool allowZero) {
        if (value.isEmpty()) {
            return false;
        }
        for (const QChar ch : value) {
            if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) {
                return false;
            }
        }

        bool ok = false;
        const qulonglong number = value.toULongLong(&ok);
        return ok && (allowZero || number > 0) &&
               number <= static_cast<qulonglong>(std::numeric_limits<int>::max());
    };
    if (!isUnsignedInteger(parts[4], true) ||
        !isUnsignedInteger(parts[5], false)) {
        return false;
    }

    bool halfmoveOk = false;
    bool fullmoveOk = false;
    const int halfmove = parts[4].toInt(&halfmoveOk);
    const int fullmove = parts[5].toInt(&fullmoveOk);
    if (!halfmoveOk || !fullmoveOk) {
        return false;
    }

    board_ = newBoard;
    currentPlayer_ = newPlayer;
    lastMove_ = newLastMove;
    halfmoveClock_ = halfmove;
    fullmoveNumber_ = fullmove;
    repetitionHistory_.clear();
    repetitionHistory_.push_back(positionKey());
    return true;
}

QString Rules::toFen() const {
    QString fen;
    // 1. Piece placement
    for (int row = 0; row < 8; ++row) {
        int emptyCount = 0;
        for (int col = 0; col < 8; ++col) {
            const auto &p = board_[row][col];
            if (!p.has_value()) {
                ++emptyCount;
            } else {
                if (emptyCount > 0) {
                    fen += QString::number(emptyCount);
                    emptyCount = 0;
                }
                char c = ' ';
                switch (p->type) {
                case PieceType::Pawn:   c = 'p'; break;
                case PieceType::Knight: c = 'n'; break;
                case PieceType::Bishop: c = 'b'; break;
                case PieceType::Rook:   c = 'r'; break;
                case PieceType::Queen:  c = 'q'; break;
                case PieceType::King:   c = 'k'; break;
                case PieceType::None:   break;
                }
                if (p->color == Color::White) {
                    c = static_cast<char>(std::toupper(c));
                }
                fen += QChar(c);
            }
        }
        if (emptyCount > 0) {
            fen += QString::number(emptyCount);
        }
        if (row < 7) {
            fen += QChar('/');
        }
    }

    // 2. Active player
    fen += (currentPlayer_ == Color::White) ? QStringLiteral(" w ") : QStringLiteral(" b ");

    // 3. Castling availability
    QString castling;
    const auto wKing = pieceAt({7, 4});
    if (wKing.has_value() && wKing->type == PieceType::King && wKing->color == Color::White && !wKing->hasMoved) {
        const auto wRookK = pieceAt({7, 7});
        if (wRookK.has_value() && wRookK->type == PieceType::Rook && wRookK->color == Color::White && !wRookK->hasMoved) {
            castling += QChar('K');
        }
        const auto wRookQ = pieceAt({7, 0});
        if (wRookQ.has_value() && wRookQ->type == PieceType::Rook && wRookQ->color == Color::White && !wRookQ->hasMoved) {
            castling += QChar('Q');
        }
    }

    const auto bKing = pieceAt({0, 4});
    if (bKing.has_value() && bKing->type == PieceType::King && bKing->color == Color::Black && !bKing->hasMoved) {
        const auto bRookK = pieceAt({0, 7});
        if (bRookK.has_value() && bRookK->type == PieceType::Rook && bRookK->color == Color::Black && !bRookK->hasMoved) {
            castling += QChar('k');
        }
        const auto bRookQ = pieceAt({0, 0});
        if (bRookQ.has_value() && bRookQ->type == PieceType::Rook && bRookQ->color == Color::Black && !bRookQ->hasMoved) {
            castling += QChar('q');
        }
    }

    if (castling.isEmpty()) {
        castling = QStringLiteral("-");
    }
    fen += castling + QStringLiteral(" ");

    // 4. En passant target
    QString ep = QStringLiteral("-");
    if (lastMove_.has_value()) {
        const auto &lm = *lastMove_;
        const auto p = pieceAt(lm.to);
        if (p.has_value() && p->type == PieceType::Pawn && std::abs(lm.to.row - lm.from.row) == 2) {
            const int epRow = (lm.from.row + lm.to.row) / 2;
            ep = QString(QChar('a' + lm.to.column)) + QString::number(8 - epRow);
        }
    }
    fen += ep + QStringLiteral(" ") + QString::number(halfmoveClock_) +
           QStringLiteral(" ") + QString::number(fullmoveNumber_);

    return fen;
}

std::optional<Rules::Move> Rules::parseSan(const QString &san) const {
    QString s = san.trimmed();
    if (s.isEmpty()) {
        return std::nullopt;
    }

    // Strip trailing annotations and check/mate markers
    while (!s.isEmpty() && (s.endsWith(QChar('+')) || s.endsWith(QChar('#')) ||
                            s.endsWith(QChar('!')) || s.endsWith(QChar('?')))) {
        s.chop(1);
    }

    const int homeRow = (currentPlayer_ == Color::White) ? 7 : 0;

    // Castling
    if (s.compare(QStringLiteral("O-O"), Qt::CaseInsensitive) == 0 ||
        s == QStringLiteral("0-0")) {
        const Move move{{homeRow, 4}, {homeRow, 6}, PieceType::None};
        if (isValidMove(move)) {
            return move;
        }
        return std::nullopt;
    }

    if (s.compare(QStringLiteral("O-O-O"), Qt::CaseInsensitive) == 0 ||
        s == QStringLiteral("0-0-0")) {
        const Move move{{homeRow, 4}, {homeRow, 2}, PieceType::None};
        if (isValidMove(move)) {
            return move;
        }
        return std::nullopt;
    }

    // Promotion piece extraction
    PieceType promo = PieceType::None;
    if (s.contains(QChar('='))) {
        const int eqIdx = s.indexOf(QChar('='));
        if (eqIdx + 1 < s.length()) {
            const char pChar = static_cast<char>(std::toupper(s[eqIdx + 1].toLatin1()));
            switch (pChar) {
            case 'Q': promo = PieceType::Queen; break;
            case 'R': promo = PieceType::Rook; break;
            case 'B': promo = PieceType::Bishop; break;
            case 'N': promo = PieceType::Knight; break;
            default: break;
            }
        }
        s = s.left(eqIdx);
    } else if (s.length() >= 3 && s[s.length() - 2].isDigit()) {
        const char pChar = static_cast<char>(std::toupper(s.back().toLatin1()));
        if (pChar == 'Q' || pChar == 'R' || pChar == 'B' || pChar == 'N') {
            switch (pChar) {
            case 'Q': promo = PieceType::Queen; break;
            case 'R': promo = PieceType::Rook; break;
            case 'B': promo = PieceType::Bishop; break;
            case 'N': promo = PieceType::Knight; break;
            default: break;
            }
            s.chop(1);
        }
    }

    if (s.length() < 2) {
        return std::nullopt;
    }

    // Last 2 characters must be target square: column ('a'-'h') and rank ('1'-'8')
    const QChar colChar = s[s.length() - 2];
    const QChar rankChar = s[s.length() - 1];

    if (colChar < QChar('a') || colChar > QChar('h') ||
        rankChar < QChar('1') || rankChar > QChar('8')) {
        return std::nullopt;
    }

    const int toCol = colChar.toLatin1() - 'a';
    const int toRow = 8 - (rankChar.toLatin1() - '0');
    const Position to{toRow, toCol};

    QString prefix = s.left(s.length() - 2);

    PieceType pieceType = PieceType::Pawn;
    if (!prefix.isEmpty()) {
        const char first = prefix[0].toLatin1();
        switch (first) {
        case 'N': pieceType = PieceType::Knight; prefix.remove(0, 1); break;
        case 'B': pieceType = PieceType::Bishop; prefix.remove(0, 1); break;
        case 'R': pieceType = PieceType::Rook;   prefix.remove(0, 1); break;
        case 'Q': pieceType = PieceType::Queen;  prefix.remove(0, 1); break;
        case 'K': pieceType = PieceType::King;   prefix.remove(0, 1); break;
        default: break;
        }
    }

    // Remove capture characters 'x' or ':'
    prefix.remove(QChar('x'));
    prefix.remove(QChar(':'));

    std::optional<int> disCol;
    std::optional<int> disRow;
    for (const QChar &ch : prefix) {
        if (ch >= QChar('a') && ch <= QChar('h')) {
            disCol = ch.toLatin1() - 'a';
        } else if (ch >= QChar('1') && ch <= QChar('8')) {
            disRow = 8 - (ch.toLatin1() - '0');
        }
    }

    std::vector<Move> candidates;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            const Position from{r, c};
            const auto piece = pieceAt(from);
            if (!piece.has_value() || piece->color != currentPlayer_ || piece->type != pieceType) {
                continue;
            }
            if (disCol.has_value() && from.column != *disCol) {
                continue;
            }
            if (disRow.has_value() && from.row != *disRow) {
                continue;
            }

            Move moveCandidate{from, to, promo};
            if (pieceType == PieceType::Pawn && (to.row == 0 || to.row == 7) && promo == PieceType::None) {
                moveCandidate.promotion = PieceType::Queen;
            }

            if (isValidMove(moveCandidate)) {
                candidates.push_back(moveCandidate);
            }
        }
    }

    if (candidates.size() == 1) {
        return candidates.front();
    }

    return std::nullopt;
}

bool Rules::tryMoveSan(const QString &san) {
    const auto move = parseSan(san);
    if (!move.has_value()) {
        return false;
    }
    return tryMove(*move);
}

QString Rules::toSan(const Move &move) const {
    if (!isValidMove(move)) {
        return QString();
    }

    const auto sourcePiece = pieceAt(move.from);
    if (!sourcePiece.has_value()) {
        return QString();
    }

    const bool isCastling = sourcePiece->type == PieceType::King &&
                            std::abs(move.to.column - move.from.column) == 2;
    if (isCastling) {
        QString san = (move.to.column > move.from.column) ? QStringLiteral("O-O") : QStringLiteral("O-O-O");
        Rules nextState = *this;
        nextState.applyMoveUnchecked(move);
        nextState.currentPlayer_ = opposite(nextState.currentPlayer_);
        if (nextState.isCheckmate(nextState.currentPlayer())) {
            san += QChar('#');
        } else if (nextState.isInCheck(nextState.currentPlayer())) {
            san += QChar('+');
        }
        return san;
    }

    const bool isPawn = sourcePiece->type == PieceType::Pawn;
    const auto targetPiece = pieceAt(move.to);
    const bool isEnPassant = isPawn && move.from.column != move.to.column && !targetPiece.has_value();
    const bool isCapture = targetPiece.has_value() || isEnPassant;

    QString san;
    if (!isPawn) {
        char pieceChar = ' ';
        switch (sourcePiece->type) {
        case PieceType::Knight: pieceChar = 'N'; break;
        case PieceType::Bishop: pieceChar = 'B'; break;
        case PieceType::Rook:   pieceChar = 'R'; break;
        case PieceType::Queen:  pieceChar = 'Q'; break;
        case PieceType::King:   pieceChar = 'K'; break;
        default: break;
        }
        san += QChar(pieceChar);

        // Disambiguation
        bool needFile = false;
        bool needRank = false;
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; ++c) {
                const Position otherFrom{r, c};
                if (otherFrom == move.from) {
                    continue;
                }
                const auto otherPiece = pieceAt(otherFrom);
                if (otherPiece.has_value() && otherPiece->color == sourcePiece->color &&
                    otherPiece->type == sourcePiece->type && isValidMove(Move{otherFrom, move.to, move.promotion})) {
                    if (otherFrom.column != move.from.column) {
                        needFile = true;
                    } else if (otherFrom.row != move.from.row) {
                        needRank = true;
                    }
                }
            }
        }

        if (needFile) {
            san += QChar('a' + move.from.column);
        }
        if (needRank) {
            san += QString::number(8 - move.from.row);
        }
    } else {
        if (isCapture) {
            san += QChar('a' + move.from.column);
        }
    }

    if (isCapture) {
        san += QChar('x');
    }

    san += QString(QChar('a' + move.to.column)) + QString::number(8 - move.to.row);

    if (isPawn && (move.to.row == 0 || move.to.row == 7)) {
        const PieceType promo = (move.promotion == PieceType::None) ? PieceType::Queen : move.promotion;
        char promoChar = 'Q';
        switch (promo) {
        case PieceType::Rook:   promoChar = 'R'; break;
        case PieceType::Bishop: promoChar = 'B'; break;
        case PieceType::Knight: promoChar = 'N'; break;
        case PieceType::Queen:  promoChar = 'Q'; break;
        default: break;
        }
        san += QStringLiteral("=") + QChar(promoChar);
    }

    Rules nextState = *this;
    nextState.applyMoveUnchecked(move);
    nextState.currentPlayer_ = opposite(nextState.currentPlayer_);
    if (nextState.isCheckmate(nextState.currentPlayer())) {
        san += QChar('#');
    } else if (nextState.isInCheck(nextState.currentPlayer())) {
        san += QChar('+');
    }

    return san;
}

bool Rules::loadPgn(const QString &pgnContent,
                    QStringList *outPgnMoves,
                    QStringList *outUciMoves,
                    QStringList *outComments) {
    if (pgnContent.trimmed().isEmpty()) {
        return false;
    }

    // Check for FEN tag
    static const QRegularExpression fenRegex(QStringLiteral("\\[FEN\\s+\"([^\"]+)\"\\]"), QRegularExpression::CaseInsensitiveOption);
    const auto fenMatch = fenRegex.match(pgnContent);
    if (fenMatch.hasMatch()) {
        if (!loadFen(fenMatch.captured(1))) {
            return false;
        }
    } else {
        reset();
    }

    int moveNum = 1;
    bool isWhite = (currentPlayer() == Color::White);
    QStringList pgnFormatted;
    QStringList uciList;
    QStringList comments;
    comments.append(QString()); // Slot for ply 0 (initial position)
    int currentPly = 0;

    const int n = pgnContent.size();
    int i = 0;

    while (i < n) {
        // Skip whitespace
        while (i < n && pgnContent[i].isSpace()) {
            ++i;
        }
        if (i >= n) {
            break;
        }

        const QChar ch = pgnContent[i];

        // Header tag outside comments: [Tag "Value"]
        if (ch == QLatin1Char('[')) {
            ++i;
            while (i < n && pgnContent[i] != QLatin1Char(']')) {
                ++i;
            }
            if (i < n && pgnContent[i] == QLatin1Char(']')) {
                ++i;
            }
            continue;
        }

        // Brace comment: { ... }
        if (ch == QLatin1Char('{')) {
            ++i;
            QString commentText;
            int depth = 1;
            while (i < n) {
                if (pgnContent[i] == QLatin1Char('{')) {
                    ++depth;
                    commentText += pgnContent[i];
                    ++i;
                } else if (pgnContent[i] == QLatin1Char('}')) {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                    commentText += pgnContent[i];
                    ++i;
                } else {
                    commentText += pgnContent[i];
                    ++i;
                }
            }
            commentText = commentText.trimmed();
            if (!commentText.isEmpty() && currentPly < comments.size()) {
                if (!comments[currentPly].isEmpty()) {
                    comments[currentPly] += QLatin1Char(' ');
                }
                comments[currentPly] += commentText;
            }
            continue;
        }

        // Line comment: ; ... \n
        if (ch == QLatin1Char(';')) {
            ++i;
            QString commentText;
            while (i < n && pgnContent[i] != QLatin1Char('\n')) {
                commentText += pgnContent[i];
                ++i;
            }
            commentText = commentText.trimmed();
            if (!commentText.isEmpty() && currentPly < comments.size()) {
                if (!comments[currentPly].isEmpty()) {
                    comments[currentPly] += QLatin1Char(' ');
                }
                comments[currentPly] += commentText;
            }
            continue;
        }

        // Variation: ( ... )
        if (ch == QLatin1Char('(')) {
            ++i;
            int depth = 1;
            while (i < n && depth > 0) {
                if (pgnContent[i] == QLatin1Char('(')) {
                    ++depth;
                } else if (pgnContent[i] == QLatin1Char(')')) {
                    --depth;
                }
                ++i;
            }
            continue;
        }

        // NAG: $1, $2, etc.
        if (ch == QLatin1Char('$')) {
            ++i;
            while (i < n && pgnContent[i].isDigit()) {
                ++i;
            }
            continue;
        }

        // Read token up to whitespace or delimiter
        const int tokenStart = i;
        while (i < n && !pgnContent[i].isSpace() &&
               pgnContent[i] != QLatin1Char('{') &&
               pgnContent[i] != QLatin1Char(';') &&
               pgnContent[i] != QLatin1Char('(') &&
               pgnContent[i] != QLatin1Char(')') &&
               pgnContent[i] != QLatin1Char('[')) {
            ++i;
        }
        const QString token = pgnContent.mid(tokenStart, i - tokenStart);

        if (token.isEmpty()) {
            continue;
        }

        // Check for game termination
        if (token == QLatin1String("1-0") || token == QLatin1String("0-1") ||
            token == QLatin1String("1/2-1/2") || token == QLatin1String("*")) {
            break;
        }

        // Check for move number (e.g. "1." or "1..." or "12.")
        if (token.contains(QLatin1Char('.'))) {
            bool isMoveNum = true;
            for (const QChar tc : token) {
                if (!tc.isDigit() && tc != QLatin1Char('.')) {
                    isMoveNum = false;
                    break;
                }
            }
            if (isMoveNum) {
                continue;
            }
        }

        // Attempt to parse SAN
        const auto moveOpt = parseSan(token);
        if (!moveOpt.has_value()) {
            continue;
        }

        const QString san = toSan(*moveOpt);
        const QString uci = toUci(*moveOpt);

        if (!tryMove(*moveOpt)) {
            break;
        }

        uciList.append(uci);
        ++currentPly;
        comments.append(QString()); // Slot for this new ply

        if (isWhite) {
            pgnFormatted.append(QStringLiteral("%1. %2").arg(moveNum).arg(san.isEmpty() ? token : san));
            isWhite = false;
        } else {
            if (!pgnFormatted.isEmpty()) {
                pgnFormatted.last() += QStringLiteral(" ") + (san.isEmpty() ? token : san);
            } else {
                pgnFormatted.append(QStringLiteral("%1... %2").arg(moveNum).arg(san.isEmpty() ? token : san));
            }
            ++moveNum;
            isWhite = true;
        }
    }

    if (outPgnMoves) {
        *outPgnMoves = pgnFormatted;
    }
    if (outUciMoves) {
        *outUciMoves = uciList;
    }
    if (outComments) {
        *outComments = comments;
    }

    return true;
}
