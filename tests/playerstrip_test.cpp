//
// Unit tests for the player strip widget.
//

#include <QLabel>
#include <QTest>
#include <algorithm>

#include "playerstrip.h"

class PlayerStripTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaults();
    void testPlayerName();
    void testTurnIndicator();
    void testMaterialDifference();
};

void PlayerStripTest::testDefaults() {
    PlayerStrip strip(PendulumWidget::PieceColor::White);

    QCOMPARE(strip.color(), PendulumWidget::PieceColor::White);
    QVERIFY(strip.clock() != nullptr);
    QCOMPARE(strip.clock()->pieceColor(), PendulumWidget::PieceColor::White);
    QVERIFY(strip.playerName().isEmpty());
    QVERIFY(!strip.isActive());
    QCOMPARE(strip.materialDifference(), 0);

    // The colour is the fallback name while the PGN carries no White tag.
    const auto labels = strip.findChildren<QLabel *>();
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("White");
    }));
}

void PlayerStripTest::testPlayerName() {
    PlayerStrip strip(PendulumWidget::PieceColor::Black);
    auto *label = strip.findChild<QLabel *>(QStringLiteral("playerNameLabel"));
    QVERIFY(label != nullptr);

    strip.setPlayerName(QStringLiteral("Stockfish 18"));
    QCOMPARE(strip.playerName(), QStringLiteral("Stockfish 18"));
    QCOMPARE(label->text(), QStringLiteral("Stockfish 18"));
    QCOMPARE(label->toolTip(), QStringLiteral("Stockfish 18"));

    // An empty name restores the colour fallback.
    strip.setPlayerName(QString());
    QVERIFY(strip.playerName().isEmpty());
    QCOMPARE(label->text(), QStringLiteral("Black"));
}

void PlayerStripTest::testTurnIndicator() {
    PlayerStrip strip(PendulumWidget::PieceColor::Black);
    auto *indicator = strip.findChild<QLabel *>(QStringLiteral("turnIndicator"));
    QVERIFY(indicator != nullptr);
    QVERIFY(!strip.isActive());
    QVERIFY(indicator->isHidden());

    strip.setActive(true);
    QVERIFY(strip.isActive());
    QVERIFY(!indicator->isHidden());

    strip.setActive(false);
    QVERIFY(!strip.isActive());
    QVERIFY(indicator->isHidden());
}

void PlayerStripTest::testMaterialDifference() {
    PlayerStrip strip(PendulumWidget::PieceColor::White);
    auto *label = strip.findChild<QLabel *>(QStringLiteral("materialLabel"));
    QVERIFY(label != nullptr);
    QVERIFY(label->text().isEmpty());

    strip.setMaterialDifference(3);
    QCOMPARE(strip.materialDifference(), 3);
    QCOMPARE(label->text(), QStringLiteral("+3"));
    QVERIFY(!label->toolTip().isEmpty());

    // The trailing side shows nothing: the leading side carries the value.
    strip.setMaterialDifference(-2);
    QCOMPARE(strip.materialDifference(), -2);
    QVERIFY(label->text().isEmpty());
    QVERIFY(label->toolTip().isEmpty());

    strip.setMaterialDifference(0);
    QVERIFY(label->text().isEmpty());
}

QTEST_MAIN(PlayerStripTest)
#include "playerstrip_test.moc"
