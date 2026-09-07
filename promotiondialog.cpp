//
// Dialog for choosing the promotion piece when a pawn reaches the last rank.
//

#include "promotiondialog.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

PromotionDialog::PromotionDialog(Rules::Color color, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Pawn Promotion"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    auto *instructionLabel = new QLabel(tr("Choose promotion piece:"), this);
    instructionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(instructionLabel);

    auto *buttonsLayout = new QHBoxLayout();
    buttonsLayout->setSpacing(8);

    const QString prefix = color == Rules::Color::White
                               ? QStringLiteral("w")
                               : QStringLiteral("b");

    const auto createPieceButton = [this, &prefix](Rules::PieceType piece,
                                                   const QString &pieceCode,
                                                   const QString &toolTip,
                                                   const QString &objectName) {
        auto *button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setIcon(QIcon(QStringLiteral(":/pieces/%1%2.svg").arg(prefix, pieceCode)));
        button->setIconSize(QSize(48, 48));
        button->setMinimumSize(60, 60);
        button->setToolTip(toolTip);
        button->setAccessibleName(toolTip);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QToolButton::clicked, this, [this, piece] {
            selectAndAccept(piece);
        });
        return button;
    };

    queenButton_ = createPieceButton(Rules::PieceType::Queen, QStringLiteral("Q"),
                                     tr("Queen (Q)"), QStringLiteral("queenButton"));
    rookButton_ = createPieceButton(Rules::PieceType::Rook, QStringLiteral("R"),
                                    tr("Rook (R)"), QStringLiteral("rookButton"));
    bishopButton_ = createPieceButton(Rules::PieceType::Bishop, QStringLiteral("B"),
                                      tr("Bishop (B)"), QStringLiteral("bishopButton"));
    knightButton_ = createPieceButton(Rules::PieceType::Knight, QStringLiteral("N"),
                                      tr("Knight (N)"), QStringLiteral("knightButton"));

    buttonsLayout->addWidget(queenButton_);
    buttonsLayout->addWidget(rookButton_);
    buttonsLayout->addWidget(bishopButton_);
    buttonsLayout->addWidget(knightButton_);

    mainLayout->addLayout(buttonsLayout);

    queenButton_->setFocus();
    adjustSize();
}

Rules::PieceType PromotionDialog::selectedPiece() const {
    return selectedPiece_;
}

void PromotionDialog::selectAndAccept(Rules::PieceType piece) {
    selectedPiece_ = piece;
    accept();
}

void PromotionDialog::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Q:
    case Qt::Key_D: // Dame (French)
        selectAndAccept(Rules::PieceType::Queen);
        return;
    case Qt::Key_R:
    case Qt::Key_T: // Tour (French)
        selectAndAccept(Rules::PieceType::Rook);
        return;
    case Qt::Key_B:
    case Qt::Key_F: // Fou (French)
        selectAndAccept(Rules::PieceType::Bishop);
        return;
    case Qt::Key_N:
    case Qt::Key_C: // Cavalier (French)
        selectAndAccept(Rules::PieceType::Knight);
        return;
    case Qt::Key_Escape:
        reject();
        return;
    default:
        QDialog::keyPressEvent(event);
        break;
    }
}
