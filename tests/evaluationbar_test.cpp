//
// Unit tests for EvaluationBar widget.
//

#include <QSignalSpy>
#include <QTest>
#include <QPainter>
#include <QPixmap>

#include "evaluationbar.h"

class EvaluationBarTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaultValues();
    void testSetValueAndClamping();
    void testSignals();
    void testColors();
    void testVisibilityFlags();
    void testScoreText();
    void testRendering();
    void testFlippedRendering();
};

void EvaluationBarTest::testDefaultValues() {
    EvaluationBar bar;
    QCOMPARE(bar.value(), 50.0);
    QCOMPARE(bar.evaluation(), 50.0);
    QCOMPARE(bar.whiteColor(), QColor("#ffffff"));
    QCOMPARE(bar.blackColor(), QColor("#312e2b"));
    QCOMPARE(bar.borderColor(), QColor("#5c4033"));
    QCOMPARE(bar.showCenterLine(), true);
    QCOMPARE(bar.showEvaluationText(), false);
    QVERIFY(bar.sizeHint().width() > 0);
    QVERIFY(bar.sizeHint().height() > 0);
    QVERIFY(bar.minimumSizeHint().width() > 0);
    QVERIFY(bar.minimumSizeHint().height() > 0);
}

void EvaluationBarTest::testSetValueAndClamping() {
    EvaluationBar bar;

    bar.setValue(75.5);
    QCOMPARE(bar.value(), 75.5);
    QCOMPARE(bar.evaluation(), 75.5);

    bar.setEvaluation(20.0);
    QCOMPARE(bar.value(), 20.0);
    QCOMPARE(bar.evaluation(), 20.0);

    // Below 0 should clamp to 0
    bar.setValue(-15.0);
    QCOMPARE(bar.value(), 0.0);

    // Above 100 should clamp to 100
    bar.setValue(125.0);
    QCOMPARE(bar.value(), 100.0);

    // Exact boundaries
    bar.setValue(0.0);
    QCOMPARE(bar.value(), 0.0);
    bar.setValue(100.0);
    QCOMPARE(bar.value(), 100.0);
}

void EvaluationBarTest::testSignals() {
    EvaluationBar bar;
    QSignalSpy valueSpy(&bar, &EvaluationBar::valueChanged);
    QSignalSpy evalSpy(&bar, &EvaluationBar::evaluationChanged);

    bar.setValue(80.0);
    QCOMPARE(valueSpy.count(), 1);
    QCOMPARE(evalSpy.count(), 1);
    QCOMPARE(valueSpy.takeFirst().at(0).toDouble(), 80.0);
    QCOMPARE(evalSpy.takeFirst().at(0).toDouble(), 80.0);

    // Setting same value should not re-emit
    bar.setValue(80.0);
    QCOMPARE(valueSpy.count(), 0);
    QCOMPARE(evalSpy.count(), 0);

    bar.setEvaluation(30.0);
    QCOMPARE(valueSpy.count(), 1);
    QCOMPARE(evalSpy.count(), 1);
    QCOMPARE(valueSpy.takeFirst().at(0).toDouble(), 30.0);
    QCOMPARE(evalSpy.takeFirst().at(0).toDouble(), 30.0);
}

void EvaluationBarTest::testColors() {
    EvaluationBar bar;
    const QColor customWhite("#f0d9b5");
    const QColor customBlack("#000000");
    const QColor customBorder("#ff0000");

    bar.setWhiteColor(customWhite);
    bar.setBlackColor(customBlack);
    bar.setBorderColor(customBorder);

    QCOMPARE(bar.whiteColor(), customWhite);
    QCOMPARE(bar.blackColor(), customBlack);
    QCOMPARE(bar.borderColor(), customBorder);
}

void EvaluationBarTest::testVisibilityFlags() {
    EvaluationBar bar;

    bar.setShowCenterLine(false);
    QCOMPARE(bar.showCenterLine(), false);

    bar.setShowEvaluationText(true);
    QCOMPARE(bar.showEvaluationText(), true);
}

void EvaluationBarTest::testScoreText() {
    EvaluationBar bar;

    // No score is drawn by default: the rounded percentage is used instead.
    QVERIFY(bar.scoreText().isEmpty());

    bar.setScoreText(QStringLiteral("+1.35"));
    QCOMPARE(bar.scoreText(), QStringLiteral("+1.35"));

    bar.setScoreText(QString());
    QVERIFY(bar.scoreText().isEmpty());

    // The bar must be wide enough to hold a score such as "+1.35".
    QVERIFY(bar.sizeHint().width() >= 32);
    QVERIFY(bar.minimumSizeHint().width() <= bar.sizeHint().width());
}

void EvaluationBarTest::testRendering() {
    EvaluationBar bar;
    bar.resize(bar.sizeHint().width(), 400);
    bar.setShowEvaluationText(true);

    QPixmap pixmap(bar.size());

    const double testValues[] = {0.0, 25.0, 50.0, 75.0, 100.0};
    for (double val : testValues) {
        bar.setValue(val);
        bar.setShowEvaluationText(false);
        bar.render(&pixmap);

        bar.setShowEvaluationText(true);
        bar.setScoreText(QStringLiteral("+1.35"));
        bar.render(&pixmap);

        bar.setScoreText(QString());
        bar.render(&pixmap);
    }
}

void EvaluationBarTest::testFlippedRendering() {
    EvaluationBar bar;
    bar.resize(bar.sizeHint().width(), 200);
    bar.setValue(75.0);
    QVERIFY(!bar.isFlipped());

    // Upright the bottom of the bar is White's side, so White's majority sits
    // below the boundary and the top of the bar is the smaller black section.
    QPixmap upright(bar.size());
    bar.render(&upright);
    QCOMPARE(upright.toImage().pixelColor(bar.width() / 2, 20), bar.blackColor());

    // Flipped, the camp at the bottom of the board is White, so the white
    // section moves to the top and keeps the same size.
    bar.setFlipped(true);
    QVERIFY(bar.isFlipped());
    QPixmap flipped(bar.size());
    bar.render(&flipped);
    QCOMPARE(flipped.toImage().pixelColor(bar.width() / 2, 20), bar.whiteColor());
    QVERIFY(upright.toImage() != flipped.toImage());

    // The score pill follows the section that holds the centre in both cases.
    bar.setShowEvaluationText(true);
    bar.setScoreText(QStringLiteral("+1.35"));
    bar.render(&flipped);
    bar.setValue(25.0);
    bar.render(&flipped);
}

QTEST_MAIN(EvaluationBarTest)
#include "evaluationbar_test.moc"
