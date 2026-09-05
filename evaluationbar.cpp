//
// Evaluation bar widget for chess games.
//

#include "evaluationbar.h"

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

QSize EvaluationBar::sizeHint() const {
    return {13, 400};
}

QSize EvaluationBar::minimumSizeHint() const {
    return {8, 120};
}

void EvaluationBar::updateToolTip() {
    setToolTip(tr("Evaluation: %1% White").arg(value_, 0, 'f', 1));
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

    // Draw border
    painter.setPen(QPen(borderColor_, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(barRect, cornerRadius, cornerRadius);

    // Optional evaluation text
    if (showEvaluationText_) {
        painter.setPen(value_ >= 50.0 ? blackColor_ : whiteColor_);
        QFont textFont = font();
        textFont.setPointSize(8);
        textFont.setBold(true);
        painter.setFont(textFont);
        const QString text = QString::number(std::round(value_));
        painter.drawText(barRect, Qt::AlignCenter, text);
    }
}
