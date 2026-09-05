//
// Unit tests for UciEngine.
//

#include <QSignalSpy>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimer>
#include <QLabel>
#include <QToolButton>
#include <QSplitter>
#include <QTest>

#include "appconfig.h"
#include "engineoutputwidget.h"
#include "chessboard.h"
#include "fakegateway.h"
#include "gamecontroller.h"
#include "gatewayclient.h"
#include "mainwindow.h"
#include "movelistwidget.h"
#include "pendulumwidget.h"
#include "uciengine.h"
#include "uciparser.h"

#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTemporaryFile>

class UciEngineTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialState();
    void testInvalidPathFailsGracefully();
    void testMainWindowEngineIntegration();
    void testMockEngineProcess();
    void testMainWindowControlButtons();
    void testMainWindowComputerGame();
    void testMainWindowRemoteEngineEagerLoad();
};

void UciEngineTest::testInitialState() {
    UciEngine engine;
    QCOMPARE(engine.state(), UciEngine::State::Disconnected);
    QVERIFY(!engine.isConnected());
    QVERIFY(!engine.isAnalyzing());
    QCOMPARE(engine.engineName(), QStringLiteral("UCI Engine"));
    QVERIFY(engine.engineAuthor().isEmpty());
}

void UciEngineTest::testInvalidPathFailsGracefully() {
    UciEngine engine;
    QSignalSpy errorSpy(&engine, &UciEngine::errorOccurred);

    const bool started = engine.startEngine(QStringLiteral("/path/to/nonexistent_chess_engine_xyz"));
    QVERIFY(!started);
    QCOMPARE(engine.state(), UciEngine::State::Disconnected);
    QCOMPARE(errorSpy.count(), 1);
}

void UciEngineTest::testMainWindowEngineIntegration() {
    MainWindow window;
    QVERIFY(window.uciEngine() != nullptr);
    QCOMPARE(window.uciEngine()->state(), UciEngine::State::Disconnected);

    // Engine output widget is wired
    QVERIFY(window.engineOutputWidget() != nullptr);
    QCOMPARE(window.engineOutputWidget()->engineStatus(), QStringLiteral("Disconnected"));
}

