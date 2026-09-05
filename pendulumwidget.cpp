//
// Created by mordicus on 25/08/2026.
//

#include "pendulumwidget.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QString>

#include <algorithm>

namespace {

constexpr int TimerIntervalMilliseconds = 100;
constexpr int HorizontalPadding = 6;
constexpr int VerticalPadding = 4;

} // namespace

PendulumWidget::PendulumWidget(QWidget *parent)
    : PendulumWidget(PieceColor::White, parent) {}

PendulumWidget::PendulumWidget(PieceColor pieceColor, QWidget *parent)
    : QWidget(parent)
    , pieceColor_(pieceColor) {
    timer_.setInterval(TimerIntervalMilliseconds);
    connect(&timer_, &QTimer::timeout, this, &PendulumWidget::updateCountdown);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumSize(75, 24);
    setAttribute(Qt::WA_OpaquePaintEvent);
    updateAccessibilityText();
}

qint64 PendulumWidget::remainingMilliseconds() const {
    return currentRemainingMilliseconds();
}

void PendulumWidget::setRemainingMilliseconds(qint64 milliseconds) {
    const qint64 boundedMilliseconds = std::max<qint64>(0, milliseconds);
    const bool wasRunning = running_;
    const qint64 oldRemaining = currentRemainingMilliseconds();

    initialMilliseconds_ = boundedMilliseconds;
    remainingMilliseconds_ = boundedMilliseconds;

    if (wasRunning) {
        elapsedTimer_.restart();
        if (boundedMilliseconds == 0) {
            running_ = false;
            timer_.stop();
            elapsedTimer_.invalidate();
        }
    }

    if (oldRemaining != boundedMilliseconds) {
        emit remainingMillisecondsChanged(boundedMilliseconds);
    }
    refreshDisplay();
    updateAccessibilityText();

    if (wasRunning && !running_) {
        emit runningChanged(false);
    }
}

bool PendulumWidget::isRunning() const {
    return running_;
}

PendulumWidget::PieceColor PendulumWidget::pieceColor() const {
    return pieceColor_;
}

void PendulumWidget::setPieceColor(PieceColor pieceColor) {
    if (pieceColor_ == pieceColor) {
        return;
    }

    pieceColor_ = pieceColor;
    updateAccessibilityText();
    update();
    emit pieceColorChanged(pieceColor_);
}

QString PendulumWidget::displayText() const {
    return displayText_;
}

QColor PendulumWidget::backgroundColor() const {
    return pieceColor_ == PieceColor::White
               ? QColor(QStringLiteral("#f0d9b5"))
               : QColor(QStringLiteral("#b58863"));
}

QColor PendulumWidget::displayColor() const {
    return pieceColor_ == PieceColor::White
               ? QColor(QStringLiteral("#312e2b"))
               : QColor(QStringLiteral("#f7f3eb"));
}

QSize PendulumWidget::sizeHint() const {
    QFont digitalFont = font();
    digitalFont.setStyleHint(QFont::Monospace);
    digitalFont.setWeight(QFont::DemiBold);
    digitalFont.setPointSizeF(std::max(9.0, font().pointSizeF() * 0.75));

    const QFontMetrics metrics(digitalFont);
    const int width = metrics.horizontalAdvance(QStringLiteral("00:00:00")) +
                      2 * HorizontalPadding;
    return {width, metrics.height() + 2 * VerticalPadding};
}

QSize PendulumWidget::minimumSizeHint() const {
    return {75, 24};
}

void PendulumWidget::set(int hours, int minutes, int seconds) {
    const qint64 totalSeconds = std::max<qint64>(0, hours) * 3600 +
                                 std::max<qint64>(0, minutes) * 60 +
                                 std::max<qint64>(0, seconds);
    setRemainingMilliseconds(totalSeconds * 1000);
}

void PendulumWidget::start() {
    if (running_ || remainingMilliseconds_ <= 0) {
        return;
    }

    elapsedTimer_.start();
    running_ = true;
    timer_.start();
    emit runningChanged(true);
}

void PendulumWidget::stop() {
    if (!running_) {
        return;
    }

    remainingMilliseconds_ = currentRemainingMilliseconds();
    running_ = false;
    timer_.stop();
    elapsedTimer_.invalidate();
    refreshDisplay();
    updateAccessibilityText();
    emit remainingMillisecondsChanged(remainingMilliseconds_);
    emit runningChanged(false);
}

void PendulumWidget::reset() {
    const bool wasRunning = running_;
    const qint64 oldRemaining = currentRemainingMilliseconds();

    running_ = false;
    timer_.stop();
    elapsedTimer_.invalidate();
    remainingMilliseconds_ = initialMilliseconds_;

    if (oldRemaining != remainingMilliseconds_) {
        emit remainingMillisecondsChanged(remainingMilliseconds_);
    }
    refreshDisplay();
    updateAccessibilityText();

    if (wasRunning) {
        emit runningChanged(false);
    }
}

void PendulumWidget::updateCountdown() {
    if (!running_) {
        return;
    }

    const qint64 remaining = currentRemainingMilliseconds();
    if (remaining != remainingMilliseconds_) {
        // remainingMilliseconds_ is the timer baseline. Keep it unchanged
        // while running so the elapsed time is only subtracted once.
        emit remainingMillisecondsChanged(remaining);
    }
    refreshDisplay();

    if (remaining <= 0) {
        remainingMilliseconds_ = 0;
        running_ = false;
        timer_.stop();
        elapsedTimer_.invalidate();
        updateAccessibilityText();
        emit runningChanged(false);
    }
}

void PendulumWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), backgroundColor());

    painter.setPen(QPen(QColor(QStringLiteral("#5c4033")), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    QFont digitalFont = font();
    digitalFont.setStyleHint(QFont::Monospace);
    digitalFont.setWeight(QFont::DemiBold);
    digitalFont.setPointSizeF(std::max(9.0, font().pointSizeF() * 0.75));
    painter.setFont(digitalFont);
    painter.setPen(displayColor());
    painter.drawText(rect().adjusted(HorizontalPadding, 0, -HorizontalPadding, 0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     displayText_);
}

qint64 PendulumWidget::currentRemainingMilliseconds() const {
    if (!running_) {
        return remainingMilliseconds_;
    }

    return std::max<qint64>(0, remainingMilliseconds_ - elapsedTimer_.elapsed());
}

QString PendulumWidget::formatTime(qint64 milliseconds) {
    const qint64 totalSeconds = std::max<qint64>(0, milliseconds) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void PendulumWidget::refreshDisplay() {
    const QString newDisplayText = formatTime(currentRemainingMilliseconds());
    if (displayText_ == newDisplayText) {
        return;
    }

    displayText_ = newDisplayText;
    update();
    emit displayTextChanged(displayText_);
}

void PendulumWidget::updateAccessibilityText() {
    const QString colorName = pieceColor_ == PieceColor::White
                                  ? tr("White")
                                  : tr("Black");
    setAccessibleName(tr("%1 clock").arg(colorName));
    setAccessibleDescription(displayText_);
}
