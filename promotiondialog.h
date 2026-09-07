//
// Dialog for choosing the promotion piece when a pawn reaches the last rank.
//

#ifndef CHESSGUI_PROMOTIONDIALOG_H
#define CHESSGUI_PROMOTIONDIALOG_H

#include <QDialog>
#include "rules.h"

class QToolButton;

class PromotionDialog : public QDialog {
    Q_OBJECT

public:
    explicit PromotionDialog(Rules::Color color, QWidget *parent = nullptr);

    [[nodiscard]] Rules::PieceType selectedPiece() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void selectAndAccept(Rules::PieceType piece);

    Rules::PieceType selectedPiece_ = Rules::PieceType::Queen;
    QToolButton *queenButton_ = nullptr;
    QToolButton *rookButton_ = nullptr;
    QToolButton *bishopButton_ = nullptr;
    QToolButton *knightButton_ = nullptr;
};

#endif // CHESSGUI_PROMOTIONDIALOG_H