void UciEngineTest::testMockEngineProcess() {
    const QString scriptPath = QDir::current().filePath(QStringLiteral("mock_engine.sh"));
    QFile scriptFile(scriptPath);
    QVERIFY(scriptFile.open(QIODevice::WriteOnly | QIODevice::Text));

    const QByteArray script =
        "#!/bin/bash\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then\n"
        "    echo \"id name MockEngine 1.0\"\n"
        "    echo \"id author TestAuthor\"\n"
        "    echo \"option name Threads type spin default 1 min 1 max 16\"\n"
        "    echo \"option name UCI_Chess960 type check default false\"\n"
        "    echo \"uciok\"\n"
        "  elif [ \"$line\" = \"isready\" ]; then\n"
        "    echo \"readyok\"\n"
        "  elif [[ \"$line\" =~ ^go.* ]]; then\n"
        "    echo \"info depth 15 score cp 65 nodes 50000 nps 1000000 time 50 pv e7e5 g1f3\"\n"
        "    echo \"bestmove e7e5 ponder g1f3\"\n"
        "  elif [ \"$line\" = \"quit\" ]; then\n"
        "    exit 0\n"
        "  fi\n"
        "done\n";

    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);

    UciEngine engine;
    QSignalSpy loadedSpy(&engine, &UciEngine::engineLoaded);
    QSignalSpy analysisSpy(&engine, &UciEngine::analysisUpdated);
    QSignalSpy bestMoveSpy(&engine, &UciEngine::bestMoveReceived);

    QVERIFY(engine.startEngine(scriptPath));
    QVERIFY(loadedSpy.wait(2000));
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(engine.engineName(), QStringLiteral("MockEngine 1.0"));
    QCOMPARE(engine.engineAuthor(), QStringLiteral("TestAuthor"));
    QTRY_COMPARE_WITH_TIMEOUT(engine.state(), UciEngine::State::Ready, 2000);
    QCOMPARE(engine.options().size(), 2);
    QCOMPARE(engine.options().at(0).name, QStringLiteral("Threads"));
    QCOMPARE(engine.options().at(1).type, UciOption::Type::Check);

    QSignalSpy rawSentSpy(&engine, &UciEngine::rawLineSent);
    engine.setOption(QStringLiteral("Threads"), 4);
    QVERIFY(rawSentSpy.count() > 0);
    QCOMPARE(rawSentSpy.last().at(0).toString(),
             QStringLiteral("setoption name Threads value 4"));

    QSignalSpy invalidFenErrorSpy(&engine, &UciEngine::errorOccurred);
    QSignalSpy invalidFenRawSpy(&engine, &UciEngine::rawLineSent);
    engine.sendPosition(QStringLiteral(
        "r2qk2r/pp2bppb/2p1p2p/2n5/4nP1N/2N3P1/PPP1Q1BP/R4R2 b kq - 0 1"));
    QCOMPARE(invalidFenErrorSpy.count(), 1);
    QCOMPARE(invalidFenRawSpy.count(), 0);

    engine.sendPosition(QString(), {QStringLiteral("e2e4")});
    engine.startAnalysis();

    if (analysisSpy.isEmpty()) {
        QVERIFY(analysisSpy.wait(2000));
    }
    QCOMPARE(analysisSpy.count(), 1);
    const auto analysis = analysisSpy.takeFirst().at(0).value<EngineAnalysisLine>();
    QCOMPARE(analysis.depth, 15);
    QCOMPARE(analysis.scoreCp, 65.0);

    QTRY_COMPARE_WITH_TIMEOUT(bestMoveSpy.count(), 1, 2000);
    QCOMPARE(bestMoveSpy.takeFirst().at(0).toString(), QStringLiteral("e7e5"));

    rawSentSpy.clear();
    engine.startTimedSearch(300000, 600000);
    QTRY_VERIFY_WITH_TIMEOUT(rawSentSpy.count() > 0, 2000);
    bool foundTimedSearch = false;
    for (const QList<QVariant> &arguments : rawSentSpy) {
        if (arguments.at(0).toString() ==
            QStringLiteral("go wtime 300000 btime 600000 winc 0 binc 0")) {
            foundTimedSearch = true;
            break;
        }
    }
    QVERIFY(foundTimedSearch);

    engine.stopEngine();
    QCOMPARE(engine.state(), UciEngine::State::Disconnected);

    QFile::remove(scriptPath);
}

