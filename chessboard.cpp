//
// Created by mordicus on 23/08/2026.
//

// The header below is generated from chessboard.ui by Qt's uic tool.

#include "chessboard.h"
#include "ui_chessboard.h"

#include <QApplication>
#include <QAction>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QLineF>
#include <QPolygonF>
#include <QStringList>
#include <QMenuBar>
#include <QMenu>
#include <algorithm>

namespace {

QChar pieceSymbol(const Rules::Piece &piece) {
    switch (piece.type) {
    case Rules::PieceType::Pawn:
        return QChar('P');
    case Rules::PieceType::Knight:
        return QChar('N');
    case Rules::PieceType::Bishop:
        return QChar('B');
    case Rules::PieceType::Rook:
        return QChar('R');
    case Rules::PieceType::Queen:
        return QChar('Q');
    case Rules::PieceType::King:
        return QChar('K');
    case Rules::PieceType::None:
        return {};
    }

    return {};
}

QString pieceName(const Rules::Piece &piece) {
    const QString color = piece.color == Rules::Color::White
                              ? QStringLiteral("w")
                              : QStringLiteral("b");
    return color + pieceSymbol(piece);
}

}

ChessBoard::ChessBoard(QWidget *parent) : QWidget(parent), ui(new Ui::ChessBoard) {
    ui->setupUi(this);
    setMinimumSize(240, 240);
    setMouseTracking(true);

    const QStringList pieceNames = {
        QStringLiteral("wK"), QStringLiteral("wQ"), QStringLiteral("wR"),
        QStringLiteral("wB"), QStringLiteral("wN"), QStringLiteral("wP"),
        QStringLiteral("bK"), QStringLiteral("bQ"), QStringLiteral("bR"),
        QStringLiteral("bB"), QStringLiteral("bN"), QStringLiteral("bP")
    };
    for (const QString &pieceNameValue : pieceNames) {
        auto *renderer = new QSvgRenderer(
            QStringLiteral(":/pieces/%1.svg").arg(pieceNameValue), this);
        if (renderer->isValid()) {
            pieceRenderers.insert(pieceNameValue, renderer);
        }
    }

}

ChessBoard::~ChessBoard() {
    delete ui;
}

void ChessBoard::setLegalMoveHighlightingEnabled(bool enabled) {
    if (legalMoveHighlightingEnabled_ == enabled) {
        return;
    }

    legalMoveHighlightingEnabled_ = enabled;
    update();
}

bool ChessBoard::legalMoveHighlightingEnabled() const {
    return legalMoveHighlightingEnabled_;
}

void ChessBoard::setLastMoveHighlightingEnabled(bool enabled) {
    if (lastMoveHighlightingEnabled_ == enabled) {
        return;
    }

    lastMoveHighlightingEnabled_ = enabled;
    update();
}

bool ChessBoard::lastMoveHighlightingEnabled() const {
    return lastMoveHighlightingEnabled_;
}

void ChessBoard::setBoardFlipped(bool flipped) {
    if (boardFlipped_ == flipped) {
        return;
    }

    boardFlipped_ = flipped;
    selectedPosition_.reset();
    legalMoves_.clear();
    pressedPosition_.reset();
    hoveredPosition_.reset();
    selectionWasActiveAtPress_ = false;
    selectionCreatedOnPress_ = false;
    dragCandidate_ = false;
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
    emit boardFlippedChanged(boardFlipped_);
    update();
}

bool ChessBoard::boardFlipped() const {
    return boardFlipped_;
}

const Rules &ChessBoard::rules() const {
    return rules_;
}

void ChessBoard::setRules(const Rules &rules) {
    rules_ = rules;
    computerMovePreview_.reset();
    recommendedMovePreview_.reset();
    clearSelection();
    clearUserAnnotations();
    update();
}

void ChessBoard::reset() {
    rules_.reset();
    computerMovePreview_.reset();
    recommendedMovePreview_.reset();
    clearSelection();
    clearUserAnnotations();
    update();
}

void ChessBoard::setComputerMovePreview(const std::optional<Rules::Move> &move) {
    computerMovePreview_ = move;
    update();
}

std::optional<Rules::Move> ChessBoard::computerMovePreview() const {
    return computerMovePreview_;
}

void ChessBoard::setRecommendedMovePreview(const std::optional<Rules::Move> &move) {
    recommendedMovePreview_ = move;
    update();
}

std::optional<Rules::Move> ChessBoard::recommendedMovePreview() const {
    return recommendedMovePreview_;
}

