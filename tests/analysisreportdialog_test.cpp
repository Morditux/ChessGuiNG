//
// Unit tests for the enriched analysis-report dialog and its accuracy helper.
//

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "analysisreportdialog.h"
#include "appconfig.h"
#include "auditreports.h"
#include "gamecontroller.h"
#include "mainwindow.h"

namespace {

AuditReport sampleReport() {
    AuditReport report;
    report.valid = true;
    report.depth = 18;
    report.plies = 4;
    report.analysedPositions = 5;
    report.white.accuracy = 78.5;
    report.white.averageCentipawnLoss = 42.0;
    report.white.scoredMoves = 2;
    report.white.centipawnMoves = 2;
    report.white.inaccuracies = 1;
    report.white.blunders = 1;
    report.black.accuracy = 95.25;
    report.black.averageCentipawnLoss = 8.0;
    report.black.scoredMoves = 2;
    report.black.centipawnMoves = 2;
    report.black.mistakes = 1;
    report.sanByPly = {QString(), QStringLiteral("e4"), QStringLiteral("e5"),
                       QStringLiteral("Nf3"), QStringLiteral("Nc6")};
    report.centipawnLossByPly = {-1, 60, 0, 0, 0};

    AuditFinding inaccuracy;
    inaccuracy.severity = AuditSeverity::Inaccuracy;
    inaccuracy.ply = 1;
    inaccuracy.centipawnLoss = 60;
    inaccuracy.bestMove = QStringLiteral("d2d4");
    inaccuracy.bestMoveSan = QStringLiteral("d4");
    report.findings.append(inaccuracy);

    AuditFinding mate;
    mate.severity = AuditSeverity::Blunder;
    mate.ply = 4;
    mate.forcedMate = true;
    mate.bestMove = QStringLiteral("g1f3");
    mate.bestMoveSan = QStringLiteral("Nf3");
    report.findings.append(mate);

    return report;
}

} // namespace

class AnalysisReportDialogTest : public QObject {
    Q_OBJECT

private slots:
    void testEmptyState();
    void testReportRendering();
    void testFindingsTable();
    void testRunningState();
    void testAnalysisAvailability();
    void testDepthSignal();
    void testPlyActivation();
    void testAccuracyFormula();
    void testMainWindowIntegration();
};

void AnalysisReportDialogTest::testEmptyState() {
    AnalysisReportDialog dialog;
    QCOMPARE(dialog.windowTitle(), QStringLiteral("Analysis report"));
    QVERIFY(dialog.depthSpin() != nullptr);
    QCOMPARE(dialog.depth(), 18);
    QCOMPARE(dialog.depthSpin()->minimum(), 6);
    QCOMPARE(dialog.depthSpin()->maximum(), 40);

    // Without a report the dialog explains what to do and shows nothing else.
    QVERIFY(dialog.emptyStateLabel()->isVisibleTo(&dialog));
    QVERIFY(!dialog.summaryLabel()->isVisibleTo(&dialog));
    QCOMPARE(dialog.findingsTable()->rowCount(), 0);
    QCOMPARE(dialog.runButton()->text(), QStringLiteral("Run analysis"));
}

void AnalysisReportDialogTest::testReportRendering() {
    AnalysisReportDialog dialog;
    dialog.setReport(sampleReport());

    QVERIFY(!dialog.emptyStateLabel()->isVisibleTo(&dialog));
    QVERIFY(dialog.summaryLabel()->isVisibleTo(&dialog));
    QVERIFY(dialog.summaryLabel()->text().contains(QStringLiteral("18")));
    QVERIFY(dialog.summaryLabel()->text().contains(QStringLiteral("5")));
    QVERIFY(dialog.summaryLabel()->text().contains(QStringLiteral("2")));

    QVERIFY(dialog.whiteStatsLabel()->text().contains(QStringLiteral("78.5")));
    QVERIFY(dialog.whiteStatsLabel()->text().contains(QStringLiteral("42")));
    QVERIFY(dialog.whiteStatsLabel()->text().contains(
        QStringLiteral("Inaccuracies 1 — mistakes 0 — blunders 1")));

    QVERIFY(dialog.blackStatsLabel()->text().contains(QStringLiteral("95.3")));
    QVERIFY(dialog.blackStatsLabel()->text().contains(QStringLiteral("8")));
    QVERIFY(dialog.blackStatsLabel()->text().contains(
        QStringLiteral("Inaccuracies 0 — mistakes 1 — blunders 0")));
}

void AnalysisReportDialogTest::testFindingsTable() {
    AnalysisReportDialog dialog;
    dialog.setReport(sampleReport());

    QTableWidget *table = dialog.findingsTable();
    QCOMPARE(table->columnCount(), 5);
    QCOMPARE(table->rowCount(), 2);

    // First row: the inaccuracy of White's first move.
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("1"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("e4"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Inaccuracy"));
    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("60"));
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("d4"));
    QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toInt(), 1);

    // A forced mate has no centipawn loss to show.
    QCOMPARE(table->item(1, 2)->text(), QStringLiteral("Blunder"));
    QCOMPARE(table->item(1, 3)->text(), QStringLiteral("-"));
    QCOMPARE(table->item(1, 4)->text(), QStringLiteral("Nf3"));

    // A report that becomes invalid clears the rows again.
    dialog.setReport(AuditReport{});
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(!dialog.summaryLabel()->isVisibleTo(&dialog));
}