void UciEngineTest::testMainWindowControlButtons() {
    const QString scriptPath = QDir::current().filePath(QStringLiteral("mock_engine_ctrl.sh"));
    QFile scriptFile(scriptPath);
    QVERIFY(scriptFile.open(QIODevice::WriteOnly | QIODevice::Text));

    const QByteArray script =
        "#!/bin/bash\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then\n"
        "    echo \"id name MockEngine 1.0\"\n"
        "    echo \"id author TestAuthor\"\n"
        "    echo \"uciok\"\n"
        "  elif [ \"$line\" = \"isready\" ]; then\n"
        "    echo \"readyok\"\n"
        "  elif [[ \"$line\" =~ ^go.* ]]; then\n"
        "    echo \"info depth 10 score cp 50 nodes 10000 pv d2d4\"\n"
        "    echo \"info currmove e2e4 currmovenumber 2\"\n"
        "  elif [ \"$line\" = \"stop\" ]; then\n"
        "    echo \"bestmove d2d4\"\n"
        "  elif [ \"$line\" = \"quit\" ]; then\n"
        "    exit 0\n"
        "  fi\n"
        "done\n";

    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);

    MainWindow window;
    auto *outputWidget = window.engineOutputWidget();
    QVERIFY(outputWidget != nullptr);

    // Initial disconnected state: all control buttons are disabled
    QVERIFY(!outputWidget->startButton()->isEnabled());
    QVERIFY(!outputWidget->pauseButton()->isEnabled());
    QVERIFY(!outputWidget->stopButton()->isEnabled());

    // Start engine process
    QVERIFY(window.uciEngine()->startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(window.uciEngine()->state(), UciEngine::State::Ready, 2000);

    // In Ready state: Start and Stop are enabled, Pause is disabled
    QVERIFY(outputWidget->startButton()->isEnabled());
    QVERIFY(!outputWidget->pauseButton()->isEnabled());
    QVERIFY(outputWidget->stopButton()->isEnabled());

    // Click Start button
    window.showRecommendedMoveCheckBox()->setChecked(true);
    outputWidget->startButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(window.uciEngine()->state(), UciEngine::State::Analyzing, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(window.chessBoard()->recommendedMovePreview().has_value(), 2000);
    const auto recommendedMove = window.chessBoard()->recommendedMovePreview();
    QVERIFY(recommendedMove.has_value());
    QCOMPARE(recommendedMove->from, (Rules::Position{6, 3}));
    QCOMPARE(recommendedMove->to, (Rules::Position{4, 3}));

    // In Analyzing state: Start is disabled, Pause and Stop are enabled
    QVERIFY(!outputWidget->startButton()->isEnabled());
    QVERIFY(outputWidget->pauseButton()->isEnabled());
    QVERIFY(outputWidget->stopButton()->isEnabled());

    // Click Pause button
    outputWidget->pauseButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(window.uciEngine()->state(), UciEngine::State::Ready, 2000);

    // Back to Ready state: Start and Stop enabled, Pause disabled
    QVERIFY(outputWidget->startButton()->isEnabled());
    QVERIFY(!outputWidget->pauseButton()->isEnabled());
    QVERIFY(outputWidget->stopButton()->isEnabled());

    // Click Stop button
    outputWidget->stopButton()->click();
    QCOMPARE(outputWidget->depth(), 0);

    window.stopEngine();
    QCOMPARE(window.uciEngine()->state(), UciEngine::State::Disconnected);
    QVERIFY(!outputWidget->startButton()->isEnabled());
    QVERIFY(!outputWidget->pauseButton()->isEnabled());
    QVERIFY(!outputWidget->stopButton()->isEnabled());

    QFile::remove(scriptPath);
}

void UciEngineTest::testMainWindowComputerGame() {
    const QString scriptPath = QDir::current().filePath(QStringLiteral("mock_engine_game.sh"));
    QFile scriptFile(scriptPath);
    QVERIFY(scriptFile.open(QIODevice::WriteOnly | QIODevice::Text));

    const QByteArray script =
        "#!/bin/bash\n"
        "preview_count=0\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then\n"
        "    echo \"id name MockGameEngine\"\n"
        "    echo \"uciok\"\n"
        "  elif [ \"$line\" = \"isready\" ]; then\n"
        "    echo \"readyok\"\n"
        "  elif [ \"$line\" = \"go infinite\" ]; then\n"
        "    if [ \"$preview_count\" -eq 0 ]; then\n"
        "      echo \"info depth 8 score cp 20 pv a2a3 e7e5\"\n"
        "    else\n"
        "      echo \"info depth 8 score cp 20 pv g1f3 b8c6\"\n"
        "    fi\n"
        "    preview_count=$((preview_count + 1))\n"
        "  elif [ \"$line\" = \"stop\" ]; then\n"
        "    echo \"bestmove a2a3\"\n"
        "  elif [[ \"$line\" =~ ^go.* ]]; then\n"
        "    echo \"info depth 8 score cp -20 pv e7e5 g1f3\"\n"
        "    echo \"bestmove e7e5\"\n"
        "  elif [ \"$line\" = \"quit\" ]; then\n"
        "    exit 0\n"
        "  fi\n"
        "done\n";

    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);

    MainWindow window;
    window.show();
    window.showComputerMoveCheckBox()->setChecked(true);
    window.showRecommendedMoveCheckBox()->setChecked(true);
    QVERIFY(window.uciEngine()->startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(window.uciEngine()->state(), UciEngine::State::Ready, 2000);

    QAction *playAction = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Play against computer")) {
            playAction = action;
            break;
        }
    }
    QVERIFY(playAction != nullptr);

    QTimer::singleShot(50, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        auto *buttonBox = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttonBox != nullptr);
        buttonBox->button(QDialogButtonBox::Ok)->click();
    });
    playAction->trigger();

    QVERIFY(!window.whiteToPlayCheckBox()->isEnabled());
    QVERIFY(window.showComputerMoveCheckBox()->isEnabled());
    QVERIFY(window.showRecommendedMoveCheckBox()->isEnabled());
    QVERIFY(window.showComputerMoveCheckBox()->isChecked());
    QVERIFY(window.showRecommendedMoveCheckBox()->isChecked());
    QVERIFY(!window.engineOutputWidget()->startButton()->isEnabled());
    QVERIFY(!window.engineOutputWidget()->pauseButton()->isEnabled());
    QVERIFY(window.engineOutputWidget()->stopButton()->isEnabled());
    QVERIFY(window.whitePendulum()->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(window.chessBoard()->computerMovePreview().has_value(), 2000);
    const auto firstComputerPreview = window.chessBoard()->computerMovePreview();
    QVERIFY(firstComputerPreview.has_value());
    QCOMPARE(firstComputerPreview->from, (Rules::Position{1, 4}));
    QCOMPARE(firstComputerPreview->to, (Rules::Position{3, 4}));

    window.pieceMoved(QChar('P'), {6, 0}, {5, 0});
    QTRY_VERIFY_WITH_TIMEOUT(window.chessBoard()->rules().currentPlayer() == Rules::Color::White,
                             2000);
    const Rules::Position expectedReplyFrom{0, 1};
    QTRY_VERIFY_WITH_TIMEOUT(
        window.chessBoard()->computerMovePreview().has_value() &&
            window.chessBoard()->computerMovePreview()->from == expectedReplyFrom,
        2000);
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::White);
    QCOMPARE(window.moveListWidget()->model()->index(1, 1).data().toString(),
             QStringLiteral("a3"));
    QVERIFY(window.gameController()->pgnText().contains(
        QStringLiteral("[TimeControl \"7200\"]")));

    // A refused history request may move keyboard focus, never the played marker.
    auto *moves = window.moveListWidget();
    const int cursor = window.gameController()->moveCursor();
    const auto *label = window.findChild<QLabel *>("currentMoveLabel");
    QVERIFY(label);
    const QString displayedMove = label->text();
    const auto start = moves->model()->index(0, 0);
    moves->scrollTo(start);
    moves->setFocus();
    QTest::mouseClick(moves->viewport(), Qt::LeftButton, {}, moves->visualRect(start).center());
    QTest::keyClick(moves, Qt::Key_Return);
    QTest::keyClick(moves, Qt::Key_Left);
    QTest::keyClick(moves, Qt::Key_Right);
    QCOMPARE(window.gameController()->moveCursor(), cursor);
    QCOMPARE(moves->currentMove(), cursor);
    QCOMPARE(label->text(), displayedMove);
    QVERIFY(!start.data(MoveListWidget::CurrentMoveRole).toBool());
    for (const auto name : {"stepBackAction", "stepForwardAction"}) {
        const auto *action = window.findChild<QAction *>(name);
        QVERIFY(action && !action->isEnabled());
        bool foundButton = false;
        for (const auto *button : window.rightSplitter()->findChildren<QToolButton *>()) {
            if (button->defaultAction() == action) {
                foundButton = true;
                QVERIFY(!button->isEnabled());
            }
        }
        QVERIFY(foundButton);
    }

    window.stopEngine();
    QFile::remove(scriptPath);
}

