//
// Unit tests for EngineOutputWidget.
//

#include <QSignalSpy>
#include <QTest>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>

#include "engineoutputwidget.h"
#include "mainwindow.h"
#include "movelistwidget.h"

class EngineOutputWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaultValues();
    void testSettersAndGetters();
    void testUpdateAnalysisLine();
    void testSetAnalysisLines();
    void testCurrmoveLineDoesNotClearAnalysis();
    void testScoreFormatting();
    void testLogHandling();
    void testClearFunctions();
    void testSignalEmission();
    void testControlButtons();
    void testControlButtonSignals();
    void testSectionCollapse();
    void testSectionCollapseKeepsData();
    void testDetailsNavigationAndPrimaryVariation();
    void testDetailsLayout();
    void testMainWindowLayout();
};

void EngineOutputWidgetTest::testDefaultValues() {
    EngineOutputWidget widget;
    QCOMPARE(widget.engineName(), QStringLiteral("UCI Engine"));
    QCOMPARE(widget.engineStatus(), QStringLiteral("Disconnected"));
    QCOMPARE(widget.depth(), 0);
    QCOMPARE(widget.seldepth(), 0);
    QCOMPARE(widget.scoreText(), QStringLiteral("-"));
    QCOMPARE(widget.nodes(), 0);
    QCOMPARE(widget.nodesPerSecond(), 0);
    QCOMPARE(widget.time(), 0);
    QCOMPARE(widget.bestMove(), QStringLiteral("-"));
    QVERIFY(widget.analysisLines().isEmpty());
    QVERIFY(widget.uciLog().isEmpty());
    QVERIFY(widget.sizeHint().width() > 0);
    QVERIFY(widget.sizeHint().height() > 0);
    QVERIFY(widget.minimumSizeHint().width() > 0);
    QVERIFY(widget.minimumSizeHint().height() > 0);
    QVERIFY(!widget.analysisSectionVisible());
    QVERIFY(!widget.logSectionVisible());
    QVERIFY(!widget.detailsVisible());
    QVERIFY(widget.detailsToggleButton() != nullptr);
    QVERIFY(widget.analysisToggleButton() != nullptr);
    QCOMPARE(widget.analysisToggleButton()->isChecked(), true);
    QVERIFY(widget.logToggleButton() != nullptr);
    QCOMPARE(widget.logToggleButton()->isChecked(), false);
    QVERIFY(widget.pvTable() != nullptr);
    QCOMPARE(widget.pvTable()->columnCount(), 5);

    QVERIFY(widget.startButton() != nullptr);
    QCOMPARE(widget.startButton()->text(), QStringLiteral("Start"));
    QVERIFY(!widget.startButton()->isEnabled());

    QVERIFY(widget.pauseButton() != nullptr);
    QCOMPARE(widget.pauseButton()->text(), QStringLiteral("Pause"));
    QVERIFY(!widget.pauseButton()->isEnabled());

    QVERIFY(widget.stopButton() != nullptr);
    QCOMPARE(widget.stopButton()->text(), QStringLiteral("Stop"));
    QVERIFY(!widget.stopButton()->isEnabled());
}

void EngineOutputWidgetTest::testSettersAndGetters() {
    EngineOutputWidget widget;

    widget.setEngineName(QStringLiteral("Stockfish 16"));
    QCOMPARE(widget.engineName(), QStringLiteral("Stockfish 16"));

    widget.setEngineStatus(QStringLiteral("Searching"));
    QCOMPARE(widget.engineStatus(), QStringLiteral("Searching"));

    widget.setDepth(20, 28);
    QCOMPARE(widget.depth(), 20);
    QCOMPARE(widget.seldepth(), 28);

    widget.setScore(45);
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.45"));

    widget.setScore(-120);
    QCOMPARE(widget.scoreText(), QStringLiteral("-1.20"));

    widget.setScore(0, 3);
    QCOMPARE(widget.scoreText(), QStringLiteral("+M3"));

    widget.setScore(0, -2);
    QCOMPARE(widget.scoreText(), QStringLiteral("-M2"));

    widget.setScoreText(QStringLiteral("+0.75"));
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.75"));

    widget.setNodes(1500000);
    QCOMPARE(widget.nodes(), 1500000);

    widget.setNodesPerSecond(2000000);
    QCOMPARE(widget.nodesPerSecond(), 2000000);

    widget.setTime(1500);
    QCOMPARE(widget.time(), 1500);

    widget.setBestMove(QStringLiteral("e2e4"));
    QCOMPARE(widget.bestMove(), QStringLiteral("e2e4"));
}