void AnalysisReportDialogTest::testRunningState() {
    AnalysisReportDialog dialog;
    QSignalSpy runSpy(&dialog, &AnalysisReportDialog::runRequested);
    QSignalSpy cancelSpy(&dialog, &AnalysisReportDialog::cancelRequested);

    dialog.setAnalysisAvailable(true);
    QVERIFY(dialog.runButton()->isEnabled());

    dialog.runButton()->click();
    QCOMPARE(runSpy.count(), 1);
    QCOMPARE(cancelSpy.count(), 0);

    dialog.setAnalysisRunning(true);
    QCOMPARE(dialog.runButton()->text(), QStringLiteral("Cancel analysis"));
    QVERIFY(!dialog.depthSpin()->isEnabled());
    dialog.setProgress(3, 10);
    QVERIFY(dialog.progressLabel()->text().contains(QStringLiteral("3")));
    QVERIFY(dialog.progressLabel()->text().contains(QStringLiteral("10")));

    dialog.runButton()->click();
    QCOMPARE(cancelSpy.count(), 1);
    QCOMPARE(runSpy.count(), 1);

    dialog.setAnalysisRunning(false);
    QCOMPARE(dialog.runButton()->text(), QStringLiteral("Run analysis"));
    QVERIFY(dialog.depthSpin()->isEnabled());
    QVERIFY(dialog.progressLabel()->text().isEmpty());
}

void AnalysisReportDialogTest::testAnalysisAvailability() {
    AnalysisReportDialog dialog;

    // Nothing to analyse and no engine: the run button stays disabled.
    QVERIFY(!dialog.runButton()->isEnabled());
    dialog.setAnalysisAvailable(true);
    QVERIFY(dialog.runButton()->isEnabled());
    dialog.setAnalysisAvailable(false);
    QVERIFY(!dialog.runButton()->isEnabled());

    // A running analysis can always be cancelled.
    dialog.setAnalysisRunning(true);
    QVERIFY(dialog.runButton()->isEnabled());
}

void AnalysisReportDialogTest::testDepthSignal() {
    AnalysisReportDialog dialog;
    QSignalSpy depthSpy(&dialog, &AnalysisReportDialog::depthChanged);

    dialog.depthSpin()->setValue(24);
    QCOMPARE(dialog.depth(), 24);
    QCOMPARE(depthSpy.count(), 1);
    QCOMPARE(depthSpy.last().at(0).toInt(), 24);

    // The programmatic setter restores a stored value without announcing it.
    dialog.setDepth(30);
    QCOMPARE(dialog.depth(), 30);
    QCOMPARE(depthSpy.count(), 1);

    dialog.setDepthRange(10, 20);
    QCOMPARE(dialog.depthSpin()->minimum(), 10);
    QCOMPARE(dialog.depthSpin()->maximum(), 20);
}

void AnalysisReportDialogTest::testPlyActivation() {
    AnalysisReportDialog dialog;
    dialog.setReport(sampleReport());

    QSignalSpy plySpy(&dialog, &AnalysisReportDialog::plyActivated);
    QTableWidget *table = dialog.findingsTable();

    // Clicking a row reports the ply it belongs to.
    QVERIFY(QMetaObject::invokeMethod(table, "cellClicked",
                                      Q_ARG(int, 1), Q_ARG(int, 0)));
    QCOMPARE(plySpy.count(), 1);
    QCOMPARE(plySpy.last().at(0).toInt(), 4);
}



void AnalysisReportDialogTest::testAccuracyFormula() {
    // A perfect move keeps the whole accuracy, and losing winning percentage
    // lowers it monotonically down to zero.
    QVERIFY(qAbs(AuditReports::moveAccuracy(0.0) - 100.0) < 0.01);
    const double small = AuditReports::moveAccuracy(5.0);
    const double medium = AuditReports::moveAccuracy(20.0);
    const double large = AuditReports::moveAccuracy(60.0);
    QVERIFY(small < 100.0);
    QVERIFY(medium < small);
    QVERIFY(large < medium);
    QVERIFY(large >= 0.0);
    QCOMPARE(AuditReports::moveAccuracy(1000.0), 0.0);
    QVERIFY(qAbs(AuditReports::moveAccuracy(-4.0) - 100.0) < 0.01);
}

void AnalysisReportDialogTest::testMainWindowIntegration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString confPath = dir.filePath(QStringLiteral("chessGui.conf"));

    MainWindow window(nullptr, confPath);
    QVERIFY(window.analysisReportAction() != nullptr);
    QCOMPARE(window.gameController()->gameAuditDepth(), 18);

    // Opening the report creates it once and shows the empty state: there is
    // no engine and no game to analyse yet.
    window.analysisReportAction()->trigger();
    auto *dialog = window.findChild<AnalysisReportDialog *>();
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->depth(), 18);
    QVERIFY(!dialog->runButton()->isEnabled());
    QVERIFY(dialog->emptyStateLabel()->isVisibleTo(dialog));

    window.analysisReportAction()->trigger();
    QCOMPARE(window.findChildren<AnalysisReportDialog *>().size(), 1);

    // Changing the depth in the dialog drives the controller and the config.
    dialog->depthSpin()->setValue(24);
    QCOMPARE(window.gameController()->gameAuditDepth(), 24);

    AppConfig reloaded(confPath);
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.auditDepth(), 24);
}

QTEST_MAIN(AnalysisReportDialogTest)
#include "analysisreportdialog_test.moc"