void UciEngineTest::testMainWindowRemoteEngineEagerLoad() {
    FakeGateway gateway;
    QVERIFY(gateway.isListening());

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    {
        AppConfig config(confPath);
        config.setRemoteEngine(QStringLiteral("127.0.0.1"), gateway.port(),
                               QStringLiteral("stockfish-17"),
                               QStringLiteral("Stockfish"), QStringLiteral("17"));
        QVERIFY(config.save());
    }

    MainWindow window(nullptr, confPath);
    // The remote engine is configured and loads immediately at startup,
    // so the UCI options are active without starting an analysis.
    QVERIFY(window.gatewayClient() != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(window.gatewayClient()->state(),
                              ChessGatewayClient::State::Ready, 5000);
    QCOMPARE(window.gatewayClient()->engineName(),
             QStringLiteral("RemoteMockEngine 1.0"));
    QVERIFY(window.gatewayClient()->isConnected());

    QAction *toggleAnalysisAction = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("Start Analysis")) {
            toggleAnalysisAction = action;
            break;
        }
    }
    QVERIFY(toggleAnalysisAction != nullptr);
    QVERIFY(toggleAnalysisAction->isEnabled());

    // The engine is already ready: starting the analysis is immediate.
    toggleAnalysisAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        gateway.uciCommands().contains(QStringLiteral("go infinite")), 5000);

    window.stopEngine();
    QCOMPARE(window.gatewayClient()->state(), ChessGatewayClient::State::Disconnected);
}

QTEST_MAIN(UciEngineTest)
#include "uciengine_test.moc"
