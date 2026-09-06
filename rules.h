//
// Chess rules engine.
//

#ifndef CHESSGUI_RULES_H
#define CHESSGUI_RULES_H

#include <QString>
#include <QStringList>
#include <array>
#include <optional>
#include <vector>

class Rules {
public:
    enum class Color {
        White,
        Black
    };

    enum class PieceType {
        None,
        Pawn,
        Knight,
        Bishop,
        Rook,
        Queen,
        King
    };

    // row 0 is rank 8 and column 0 is file a.
    struct Position {
        int row = -1;
        int column = -1;

        friend bool operator==(const Position &, const Position &) = default;
    };

    struct Piece {
        PieceType type = PieceType::None;
        Color color = Color::White;
        bool hasMoved = false;
    };

    struct Move {
        Position from;
        Position to;
        // None means queen promotion when a pawn reaches the last rank.
        PieceType promotion = PieceType::None;
    };

    Rules();

    void reset();

    [[nodiscard]] std::optional<Piece> pieceAt(Position position) const;
    [[nodiscard]] Color currentPlayer() const;

    [[nodiscard]] bool isValidMove(Position from, Position to) const;
    [[nodiscard]] bool isValidMove(const Move &move) const;

    bool tryMove(Position from, Position to,
                 PieceType promotion = PieceType::None);
    bool tryMove(const Move &move);

    [[nodiscard]] std::vector<Position> legalMoves(Position from) const;

    [[nodiscard]] bool isInCheck(Color color) const;
    [[nodiscard]] bool isCheckmate(Color color) const;
    [[nodiscard]] bool isStalemate(Color color) const;
    [[nodiscard]] bool isGameOver() const;

    [[nodiscard]] static bool isInside(Position position);
    [[nodiscard]] static QString toUci(Position from, Position to, PieceType promotion = PieceType::None);
    [[nodiscard]] static QString toUci(const Move &move);

    bool loadFen(const QString &fen);
    [[nodiscard]] QString toFen() const;

    [[nodiscard]] std::optional<Move> lastMove() const;

    [[nodiscard]] std::optional<Move> parseSan(const QString &san) const;
    [[nodiscard]] QString toSan(const Move &move) const;
    bool tryMoveSan(const QString &san);

    bool loadPgn(const QString &pgnContent,
                 QStringList *outPgnMoves = nullptr,
                 QStringList *outUciMoves = nullptr,
                 QStringList *outComments = nullptr);

private:
    using Board = std::array<std::array<std::optional<Piece>, 8>, 8>;

    Board board_{};
    Color currentPlayer_ = Color::White;
    std::optional<Move> lastMove_;

    [[nodiscard]] static Color opposite(Color color);
    [[nodiscard]] static bool isPromotionPiece(PieceType type);

    [[nodiscard]] bool isPseudoLegalMove(const Move &move) const;
    [[nodiscard]] bool isPathClear(Position from, Position to) const;
    [[nodiscard]] bool isSquareAttacked(Position position, Color byColor) const;
    [[nodiscard]] bool hasLegalMove(Color color) const;

    void applyMoveUnchecked(const Move &move);
};

#endif // CHESSGUI_RULES_H
