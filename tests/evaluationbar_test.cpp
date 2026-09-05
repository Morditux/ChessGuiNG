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
    void testRendering();
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

void EvaluationBarTest::testRendering() {
    EvaluationBar bar;
    bar.resize(13, 400);

    QPixmap pixmap(bar.size());

    const double testValues[] = {0.0, 25.0, 50.0, 75.0, 100.0};
    for (double val : testValues) {
        bar.setValue(val);
        bar.setShowEvaluationText(false);
        bar.render(&pixmap);

        bar.setShowEvaluationText(true);
        bar.render(&pixmap);
    }
}

QTEST_MAIN(EvaluationBarTest)
#include "evaluationbar_test.moc"
