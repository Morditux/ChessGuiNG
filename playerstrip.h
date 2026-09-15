//
// Per-player strip shown above and below the board.
//

#ifndef CHESSGUI_PLAYERSTRIP_H
#define CHESSGUI_PLAYERSTRIP_H

#include <QString>
#include <QWidget>

#include "pendulumwidget.h"

class QLabel;
class QResizeEvent;

// A read-only view of one player's game state: whether it is that player's
// turn, the player's name, the material balance and the clock. The widget
// holds no game logic; the controller fills it through the setters.
class PlayerStrip : public QWidget {
    Q_OBJECT

public:
    explicit PlayerStrip(PendulumWidget::PieceColor color, QWidget *parent = nullptr);

    [[nodiscard]] PendulumWidget::PieceColor color() const;
    [[nodiscard]] PendulumWidget *clock() const;

    // Player name, for example the PGN White/Black tag. An empty name falls
    // back to the colour of the strip.
    [[nodiscard]] QString playerName() const;
    void setPlayerName(const QString &name);

    // True while it is this player's turn; drives the turn indicator.
    [[nodiscard]] bool isActive() const;
    void setActive(bool active);

    // Material difference in pawns from this player's point of view. Only a
    // positive value is shown, so the leading side carries the indicator.
    [[nodiscard]] int materialDifference() const;
    void setMaterialDifference(int pawns);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    [[nodiscard]] QString defaultPlayerName() const;
    void updateNameLabel();
    void updateMaterialLabel();

    PendulumWidget::PieceColor color_ = PendulumWidget::PieceColor::White;
    QString playerName_;
    bool active_ = false;
    int materialDifference_ = 0;

    QLabel *turnIndicator_ = nullptr;
    QLabel *nameLabel_ = nullptr;
    QLabel *materialLabel_ = nullptr;
    PendulumWidget *clock_ = nullptr;
};

#endif // CHESSGUI_PLAYERSTRIP_H
