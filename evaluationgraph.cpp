//
// Win-probability curve of a whole game.
//

#include "evaluationgraph.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <algorithm>

namespace {
// Vertical breathing room so the extreme points stay visible.
constexpr qreal VerticalMargin = 3.0;
constexpr qreal MarkerRadius = 2.5;
} // namespace

EvaluationGraph::EvaluationGraph(QWidget *parent)
    : QWidget(parent) {
    // Preferred rather than Fixed vertically: the curve must be able to give
    // its room back to the move list when the panel is short.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(tr("Evaluation graph"));
    setAccessibleDescription(
        tr("Evaluation of each move of the game; click to jump to a move"));
}

const QVector<double> &EvaluationGraph::evaluations() const {
    return evaluations_;
}

void EvaluationGraph::setEvaluations(const QVector<double> &percentages) {
    if (evaluations_ == percentages) {
        return;
    }
    evaluations_ = percentages;
    currentPly_ = qBound(0, currentPly_, qMax(0, evaluations_.size() - 1));
    update();
}

int EvaluationGraph::currentPly() const {
    return currentPly_;
}

void EvaluationGraph::setCurrentPly(int ply) {
    const int clamped = qBound(0, ply, qMax(0, evaluations_.size() - 1));
    if (currentPly_ == clamped) {
        return;
    }
    currentPly_ = clamped;
    update();
}

const QVector<AuditAnnotation> &EvaluationGraph::auditAnnotations() const {
    return auditAnnotations_;
}

void EvaluationGraph::setAuditAnnotations(const QVector<AuditAnnotation> &annotations) {
    if (auditAnnotations_ == annotations) {
        return;
    }
    auditAnnotations_ = annotations;
    update();
}

QSize EvaluationGraph::sizeHint() const {
    return {260, 88};
}

QRectF EvaluationGraph::plotRect() const {
    return QRectF(rect()).adjusted(0.5, VerticalMargin, -0.5, -VerticalMargin);
}

int EvaluationGraph::xAtPly(int ply) const {
    const QRectF plot = plotRect();
    const int count = evaluations_.size();
    if (count <= 1) {
        return qRound(plot.center().x());
    }

    const qreal step = plot.width() / qreal(count - 1);
    return qRound(plot.left() + step * ply);
}

qreal EvaluationGraph::yAtValue(double percentage) const {
    const QRectF plot = plotRect();
    const double clamped = std::clamp(percentage, 0.0, 100.0);
    return plot.bottom() - plot.height() * (clamped / 100.0);
}

int EvaluationGraph::plyAtX(int x) const {
    const QRectF plot = plotRect();
    const int count = evaluations_.size();
    if (count <= 1 || plot.width() <= 0.0) {
        return 0;
    }

    const qreal step = plot.width() / qreal(count - 1);
    return qBound(0, qRound((x - plot.left()) / step), count - 1);
}

void EvaluationGraph::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plot = plotRect();
    if (plot.width() <= 0.0 || plot.height() <= 0.0) {
        return;
    }

    const int count = evaluations_.size();

    // The curve, as one point per ply. A single point is stretched across the
    // whole plot so that the two territories can still be filled.
    QPolygonF curve;
    curve.reserve(qMax(2, count));
    if (count == 0) {
        const qreal mid = yAtValue(50.0);
        curve << QPointF(plot.left(), mid) << QPointF(plot.right(), mid);
    } else {
        for (int ply = 0; ply < count; ++ply) {
            curve << QPointF(xAtPly(ply), yAtValue(evaluations_.at(ply)));
        }
        if (count == 1) {
            curve.prepend(QPointF(plot.left(), curve.first().y()));
            curve.append(QPointF(plot.right(), curve.last().y()));
        }
    }

    // Advantage band between the equilibrium line and the curve: white when
    // White is ahead, dark when Black is. Clipping the same band splits it at
    // the equilibrium line, so a curve that crosses it is filled on both
    // sides correctly.
    const qreal midY = plot.center().y();
    QPolygonF band;
    band.reserve(curve.size() + 2);
    band << QPointF(plot.left(), midY);
    for (const QPointF &point : std::as_const(curve)) {
        band << QPointF(point.x(), qBound(plot.top(), point.y(), plot.bottom()));
    }
    band << QPointF(plot.right(), midY);

    painter.fillRect(plot, plotColor_);

    const auto fillBand = [&painter, &band, &plot, midY](const QColor &color,
                                                         bool upperHalf) {
        const QRectF half = upperHalf
                                ? QRectF(plot.left(), plot.top(), plot.width(),
                                         midY - plot.top())
                                : QRectF(plot.left(), midY, plot.width(),
                                         plot.bottom() - midY);
        painter.save();
        painter.setClipRect(half);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(band);
        painter.restore();
    };

    fillBand(whiteColor_, true);
    fillBand(blackColor_, false);

    // Equilibrium line.
    painter.setPen(QPen(QColor(128, 128, 128, 160), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(plot.left(), midY), QPointF(plot.right(), midY));

    // Curve outline and frame.
    painter.setPen(QPen(borderColor_, 1.0));
    painter.drawPolyline(curve);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plot);

    // Blunders and mistakes reported by the game audit.
    for (int ply = 1; ply < auditAnnotations_.size() && ply < count; ++ply) {
        const AuditAnnotation &annotation = auditAnnotations_.at(ply);
        if (!annotation.isValid()) {
            continue;
        }
        painter.setPen(QPen(borderColor_, 1.0));
        painter.setBrush(PgnAnnotations::auditColor(annotation.severity));
        painter.drawEllipse(QPointF(xAtPly(ply), yAtValue(evaluations_.at(ply))),
                            MarkerRadius, MarkerRadius);
    }

    // Cursor on the position currently displayed on the board.
    if (count > 0) {
        const int cursorX = xAtPly(currentPly_);
        QColor cursorColor = palette().color(QPalette::Highlight);
        cursorColor.setAlpha(220);
        painter.setPen(QPen(cursorColor, 2.0));
        painter.drawLine(QPointF(cursorX, plot.top()), QPointF(cursorX, plot.bottom()));
    }
}

void EvaluationGraph::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || evaluations_.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int ply = plyAtX(qRound(event->position().x()));
    setCurrentPly(ply);
    emit plySelected(ply);
    event->accept();
}