void ChessBoard::clearMovePreviews() {
    if (!computerMovePreview_.has_value() && !recommendedMovePreview_.has_value()) {
        return;
    }

    computerMovePreview_.reset();
    recommendedMovePreview_.reset();
    update();
}

void ChessBoard::setAuditAnnotation(const AuditAnnotation &annotation) {
    if (auditAnnotation_ == annotation) return;
    auditAnnotation_ = annotation;
    update();
}

AuditAnnotation ChessBoard::auditAnnotation() const {
    return auditAnnotation_;
}

const std::vector<UserArrow> &ChessBoard::userArrows() const {
    return userArrows_;
}

const std::vector<SquareAnnotation> &ChessBoard::squareAnnotations() const {
    return squareAnnotations_;
}

void ChessBoard::setUserArrows(const std::vector<UserArrow> &arrows) {
    if (userArrows_ == arrows) {
        return;
    }
    userArrows_ = arrows;
    emit userAnnotationsChanged();
    update();
}

void ChessBoard::setSquareAnnotations(const std::vector<SquareAnnotation> &annotations) {
    if (squareAnnotations_ == annotations) {
        return;
    }
    squareAnnotations_ = annotations;
    emit userAnnotationsChanged();
    update();
}

void ChessBoard::toggleUserArrow(const Rules::Position &from, const Rules::Position &to, const QColor &color) {
    auto it = std::find_if(userArrows_.begin(), userArrows_.end(), [&](const UserArrow &a) {
        return a.from == from && a.to == to;
    });
    if (it != userArrows_.end()) {
        if (it->color == color) {
            userArrows_.erase(it);
        } else {
            it->color = color;
        }
    } else {
        userArrows_.push_back({from, to, color});
    }
    emit userAnnotationsChanged();
    update();
}

void ChessBoard::toggleSquareAnnotation(const Rules::Position &pos, const QColor &color) {
    auto it = std::find_if(squareAnnotations_.begin(), squareAnnotations_.end(), [&](const SquareAnnotation &s) {
        return s.position == pos;
    });
    if (it != squareAnnotations_.end()) {
        if (it->color == color) {
            squareAnnotations_.erase(it);
        } else {
            it->color = color;
        }
    } else {
        squareAnnotations_.push_back({pos, color});
    }
    emit userAnnotationsChanged();
    update();
}

void ChessBoard::clearUserAnnotations() {
    if (userArrows_.empty() && squareAnnotations_.empty()) {
        return;
    }
    userArrows_.clear();
    squareAnnotations_.clear();
    emit userAnnotationsChanged();
    update();
}

bool ChessBoard::hasUserAnnotations() const {
    return !userArrows_.empty() || !squareAnnotations_.empty();
}

QColor ChessBoard::annotationColorForModifiers(Qt::KeyboardModifiers modifiers) {
    if (modifiers.testFlag(Qt::ShiftModifier) &&
        (modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier))) {
        return PgnAnnotations::orangeColor();
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        return PgnAnnotations::redColor();
    }
    if (modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier)) {
        return PgnAnnotations::blueColor();
    }
    return PgnAnnotations::greenColor();
}

ChessBoard::BoardGeometry ChessBoard::boardGeometry() const {
    constexpr int leftMargin = 28;
    constexpr int rightMargin = 8;
    constexpr int topMargin = 8;
    constexpr int bottomMargin = 28;

    const int availableWidth = width() - leftMargin - rightMargin;
    const int availableHeight = height() - topMargin - bottomMargin;
    const int availableSide = std::min(availableWidth, availableHeight);
    const int squareSize = availableSide / 8;

    if (squareSize <= 0) {
        return {};
    }

    const int boardSide = squareSize * 8;
    return {
        leftMargin + (availableWidth - boardSide) / 2,
        topMargin + (availableHeight - boardSide) / 2,
        squareSize,
        boardSide
    };
}

Rules::Position ChessBoard::displayedPosition(Rules::Position position) const {
    if (!boardFlipped_) {
        return position;
    }

    return {7 - position.row, 7 - position.column};
}

Rules::Position ChessBoard::modelPosition(Rules::Position position) const {
    return displayedPosition(position);
}