void EngineOutputWidgetTest::testUpdateAnalysisLine() {
    EngineOutputWidget widget;

    EngineAnalysisLine line1;
    line1.multipv = 1;
    line1.depth = 18;
    line1.seldepth = 24;
    line1.scoreCp = 65;
    line1.nodes = 500000;
    line1.nps = 1000000;
    line1.timeMs = 500;
    line1.pv = QStringLiteral("e2e4 c7c5 g1f3");

    widget.updateAnalysisLine(line1);

    QCOMPARE(widget.depth(), 18);
    QCOMPARE(widget.seldepth(), 24);
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.65"));
    QCOMPARE(widget.nodes(), 500000);
    QCOMPARE(widget.nodesPerSecond(), 1000000);
    QCOMPARE(widget.time(), 500);
    QCOMPARE(widget.bestMove(), QStringLiteral("e2e4"));
    QCOMPARE(widget.analysisLines().size(), 1);
    QCOMPARE(widget.pvTable()->rowCount(), 1);
    QCOMPARE(widget.pvTable()->item(0, 0)->text(), QStringLiteral("1"));
    QCOMPARE(widget.pvTable()->item(0, 1)->text(), QStringLiteral("+0.65"));
    QCOMPARE(widget.pvTable()->item(0, 4)->text(), QStringLiteral("e2e4 c7c5 g1f3"));

    // Add multiPV 2
    EngineAnalysisLine line2;
    line2.multipv = 2;
    line2.depth = 18;
    line2.seldepth = 22;
    line2.scoreCp = 20;
    line2.nodes = 500000;
    line2.nps = 1000000;
    line2.timeMs = 500;
    line2.pv = QStringLiteral("d2d4 d7d5 c2c4");

    widget.updateAnalysisLine(line2);
    QCOMPARE(widget.analysisLines().size(), 2);
    QCOMPARE(widget.pvTable()->rowCount(), 2);
    QCOMPARE(widget.pvTable()->item(1, 0)->text(), QStringLiteral("2"));
    QCOMPARE(widget.pvTable()->item(1, 1)->text(), QStringLiteral("+0.20"));
}

void EngineOutputWidgetTest::testSetAnalysisLines() {
    EngineOutputWidget widget;

    EngineAnalysisLine line1{1, 15, 20, 30.0, std::nullopt, 100000, 800000, 125, QStringLiteral("e2e4 e7e5")};
    EngineAnalysisLine line2{2, 15, 18, -10.0, std::nullopt, 100000, 800000, 125, QStringLiteral("c2c4 e7e5")};

    widget.setAnalysisLines({line2, line1}); // Unsorted input should be sorted by multipv

    QCOMPARE(widget.analysisLines().size(), 2);
    QCOMPARE(widget.analysisLines().at(0).multipv, 1);
    QCOMPARE(widget.analysisLines().at(1).multipv, 2);
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.30"));
    QCOMPARE(widget.bestMove(), QStringLiteral("e2e4"));
}

