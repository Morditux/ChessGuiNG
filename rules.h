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

    // What a null move has to put back, which is only the bookkeeping the turn
    // flip touches: no piece moves, so there is nothing else to restore.
    struct NullUndo {
        bool hadLastMove = false;
        Move lastMove;
        int halfmoveClock = 0;
        int fullmoveNumber = 1;
        Color previousPlayer = Color::White;
        bool pushedRepetition = false;
        quint64 previousHash = 0;
    };

    // Passes the turn without moving a piece, which is what null-move pruning
    // needs. The previous move is forgotten, so no en passant target survives,
    // and the fifty-move clock is left alone: a null move is not a move for the
    // rule and must not turn a pruning test into a draw. It is refused while the
    // side to move is in check, where passing describes no legal position.
    bool makeNullMove(NullUndo &undo);
    void unmakeNullMove(const NullUndo &undo);

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

    // Material-only description of a position, shared by the draw tests. The
    // arrays are indexed by colour index (White = 0, Black = 1).
    struct MaterialCounts {
        int knights[2] = {0, 0};
        int evenSquaredBishops[2] = {0, 0};
        int oddSquaredBishops[2] = {0, 0};
        bool hasPawnOrMajor = false;
    };

    // What the material on the board can still achieve. Both flags stay false
    // as soon as a pawn, a rook or a queen is present.
    struct MaterialDraw {
        // FIDE dead position: no series of legal moves, however cooperative
        // the defence, can deliver mate.
        bool dead = false;
        // Mate is possible only with the defender's help and cannot be forced
        // (king and two knights against a king, a lone minor on each side).
        // `dead` is a subset of it. The heuristic evaluator scores these as
        // drawn; the game itself is still played out.
        bool unforceable = false;
    };

    // Single source of truth for the material-draw rules: it is used by
    // isInsufficientMaterial() and by the heuristic evaluator, which counts the
    // material during its own board pass and must not grow a second, drifting
    // copy of the rule.
    [[nodiscard]] static MaterialDraw classifyMaterialDraw(const MaterialCounts &counts);

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
    // Zobrist keys of the repetition-relevant positions, newest last.
    std::vector<quint64> repetitionHistory_;
    // Cached king squares indexed by Color (White = 0, Black = 1), kept in
    // sync by applyMoveUnchecked/unmakeMove so isInCheck never scans the board.
    std::array<Position, 2> kingPosition_{{{-1, -1}, {-1, -1}}};

    // Tag used by detachedCopy() to build a position without running the
    // expensive reset() (board setup, hashing and repetition bookkeeping).
    struct NoInitTag {};
    explicit Rules(NoInitTag);

    [[nodiscard]] static Color opposite(Color color);
    [[nodiscard]] static int colorIndex(Color color);
    void recomputeKingPositions();
    // Zobrist key of the position as the repetition rule sees it: the en
    // passant target only counts when the capture is actually available.
    [[nodiscard]] quint64 repetitionKey() const;
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
