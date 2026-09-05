//
// Unit tests for the PendulumWidget countdown.
//

#include <QSignalSpy>
#include <QTest>

#include "pendulumwidget.h"

class PendulumWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaultValues();
    void testSetAndReset();
    void testCountdown();
    void testColors();
    void testRendering();
};

void PendulumWidgetTest::testDefaultValues() {
    PendulumWidget pendulum;

    QCOMPARE(pendulum.remainingMilliseconds(), 0LL);
    QCOMPARE(pendulum.displayText(), QStringLiteral("00:00"));
    QCOMPARE(pendulum.isRunning(), false);
    QCOMPARE(pendulum.pieceColor(), PendulumWidget::PieceColor::White);
    QVERIFY(pendulum.sizeHint().width() > 0);
    QVERIFY(pendulum.sizeHint().height() > 0);
}

void PendulumWidgetTest::testSetAndReset() {
    PendulumWidget pendulum(PendulumWidget::PieceColor::Black);

    pendulum.set(1, 0, 0);
    QCOMPARE(pendulum.remainingMilliseconds(), 3600000LL);
    QCOMPARE(pendulum.displayText(), QStringLiteral("01:00:00"));

    pendulum.set(0, 1, 5);
    QCOMPARE(pendulum.remainingMilliseconds(), 65000LL);
    QCOMPARE(pendulum.displayText(), QStringLiteral("01:05"));

    pendulum.start();
    QTest::qWait(120);
    pendulum.stop();
    QVERIFY(pendulum.remainingMilliseconds() < 65000LL);
    QVERIFY(pendulum.remainingMilliseconds() > 63000LL);

    pendulum.reset();
    QCOMPARE(pendulum.remainingMilliseconds(), 65000LL);
    QCOMPARE(pendulum.displayText(), QStringLiteral("01:05"));
    QCOMPARE(pendulum.isRunning(), false);
}

void PendulumWidgetTest::testCountdown() {
    PendulumWidget pendulum;
    QSignalSpy runningSpy(&pendulum, &PendulumWidget::runningChanged);

    pendulum.set(0, 0, 1);
    pendulum.start();
    QCOMPARE(pendulum.isRunning(), true);
    QCOMPARE(runningSpy.count(), 1);

    QTest::qWait(1200);

    QCOMPARE(pendulum.remainingMilliseconds(), 0LL);
    QCOMPARE(pendulum.displayText(), QStringLiteral("00:00"));
    QCOMPARE(pendulum.isRunning(), false);
    QCOMPARE(runningSpy.count(), 2);
    QCOMPARE(runningSpy.at(1).at(0).toBool(), false);
}

void PendulumWidgetTest::testColors() {
    PendulumWidget pendulum(PendulumWidget::PieceColor::White);
    const QColor whiteBackground = pendulum.backgroundColor();
    const QColor whiteDisplay = pendulum.displayColor();

    pendulum.setPieceColor(PendulumWidget::PieceColor::Black);
    QVERIFY(pendulum.backgroundColor() != whiteBackground);
    QVERIFY(pendulum.displayColor() != whiteDisplay);
    QCOMPARE(pendulum.pieceColor(), PendulumWidget::PieceColor::Black);
}

void PendulumWidgetTest::testRendering() {
    PendulumWidget pendulum;
    pendulum.resize(220, 60);

    QPixmap pixmap(pendulum.size());
    pendulum.render(&pixmap);
    pendulum.set(1, 0, 0);
    pendulum.render(&pixmap);
}

QTEST_MAIN(PendulumWidgetTest)
#include "pendulumwidget_test.moc"