void EngineOutputWidgetTest::testCurrmoveLineDoesNotClearAnalysis() {
    EngineOutputWidget widget;

    // First, regular PV line
    EngineAnalysisLine fullLine;
    fullLine.multipv = 1;
    fullLine.depth = 20;
    fullLine.scoreCp = 45.0;
    fullLine.pv = QStringLiteral("e2e4 e7e5");
    widget.updateAnalysisLine(fullLine);

    QCOMPARE(widget.depth(), 20);
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.45"));
    QCOMPARE(widget.pvTable()->item(0, 1)->text(), QStringLiteral("+0.45"));
    QCOMPARE(widget.pvTable()->item(0, 4)->text(), QStringLiteral("e2e4 e7e5"));

    // Now, incoming currmove line (like Stockfish "info depth 35 currmove f2f4 currmovenumber 1")
    EngineAnalysisLine currLine;
    currLine.multipv = 1;
    currLine.depth = 35;
    currLine.currmove = QStringLiteral("f2f4");
    currLine.currmovenumber = 1;
    widget.updateAnalysisLine(currLine);

    // Depth should be updated, but PV and Score must NOT be wiped out!
    QCOMPARE(widget.depth(), 35);
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.45"));
    QCOMPARE(widget.pvTable()->item(0, 1)->text(), QStringLiteral("+0.45"));
    QCOMPARE(widget.pvTable()->item(0, 4)->text(), QStringLiteral("e2e4 e7e5"));
}

void EngineOutputWidgetTest::testScoreFormatting() {
    EngineOutputWidget widget;

    widget.setScore(0.0);
    QCOMPARE(widget.scoreText(), QStringLiteral("0.00"));

    widget.setScore(150.0);
    QCOMPARE(widget.scoreText(), QStringLiteral("+1.50"));

    widget.setScore(-75.0);
    QCOMPARE(widget.scoreText(), QStringLiteral("-0.75"));

    widget.setScore(0, 1);
    QCOMPARE(widget.scoreText(), QStringLiteral("+M1"));

    widget.setScore(0, -4);
    QCOMPARE(widget.scoreText(), QStringLiteral("-M4"));
}

void EngineOutputWidgetTest::testLogHandling() {
    EngineOutputWidget widget;

    widget.appendUciLog(QStringLiteral("uci"));
    widget.appendUciLog(QStringLiteral("isready"));
    widget.appendUciLog(QStringLiteral("readyok"));

    QVERIFY(widget.uciLog().contains(QStringLiteral("uci")));
    QVERIFY(widget.uciLog().contains(QStringLiteral("isready")));
    QVERIFY(widget.uciLog().contains(QStringLiteral("readyok")));

    widget.clearLog();
    QVERIFY(widget.uciLog().trimmed().isEmpty());
}

void EngineOutputWidgetTest::testClearFunctions() {
    EngineOutputWidget widget;

    widget.setDepth(15, 20);
    widget.setScore(100.0);
    widget.setNodes(50000);
    widget.appendUciLog(QStringLiteral("info depth 15"));

    widget.clearAnalysis();
    QCOMPARE(widget.depth(), 0);
    QCOMPARE(widget.scoreText(), QStringLiteral("-"));
    QCOMPARE(widget.nodes(), 0);
    QVERIFY(!widget.uciLog().isEmpty()); // Log not cleared by clearAnalysis

    widget.clear();
    QVERIFY(widget.uciLog().trimmed().isEmpty());
}

