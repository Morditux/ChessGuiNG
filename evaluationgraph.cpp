//
// Win-probability curve of a whole game.
//

#include "evaluationgraph.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QToolTip>
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

int EvaluationGraph::analysedCount() const {
    return analysedCount_;
}

void EvaluationGraph::setAnalysedCount(int count) {
    const int bounded =
        count < 0 ? -1
                  : qMin(count, static_cast<int>(evaluations_.size()));
    if (analysedCount_ == bounded) {
        return;
    }
    analysedCount_ = bounded;
    update();
}

void EvaluationGraph::setEvaluations(const QVector<double> &percentages) {
    if (evaluations_ == percentages) {
        return;
    }
    evaluations_ = percentages;
    currentPly_ = qBound(0, currentPly_, qMax(0, evaluations_.size() - 1));
    if (analysedCount_ >= 0) {
        analysedCount_ =
            qMin(analysedCount_, static_cast<int>(evaluations_.size()));
    }
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

QString EvaluationGraph::tooltipForPly(int ply) const {
    if (ply < 0 || ply >= evaluations_.size()) {
        return {};
    }

    // Ply 0 is the starting position; the others follow a single move.
    const QString position =
        ply == 0 ? tr("Start")
                 : tr("Move %1 (%2)")
                       .arg((ply + 1) / 2)
                       .arg(ply % 2 == 1 ? tr("White") : tr("Black"));
    return tr("%1 — Evaluation: %2% White")
        .arg(position)
        .arg(evaluations_.at(ply), 0, 'f', 1);
}

bool EvaluationGraph::event(QEvent *event) {
    if (event->type() == QEvent::ToolTip && !evaluations_.isEmpty()) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int ply = plyAtX(help->pos().x());
        QToolTip::showText(help->globalPos(), tooltipForPly(ply), this);
        return true;
    }

    return QWidget::event(event);
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
        // Intersect, so a caller can restrict the band to the analysed part of
        // the curve and still get the equilibrium split.
        painter.setClipRect(half, Qt::IntersectClip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(band);
        painter.restore();
    };

    const int settled = analysedCount_ < 0
                            ? count
                            : qBound(0, analysedCount_, count);

    // Everything the static pass filled in is drawn faded first; the analysed
    // prefix is then painted over it at full strength.
    if (settled < count) {
        QColor pendingWhite = whiteColor_;
        QColor pendingBlack = blackColor_;
        pendingWhite.setAlpha(90);
        pendingBlack.setAlpha(90);
        fillBand(pendingWhite, true);
        fillBand(pendingBlack, false);
    }

    const QRectF settledRect(plot.left(), plot.top(),
                             settled > 0 ? xAtPly(settled - 1) - plot.left()
                                         : 0.0,
                             plot.height());
    if (settled < count) {
        painter.save();
        painter.setClipRect(settledRect, Qt::IntersectClip);
    }
    fillBand(whiteColor_, true);
    fillBand(blackColor_, false);
    if (settled < count) {
        painter.restore();
    }

    // Equilibrium line.
    painter.setPen(QPen(QColor(128, 128, 128, 160), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(plot.left(), midY), QPointF(plot.right(), midY));

    // Curve outline and frame: the unanalysed tail is faded, the analysed
    // prefix is drawn on top of it.
    if (settled < count) {
        QColor pendingBorder = borderColor_;
        pendingBorder.setAlpha(80);
        painter.setPen(QPen(pendingBorder, 1.0));
        painter.drawPolyline(curve);
        painter.save();
        painter.setClipRect(settledRect, Qt::IntersectClip);
    }
    painter.setPen(QPen(borderColor_, 1.0));
    painter.drawPolyline(curve);
    if (settled < count) {
        painter.restore();
    }
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plot);

    // Blunders and mistakes reported by the game audit.
    for (int ply = 1; ply < auditAnnotations_.size() && ply < count &&
                     ply < settled;
         ++ply) {
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
