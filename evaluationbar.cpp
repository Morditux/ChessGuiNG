//
// Evaluation bar widget for chess games.
//

#include "evaluationbar.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

EvaluationBar::EvaluationBar(QWidget *parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setMinimumSize(8, 100);
    updateToolTip();
}

double EvaluationBar::value() const {
    return value_;
}

double EvaluationBar::evaluation() const {
    return value_;
}

void EvaluationBar::setValue(double value) {
    const double clamped = std::clamp(value, 0.0, 100.0);
    if (qFuzzyCompare(value_, clamped)) {
        return;
    }

    value_ = clamped;
    updateToolTip();
    emit valueChanged(value_);
    emit evaluationChanged(value_);
    update();
}

void EvaluationBar::setEvaluation(double eval) {
    setValue(eval);
}

QColor EvaluationBar::whiteColor() const {
    return whiteColor_;
}

void EvaluationBar::setWhiteColor(const QColor &color) {
    if (whiteColor_ == color) {
        return;
    }
    whiteColor_ = color;
    update();
}

QColor EvaluationBar::blackColor() const {
    return blackColor_;
}

void EvaluationBar::setBlackColor(const QColor &color) {
    if (blackColor_ == color) {
        return;
    }
    blackColor_ = color;
    update();
}

QColor EvaluationBar::borderColor() const {
    return borderColor_;
}

void EvaluationBar::setBorderColor(const QColor &color) {
    if (borderColor_ == color) {
        return;
    }
    borderColor_ = color;
    update();
}

bool EvaluationBar::showCenterLine() const {
    return showCenterLine_;
}

void EvaluationBar::setShowCenterLine(bool show) {
    if (showCenterLine_ == show) {
        return;
    }
    showCenterLine_ = show;
    update();
}

bool EvaluationBar::showEvaluationText() const {
    return showEvaluationText_;
}

void EvaluationBar::setShowEvaluationText(bool show) {
    if (showEvaluationText_ == show) {
        return;
    }
    showEvaluationText_ = show;
    update();
}

QString EvaluationBar::scoreText() const {
    return scoreText_;
}

void EvaluationBar::setScoreText(const QString &scoreText) {
    if (scoreText_ == scoreText) {
        return;
    }
    scoreText_ = scoreText;
    updateToolTip();
    update();
}

QSize EvaluationBar::sizeHint() const {
    // Wide enough to hold a score such as "+1.35" at the default font size.
    return {38, 400};
}

QSize EvaluationBar::minimumSizeHint() const {
    return {24, 120};
}

void EvaluationBar::updateToolTip() {
    const QString percentage = tr("Evaluation: %1% White").arg(value_, 0, 'f', 1);
    setToolTip(scoreText_.isEmpty()
                   ? percentage
                   : tr("Evaluation: %1 (%2)").arg(scoreText_, percentage));
}

void EvaluationBar::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal marginX = 1.5;
    const qreal marginY = 6.0;
    const qreal barWidth = std::max<qreal>(0.0, width() - 2.0 * marginX);
    const qreal barHeight = std::max<qreal>(0.0, height() - 2.0 * marginY);

    if (barWidth <= 0.0 || barHeight <= 0.0) {
        return;
    }

    const QRectF barRect(marginX, marginY, barWidth, barHeight);
    const qreal cornerRadius = 2.0;

    QPainterPath clipPath;
    clipPath.addRoundedRect(barRect, cornerRadius, cornerRadius);

    painter.save();
    painter.setClipPath(clipPath);

    // Value represents White's evaluation from 0 (all black) to 100 (all white).
    // In standard chess layout, Black is at the top and White is at the bottom.
    const qreal blackRatio = (100.0 - value_) / 100.0;
    const qreal blackHeight = barHeight * blackRatio;

    // Top: Black section
    if (blackHeight > 0.0) {
        const QRectF blackRect(barRect.left(), barRect.top(), barRect.width(), blackHeight);
        painter.fillRect(blackRect, blackColor_);
    }

    // Bottom: White section
    const qreal whiteHeight = barHeight - blackHeight;
    if (whiteHeight > 0.0) {
        const QRectF whiteRect(barRect.left(), barRect.top() + blackHeight, barRect.width(), whiteHeight);
        painter.fillRect(whiteRect, whiteColor_);
    }

    // Center indicator line (50% mark)
    if (showCenterLine_) {
        const qreal midY = barRect.top() + barHeight * 0.5;
        painter.setPen(QPen(QColor(128, 128, 128, 160), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(barRect.left(), midY), QPointF(barRect.right(), midY));
    }

    painter.restore();

    // Boundary between the two sections: without it the white section can
    // vanish into a light window background.
    if (blackHeight > 0.0 && whiteHeight > 0.0) {
        const qreal boundaryY = barRect.top() + blackHeight;
        painter.setPen(QPen(borderColor_, 1.0));
        painter.drawLine(QPointF(barRect.left(), boundaryY),
                         QPointF(barRect.right(), boundaryY));
    }

    // Draw border
    painter.setPen(QPen(borderColor_, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(barRect, cornerRadius, cornerRadius);

    // Optional evaluation text: the number is what makes the gauge readable.
    // Around equality the centre of the bar is exactly on the boundary between
    // the two sections, so the score is drawn in a pill rather than straddling
    // it.
    if (showEvaluationText_) {
        const QString text = scoreText_.isEmpty()
                                 ? QString::number(std::round(value_))
                                 : scoreText_;

        QFont textFont = font();
        textFont.setPointSize(8);
        textFont.setBold(true);
        painter.setFont(textFont);

        const bool whiteSectionAtCenter = value_ >= 50.0;
        const QColor pillColor = whiteSectionAtCenter ? whiteColor_ : blackColor_;
        const QColor textColor = whiteSectionAtCenter ? blackColor_ : whiteColor_;

        const QFontMetricsF metrics(textFont);
        const qreal pillWidth =
            std::min(barRect.width() - 2.0, metrics.horizontalAdvance(text) + 6.0);
        const QRectF pillRect(barRect.center().x() - pillWidth / 2.0,
                              barRect.center().y() - metrics.height() / 2.0 - 1.0,
                              pillWidth,
                              metrics.height() + 2.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(pillColor);
        painter.drawRoundedRect(pillRect, 2.0, 2.0);

        painter.setPen(textColor);
        painter.drawText(pillRect, Qt::AlignCenter, text);
    }
}
