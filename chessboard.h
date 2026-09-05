//
// Created by mordicus on 23/08/2026.
//

#ifndef CHESSGUI_CHESSBOARD_H
#define CHESSGUI_CHESSBOARD_H

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QSvgRenderer>
#include <QWidget>

#include "rules.h"

#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE

namespace Ui {
    class ChessBoard;
}

QT_END_NAMESPACE

class QEvent;
class QMouseEvent;
class QPainter;
class QColor;

class ChessBoard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool legalMoveHighlightingEnabled READ legalMoveHighlightingEnabled
               WRITE setLegalMoveHighlightingEnabled)
    Q_PROPERTY(bool lastMoveHighlightingEnabled READ lastMoveHighlightingEnabled
               WRITE setLastMoveHighlightingEnabled)
    Q_PROPERTY(bool boardFlipped READ boardFlipped WRITE setBoardFlipped
               NOTIFY boardFlippedChanged)

public:
    explicit ChessBoard(QWidget *parent = nullptr);

    ~ChessBoard() override;

    void setLegalMoveHighlightingEnabled(bool enabled);
    [[nodiscard]] bool legalMoveHighlightingEnabled() const;
    void setLastMoveHighlightingEnabled(bool enabled);
    [[nodiscard]] bool lastMoveHighlightingEnabled() const;
    void setBoardFlipped(bool flipped);
    [[nodiscard]] bool boardFlipped() const;
    [[nodiscard]] const Rules &rules() const;
    void setRules(const Rules &rules);
    void reset();
    void setComputerMovePreview(const std::optional<Rules::Move> &move);
    [[nodiscard]] std::optional<Rules::Move> computerMovePreview() const;
    void setRecommendedMovePreview(const std::optional<Rules::Move> &move);
    [[nodiscard]] std::optional<Rules::Move> recommendedMovePreview() const;
    void clearMovePreviews();

signals:
    void pieceMoved(QChar piece, Rules::Position oldPosition,
                    Rules::Position newPosition);
    void boardFlippedChanged(bool flipped);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

    QSize sizeHint() const override;

private:
    struct BoardGeometry {
        int boardX = 0;
        int boardY = 0;
        int squareSize = 0;
        int boardSide = 0;
    };

    Ui::ChessBoard *ui;
    QHash<QString, QSvgRenderer *> pieceRenderers;
    Rules rules_;
    std::optional<Rules::Position> selectedPosition_;
    std::vector<Rules::Position> legalMoves_;
    std::optional<Rules::Position> pressedPosition_;
    std::optional<Rules::Position> hoveredPosition_;
    QPoint pressPoint_;
    QPoint dragOffset_;
    QPoint dragCursorPosition_;
    bool selectionWasActiveAtPress_ = false;
    bool selectionCreatedOnPress_ = false;
    bool dragCandidate_ = false;
    bool dragging_ = false;
    bool legalMoveHighlightingEnabled_ = true;
    bool lastMoveHighlightingEnabled_ = true;
    bool boardFlipped_ = false;
    std::optional<Rules::Move> computerMovePreview_;
    std::optional<Rules::Move> recommendedMovePreview_;

    [[nodiscard]] BoardGeometry boardGeometry() const;
    [[nodiscard]] Rules::Position displayedPosition(Rules::Position position) const;
    [[nodiscard]] Rules::Position modelPosition(Rules::Position position) const;
    [[nodiscard]] std::optional<Rules::Position> positionAt(const QPoint &point) const;
    [[nodiscard]] QRect squareRect(Rules::Position position) const;
    [[nodiscard]] bool isLegalDestination(Rules::Position position) const;
    [[nodiscard]] bool isCurrentPlayerPiece(Rules::Position position) const;

    void selectPosition(Rules::Position position);
    void clearSelection();
    void handleClick(Rules::Position position);
    [[nodiscard]] bool tryMoveTo(Rules::Position position);
    void updateHoveredPosition(const QPoint &point);
    void drawMoveArrow(QPainter &painter, const Rules::Move &move,
                       const QColor &color, Qt::PenStyle style) const;
};


#endif //CHESSGUI_CHESSBOARD_H
