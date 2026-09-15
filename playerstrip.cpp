//
// Per-player strip shown above and below the board.
//

#include "playerstrip.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>

namespace {
constexpr int TurnIndicatorSize = 10;
// Keeps the clocks of both strips aligned even when only one shows material.
constexpr int MaterialLabelWidth = 30;
} // namespace

PlayerStrip::PlayerStrip(PendulumWidget::PieceColor color, QWidget *parent)
    : QWidget(parent)
    , color_(color) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(6);

    turnIndicator_ = new QLabel(this);
    turnIndicator_->setObjectName(QStringLiteral("turnIndicator"));
    turnIndicator_->setFixedSize(TurnIndicatorSize, TurnIndicatorSize);
    turnIndicator_->setStyleSheet(
        QStringLiteral("background-color: palette(highlight); border-radius: %1px;")
            .arg(TurnIndicatorSize / 2));
    turnIndicator_->setAccessibleName(tr("Side to move"));
    turnIndicator_->setVisible(false);
    layout->addWidget(turnIndicator_);

    nameLabel_ = new QLabel(this);
    nameLabel_->setObjectName(QStringLiteral("playerNameLabel"));
    nameLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(nameLabel_, 1);

    materialLabel_ = new QLabel(this);
    materialLabel_->setObjectName(QStringLiteral("materialLabel"));
    materialLabel_->setMinimumWidth(MaterialLabelWidth);
    materialLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(materialLabel_);

    clock_ = new PendulumWidget(color, this);
    layout->addWidget(clock_);

    updateNameLabel();
    updateMaterialLabel();
}

PendulumWidget::PieceColor PlayerStrip::color() const {
    return color_;
}

PendulumWidget *PlayerStrip::clock() const {
    return clock_;
}

QString PlayerStrip::defaultPlayerName() const {
    return color_ == PendulumWidget::PieceColor::White ? tr("White") : tr("Black");
}

QString PlayerStrip::playerName() const {
    return playerName_;
}

void PlayerStrip::setPlayerName(const QString &name) {
    const QString trimmed = name.trimmed();
    if (playerName_ == trimmed) {
        return;
    }
    playerName_ = trimmed;
    updateNameLabel();
}

bool PlayerStrip::isActive() const {
    return active_;
}

void PlayerStrip::setActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    turnIndicator_->setVisible(active);
}

int PlayerStrip::materialDifference() const {
    return materialDifference_;
}

void PlayerStrip::setMaterialDifference(int pawns) {
    if (materialDifference_ == pawns) {
        return;
    }
    materialDifference_ = pawns;
    updateMaterialLabel();
}

void PlayerStrip::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateNameLabel();
}

void PlayerStrip::updateNameLabel() {
    const QString name = playerName_.isEmpty() ? defaultPlayerName() : playerName_;
    // The name shares its row with the clock and the material balance: a long
    // engine name is elided rather than pushing them out of the strip.
    const QString elided =
        nameLabel_->fontMetrics().elidedText(name, Qt::ElideRight,
                                             qMax(0, nameLabel_->width()));
    nameLabel_->setText(elided.isEmpty() ? name : elided);
    nameLabel_->setToolTip(name);
    nameLabel_->setAccessibleName(name);
}

void PlayerStrip::updateMaterialLabel() {
    if (materialDifference_ <= 0) {
        materialLabel_->setText(QString());
        materialLabel_->setToolTip(QString());
        materialLabel_->setAccessibleDescription(QString());
        return;
    }

    const QString text = QStringLiteral("+%1").arg(materialDifference_);
    const QString description = tr("Up %1 in material").arg(materialDifference_);
    materialLabel_->setText(text);
    materialLabel_->setToolTip(description);
    materialLabel_->setAccessibleDescription(description);
}