std::optional<Rules::Position> ChessBoard::positionAt(const QPoint &point) const {
    const BoardGeometry geometry = boardGeometry();
    if (geometry.squareSize <= 0 ||
        point.x() < geometry.boardX ||
        point.x() >= geometry.boardX + geometry.boardSide ||
        point.y() < geometry.boardY ||
        point.y() >= geometry.boardY + geometry.boardSide) {
        return std::nullopt;
    }

    const Rules::Position displayed{
        (point.y() - geometry.boardY) / geometry.squareSize,
        (point.x() - geometry.boardX) / geometry.squareSize
    };
    return modelPosition(displayed);
}

QRect ChessBoard::squareRect(Rules::Position position) const {
    const BoardGeometry geometry = boardGeometry();
    if (geometry.squareSize <= 0 || !Rules::isInside(position)) {
        return {};
    }

    const Rules::Position displayed = displayedPosition(position);
    return {
        geometry.boardX + displayed.column * geometry.squareSize,
        geometry.boardY + displayed.row * geometry.squareSize,
        geometry.squareSize,
        geometry.squareSize
    };
}

bool ChessBoard::isLegalDestination(Rules::Position position) const {
    return std::find(legalMoves_.cbegin(), legalMoves_.cend(), position) !=
           legalMoves_.cend();
}

bool ChessBoard::isCurrentPlayerPiece(Rules::Position position) const {
    const auto piece = rules_.pieceAt(position);
    return piece.has_value() && piece->color == rules_.currentPlayer();
}

void ChessBoard::selectPosition(Rules::Position position) {
    if (!isCurrentPlayerPiece(position)) {
        return;
    }

    selectedPosition_ = position;
    legalMoves_ = rules_.legalMoves(position);
    update();
}

void ChessBoard::clearSelection() {
    selectedPosition_.reset();
    legalMoves_.clear();
    update();
}

void ChessBoard::handleClick(Rules::Position position) {
    if (!selectedPosition_.has_value()) {
        if (isCurrentPlayerPiece(position)) {
            selectPosition(position);
        }
        return;
    }

    if (position == *selectedPosition_) {
        clearSelection();
        return;
    }

    if (isLegalDestination(position)) {
        (void)tryMoveTo(position);
        return;
    }

    // Clicking one of the current player's other pieces starts a new move.
    // Keep the current selection for an invalid empty or opponent square so a
    // user can try another destination without selecting the source again.
    if (isCurrentPlayerPiece(position)) {
        selectPosition(position);
    }
}

bool ChessBoard::tryMoveTo(Rules::Position position) {
    if (!selectedPosition_.has_value() || !isLegalDestination(position)) {
        return false;
    }

    const Rules::Position oldPosition = *selectedPosition_;
    const auto piece = rules_.pieceAt(oldPosition);
    if (!piece.has_value()) {
        return false;
    }

    // The board is a read-only view: it never mutates its position cache. It
    // reports the move intent; the controller owns the authoritative state
    // and refreshes the view through setRules().
    clearSelection();
    emit pieceMoved(pieceSymbol(*piece), oldPosition, position);
    return true;
}

void ChessBoard::updateHoveredPosition(const QPoint &point) {
    const auto position = positionAt(point);
    if (hoveredPosition_ == position) {
        return;
    }

    hoveredPosition_ = position;
    update();
}

void ChessBoard::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        const QPoint point = event->position().toPoint();
        const auto pos = positionAt(point);
        if (pos.has_value()) {
            rightPressPosition_ = pos;
            rightPressPoint_ = point;
            rightDragCurrentPoint_ = point;
            isRightDragging_ = false;
        }
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint point = event->position().toPoint();
    pressedPosition_ = positionAt(point);

    const bool isPieceClick = pressedPosition_.has_value() &&
                              (isCurrentPlayerPiece(*pressedPosition_) || isLegalDestination(*pressedPosition_));
    if (!isPieceClick && hasUserAnnotations()) {
        clearUserAnnotations();
    }

    pressPoint_ = point;
    dragCursorPosition_ = point;
    dragging_ = false;
    selectionWasActiveAtPress_ = pressedPosition_.has_value() &&
                                  selectedPosition_ == pressedPosition_;
    selectionCreatedOnPress_ = false;
    dragCandidate_ = false;

    if (pressedPosition_.has_value()) {
        if (!selectedPosition_.has_value() &&
            isCurrentPlayerPiece(*pressedPosition_)) {
            selectPosition(*pressedPosition_);
            selectionCreatedOnPress_ = true;
        }

        dragCandidate_ = selectedPosition_ == pressedPosition_;
        if (dragCandidate_) {
            dragOffset_ = point - squareRect(*pressedPosition_).center();
        }
    }

    event->accept();
}

