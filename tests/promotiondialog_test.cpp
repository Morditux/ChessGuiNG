//
// Unit tests for PromotionDialog.
//

#include <QtTest>
#include <QToolButton>

#include "promotiondialog.h"
#include "rules.h"

class PromotionDialogTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialStateWhite();
    void testInitialStateBlack();
    void testPieceSelectionByClick();
    void testPieceSelectionByKeyPress();
    void testCancelByEscape();
};

void PromotionDialogTest::testInitialStateWhite() {
    PromotionDialog dialog(Rules::Color::White);
    QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Queen);
    QCOMPARE(dialog.windowTitle(), QStringLiteral("Pawn Promotion"));

    auto *queenBtn = dialog.findChild<QToolButton *>(QStringLiteral("queenButton"));
    auto *rookBtn = dialog.findChild<QToolButton *>(QStringLiteral("rookButton"));
    auto *bishopBtn = dialog.findChild<QToolButton *>(QStringLiteral("bishopButton"));
    auto *knightBtn = dialog.findChild<QToolButton *>(QStringLiteral("knightButton"));

    QVERIFY(queenBtn != nullptr);
    QVERIFY(rookBtn != nullptr);
    QVERIFY(bishopBtn != nullptr);
    QVERIFY(knightBtn != nullptr);

    QVERIFY(!queenBtn->icon().isNull());
    QVERIFY(!rookBtn->icon().isNull());
    QVERIFY(!bishopBtn->icon().isNull());
    QVERIFY(!knightBtn->icon().isNull());
}

void PromotionDialogTest::testInitialStateBlack() {
    PromotionDialog dialog(Rules::Color::Black);
    QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Queen);

    auto *queenBtn = dialog.findChild<QToolButton *>(QStringLiteral("queenButton"));
    auto *knightBtn = dialog.findChild<QToolButton *>(QStringLiteral("knightButton"));

    QVERIFY(queenBtn != nullptr);
    QVERIFY(knightBtn != nullptr);
    QVERIFY(!queenBtn->icon().isNull());
    QVERIFY(!knightBtn->icon().isNull());
}

void PromotionDialogTest::testPieceSelectionByClick() {
    {
        PromotionDialog dialog(Rules::Color::White);
        auto *rookBtn = dialog.findChild<QToolButton *>(QStringLiteral("rookButton"));
        QVERIFY(rookBtn != nullptr);
        rookBtn->click();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Rook);
    }
    {
        PromotionDialog dialog(Rules::Color::White);
        auto *bishopBtn = dialog.findChild<QToolButton *>(QStringLiteral("bishopButton"));
        QVERIFY(bishopBtn != nullptr);
        bishopBtn->click();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Bishop);
    }
    {
        PromotionDialog dialog(Rules::Color::White);
        auto *knightBtn = dialog.findChild<QToolButton *>(QStringLiteral("knightButton"));
        QVERIFY(knightBtn != nullptr);
        knightBtn->click();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Knight);
    }
    {
        PromotionDialog dialog(Rules::Color::White);
        auto *queenBtn = dialog.findChild<QToolButton *>(QStringLiteral("queenButton"));
        QVERIFY(queenBtn != nullptr);
        queenBtn->click();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Queen);
    }
}

void PromotionDialogTest::testPieceSelectionByKeyPress() {
    // Test English keys
    {
        PromotionDialog dialog(Rules::Color::White);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_N);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Knight);
    }
    {
        PromotionDialog dialog(Rules::Color::White);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_R);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Rook);
    }
    {
        PromotionDialog dialog(Rules::Color::White);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_B);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Bishop);
    }
    {
        PromotionDialog dialog(Rules::Color::White);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_Q);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Queen);
    }

    // Test French shortcut keys (D, T, F, C)
    {
        PromotionDialog dialog(Rules::Color::Black);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_C);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Knight);
    }
    {
        PromotionDialog dialog(Rules::Color::Black);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_T);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Rook);
    }
    {
        PromotionDialog dialog(Rules::Color::Black);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_F);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Bishop);
    }
    {
        PromotionDialog dialog(Rules::Color::Black);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_D);
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.selectedPiece(), Rules::PieceType::Queen);
    }
}

void PromotionDialogTest::testCancelByEscape() {
    PromotionDialog dialog(Rules::Color::White);
    dialog.show();
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
}

QTEST_MAIN(PromotionDialogTest)
#include "promotiondialog_test.moc"