void EngineOutputWidgetTest::testSignalEmission() {
    EngineOutputWidget widget;
    widget.resize(800, 220);
    widget.show();
    QCoreApplication::processEvents();
    QSignalSpy spy(&widget, &EngineOutputWidget::lineSelected);

    EngineAnalysisLine line;
    line.multipv = 1;
    line.pv = QStringLiteral("e2e4 e7e5");
    widget.updateAnalysisLine(line);

    widget.setDetailsVisible(true);
    widget.pvTable()->setCurrentCell(0, 0);
    widget.pvTable()->setFocus();
    QTest::keyClick(widget.pvTable(), Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
}

void EngineOutputWidgetTest::testControlButtons() {
    EngineOutputWidget widget;

    widget.setControlButtonsEnabled(true, false, true);
    QVERIFY(widget.startButton()->isEnabled());
    QVERIFY(!widget.pauseButton()->isEnabled());
    QVERIFY(widget.stopButton()->isEnabled());

    widget.setControlButtonsEnabled(false, true, false);
    QVERIFY(!widget.startButton()->isEnabled());
    QVERIFY(widget.pauseButton()->isEnabled());
    QVERIFY(!widget.stopButton()->isEnabled());

    widget.setControlButtonsEnabled(false, false, false);
    QVERIFY(!widget.startButton()->isEnabled());
    QVERIFY(!widget.pauseButton()->isEnabled());
    QVERIFY(!widget.stopButton()->isEnabled());
}

void EngineOutputWidgetTest::testControlButtonSignals() {
    EngineOutputWidget widget;
    widget.setControlButtonsEnabled(true, true, true);

    QSignalSpy startSpy(&widget, &EngineOutputWidget::startClicked);
    QSignalSpy pauseSpy(&widget, &EngineOutputWidget::pauseClicked);
    QSignalSpy stopSpy(&widget, &EngineOutputWidget::stopClicked);

    widget.startButton()->click();
    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(pauseSpy.count(), 0);
    QCOMPARE(stopSpy.count(), 0);

    widget.pauseButton()->click();
    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(pauseSpy.count(), 1);
    QCOMPARE(stopSpy.count(), 0);

    widget.stopButton()->click();
    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(pauseSpy.count(), 1);
    QCOMPARE(stopSpy.count(), 1);
}

void EngineOutputWidgetTest::testSectionCollapse() {
    EngineOutputWidget widget;
    QSignalSpy analysisSpy(&widget, &EngineOutputWidget::analysisSectionToggled);
    QSignalSpy logSpy(&widget, &EngineOutputWidget::logSectionToggled);
    QSignalSpy detailsSpy(&widget, &EngineOutputWidget::detailsToggled);

    QVERIFY(!widget.detailsVisible());
    widget.detailsToggleButton()->click();
    QVERIFY(widget.detailsVisible());
    QVERIFY(widget.analysisSectionVisible());
    QVERIFY(!widget.logSectionVisible());
    QCOMPARE(detailsSpy.count(), 1);
    QCOMPARE(detailsSpy.takeFirst().at(0).toBool(), true);

    widget.logToggleButton()->click();
    QCOMPARE(widget.detailsPage(), EngineOutputWidget::DetailsPage::UciLog);
    QVERIFY(!widget.analysisSectionVisible());
    QVERIFY(widget.logSectionVisible());

    const int expandedHeight = widget.sizeHint().height();
    widget.detailsToggleButton()->click();
    QVERIFY(!widget.detailsVisible());
    QVERIFY(widget.sizeHint().height() < expandedHeight);
    QVERIFY(widget.minimumSizeHint().height() < expandedHeight);
    QVERIFY(analysisSpy.count() > 0);
    QVERIFY(logSpy.count() > 0);
}

void EngineOutputWidgetTest::testSectionCollapseKeepsData() {
    EngineOutputWidget widget;

    EngineAnalysisLine line;
    line.multipv = 1;
    line.depth = 16;
    line.scoreCp = 55.0;
    line.pv = QStringLiteral("e2e4 e7e5");
    widget.updateAnalysisLine(line);
    widget.appendUciLog(QStringLiteral("readyok"));

    widget.setDetailsVisible(false);

    // Hidden sections still receive and store data
    EngineAnalysisLine line2;
    line2.multipv = 1;
    line2.depth = 18;
    line2.scoreCp = 60.0;
    line2.pv = QStringLiteral("d2d4 d7d5");
    widget.updateAnalysisLine(line2);
    widget.appendUciLog(QStringLiteral("uciok"));

    QCOMPARE(widget.depth(), 18);
    QCOMPARE(widget.scoreText(), QStringLiteral("+0.60"));
    QCOMPARE(widget.pvTable()->rowCount(), 1);
    QCOMPARE(widget.pvTable()->item(0, 4)->text(), QStringLiteral("d2d4 d7d5"));
    QVERIFY(widget.uciLog().contains(QStringLiteral("readyok")));
    QVERIFY(widget.uciLog().contains(QStringLiteral("uciok")));

    // Expanding the details view reports the selected page and keeps data.
    widget.setDetailsVisible(true);
    QVERIFY(widget.analysisSectionVisible());
    QVERIFY(!widget.logSectionVisible());
    QCOMPARE(widget.pvTable()->item(0, 4)->text(), QStringLiteral("d2d4 d7d5"));
}

void EngineOutputWidgetTest::testDetailsNavigationAndPrimaryVariation() {
    EngineOutputWidget widget;
    widget.resize(800, 220);
    widget.show();
    QCoreApplication::processEvents();
    auto *primaryPv = widget.findChild<QLabel *>(QStringLiteral("primaryPvLabel"));
    QVERIFY(primaryPv != nullptr);
    QCOMPARE(primaryPv->text(), QStringLiteral("<b>PV:</b> -"));

    EngineAnalysisLine first;
    first.multipv = 1;
    first.scoreCp = 42.0;
    first.pv = QStringLiteral("e2e4 e7e5 g1f3");
    widget.updateAnalysisLine(first);
    QVERIFY(primaryPv->text().contains(QStringLiteral("e2e4 e7e5 g1f3")));
    QCOMPARE(primaryPv->toolTip(), QStringLiteral("e2e4 e7e5 g1f3"));

    QSignalSpy pageSpy(&widget, &EngineOutputWidget::detailsPageChanged);
    widget.detailsToggleButton()->click();
    QVERIFY(widget.detailsVisible());
    widget.logToggleButton()->click();
    QCOMPARE(widget.detailsPage(), EngineOutputWidget::DetailsPage::UciLog);
    QCOMPARE(pageSpy.count(), 1);
    QCOMPARE(pageSpy.takeFirst().at(0).toInt(),
             static_cast<int>(EngineOutputWidget::DetailsPage::UciLog));
    QVERIFY(widget.logEdit()->isVisible());
    widget.detailsToggleButton()->click();
    QVERIFY(!widget.detailsVisible());
}

void EngineOutputWidgetTest::testDetailsLayout() {
    EngineOutputWidget widget;
    widget.resize(800, 320);
    widget.show();
    widget.setDetailsVisible(true);
    QCoreApplication::processEvents();

    QVERIFY(widget.pvTable()->isVisible());
    QVERIFY(widget.pvTable()->width() >= widget.width() / 2);
    QVERIFY(widget.pvTable()->geometry().top() > 0);
    QVERIFY(widget.pvTable()->height() > widget.pvTable()->horizontalHeader()->height());
}

void EngineOutputWidgetTest::testMainWindowLayout() {
    MainWindow window;
    window.resize(900, 650);
    window.show();

    QVERIFY(window.engineOutputWidget() != nullptr);
    QVERIFY(window.chessBoard() != nullptr);
    QVERIFY(window.evaluationBar() != nullptr);
    QVERIFY(window.moveListWidget() != nullptr);

    auto *menuBar = window.findChild<QMenuBar *>();
    QVERIFY(menuBar != nullptr);
    QCOMPARE(menuBar->geometry().x(), 0);
    QCOMPARE(menuBar->width(), window.width());

    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolBar != nullptr);
    QCOMPARE(toolBar->geometry().x(), 0);
    QCOMPARE(toolBar->width(), window.width());
    QVERIFY(toolBar->geometry().top() >= menuBar->geometry().bottom());

    int toolActionCount = 0;
    for (QAction *action : toolBar->actions()) {
        if (!action->isSeparator()) {
            ++toolActionCount;
            QVERIFY(!action->icon().isNull());
        }
    }
    QCOMPARE(toolActionCount, 14);

    // Verify main splitter contains top pane and bottom engine output widget
    auto splitters = window.findChildren<QSplitter *>();
    QVERIFY(splitters.size() >= 2);

    // The splitter handles must be styled so they stay visible
    QVERIFY(!window.mainSplitter()->styleSheet().isEmpty());

    bool foundVerticalSplitter = false;
    for (auto *splitter : splitters) {
        if (splitter->orientation() == Qt::Vertical &&
            splitter->widget(1) == window.engineOutputWidget()) {
            foundVerticalSplitter = true;
            QVERIFY(splitter->count() == 2);
            QCOMPARE(splitter->widget(1), window.engineOutputWidget());
        }
    }
    QVERIFY(foundVerticalSplitter);
}

QTEST_MAIN(EngineOutputWidgetTest)
#include "engineoutputwidget_test.moc"