void ChessBoard::mouseMoveEvent(QMouseEvent *event) {
    const QPoint point = event->position().toPoint();

    if ((event->buttons() & Qt::RightButton) && rightPressPosition_.has_value()) {
        rightDragCurrentPoint_ = point;
        if (!isRightDragging_ &&
            (point - rightPressPoint_).manhattanLength() >= QApplication::startDragDistance()) {
            isRightDragging_ = true;
        }
        if (isRightDragging_) {
            update();
        }
        event->accept();
        return;
    }

    dragCursorPosition_ = point - dragOffset_;
    updateHoveredPosition(point);

    if ((event->buttons() & Qt::LeftButton) && !dragging_ && dragCandidate_ &&
        pressedPosition_.has_value() && selectedPosition_ == pressedPosition_ &&
        (point - pressPoint_).manhattanLength() >= QApplication::startDragDistance()) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
        update();
    } else if (!dragging_) {
        const auto position = positionAt(point);
        const bool pointsAtPiece = position.has_value() &&
                                   isCurrentPlayerPiece(*position);
        const bool pointsAtDestination = position.has_value() &&
                                         isLegalDestination(*position);
        setCursor(pointsAtPiece || pointsAtDestination
                      ? Qt::OpenHandCursor
                      : Qt::ArrowCursor);
    }

    if (dragging_) {
        update();
    }

    event->accept();
}

void ChessBoard::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        if (rightPressPosition_.has_value()) {
            const QPoint point = event->position().toPoint();
            const auto releasedPosition = positionAt(point);
            const QColor color = annotationColorForModifiers(event->modifiers());

            if (!isRightDragging_) {
                toggleSquareAnnotation(*rightPressPosition_, color);
            } else if (releasedPosition.has_value() && *releasedPosition != *rightPressPosition_) {
                toggleUserArrow(*rightPressPosition_, *releasedPosition, color);
            }

            rightPressPosition_.reset();
            isRightDragging_ = false;
            update();
        }
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const auto releasedPosition = positionAt(event->position().toPoint());
    if (dragging_) {
        bool moveCompleted = false;
        if (releasedPosition.has_value()) {
            moveCompleted = tryMoveTo(*releasedPosition);
        }

        if (!moveCompleted && selectedPosition_.has_value()) {
            // Stop drawing the dragged copy and let paintEvent render the
            // original piece at its source square again.
            dragCursorPosition_ = squareRect(*selectedPosition_).center();
        }
    } else if (releasedPosition.has_value()) {
        const bool releasedOnSelectedPosition =
            selectedPosition_.has_value() &&
            *selectedPosition_ == *releasedPosition;
        const bool isInitialSelectionClick = selectionCreatedOnPress_ &&
                                              releasedOnSelectedPosition;
        const bool isSelectionToggleClick = selectionWasActiveAtPress_ &&
                                             releasedOnSelectedPosition;

        if (!isInitialSelectionClick && !isSelectionToggleClick) {
            handleClick(*releasedPosition);
        } else if (isSelectionToggleClick) {
            clearSelection();
        }
    }

    dragging_ = false;
    dragCandidate_ = false;
    pressedPosition_.reset();
    selectionWasActiveAtPress_ = false;
    selectionCreatedOnPress_ = false;
    updateHoveredPosition(event->position().toPoint());

    const auto position = positionAt(event->position().toPoint());
    const bool pointsAtPiece = position.has_value() &&
                               isCurrentPlayerPiece(*position);
    const bool pointsAtDestination = position.has_value() &&
                                     isLegalDestination(*position);
    setCursor(pointsAtPiece || pointsAtDestination
                  ? Qt::OpenHandCursor
                  : Qt::ArrowCursor);
    update();
    event->accept();
}

void ChessBoard::leaveEvent(QEvent *event) {
    hoveredPosition_.reset();
    if (!dragging_) {
        setCursor(Qt::ArrowCursor);
    }
    if (isRightDragging_) {
        isRightDragging_ = false;
        rightPressPosition_.reset();
    }
    update();

    QWidget::leaveEvent(event);
}

void ChessBoard::drawMoveArrow(QPainter &painter, const Rules::Move &move,
                                const QColor &color, Qt::PenStyle style) const {
    const QRect fromSquare = squareRect(move.from);
    const QRect toSquare = squareRect(move.to);
    if (fromSquare.isEmpty() || toSquare.isEmpty()) {
        return;
    }

    drawArrow(painter, fromSquare.center(), toSquare.center(), color, style);
}

