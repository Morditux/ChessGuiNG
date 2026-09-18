//
// Chess rules engine.
//

#ifndef CHESSGUI_RULES_H
#define CHESSGUI_RULES_H

#include <QMetaType>
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

        friend bool operator==(const Move &, const Move &) = default;
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

    // Search support. `generatePseudoLegalMoves` writes directly the moves of
    // the side to move (without scanning all 64 targets per piece) into the
    // caller's buffer and returns their count. `makeMove` expects one of those
    // moves and reports whether it is legal, i.e. the moving side is not left
    // in check; a rejected move leaves the position unchanged. The make/unmake
    // pair mutates and restores the position in place, so the search never
    // copies the whole game state.
    struct Undo {
        Piece movedPiece;
        bool hasCapture = false;
        Piece capturedPiece;
        Position capturedSquare{-1, -1};
        bool castled = false;
        Piece rookPiece;
        Position rookFrom{-1, -1};
        Position rookTo{-1, -1};
        int halfmoveClock = 0;
        int fullmoveNumber = 1;
        Color previousPlayer = Color::White;
        bool hadLastMove = false;
        Move lastMove;
        bool pushedRepetition = false;
        quint64 previousHash = 0;
    };

    [[nodiscard]] int generatePseudoLegalMoves(Move *moves, int capacity) const;
    bool makeMove(const Move &move, Undo &undo);
    void unmakeMove(const Move &move, const Undo &undo);

    // Zobrist key of the current position, including the side to move, the
    // castling rights and the en passant file.
    [[nodiscard]] quint64 zobristKey() const;

    // When disabled, `tryMove`/`makeMove` no longer record positions for
    // repetition detection. The search turns it off because it never consults
    // the repetition history and the bookkeeping is expensive.
    void setTrackRepetition(bool enabled);
    [[nodiscard]] bool trackRepetition() const;

    // Copy of the position with an empty repetition history, ready for a
    // search that manages its own state and never consults it.
    [[nodiscard]] Rules detachedCopy() const;

    [[nodiscard]] bool isInCheck(Color color) const;
    [[nodiscard]] bool isCheckmate(Color color) const;
    [[nodiscard]] bool isStalemate(Color color) const;
    [[nodiscard]] bool isGameOver() const;
    [[nodiscard]] bool isInsufficientMaterial() const;
    [[nodiscard]] bool isThreefoldRepetition() const;
    [[nodiscard]] bool isFiftyMoveRule() const;
    [[nodiscard]] bool isDraw() const;

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
    int halfmoveClock_ = 0;
    int fullmoveNumber_ = 1;
    bool trackRepetition_ = true;
    quint64 hash_ = 0;
    std::vector<QString> repetitionHistory_;

    [[nodiscard]] static Color opposite(Color color);
    [[nodiscard]] QString positionKey() const;
    // Zobrist contributions recomputed from the board; `hash_` is updated
    // incrementally on every move.
    void recomputeHash();
    [[nodiscard]] quint64 castlingHash() const;
    [[nodiscard]] quint64 enPassantHash() const;
    [[nodiscard]] bool enPassantCaptureAvailable() const;
    [[nodiscard]] static bool isPromotionPiece(PieceType type);

    [[nodiscard]] bool isPseudoLegalMove(const Move &move) const;
    [[nodiscard]] bool isPathClear(Position from, Position to) const;
    [[nodiscard]] bool isSquareAttacked(Position position, Color byColor) const;
    [[nodiscard]] bool hasLegalMove(Color color) const;

    void applyMoveUnchecked(const Move &move);
};

Q_DECLARE_METATYPE(Rules::Color)

#endif // CHESSGUI_RULES_H
