//
// Unit tests for the evaluator settings dialog.
//

#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

#include "evalparamfields.h"
#include "evalparamsdialog.h"
#include "heuristiceval.h"

class EvalParamsDialogTest : public QObject {
    Q_OBJECT

private slots:
    void testShowsEveryWeight();
    void testEditedValuesAreReported();
    void testSearchLimits();
    void testRestoreDefaults();
};

void EvalParamsDialogTest::testShowsEveryWeight() {
    HeuristicEval::EvalParams params;
    params.pawnValue = 111;
    params.isolatedPawnPenaltyEG = 42;
    params.mopUpEdgeBonus = 7;

    EvalParamsDialog dialog(params, 0, 2);

    // Every field of the shared table has a spin box showing the current value
    // and offering the default in its tooltip.
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        const QString name = QStringLiteral("evalParam_%1")
                                 .arg(QString::fromLatin1(field.key));
        auto *spin = dialog.findChild<QSpinBox *>(name);
        QVERIFY2(spin != nullptr, qPrintable(name));
        QCOMPARE(spin->value(), params.*field.member);
        QCOMPARE(spin->minimum(), field.minimum);
        QCOMPARE(spin->maximum(), field.maximum);
        QVERIFY(spin->toolTip().contains(
            QString::number(HeuristicEval::EvalParams{}.*field.member)));
    }

    auto *pawnSpin = dialog.findChild<QSpinBox *>(QStringLiteral("evalParam_pawnValue"));
    QVERIFY(pawnSpin != nullptr);
    QCOMPARE(pawnSpin->value(), 111);
}

void EvalParamsDialogTest::testEditedValuesAreReported() {
    EvalParamsDialog dialog(HeuristicEval::EvalParams{}, 0, 2);

    auto *knightSpin = dialog.findChild<QSpinBox *>(QStringLiteral("evalParam_knightValue"));
    auto *rookSpin = dialog.findChild<QSpinBox *>(QStringLiteral("evalParam_rookSeventhRankEG"));
    QVERIFY(knightSpin != nullptr);
    QVERIFY(rookSpin != nullptr);

    knightSpin->setValue(350);
    rookSpin->setValue(45);

    const HeuristicEval::EvalParams reported = dialog.params();
    QCOMPARE(reported.knightValue, 350);
    QCOMPARE(reported.rookSeventhRankEG, 45);

    // Everything else keeps the value the dialog was built with.
    const HeuristicEval::EvalParams defaults;
    QCOMPARE(reported.pawnValue, defaults.pawnValue);
    QCOMPARE(reported.kingShelterEndgamePercent,
             defaults.kingShelterEndgamePercent);
}

void EvalParamsDialogTest::testSearchLimits() {
    EvalParamsDialog dialog(HeuristicEval::EvalParams{}, 3, 4);

    auto *threadsSpin = dialog.findChild<QSpinBox *>(QStringLiteral("evalThreadsSpin"));
    auto *depthSpin = dialog.findChild<QSpinBox *>(QStringLiteral("evalDepthSpin"));
    QVERIFY(threadsSpin != nullptr);
    QVERIFY(depthSpin != nullptr);

    QCOMPARE(threadsSpin->value(), 3);
    QCOMPARE(threadsSpin->minimum(), 0);
    QCOMPARE(threadsSpin->specialValueText(), QStringLiteral("Automatic"));
    QCOMPARE(dialog.searchThreads(), 3);

    QCOMPARE(depthSpin->value(), 4);
    QCOMPARE(depthSpin->minimum(), 1);
    QCOMPARE(depthSpin->maximum(), HeuristicEval::MaxSearchDepth);
    QCOMPARE(dialog.evaluationDepth(), 4);

    // Out-of-range stored values are clamped instead of refused.
    EvalParamsDialog clamped(HeuristicEval::EvalParams{}, 99, 0);
    QCOMPARE(clamped.searchThreads(), 16);
    QCOMPARE(clamped.evaluationDepth(), 1);
}

void EvalParamsDialogTest::testRestoreDefaults() {
    HeuristicEval::EvalParams tuned;
    tuned.queenValue = 1000;
    tuned.mobilityRookEG = 9;
    EvalParamsDialog dialog(tuned, 2, 4);

    auto *restore = dialog.findChild<QPushButton *>(
        QStringLiteral("restoreDefaultsButton"));
    QVERIFY(restore != nullptr);
    restore->click();

    const HeuristicEval::EvalParams defaults;
    const HeuristicEval::EvalParams reported = dialog.params();
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        QCOMPARE(reported.*field.member, defaults.*field.member);
    }
    QCOMPARE(dialog.searchThreads(), 0);
    QCOMPARE(dialog.evaluationDepth(), 2);

    // The restore also put the widgets back, not only the returned struct.
    auto *queenSpin = dialog.findChild<QSpinBox *>(QStringLiteral("evalParam_queenValue"));
    QVERIFY(queenSpin != nullptr);
    QCOMPARE(queenSpin->value(), defaults.queenValue);
}

QTEST_MAIN(EvalParamsDialogTest)
#include "evalparamsdialog_test.moc"