void ChessBoard::drawArrow(QPainter &painter, const QPointF &start, const QPointF &end,
                           const QColor &color, Qt::PenStyle style) const {
    const QLineF line(start, end);
    if (line.length() < 1.0) {
        return;
    }

    const qreal lineWidth = std::max(3.0, boardGeometry().squareSize * 0.12);
    const QPointF direction = (end - start) / line.length();
    const QPointF normal(-direction.y(), direction.x());
    const qreal arrowLength = boardGeometry().squareSize * 0.30;
    const qreal arrowWidth = boardGeometry().squareSize * 0.22;
    const QPointF arrowBase = end - direction * arrowLength;

    painter.save();
    QColor arrowColor = color;
    arrowColor.setAlpha(210);
    QPen pen(arrowColor, lineWidth, style, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(start, arrowBase);

    painter.setPen(Qt::NoPen);
    painter.setBrush(arrowColor);
    painter.drawPolygon(QPolygonF{end,
                                  arrowBase + normal * arrowWidth,
                                  arrowBase - normal * arrowWidth});
    painter.restore();
}

void ChessBoard::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#f7f3eb"));

    const int leftMargin = 28;
    const int bottomMargin = 28;
    const BoardGeometry geometry = boardGeometry();
    if (geometry.squareSize <= 0) {
        return;
    }

    const QColor lightSquare("#f0d9b5");
    const QColor darkSquare("#b58863");

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const Rules::Position position{row, column};
            const QRect square = squareRect(position);
            const Rules::Position displayed = displayedPosition(position);
            painter.fillRect(square, (displayed.row + displayed.column) % 2 == 0
                                       ? lightSquare
                                       : darkSquare);
        }
    }

    if (lastMoveHighlightingEnabled_ && rules_.lastMove().has_value()) {
        const Rules::Move lastMove = *rules_.lastMove();
        QColor lastMoveColor("#cdd26a");
        lastMoveColor.setAlpha(140);
        painter.fillRect(squareRect(lastMove.from), lastMoveColor);
        painter.fillRect(squareRect(lastMove.to), lastMoveColor);
    }

    if (legalMoveHighlightingEnabled_) {
        QColor legalMoveColor("#83b85c");
        legalMoveColor.setAlpha(115);
        for (const Rules::Position position : legalMoves_) {
            painter.fillRect(squareRect(position), legalMoveColor);
        }

        if (selectedPosition_.has_value()) {
            QColor selectedColor("#e8c65a");
            selectedColor.setAlpha(180);
            painter.fillRect(squareRect(*selectedPosition_), selectedColor);
        }

        if (hoveredPosition_.has_value() &&
            isLegalDestination(*hoveredPosition_)) {
            QColor hoveredColor("#6fae4f");
            hoveredColor.setAlpha(155);
            painter.fillRect(squareRect(*hoveredPosition_), hoveredColor);
        }
    }

    for (const auto &annotation : squareAnnotations_) {
        QColor highlightColor = annotation.color;
        highlightColor.setAlpha(130);
        painter.fillRect(squareRect(annotation.position), highlightColor);
    }

    // The audit overlay is deliberately not part of user annotations: it is
    // a read-only visual explanation of the selected audited move.
    if (auditAnnotation_.isValid()) {
        const auto parseAuditMove = [](const QString &text) -> std::optional<Rules::Move> {
            if (text.size() < 4) return std::nullopt;
            const int fromColumn = text.at(0).toLatin1() - 'a';
            const int fromRow = 8 - text.at(1).digitValue();
            const int toColumn = text.at(2).toLatin1() - 'a';
            const int toRow = 8 - text.at(3).digitValue();
            Rules::Move move{{fromRow, fromColumn}, {toRow, toColumn}, Rules::PieceType::None};
            return Rules::isInside(move.from) && Rules::isInside(move.to) ? std::optional<Rules::Move>(move) : std::nullopt;
        };
        if (const auto move = parseAuditMove(auditAnnotation_.playedMove); move.has_value()) {
            QColor color = PgnAnnotations::auditColor(auditAnnotation_.severity);
            color.setAlpha(125);
            painter.fillRect(squareRect(move->to), color);
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const Rules::Position position{row, column};
            if (dragging_ && selectedPosition_.has_value() &&
                position == *selectedPosition_) {
                continue;
            }

            const auto piece = rules_.pieceAt(position);
            if (!piece.has_value()) {
                continue;
            }

            const auto renderer = pieceRenderers.constFind(pieceName(*piece));
            if (renderer == pieceRenderers.cend()) {
                continue;
            }

            const QRectF square(squareRect(position));
            const qreal padding = geometry.squareSize * 0.06;
            renderer.value()->render(&painter, square.adjusted(
                padding, padding, -padding, -padding));
        }
    }

    if (dragging_ && selectedPosition_.has_value()) {
        const auto piece = rules_.pieceAt(*selectedPosition_);
        if (piece.has_value()) {
            const auto renderer = pieceRenderers.constFind(pieceName(*piece));
            if (renderer != pieceRenderers.cend()) {
                const QRectF draggedSquare(
                    dragCursorPosition_.x() - geometry.squareSize / 2.0,
                    dragCursorPosition_.y() - geometry.squareSize / 2.0,
                    geometry.squareSize,
                    geometry.squareSize);
                const qreal padding = geometry.squareSize * 0.06;
                renderer.value()->render(&painter, draggedSquare.adjusted(
                    padding, padding, -padding, -padding));
            }
        }
    }

    if (computerMovePreview_.has_value()) {
        drawMoveArrow(painter, *computerMovePreview_, QColor("#8b5fbf"), Qt::DashLine);
    }
    if (recommendedMovePreview_.has_value()) {
        drawMoveArrow(painter, *recommendedMovePreview_, QColor("#2878b5"), Qt::SolidLine);
    }
    if (auditAnnotation_.isValid()) {
        const QString text = auditAnnotation_.playedMove;
        if (text.size() >= 4) {
            const Rules::Move move{{8 - text.at(1).digitValue(), text.at(0).toLatin1() - 'a'},
                                   {8 - text.at(3).digitValue(), text.at(2).toLatin1() - 'a'},
                                   Rules::PieceType::None};
            if (Rules::isInside(move.from) && Rules::isInside(move.to)) {
                drawMoveArrow(painter, move, PgnAnnotations::auditColor(auditAnnotation_.severity), Qt::SolidLine);
            }
        }
    }

    for (const auto &arrow : userArrows_) {
        const QRect fromSquare = squareRect(arrow.from);
        const QRect toSquare = squareRect(arrow.to);
        if (!fromSquare.isEmpty() && !toSquare.isEmpty()) {
            drawArrow(painter, fromSquare.center(), toSquare.center(), arrow.color, Qt::SolidLine);
        }
    }

    if (isRightDragging_ && rightPressPosition_.has_value()) {
        const auto targetPos = positionAt(rightDragCurrentPoint_);
        const QColor previewColor = annotationColorForModifiers(QApplication::keyboardModifiers());
        const QRect fromSquare = squareRect(*rightPressPosition_);
        if (!fromSquare.isEmpty()) {
            if (targetPos.has_value() && *targetPos != *rightPressPosition_) {
                drawArrow(painter, fromSquare.center(), squareRect(*targetPos).center(),
                          previewColor, Qt::SolidLine);
            } else {
                drawArrow(painter, fromSquare.center(), rightDragCurrentPoint_,
                          previewColor, Qt::DashLine);
            }
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(QPen(QColor("#5c4033"), 2));
    painter.drawRect(geometry.boardX, geometry.boardY,
                     geometry.boardSide - 1, geometry.boardSide - 1);

    QFont coordinateFont = font();
    coordinateFont.setBold(true);
    painter.setFont(coordinateFont);
    painter.setPen(QColor("#5c4033"));

    for (int column = 0; column < 8; ++column) {
        const int fileColumn = boardFlipped_ ? 7 - column : column;
        const QRect fileLabel(
            geometry.boardX + column * geometry.squareSize,
            geometry.boardY + geometry.boardSide + 3,
            geometry.squareSize,
            bottomMargin - 3);
        painter.drawText(fileLabel, Qt::AlignHCenter | Qt::AlignTop,
                         QString(QChar('a' + fileColumn)));
    }

    for (int row = 0; row < 8; ++row) {
        const int rank = boardFlipped_ ? row + 1 : 8 - row;
        const QRect rankLabel(
            geometry.boardX - leftMargin + 3,
            geometry.boardY + row * geometry.squareSize,
            leftMargin - 6,
            geometry.squareSize);
        painter.drawText(rankLabel, Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(rank));
    }
}

QSize ChessBoard::sizeHint() const {
    return {480, 480};
}
