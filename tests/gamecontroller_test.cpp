//
// Unit tests for GameController.
//

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include "fakegateway.h"
#include "gamecontroller.h"
#include "gatewayclient.h"
#include "uciengine.h"
#include "uciparser.h"

namespace {

constexpr qint64 WaitTimeout = 5000;

QString writeMockEngineScript(const QString &fileName) {
    const QString scriptPath = QDir::current().filePath(fileName);
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }

    const QByteArray script =
        "#!/bin/bash\n"
        "preview_count=0\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then\n"
        "    echo \"id name MockControllerEngine\"\n"
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
        "  elif [[ \"$line\" =~ ^go.* ]]; then\n"
        "    echo \"info depth 8 score cp -20 pv e7e5 g1f3\"\n"
        "    echo \"bestmove e7e5\"\n"
        "  elif [ \"$line\" = \"stop\" ]; then\n"
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
    return scriptPath;
}

QString writeAuditEngineScript(const QString &fileName) {
    const QString scriptPath = QDir::current().filePath(fileName);
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    const QByteArray script =
        "#!/bin/bash\n"
        "moves=0\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then echo \"id name AuditEngine\"; echo \"uciok\";\n"
        "  elif [ \"$line\" = \"isready\" ]; then echo \"readyok\";\n"
        "  elif [[ \"$line\" == position* ]]; then\n"
        "    moves=0; if [[ \"$line\" == *\" moves \"* ]]; then rest=${line#* moves }; for m in $rest; do moves=$((moves + 1)); done; fi;\n"
        "  elif [ \"$line\" = \"go depth 18\" ]; then\n"
        "    if [ \"$moves\" -eq 0 ]; then echo \"info depth 18 score cp 100 pv d2d4\"; echo \"bestmove d2d4\";\n"
        "    elif [ \"$moves\" -eq 1 ]; then echo \"info depth 18 score cp -10 pv e7e5\"; echo \"bestmove e7e5\";\n"
        "    else echo \"info depth 18 score cp 150 pv g1f3\"; echo \"bestmove g1f3\"; fi;\n"
        "  elif [ \"$line\" = \"stop\" ]; then echo \"bestmove 0000\";\n"
        "  elif [ \"$line\" = \"quit\" ]; then exit 0; fi\n"
        "done\n";
    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser | QFile::ReadGroup |
                                      QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    return scriptPath;
}

QString writeMateAuditEngineScript(const QString &fileName) {
    const QString scriptPath = QDir::current().filePath(fileName);
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    const QByteArray script =
        "#!/bin/bash\n"
        "moves=0\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then echo \"id name MateAuditEngine\"; echo \"uciok\";\n"
        "  elif [ \"$line\" = \"isready\" ]; then echo \"readyok\";\n"
        "  elif [[ \"$line\" == position* ]]; then moves=0; if [[ \"$line\" == *\" moves \"* ]]; then moves=1; fi;\n"
        "  elif [ \"$line\" = \"go depth 18\" ]; then\n"
        "    if [ \"$moves\" -eq 0 ]; then echo \"info depth 18 score mate 3 pv d2d4\"; echo \"bestmove d2d4\";\n"
        "    else echo \"info depth 18 score mate 2 pv e7e5\"; echo \"bestmove e7e5\"; fi;\n"
        "  elif [ \"$line\" = \"stop\" ]; then echo \"bestmove 0000\";\n"
        "  elif [ \"$line\" = \"quit\" ]; then exit 0; fi\n"
        "done\n";
    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser | QFile::ReadGroup |
                                      QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    return scriptPath;
}

// Engine used by the analysis-report test: it answers the configured depth and
// reports one score per main-line position, so that the report's accuracy and
// centipawn losses are deterministic.
QString writeReportAuditEngineScript(const QString &fileName) {
    const QString scriptPath = QDir::current().filePath(fileName);
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    const QByteArray script =
        "#!/bin/bash\n"
        "moves=0\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then echo \"id name ReportAuditEngine\"; echo \"uciok\";\n"
        "  elif [ \"$line\" = \"isready\" ]; then echo \"readyok\";\n"
        "  elif [[ \"$line\" == position* ]]; then\n"
        "    moves=0; if [[ \"$line\" == *\" moves \"* ]]; then rest=${line#* moves }; for m in $rest; do moves=$((moves + 1)); done; fi;\n"
        "  elif [[ \"$line\" =~ ^go\\ depth\\ ([0-9]+) ]]; then\n"
        "    depth=${BASH_REMATCH[1]};\n"
        "    if [ \"$moves\" -eq 0 ]; then echo \"info depth $depth score cp 20 pv d2d4\"; echo \"bestmove d2d4\";\n"
        "    elif [ \"$moves\" -eq 1 ]; then echo \"info depth $depth score cp 280 pv e7e5\"; echo \"bestmove e7e5\";\n"
        "    else echo \"info depth $depth score cp -280 pv g1f3\"; echo \"bestmove g1f3\"; fi;\n"
        "  elif [ \"$line\" = \"stop\" ]; then echo \"bestmove 0000\";\n"
        "  elif [ \"$line\" = \"quit\" ]; then exit 0; fi\n"
        "done\n";
    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser | QFile::ReadGroup |
                                      QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    return scriptPath;
}

} // namespace

class GameControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialState();
    void testLoadFen();
    void testEvaluationScore();
    void testEvaluationCurve();
    void testEngineRefinesCurve();
    void testMaterialBalance();
    void testNewGame();
    void testLoadPgn();
    void testRequestMove();
    void testRequestMoveIllegal();
    void testStepBackAndForward();
    void testGoToMove();
    void testStepBackTruncatesHistory();
    void testSetSideToMove();
    void testComputerGameBookMove();
    void testComputerGameWithMockEngine();
    void testClockIncrementGranted();
    void testEngineDisconnectFinishesGame();
    void testRemoteEngineConfiguration();
    void testEagerAnalysisWithRemoteEngine();
    void testEagerComputerGameWithRemoteEngine();
    void testAnnotationsAtCursor();
    void testAnnotationsPersistAcrossNavigation();
    void testAnnotationsTruncatedOnBranching();
    void testPgnTextWithAnnotationsRoundTrip();
    void testGameAuditExportsAndReloadsMarkers();
    void testGameAuditDepthAndReport();
    void testGameAuditFollowsTheBoard();
    void testStoppingCancelsTheGameAudit();
    void testGameAuditCanBeCancelledWithoutPartialResults();
    void testGameAuditMarksLostForcedMateAsBlunder();
    void testPromotionAndUnderpromotion();
    void testGoToStartAndGoToEnd();
    void testDrawClaims();
    void testTakeBackInFreePlay();
    void testTakeBackAfterFreePlayCheckmate();
    void testTakeBackInComputerGame();
    void testResignAgainstEngine();
    void testDrawOffers();
    void testAnalysisSettings();
    void testEvaluationBreakdownAndHeuristicHint();
    void testEvaluationDepthSetting();
    void testEvaluationCurveIsComputedInTheBackground();
    void testEvaluationCurveCanBeCancelled();
};

void GameControllerTest::initTestCase() {
    qRegisterMetaType<std::optional<Rules::Move>>("std::optional<Rules::Move>");
    qRegisterMetaType<Rules::Color>("Rules::Color");
}

void GameControllerTest::testInitialState() {
    GameController controller;
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(controller.initialFen().isEmpty());
    QVERIFY(controller.uciMoves().isEmpty());
    QVERIFY(controller.pgnText().isEmpty());
    QVERIFY(!controller.isComputerGameActive());
    QVERIFY(!controller.isComputerGamePending());
    QVERIFY(controller.engine() != nullptr);
    QVERIFY(!controller.engine()->isConnected());
}

void GameControllerTest::testLoadFen() {
    GameController controller;
    QSignalSpy positionSpy(&controller, &GameController::positionChanged);
    QSignalSpy historySpy(&controller, &GameController::historyChanged);
    QSignalSpy evaluationSpy(&controller, &GameController::evaluationChanged);

    const QString fen = QStringLiteral(
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5");
    QVERIFY(controller.loadFen(fen));
    QCOMPARE(controller.initialFen(), fen);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(controller.rules().pieceAt({4, 2}).has_value());
    QCOMPARE(controller.rules().pieceAt({4, 2})->type, Rules::PieceType::Bishop);
    QVERIFY(controller.pgnText().contains(fen));
    QVERIFY(controller.pgnText().contains(QStringLiteral("Position loaded from FEN.")));
    QCOMPARE(positionSpy.count(), 1);
    QCOMPARE(historySpy.count(), 1);
    // The static evaluation is published at once and its background search
    // refinement follows, so at least one update is guaranteed.
    QVERIFY(evaluationSpy.count() >= 1);

    QVERIFY(!controller.loadFen(QStringLiteral("not a fen")));
    QCOMPARE(controller.initialFen(), fen);
}

void GameControllerTest::testEvaluationScore() {
    GameController controller;
    QSignalSpy scoreSpy(&controller, &GameController::evaluationScoreChanged);

    controller.updateEvaluation();
    QCOMPARE(scoreSpy.count(), 1);
    const QString initialScore = scoreSpy.at(0).at(0).toString();
    QVERIFY2(QRegularExpression(QStringLiteral("^[+-]?\\d+\\.\\d{2}$"))
                 .match(initialScore).hasMatch(),
             qPrintable(initialScore));

    // Fool's mate: the checkmate is already on the board, so the heuristic
    // cannot report a distance and the score is labelled "Mate".
    QVERIFY(controller.loadFen(QStringLiteral(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3")));
    scoreSpy.clear();
    controller.updateEvaluation();
    QCOMPARE(scoreSpy.count(), 1);
    QCOMPARE(scoreSpy.at(0).at(0).toString(), QStringLiteral("Mate"));
}

void GameControllerTest::testNewGame() {
    const QString scriptPath =
        writeMockEngineScript(QStringLiteral("mock_newgame_engine.sh"));
    GameController controller;
    const QString fen = QStringLiteral(
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5");

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);
    QVERIFY(controller.loadFen(fen));
    QVERIFY(controller.requestMove({6, 3}, {4, 3}));
    QCOMPARE(controller.uciMoves().size(), 1);

    QSignalSpy analysisProbe(&controller, &GameController::evaluationChanged);
    controller.startAnalysis();
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Analyzing, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(analysisProbe.count() >= 1, 2000);

    QSignalSpy positionSpy(&controller, &GameController::positionChanged);
    QSignalSpy historySpy(&controller, &GameController::historyChanged);
    QSignalSpy evaluationSpy(&controller, &GameController::evaluationChanged);
    QSignalSpy computerStateSpy(&controller, &GameController::computerGameStateChanged);

    controller.newGame();

    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(controller.initialFen().isEmpty());
    QVERIFY(controller.uciMoves().isEmpty());
    QVERIFY(controller.pgnText().isEmpty());
    QVERIFY(!controller.isComputerGameActive());
    const auto rook = controller.rules().pieceAt({7, 0});
    QVERIFY(rook.has_value());
    QCOMPARE(rook->type, Rules::PieceType::Rook);
    // The running analysis is stopped and the engine is synced to the
    // starting position without restarting it.
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);
    QVERIFY(!controller.isEngineAnalyzing());
    QCOMPARE(positionSpy.count(), 1);
    QCOMPARE(historySpy.count(), 1);
    // The static evaluation is published at once and its background search
    // refinement follows, so at least one update is guaranteed.
    QVERIFY(evaluationSpy.count() >= 1);
    QCOMPARE(computerStateSpy.count(), 1);
    QCOMPARE(computerStateSpy.at(0).at(0).toBool(), false);

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testLoadPgn() {
    GameController controller;
    const QString pgn = QStringLiteral(
        "[Event \"Scholar's Mate\"]\n"
        "1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0"
    );

    QVERIFY(controller.loadPgn(pgn));
    QCOMPARE(controller.uciMoves().size(), 7);
    QVERIFY(controller.rules().isCheckmate(Rules::Color::Black));
    QVERIFY(controller.pgnText().contains(QStringLiteral("Qxf7#")));
    // After White delivers mate, the mated side is to move.
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);
}

void GameControllerTest::testRequestMove() {
    GameController controller;
    QSignalSpy positionSpy(&controller, &GameController::positionChanged);
    QSignalSpy historySpy(&controller, &GameController::historyChanged);
    QSignalSpy evaluationSpy(&controller, &GameController::evaluationChanged);

    QVERIFY(controller.requestMove({6, 4}, {4, 4}));
    QCOMPARE(controller.uciMoves(), QStringList({QStringLiteral("e2e4")}));
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. e4")));
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);
    QCOMPARE(positionSpy.count(), 1);
    QCOMPARE(historySpy.count(), 1);
    QCOMPARE(evaluationSpy.count(), 1);

    QVERIFY(controller.requestMove({1, 0}, {3, 0}));
    QCOMPARE(controller.uciMoves(),
             QStringList({QStringLiteral("e2e4"), QStringLiteral("a7a5")}));
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. e4 a5")));
}

void GameControllerTest::testRequestMoveIllegal() {
    GameController controller;
    QSignalSpy positionSpy(&controller, &GameController::positionChanged);

    // Knight jumping over the pawn wall is illegal from the start position.
    QVERIFY(!controller.requestMove({7, 1}, {4, 2}));
    QVERIFY(controller.uciMoves().isEmpty());
    QCOMPARE(positionSpy.count(), 0);
}

void GameControllerTest::testStepBackAndForward() {
    GameController controller;
    QSignalSpy positionSpy(&controller, &GameController::positionChanged);

    QCOMPARE(controller.moveCursor(), 0);
    QVERIFY(!controller.canStepBack());
    QVERIFY(!controller.canStepForward());
    QVERIFY(!controller.stepBack());
    QVERIFY(!controller.stepForward());

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    QCOMPARE(controller.uciMoves().size(), 2);
    QCOMPARE(controller.moveCursor(), 2);
    QVERIFY(controller.canStepBack());
    QVERIFY(!controller.canStepForward());

    const QString finalFen = controller.rules().toFen();

    QVERIFY(controller.stepBack());
    QCOMPARE(controller.moveCursor(), 1);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);
    const QString afterE4 = controller.rules().toFen();

    QVERIFY(controller.stepBack());
    QCOMPARE(controller.moveCursor(), 0);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    const auto pawn = controller.rules().pieceAt({6, 4});
    QVERIFY(pawn.has_value());
    QCOMPARE(pawn->type, Rules::PieceType::Pawn);
    QVERIFY(!controller.canStepBack());
    QVERIFY(!controller.stepBack());

    QVERIFY(controller.stepForward());
    QCOMPARE(controller.moveCursor(), 1);
    QCOMPARE(controller.rules().toFen(), afterE4);

    QVERIFY(controller.stepForward());
    QCOMPARE(controller.moveCursor(), 2);
    QCOMPARE(controller.rules().toFen(), finalFen);
    QVERIFY(!controller.canStepForward());
    QVERIFY(!controller.stepForward());

    // Navigation never alters the recorded history.
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. e4 e5")));
    QCOMPARE(positionSpy.count(), 6);
}

void GameControllerTest::testGoToMove() {
    GameController controller;
    QSignalSpy positionSpy(&controller, &GameController::positionChanged);

    QVERIFY(!controller.goToMove(1));
    QCOMPARE(controller.moveCursor(), 0);
    QCOMPARE(positionSpy.count(), 0);

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    QVERIFY(controller.requestMove({6, 2}, {4, 2})); // c2c4
    QVERIFY(controller.requestMove({1, 6}, {3, 6})); // g7g5
    QCOMPARE(controller.uciMoves().size(), 4);

    const QString finalFen = controller.rules().toFen();

    QVERIFY(controller.goToMove(2));
    QCOMPARE(controller.moveCursor(), 2);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);

    QVERIFY(controller.goToMove(0));
    QCOMPARE(controller.moveCursor(), 0);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);

    // Out-of-range indices are clamped.
    QVERIFY(controller.goToMove(99));
    QCOMPARE(controller.moveCursor(), 4);
    QCOMPARE(controller.rules().toFen(), finalFen);
    QVERIFY(controller.goToMove(-5));
    QCOMPARE(controller.moveCursor(), 0);

    // Navigating to the current position is a no-op.
    QVERIFY(!controller.goToMove(0));
    QVERIFY(!controller.goToMove(controller.moveCursor()));

    // The recorded history is untouched.
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. e4 e5")));
    QCOMPARE(controller.pgnMoves(),
             QStringList({QStringLiteral("1. e4 e5"), QStringLiteral("2. c4 g5")}));
    QCOMPARE(positionSpy.count(), 8);
}

void GameControllerTest::testStepBackTruncatesHistory() {
    GameController controller;
    QSignalSpy historySpy(&controller, &GameController::historyChanged);

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    QVERIFY(controller.requestMove({6, 2}, {4, 2})); // c2c3
    QCOMPARE(controller.uciMoves().size(), 3);

    QVERIFY(controller.stepBack());
    QCOMPARE(controller.moveCursor(), 2);

    // A move played from the middle discards the abandoned continuation.
    QVERIFY(controller.requestMove({7, 6}, {5, 5})); // g1f3
    QCOMPARE(controller.uciMoves(),
             QStringList({QStringLiteral("e2e4"), QStringLiteral("e7e5"),
                          QStringLiteral("g1f3")}));
    QCOMPARE(controller.moveCursor(), 3);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);
    QVERIFY(!controller.canStepForward());
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. e4 e5")));
    QVERIFY(controller.pgnText().contains(QStringLiteral("2. Nf3")));
    QVERIFY(!controller.pgnText().contains(QStringLiteral("c3")));
    QCOMPARE(historySpy.count(), 4);
}

void GameControllerTest::testSetSideToMove() {
    GameController controller;
    controller.setSideToMove(false);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);
    QVERIFY(controller.initialFen().contains(QStringLiteral(" b ")));
    QVERIFY(controller.pgnText().contains(QStringLiteral("Position loaded with Black to move.")));

    controller.setSideToMove(true);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(controller.initialFen().contains(QStringLiteral(" w ")));
}

void GameControllerTest::testComputerGameBookMove() {
    const QString scriptPath = writeMockEngineScript(QStringLiteral("mock_book_engine.sh"));
    GameController controller;
    QSignalSpy gameStateSpy(&controller, &GameController::computerGameStateChanged);
    QSignalSpy humanTurnSpy(&controller, &GameController::humanTurnBegan);
    QSignalSpy finishedSpy(&controller, &GameController::gameFinished);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = true;
    settings.engineName = QStringLiteral("MockControllerEngine");
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());

    // The engine plays from the opening book: a move is recorded without a
    // timed search, and the human's turn begins.
    QTRY_COMPARE_WITH_TIMEOUT(controller.uciMoves().size(), 1, WaitTimeout);
    QCOMPARE(humanTurnSpy.count(), 1);
    QCOMPARE(gameStateSpy.count(), 1);
    QCOMPARE(gameStateSpy.first().at(0).toBool(), true);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);
    QVERIFY(controller.pgnText().contains(QStringLiteral("[White \"MockControllerEngine\"]")));

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testComputerGameWithMockEngine() {
    const QString scriptPath = writeMockEngineScript(QStringLiteral("mock_game_engine.sh"));
    GameController controller;
    QSignalSpy computerPreviewSpy(&controller, &GameController::computerMovePreviewChanged);
    QSignalSpy recommendedPreviewSpy(&controller, &GameController::recommendedMovePreviewChanged);
    QSignalSpy computerTurnSpy(&controller, &GameController::computerTurnBegan);
    QSignalSpy humanTurnSpy(&controller, &GameController::humanTurnBegan);
    QSignalSpy finishedSpy(&controller, &GameController::gameFinished);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    controller.setRecommendedMovePreviewEnabled(true);
    controller.setComputerMovePreviewEnabled(true);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = false;
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());

    // Human's turn first: preview analysis shows the recommended and the
    // computer's planned moves.
    QTRY_COMPARE_WITH_TIMEOUT(recommendedPreviewSpy.count(), 1, WaitTimeout);
    const auto recommended = recommendedPreviewSpy.first().at(0).value<std::optional<Rules::Move>>();
    QVERIFY(recommended.has_value());
    QCOMPARE(recommended->from, (Rules::Position{6, 0}));
    QCOMPARE(recommended->to, (Rules::Position{5, 0}));

    QTRY_COMPARE_WITH_TIMEOUT(computerPreviewSpy.count(), 1, WaitTimeout);
    const auto computerPreview = computerPreviewSpy.first().at(0).value<std::optional<Rules::Move>>();
    QVERIFY(computerPreview.has_value());
    QCOMPARE(computerPreview->from, (Rules::Position{1, 4}));
    QCOMPARE(computerPreview->to, (Rules::Position{3, 4}));

    // Human plays a2a3. The computer answers from the opening book (or the
    // engine): wait until Black has moved.
    QVERIFY(controller.requestMove({6, 0}, {5, 0}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.rules().currentPlayer() == Rules::Color::White &&
                                 controller.uciMoves().size() == 2,
                             WaitTimeout);
    QCOMPARE(controller.uciMoves().first(), QStringLiteral("a2a3"));
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. a3 ")));
    QCOMPARE(humanTurnSpy.count(), 2);

    // Leave the book with a second non-book move: the engine must answer a
    // timed search with its best move.
    QVERIFY(controller.requestMove({6, 7}, {5, 7}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.uciMoves().size(), 4, WaitTimeout);
    QCOMPARE(controller.uciMoves().at(2), QStringLiteral("h2h3"));
    QCOMPARE(controller.uciMoves().at(3), QStringLiteral("e7e5"));
    QVERIFY(controller.pgnText().contains(QStringLiteral("2. h3 e5")));
    QCOMPARE(computerTurnSpy.count(), 1);
    QCOMPARE(humanTurnSpy.count(), 3);
    QCOMPARE(finishedSpy.count(), 0);

    // A new preview analysis runs after the engine's reply.
    QTRY_VERIFY_WITH_TIMEOUT(computerPreviewSpy.count() >= 2, WaitTimeout);
    const auto secondPreview = computerPreviewSpy.last().at(0).value<std::optional<Rules::Move>>();
    QVERIFY(secondPreview.has_value());
    QCOMPARE(secondPreview->from, (Rules::Position{0, 1}));

    controller.stopEngine();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, WaitTimeout);
    QCOMPARE(finishedSpy.first().at(1).toString(), QStringLiteral("The engine disconnected."));
    QVERIFY(!controller.isComputerGameActive());
    QFile::remove(scriptPath);
}

void GameControllerTest::testClockIncrementGranted() {
    const QString scriptPath = writeMockEngineScript(QStringLiteral("mock_increment_engine.sh"));
    GameController controller;
    QSignalSpy incrementSpy(&controller, &GameController::clockIncrementGranted);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = false;
    settings.incrementMilliseconds = 3000;
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());

    // No increment is granted before a move has been played.
    QCOMPARE(incrementSpy.count(), 0);

    QVERIFY(controller.requestMove({6, 0}, {5, 0}));
    QTRY_VERIFY_WITH_TIMEOUT(controller.uciMoves().size() == 2, WaitTimeout);

    // The human mover is credited first, then the computer for its reply.
    QVERIFY(incrementSpy.count() >= 1);
    QCOMPARE(incrementSpy.first().at(0).value<Rules::Color>(), Rules::Color::White);
    QCOMPARE(incrementSpy.first().at(1).toLongLong(), 3000LL);

    QVERIFY(incrementSpy.count() >= 2);
    QCOMPARE(incrementSpy.at(1).at(0).value<Rules::Color>(), Rules::Color::Black);
    QCOMPARE(incrementSpy.at(1).at(1).toLongLong(), 3000LL);

    QVERIFY(controller.pgnText().contains(QStringLiteral("[TimeControl \"300+3\"]")));

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testEngineDisconnectFinishesGame() {
    const QString scriptPath = writeMockEngineScript(QStringLiteral("mock_disconnect_engine.sh"));
    GameController controller;
    QSignalSpy finishedSpy(&controller, &GameController::gameFinished);
    QSignalSpy stateSpy(&controller, &GameController::computerGameStateChanged);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = true;
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());
    QTRY_COMPARE_WITH_TIMEOUT(controller.uciMoves().size(), 1, WaitTimeout);

    controller.stopEngine();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, WaitTimeout);
    QVERIFY(!controller.isComputerGameActive());
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(stateSpy.last().at(0).toBool(), false);
    QFile::remove(scriptPath);
}

void GameControllerTest::testRemoteEngineConfiguration() {
    GameController controller;
    QVERIFY(!controller.hasRemoteEngine());
    QVERIFY(controller.gatewayClient() != nullptr);
    QCOMPARE(controller.gatewayClient()->state(), ChessGatewayClient::State::Disconnected);

    controller.setRemoteEngine(QStringLiteral("127.0.0.1"), 9000,
                               QStringLiteral("stockfish-17"),
                               QStringLiteral("Stockfish"));
    QVERIFY(controller.hasRemoteEngine());
    QCOMPARE(controller.gatewayClient()->state(), ChessGatewayClient::State::Connecting);

    controller.clearRemoteEngine();
    QVERIFY(!controller.hasRemoteEngine());
    QCOMPARE(controller.gatewayClient()->state(), ChessGatewayClient::State::Disconnected);
}

void GameControllerTest::testEagerAnalysisWithRemoteEngine() {
    FakeGateway gateway;
    QVERIFY(gateway.isListening());

    GameController controller;
    controller.setRemoteEngine(QStringLiteral("127.0.0.1"), gateway.port(),
                               QStringLiteral("stockfish-17"),
                               QStringLiteral("Stockfish"));
    QVERIFY(controller.hasRemoteEngine());

    // The engine is loaded immediately, as soon as it is selected.
    QTRY_VERIFY_WITH_TIMEOUT(controller.isEngineConnected(), 5000);
    QCOMPARE(controller.gatewayClient()->engineName(),
             QStringLiteral("RemoteMockEngine 1.0"));
    QCOMPARE(controller.gatewayClient()->state(), ChessGatewayClient::State::Ready);

    // Starting an analysis is immediate: the engine is already ready.
    controller.toggleAnalysis();
    QTRY_VERIFY_WITH_TIMEOUT(
        gateway.uciCommands().contains(QStringLiteral("go infinite")), 5000);

    controller.stopEngine();
    QCOMPARE(controller.gatewayClient()->state(), ChessGatewayClient::State::Disconnected);
    QVERIFY(!controller.isEngineConnected());
}

void GameControllerTest::testEagerComputerGameWithRemoteEngine() {
    FakeGateway gateway;
    QVERIFY(gateway.isListening());

    GameController controller;
    QSignalSpy humanTurnSpy(&controller, &GameController::humanTurnBegan);
    QSignalSpy stateSpy(&controller, &GameController::computerGameStateChanged);

    controller.setRemoteEngine(QStringLiteral("127.0.0.1"), gateway.port(),
                               QStringLiteral("stockfish-17"),
                               QStringLiteral("Stockfish"));

    ComputerGameSettings settings;
    settings.enginePlaysWhite = false;
    controller.startComputerGame(settings);
    QVERIFY(!controller.isComputerGameActive());

    // The game begins once the remote engine, loaded eagerly, is ready.
    QTRY_VERIFY_WITH_TIMEOUT(controller.isComputerGameActive(), 5000);
    QCOMPARE(humanTurnSpy.count(), 1);
    QCOMPARE(stateSpy.first().at(0).toBool(), true);

    // The computer answers a timed search through the gateway.
    QVERIFY(controller.requestMove({6, 0}, {5, 0}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.uciMoves().size(), 2, WaitTimeout);
    QVERIFY(controller.requestMove({6, 7}, {5, 7}));
    QTRY_COMPARE_WITH_TIMEOUT(controller.uciMoves().size(), 4, WaitTimeout);
    QCOMPARE(controller.uciMoves().at(3), QStringLiteral("e7e5"));

    controller.stopEngine();
    QVERIFY(!controller.isComputerGameActive());
}

void GameControllerTest::testAnnotationsAtCursor() {
    GameController controller;
    QCOMPARE(controller.moveCursor(), 0);
    QVERIFY(!controller.hasAnnotationsAtCursor());
    QVERIFY(controller.arrowsAtCursor().empty());
    QVERIFY(controller.squaresAtCursor().empty());

    QSignalSpy spy(&controller, &GameController::annotationsChanged);
    QSignalSpy historySpy(&controller, &GameController::historyChanged);

    const std::vector<UserArrow> arrows{{Rules::Position{6, 4}, Rules::Position{4, 4}, PgnAnnotations::greenColor()}};
    const std::vector<SquareAnnotation> squares{{Rules::Position{4, 4}, PgnAnnotations::redColor()}};

    controller.setAnnotationsAtCursor(arrows, squares, QStringLiteral("Opening note"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(historySpy.count(), 1);
    QVERIFY(controller.hasAnnotationsAtCursor());
    QCOMPARE(controller.arrowsAtCursor(), arrows);
    QCOMPARE(controller.squaresAtCursor(), squares);
    QCOMPARE(controller.commentAtCursor(), QStringLiteral("Opening note"));

    // Redundant set does not re-emit
    controller.setAnnotationsAtCursor(arrows, squares, QStringLiteral("Opening note"));
    QCOMPARE(spy.count(), 1);

    // Clear
    controller.clearAnnotationsAtCursor();
    QCOMPARE(spy.count(), 2);
    QVERIFY(!controller.hasAnnotationsAtCursor());
    QVERIFY(controller.arrowsAtCursor().empty());
    QVERIFY(controller.squaresAtCursor().empty());
}

void GameControllerTest::testAnnotationsPersistAcrossNavigation() {
    GameController controller;
    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // 1. e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // 1... e5
    QVERIFY(controller.requestMove({7, 6}, {5, 5})); // 2. Nf3
    QCOMPARE(controller.moveCursor(), 3);

    const std::vector<UserArrow> e4e5Arrow{{Rules::Position{4, 4}, Rules::Position{3, 4}, PgnAnnotations::blueColor()}};
    const std::vector<SquareAnnotation> e4Square{{Rules::Position{4, 4}, PgnAnnotations::greenColor()}};

    // Annotate ply 1 (after 1. e4)
    QVERIFY(controller.goToMove(1));
    QCOMPARE(controller.moveCursor(), 1);
    controller.setAnnotationsAtCursor(e4e5Arrow, {});

    // Annotate ply 0 (initial position)
    QVERIFY(controller.goToMove(0));
    QCOMPARE(controller.moveCursor(), 0);
    controller.setAnnotationsAtCursor({}, e4Square);

    // Navigate to ply 1 and verify
    QVERIFY(controller.stepForward());
    QCOMPARE(controller.moveCursor(), 1);
    QCOMPARE(controller.arrowsAtCursor(), e4e5Arrow);
    QVERIFY(controller.squaresAtCursor().empty());

    // Navigate to ply 2 and verify empty
    QVERIFY(controller.stepForward());
    QCOMPARE(controller.moveCursor(), 2);
    QVERIFY(!controller.hasAnnotationsAtCursor());

    // Navigate back to ply 0 and verify
    QVERIFY(controller.goToMove(0));
    QCOMPARE(controller.moveCursor(), 0);
    QVERIFY(controller.arrowsAtCursor().empty());
    QCOMPARE(controller.squaresAtCursor(), e4Square);
}

void GameControllerTest::testAnnotationsTruncatedOnBranching() {
    GameController controller;
    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // 1. e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // 1... e5
    QVERIFY(controller.requestMove({7, 6}, {5, 5})); // 2. Nf3
    QCOMPARE(controller.uciMoves().size(), 3);

    // Annotate ply 3
    const std::vector<SquareAnnotation> f3Square{{Rules::Position{5, 5}, PgnAnnotations::orangeColor()}};
    controller.setAnnotationsAtCursor({}, f3Square);
    QVERIFY(controller.hasAnnotationsAt(3));

    // Step back to ply 2 (after 1... e5)
    QVERIFY(controller.goToMove(2));
    QCOMPARE(controller.moveCursor(), 2);

    // Play alternative move 2. Bc4
    QVERIFY(controller.requestMove({7, 5}, {4, 2})); // f1c4
    QCOMPARE(controller.moveCursor(), 3);
    QCOMPARE(controller.uciMoves().size(), 3);
    QCOMPARE(controller.uciMoves().last(), QStringLiteral("f1c4"));

    // The old annotation at ply 3 must have been replaced with a fresh empty entry
    QVERIFY(!controller.hasAnnotationsAt(3));
    QVERIFY(controller.squaresAtCursor().empty());
}

void GameControllerTest::testPgnTextWithAnnotationsRoundTrip() {
    GameController controller;

    // Ply 0 annotation
    const std::vector<SquareAnnotation> e4Square{{Rules::Position{4, 4}, PgnAnnotations::greenColor()}};
    controller.setAnnotationsAtCursor({}, e4Square);

    // 1. e4 with arrow and comment
    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    const std::vector<UserArrow> e2e4Arrow{{Rules::Position{6, 4}, Rules::Position{4, 4}, PgnAnnotations::greenColor()}};
    controller.setAnnotationsAtCursor(e2e4Arrow, {}, QStringLiteral("Best by test"));

    // 1... e5 with red square
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    const std::vector<SquareAnnotation> e5Square{{Rules::Position{3, 4}, PgnAnnotations::redColor()}};
    controller.setAnnotationsAtCursor({}, e5Square);

    const QString pgn = controller.pgnText();
    QVERIFY(pgn.contains(QStringLiteral("{ [%csl Ge4] }")));
    QVERIFY(pgn.contains(QStringLiteral("1. e4 { [%cal Ge2e4] Best by test }")));
    QVERIFY(pgn.contains(QStringLiteral("1... e5 { [%csl Re5] }")));

    // Reload into another controller
    GameController reloaded;
    QVERIFY(reloaded.loadPgn(pgn));
    QCOMPARE(reloaded.uciMoves().size(), 2);

    // Verify ply 0
    QCOMPARE(reloaded.squaresAt(0), e4Square);
    QVERIFY(reloaded.arrowsAt(0).empty());

    // Verify ply 1
    QCOMPARE(reloaded.arrowsAt(1), e2e4Arrow);
    QCOMPARE(reloaded.commentAt(1), QStringLiteral("Best by test"));

    // Verify ply 2
    QCOMPARE(reloaded.squaresAt(2), e5Square);

    // Full round-trip PGN match
    QCOMPARE(reloaded.pgnText(), pgn);
}

void GameControllerTest::testGameAuditExportsAndReloadsMarkers() {
    const QString scriptPath = writeAuditEngineScript(QStringLiteral("mock_audit_engine.sh"));
    GameController controller;
    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e5
    const std::vector<SquareAnnotation> manualSquare{{Rules::Position{4, 4}, PgnAnnotations::greenColor()}};
    controller.setAnnotationsAtCursor({}, manualSquare, QStringLiteral("Manual note"));
    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, WaitTimeout);

    QSignalSpy progressSpy(&controller, &GameController::auditProgressChanged);
    QSignalSpy completedSpy(&controller, &GameController::auditCompleted);
    QVERIFY(controller.canStartGameAudit());
    QVERIFY(controller.startGameAudit());
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, WaitTimeout);
    QVERIFY(completedSpy.first().first().toBool());
    QCOMPARE(progressSpy.last().at(0).toInt(), 3);
    QCOMPARE(progressSpy.last().at(1).toInt(), 3);

    QCOMPARE(controller.auditAt(1).severity, AuditSeverity::Inaccuracy);
    QCOMPARE(controller.auditAt(1).centipawnLoss, 90);
    QCOMPARE(controller.auditAt(2).severity, AuditSeverity::Mistake);
    QCOMPARE(controller.auditAt(2).centipawnLoss, 140);
    QCOMPARE(controller.squaresAt(2), manualSquare);
    QCOMPARE(controller.commentAt(2), QStringLiteral("Manual note"));
    const QString pgn = controller.pgnText();
    QVERIFY(pgn.contains(QStringLiteral("$6")));
    QVERIFY(pgn.contains(QStringLiteral("$2")));
    QVERIFY(pgn.contains(QStringLiteral("[%chessgui-audit severity=inaccuracy")));

    GameController reloaded;
    QVERIFY(reloaded.loadPgn(pgn));
    QCOMPARE(reloaded.auditAt(1), controller.auditAt(1));
    QCOMPARE(reloaded.auditAt(2), controller.auditAt(2));
    QCOMPARE(reloaded.squaresAt(2), manualSquare);
    QCOMPARE(reloaded.commentAt(2), QStringLiteral("Manual note"));
    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testGameAuditCanBeCancelledWithoutPartialResults() {
    const QString scriptPath = writeAuditEngineScript(QStringLiteral("mock_cancel_audit_engine.sh"));
    GameController controller;
    QVERIFY(controller.requestMove({6, 4}, {4, 4}));
    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, WaitTimeout);
    QVERIFY(controller.startGameAudit());
    controller.cancelGameAudit();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isGameAuditActive(), WaitTimeout);
    QVERIFY(controller.auditFindings().isEmpty());
    QVERIFY(!controller.pgnText().contains(QStringLiteral("chessgui-audit")));
    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testGameAuditMarksLostForcedMateAsBlunder() {
    const QString scriptPath = writeMateAuditEngineScript(QStringLiteral("mock_mate_audit_engine.sh"));
    GameController controller;
    QVERIFY(controller.requestMove({6, 4}, {4, 4}));
    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, WaitTimeout);
    QVERIFY(controller.startGameAudit());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isGameAuditActive(), WaitTimeout);
    QCOMPARE(controller.auditAt(1).severity, AuditSeverity::Blunder);
    QVERIFY(controller.auditAt(1).forcedMate);
    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testPromotionAndUnderpromotion() {
    GameController controller;
    const QString fen = QStringLiteral("k7/4P3/8/8/8/8/8/K7 w - - 0 1");
    QVERIFY(controller.loadFen(fen));

    const Rules::Position e7{1, 4};
    const Rules::Position e8{0, 4};
    const Rules::Position d8{0, 3};

    // Non-promotion move check
    QVERIFY(!controller.isPromotionMove({7, 0}, {6, 0}));
    // Illegal pawn move check
    QVERIFY(!controller.isPromotionMove(e7, d8));
    // Valid promotion move check
    QVERIFY(controller.isPromotionMove(e7, e8));

    // 1. Underpromote to Knight
    QVERIFY(controller.requestMove(e7, e8, Rules::PieceType::Knight));
    const auto knight = controller.rules().pieceAt(e8);
    QVERIFY(knight.has_value());
    QCOMPARE(knight->type, Rules::PieceType::Knight);
    QCOMPARE(controller.uciMoves().last(), QStringLiteral("e7e8n"));
    QVERIFY(controller.pgnMoves().last().endsWith(QStringLiteral("=N")) ||
            controller.pgnMoves().last().endsWith(QStringLiteral("=N+")));

    // 2. Test underpromotion to Rook
    controller.loadFen(fen);
    QVERIFY(controller.requestMove(e7, e8, Rules::PieceType::Rook));
    const auto rook = controller.rules().pieceAt(e8);
    QVERIFY(rook.has_value());
    QCOMPARE(rook->type, Rules::PieceType::Rook);
    QCOMPARE(controller.uciMoves().last(), QStringLiteral("e7e8r"));

    // 3. Test underpromotion to Bishop
    controller.loadFen(fen);
    QVERIFY(controller.requestMove(e7, e8, Rules::PieceType::Bishop));
    const auto bishop = controller.rules().pieceAt(e8);
    QVERIFY(bishop.has_value());
    QCOMPARE(bishop->type, Rules::PieceType::Bishop);
    QCOMPARE(controller.uciMoves().last(), QStringLiteral("e7e8b"));

    // 4. Test default promotion (PieceType::None) -> promotes to Queen
    controller.loadFen(fen);
    QVERIFY(controller.requestMove(e7, e8, Rules::PieceType::None));
    const auto queen = controller.rules().pieceAt(e8);
    QVERIFY(queen.has_value());
    QCOMPARE(queen->type, Rules::PieceType::Queen);
    QCOMPARE(controller.uciMoves().last(), QStringLiteral("e7e8q"));
}

void GameControllerTest::testGoToStartAndGoToEnd() {
    GameController controller;
    const QString pgn = QStringLiteral("1. e4 e5 2. Nf3 Nc6 *");
    QVERIFY(controller.loadPgn(pgn));
    QCOMPARE(controller.moveCursor(), 4);
    QVERIFY(controller.canStepBack());
    QVERIFY(!controller.canStepForward());

    // Go to start
    QVERIFY(controller.goToStart());
    QCOMPARE(controller.moveCursor(), 0);
    QVERIFY(!controller.canStepBack());
    QVERIFY(controller.canStepForward());

    // Go to start again should return false (already at start)
    QVERIFY(!controller.goToStart());

    // Step forward one
    QVERIFY(controller.stepForward());
    QCOMPARE(controller.moveCursor(), 1);

    // Go to end
    QVERIFY(controller.goToEnd());
    QCOMPARE(controller.moveCursor(), 4);
    QVERIFY(controller.canStepBack());
    QVERIFY(!controller.canStepForward());

    // Go to end again should return false (already at end)
    QVERIFY(!controller.goToEnd());
}

void GameControllerTest::testDrawClaims() {
    GameController controller;
    QSignalSpy finishedSpy(&controller, &GameController::gameFinished);

    // Fifty-move rule: one quiet move reaches 100 halfmoves.
    QVERIFY(controller.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/N3K2R w - - 99 60")));
    QVERIFY(!controller.canClaimDraw());
    QVERIFY(controller.requestMove({7, 0}, {6, 2})); // Na1-c2
    QVERIFY(controller.canClaimDraw());
    controller.claimDraw();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toString(), QStringLiteral("1/2-1/2"));
    QVERIFY(finishedSpy.first().at(1).toString().contains(
        QStringLiteral("fifty-move")));
    QVERIFY(controller.pgnText().contains(
        QStringLiteral("[Result \"1/2-1/2\"]")));
    QVERIFY(!controller.canClaimDraw());

    // Threefold repetition: return to the same position three times.
    QVERIFY(controller.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/N3K1N1 w - - 0 1")));
    for (int cycle = 0; cycle < 2; ++cycle) {
        QVERIFY(controller.requestMove({7, 0}, {6, 2})); // Nc2
        QVERIFY(controller.requestMove({0, 4}, {0, 3})); // Kd8
        QVERIFY(controller.requestMove({6, 2}, {7, 0})); // Na1
        QVERIFY(controller.requestMove({0, 3}, {0, 4})); // Ke8
    }
    QVERIFY(controller.canClaimDraw());
    controller.claimDraw();
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).toString(), QStringLiteral("1/2-1/2"));
    QVERIFY(finishedSpy.at(1).at(1).toString().contains(
        QStringLiteral("threefold")));

    // No claim is possible before a draw condition is reached.
    controller.newGame();
    QVERIFY(!controller.canClaimDraw());
    controller.claimDraw();
    QCOMPARE(finishedSpy.count(), 2);
}

void GameControllerTest::testMaterialBalance() {
    GameController controller;

    // The initial position is balanced.
    QCOMPARE(controller.materialBalance(Rules::Color::White), 0);
    QCOMPARE(controller.materialBalance(Rules::Color::Black), 0);

    // 1. e4 e5 2. Nf3 Nc6 3. Nxe5, leaving White a pawn up.
    QVERIFY(controller.requestMove({6, 4}, {4, 4}));
    QVERIFY(controller.requestMove({1, 4}, {3, 4}));
    QVERIFY(controller.requestMove({7, 6}, {5, 5}));
    QVERIFY(controller.requestMove({0, 1}, {2, 2}));
    QVERIFY(controller.requestMove({5, 5}, {3, 4}));

    QCOMPARE(controller.materialBalance(Rules::Color::White), 100);
    QCOMPARE(controller.materialBalance(Rules::Color::Black), -100);
}

void GameControllerTest::testEvaluationCurve() {
    GameController controller;
    QSignalSpy curveSpy(&controller, &GameController::evaluationCurveChanged);

    // The curve always describes the starting position.
    QCOMPARE(controller.evaluationCurve().size(), 1);

    QVERIFY(controller.requestMove({6, 4}, {4, 4}));
    QCOMPARE(controller.evaluationCurve().size(), 2);
    QVERIFY(controller.requestMove({1, 4}, {3, 4}));
    QCOMPARE(controller.evaluationCurve().size(), 3);
    QVERIFY(curveSpy.count() >= 2);

    for (const double value : controller.evaluationCurve()) {
        QVERIFY(value >= 0.0 && value <= 100.0);
    }

    // Playing from the middle discards the abandoned line with its points.
    QVERIFY(controller.goToMove(1));
    QVERIFY(controller.requestMove({0, 1}, {2, 2}));
    QCOMPARE(controller.evaluationCurve().size(), 3);

    // Loading a game rebuilds the whole curve for the replayed moves.
    QVERIFY(controller.loadPgn(QStringLiteral("1. e4 e5 2. Nf3 Nc6 3. Bb5 *")));
    QCOMPARE(controller.evaluationCurve().size(), 6);
}

void GameControllerTest::testEngineRefinesCurve() {
    const QString scriptPath =
        writeMockEngineScript(QStringLiteral("mock_curve_engine.sh"));
    GameController controller;

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);
    QVERIFY(controller.requestMove({6, 4}, {4, 4}));
    QCOMPARE(controller.evaluationCurve().size(), 2);

    // The mock engine reports 20 centipawns for the side to move, which is
    // Black here: the point for the current ply becomes the engine score.
    QSignalSpy curveSpy(&controller, &GameController::evaluationCurveChanged);
    controller.startAnalysis();
    QTRY_VERIFY_WITH_TIMEOUT(curveSpy.count() >= 1, 2000);
    QCOMPARE(controller.evaluationCurve().last(),
             UciParser::scoreToWinningPercentage(-20.0));
}

void GameControllerTest::testTakeBackInFreePlay() {
    GameController controller;
    QSignalSpy statusSpy(&controller, &GameController::statusMessage);
    QSignalSpy historySpy(&controller, &GameController::historyChanged);

    QVERIFY(!controller.canTakeBack());
    QVERIFY(!controller.takeBack());

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    QVERIFY(controller.requestMove({6, 3}, {4, 3})); // d2d4
    QVERIFY(controller.canTakeBack());
    QCOMPARE(controller.uciMoves().size(), 3);

    const int historyBefore = historySpy.count();
    QVERIFY(controller.takeBack());
    QCOMPARE(controller.uciMoves().size(), 2);
    QCOMPARE(controller.moveCursor(), 2);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(controller.pgnText().contains(QStringLiteral("1. e4 e5")));
    QVERIFY(!controller.pgnText().contains(QStringLiteral("d4")));
    QVERIFY(historySpy.count() > historyBefore);
    QCOMPARE(statusSpy.count(), 1);

    // The evaluation curve follows the shortened history.
    QCOMPARE(controller.evaluationCurve().size(), 3);

    // A review position must be left before another move can be taken back.
    QVERIFY(controller.stepBack());
    QVERIFY(!controller.canTakeBack());
    QVERIFY(!controller.takeBack());
    QCOMPARE(controller.uciMoves().size(), 2);
    QVERIFY(controller.goToEnd());
    QVERIFY(controller.canTakeBack());

    // Taking back every remaining move returns to the initial position.
    QVERIFY(controller.takeBack());
    QVERIFY(controller.takeBack());
    QVERIFY(controller.uciMoves().isEmpty());
    QCOMPARE(controller.moveCursor(), 0);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QCOMPARE(controller.evaluationCurve().size(), 1);
    QVERIFY(!controller.canTakeBack());
}

void GameControllerTest::testTakeBackAfterFreePlayCheckmate() {
    GameController controller;

    // Ra8 is mate. A free game is not resumed by a result, so the mating move
    // can be taken back to try another continuation.
    QVERIFY(controller.loadFen(QStringLiteral("6k1/5ppp/8/8/8/8/8/R6K w - - 0 1")));
    QVERIFY(controller.requestMove({7, 0}, {0, 0}));
    QVERIFY(controller.rules().isCheckmate(Rules::Color::Black));

    QVERIFY(controller.canTakeBack());
    QVERIFY(controller.takeBack());
    QVERIFY(controller.uciMoves().isEmpty());
    QVERIFY(!controller.rules().isCheckmate(Rules::Color::Black));
    QVERIFY(controller.rules().pieceAt({7, 0}).has_value());
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
}

void GameControllerTest::testTakeBackInComputerGame() {
    const QString scriptPath =
        writeMockEngineScript(QStringLiteral("mock_takeback_engine.sh"));
    GameController controller;
    QSignalSpy humanTurnSpy(&controller, &GameController::humanTurnBegan);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = false;
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());
    QVERIFY(!controller.canTakeBack());

    // The engine answers the human's move, either from the book or from its
    // timed search; wait until it is the human's turn again.
    QVERIFY(controller.requestMove({6, 0}, {5, 0})); // a2a3
    QTRY_VERIFY_WITH_TIMEOUT(controller.rules().currentPlayer() == Rules::Color::White &&
                                 controller.uciMoves().size() == 2,
                             WaitTimeout);
    QVERIFY(controller.canTakeBack());

    const int humanTurnsBefore = humanTurnSpy.count();
    QVERIFY(controller.takeBack());
    QCOMPARE(controller.uciMoves().size(), 0);
    QCOMPARE(controller.moveCursor(), 0);
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(controller.isComputerGameActive());
    QVERIFY(humanTurnSpy.count() > humanTurnsBefore);
    // Nothing is left to take back, and the engine is not asked to move again.
    QVERIFY(!controller.canTakeBack());

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testResignAgainstEngine() {
    const QString scriptPath =
        writeMockEngineScript(QStringLiteral("mock_resign_engine.sh"));
    GameController controller;
    QSignalSpy finishedSpy(&controller, &GameController::gameFinished);

    // Resigning is only available in a game against the engine.
    QVERIFY(!controller.canResign());
    controller.resign();
    QCOMPARE(finishedSpy.count(), 0);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = false;
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());
    QVERIFY(controller.canResign());

    controller.resign();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toString(), QStringLiteral("0-1"));
    QVERIFY(finishedSpy.first().at(1).toString().contains(QStringLiteral("resigned")));
    QVERIFY(!controller.isComputerGameActive());
    QVERIFY(!controller.canResign());
    QVERIFY(controller.pgnText().contains(QStringLiteral("[Result \"0-1\"]")));
    // A finished game against the engine is not resumed by a take-back.
    QVERIFY(!controller.canTakeBack());

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testDrawOffers() {
    // Free play: the two players share the board, so the offer is the agreement.
    GameController freePlay;
    QSignalSpy freeFinished(&freePlay, &GameController::gameFinished);
    QVERIFY(!freePlay.canOfferDraw());
    freePlay.offerDraw();
    QCOMPARE(freeFinished.count(), 0);

    QVERIFY(freePlay.requestMove({6, 4}, {4, 4}));
    QVERIFY(freePlay.requestMove({1, 4}, {3, 4}));
    QVERIFY(freePlay.canOfferDraw());
    freePlay.offerDraw();
    QCOMPARE(freeFinished.count(), 1);
    QCOMPARE(freeFinished.first().at(0).toString(), QStringLiteral("1/2-1/2"));
    QVERIFY(freePlay.pgnText().contains(QStringLiteral("[Result \"1/2-1/2\"]")));
    QVERIFY(!freePlay.canOfferDraw());

    // Against the engine: a balanced position is accepted.
    const QString scriptPath =
        writeMockEngineScript(QStringLiteral("mock_draw_engine.sh"));
    GameController controller;
    QSignalSpy finishedSpy(&controller, &GameController::gameFinished);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    ComputerGameSettings settings;
    settings.enginePlaysWhite = false;
    controller.startComputerGame(settings);
    QVERIFY(controller.isComputerGameActive());
    QVERIFY(controller.canOfferDraw());

    controller.offerDraw();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toString(), QStringLiteral("1/2-1/2"));
    QVERIFY(finishedSpy.first().at(1).toString().contains(QStringLiteral("accepted")));
    QVERIFY(!controller.canOfferDraw());
    QVERIFY(!controller.isComputerGameActive());

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testAnalysisSettings() {
    const QString scriptPath =
        writeMockEngineScript(QStringLiteral("mock_analysis_settings.sh"));
    GameController controller;

    QCOMPARE(controller.analysisDepth(), 0);
    QCOMPARE(controller.analysisMultiPv(), 1);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    QSignalSpy sentSpy(controller.engine(), &EngineBackend::rawLineSent);
    const auto wasSent = [&sentSpy](const QString &command) {
        for (const QList<QVariant> &arguments : sentSpy) {
            if (arguments.at(0).toString() == command) {
                return true;
            }
        }
        return false;
    };

    controller.setAnalysisSettings(12, 3);
    QCOMPARE(controller.analysisDepth(), 12);
    QCOMPARE(controller.analysisMultiPv(), 3);

    controller.startAnalysis();
    QTRY_VERIFY_WITH_TIMEOUT(wasSent(QStringLiteral("go depth 12")), WaitTimeout);
    QTRY_VERIFY_WITH_TIMEOUT(
        wasSent(QStringLiteral("setoption name MultiPV value 3")), WaitTimeout);

    // Out-of-range values are clamped to the supported limits.
    controller.setAnalysisSettings(-5, 99);
    QCOMPARE(controller.analysisDepth(), 0);
    QCOMPARE(controller.analysisMultiPv(), 8);

    controller.stopAnalysis();
    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testGameAuditDepthAndReport() {
    const QString scriptPath =
        writeReportAuditEngineScript(QStringLiteral("mock_report_audit_engine.sh"));
    GameController controller;

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    // The depth is configurable and clamped to a usable range.
    QCOMPARE(controller.gameAuditDepth(), 18);
    controller.setGameAuditDepth(12);
    QCOMPARE(controller.gameAuditDepth(), 12);
    controller.setGameAuditDepth(0);
    QCOMPARE(controller.gameAuditDepth(), 1);
    controller.setGameAuditDepth(12);

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5

    QVERIFY(!controller.auditReport().valid);
    QSignalSpy reportSpy(&controller, &GameController::auditReportChanged);
    QVERIFY(controller.canStartGameAudit());
    QVERIFY(controller.startGameAudit());
    QTRY_VERIFY_WITH_TIMEOUT(controller.auditReport().valid, WaitTimeout);

    const AuditReport &report = controller.auditReport();
    QCOMPARE(report.depth, 12);
    QCOMPARE(report.plies, 2);
    QCOMPARE(report.analysedPositions, 3);
    QCOMPARE(report.sanByPly.size(), 3);
    QCOMPARE(report.sanByPly.at(1), QStringLiteral("e4"));
    QCOMPARE(report.sanByPly.at(2), QStringLiteral("e5"));
    QVERIFY(reportSpy.count() >= 1);

    // White loses 300 centipawns with e4, Black answers with the best move.
    QCOMPARE(report.findings.size(), 1);
    const AuditFinding &finding = report.findings.first();
    QVERIFY(finding.severity == AuditSeverity::Blunder);
    QCOMPARE(finding.ply, 1);
    QCOMPARE(finding.centipawnLoss, 300);
    QCOMPARE(finding.bestMove, QStringLiteral("d2d4"));
    QCOMPARE(finding.bestMoveSan, QStringLiteral("d4"));

    QCOMPARE(report.white.blunders, 1);
    QCOMPARE(report.white.inaccuracies, 0);
    QCOMPARE(report.white.mistakes, 0);
    QCOMPARE(report.white.scoredMoves, 1);
    QCOMPARE(report.white.averageCentipawnLoss, 300.0);
    QVERIFY(report.white.accuracy > 25.0);
    QVERIFY(report.white.accuracy < 35.0);

    QCOMPARE(report.black.blunders, 0);
    QCOMPARE(report.black.scoredMoves, 1);
    QCOMPARE(report.black.averageCentipawnLoss, 0.0);
    QVERIFY(report.black.accuracy > 99.0);

    QCOMPARE(report.centipawnLossByPly.size(), 3);
    QCOMPARE(report.centipawnLossByPly.at(1), 300);
    QCOMPARE(report.centipawnLossByPly.at(2), 0);

    // Playing on makes the report describe a game that no longer exists.
    QVERIFY(controller.requestMove({6, 2}, {4, 2})); // c2c4
    QVERIFY(!controller.auditReport().valid);

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testGameAuditFollowsTheBoard() {
    const QString scriptPath =
        writeReportAuditEngineScript(QStringLiteral("mock_follow_audit_engine.sh"));
    GameController controller;

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    QVERIFY(controller.requestMove({6, 3}, {4, 3})); // d2d4
    QCOMPARE(controller.moveCursor(), 3);

    // The audit walks the main line: the board must visit every analysed
    // position so that the reviewer can follow the analysis.
    QVector<int> visited;
    const auto connection = connect(&controller, &GameController::positionChanged,
                                    [&controller, &visited] {
                                        visited.append(controller.moveCursor());
                                    });

    QVERIFY(controller.canStepBack());
    QVERIFY(controller.startGameAudit());
    QVERIFY(controller.isGameAuditActive());
    // The analysis owns the board: navigation is refused while it runs.
    QVERIFY(!controller.canStepBack());
    QVERIFY(!controller.canStepForward());

    QTRY_VERIFY_WITH_TIMEOUT(controller.auditReport().valid, WaitTimeout);
    disconnect(connection);

    QVERIFY(visited.size() >= 4);
    QVERIFY(visited.mid(0, 4) == (QVector<int>{0, 1, 2, 3}));

    // The user's position is given back at the end.
    QCOMPARE(controller.moveCursor(), 3);
    QVERIFY(controller.canStepBack());
    QVERIFY(!controller.canStepForward());

    // The engine was left on the position the user is looking at: after
    // 1. e4 e5 2. d4 it is Black to move.
    QCOMPARE(controller.rules().currentPlayer(), Rules::Color::Black);

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testStoppingCancelsTheGameAudit() {
    const QString scriptPath =
        writeReportAuditEngineScript(QStringLiteral("mock_stop_audit_engine.sh"));
    GameController controller;
    QSignalSpy completedSpy(&controller, &GameController::auditCompleted);

    QVERIFY(controller.startEngine(scriptPath));
    QTRY_COMPARE_WITH_TIMEOUT(controller.engine()->state(), UciEngine::State::Ready, 2000);

    QVERIFY(controller.requestMove({6, 4}, {4, 4})); // e2e4
    QVERIFY(controller.requestMove({1, 4}, {3, 4})); // e7e5
    QVERIFY(controller.requestMove({6, 3}, {4, 3})); // d2d4
    const int savedCursor = controller.moveCursor();

    QVERIFY(controller.startGameAudit());
    QVERIFY(controller.isGameAuditActive());

    // Stopping the engine during the analysis cancels the whole run: it must
    // not walk on to the next position and restart the search.
    controller.stopAnalysis();
    QVERIFY(!controller.isGameAuditActive());
    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.first().at(0).toBool(), false);
    QVERIFY(!controller.auditReport().valid);

    // Let the engine answer the stop and the pending timers run: the audit
    // must stay cancelled.
    QTest::qWait(300);
    QVERIFY(!controller.isGameAuditActive());
    QVERIFY(!controller.auditReport().valid);
    QCOMPARE(controller.moveCursor(), savedCursor);
    QCOMPARE(completedSpy.count(), 1);

    // A new analysis can be started afterwards.
    QTRY_VERIFY_WITH_TIMEOUT(controller.engine()->state() == UciEngine::State::Ready,
                             WaitTimeout);
    QVERIFY(controller.startGameAudit());
    QTRY_VERIFY_WITH_TIMEOUT(controller.auditReport().valid, WaitTimeout);

    controller.stopEngine();
    QFile::remove(scriptPath);
}

void GameControllerTest::testEvaluationBreakdownAndHeuristicHint() {
    GameController controller;
    QSignalSpy breakdownSpy(&controller,
                            &GameController::evaluationBreakdownChanged);
    QSignalSpy recommendedSpy(&controller,
                              &GameController::recommendedMovePreviewChanged);

    // Every evaluation update explains itself.
    QVERIFY(controller.loadFen(QStringLiteral("4k3/8/8/8/8/8/P7/4K3 w - - 0 1")));
    QVERIFY(breakdownSpy.count() > 0);

    // Without an engine the recommended-move preview comes from the heuristic
    // search of the live evaluation, and it is a legal move.
    QVERIFY(!controller.isEngineConnected());
    controller.setRecommendedMovePreviewEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(recommendedSpy.count() > 0, WaitTimeout);
    const auto preview =
        recommendedSpy.last().at(0).value<std::optional<Rules::Move>>();
    QVERIFY(preview.has_value());
    QVERIFY(controller.rules().isValidMove(*preview));

    // Turning the preview off clears the arrow.
    controller.setRecommendedMovePreviewEnabled(false);
    QVERIFY(!recommendedSpy.last().at(0).value<std::optional<Rules::Move>>().has_value());

    // A finished game has no hint to give.
    controller.setRecommendedMovePreviewEnabled(true);
    QVERIFY(controller.loadFen(QStringLiteral("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")));
    QVERIFY(controller.rules().isGameOver());
    QTRY_VERIFY_WITH_TIMEOUT(
        recommendedSpy.count() > 0 &&
            !recommendedSpy.last().at(0).value<std::optional<Rules::Move>>().has_value(),
        WaitTimeout);
}

void GameControllerTest::testEvaluationDepthSetting() {
    GameController controller;
    QCOMPARE(controller.evaluationDepth(), HeuristicEval::DefaultSearchDepth);

    controller.setEvaluationDepth(3);
    QCOMPARE(controller.evaluationDepth(), 3);
    controller.setEvaluationDepth(99);
    QCOMPARE(controller.evaluationDepth(), HeuristicEval::MaxSearchDepth);
    controller.setEvaluationDepth(0);
    QCOMPARE(controller.evaluationDepth(), 1);

    // Changing how the evaluator is configured only shows up after a refresh,
    // which recomputes the score and the whole-game curve.
    QVERIFY(controller.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));
    controller.setEvaluationDepth(2);
    QSignalSpy scoreSpy(&controller, &GameController::evaluationScoreChanged);
    QSignalSpy curveSpy(&controller, &GameController::evaluationCurveChanged);
    controller.refreshEvaluation();

    QVERIFY(scoreSpy.count() > 0);
    QVERIFY(curveSpy.count() > 0);
    QCOMPARE(controller.evaluationCurve().size(), 1);
    const double start = controller.evaluationCurve().first();
    QVERIFY(start >= 0.0 && start <= 100.0);
}

void GameControllerTest::testEvaluationCurveIsComputedInTheBackground() {
    GameController controller;
    // A deeper search than the static pass, so that the points really change as
    // the worker walks the game.
    controller.setEvaluationDepth(3);
    QSignalSpy progressSpy(&controller,
                           &GameController::evaluationCurveProgressChanged);
    QSignalSpy curveSpy(&controller, &GameController::evaluationCurveChanged);

    int progressiveChanges = 0;
    int deepestAnalysedWhileComputing = 0;
    connect(&controller, &GameController::evaluationCurveChanged, &controller,
            [&controller, &progressiveChanges] {
                if (controller.isEvaluationCurveComputing()) {
                    ++progressiveChanges;
                }
            });
    connect(&controller, &GameController::evaluationCurveProgressChanged,
            &controller,
            [&controller, &deepestAnalysedWhileComputing](int, int) {
                if (controller.isEvaluationCurveComputing()) {
                    deepestAnalysedWhileComputing = qMax(
                        deepestAnalysedWhileComputing,
                        controller.evaluationCurveAnalysedCount());
                }
            });

    // The static pass is synchronous, so the curve is complete and in range
    // before the worker has searched anything.
    QVERIFY(controller.loadPgn(QStringLiteral(
        "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 *")));
    const int expectedPoints = controller.uciMoves().size() + 1;
    QCOMPARE(controller.evaluationCurve().size(), expectedPoints);
    QVERIFY(progressSpy.count() > 0);
    QCOMPARE(progressSpy.first().at(1).toInt(), expectedPoints);
    // The whole curve is provisional until the analysis has walked it, which
    // is what the graph shows by fading the tail.
    QCOMPARE(controller.evaluationCurveAnalysedCount(), 0);
    QVERIFY(controller.isEvaluationCurveComputing());

    // Every point the worker scores is published on the spot, so the graph
    // follows the analysis and never shows an out-of-range value on the way.
    bool allInRange = true;
    connect(&controller, &GameController::evaluationCurveChanged, &controller,
            [&controller, &allInRange] {
                for (const double value : controller.evaluationCurve()) {
                    if (value < 0.0 || value > 100.0) {
                        allInRange = false;
                    }
                }
            });

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isEvaluationCurveComputing(),
                             WaitTimeout);
    QCOMPARE(controller.evaluationCurve().size(), expectedPoints);
    for (const double value : controller.evaluationCurve()) {
        QVERIFY(value >= 0.0 && value <= 100.0);
    }
    QVERIFY(allInRange);
    // The analysis visibly advanced while it was running, and everything is
    // settled once it is over.
    QVERIFY(deepestAnalysedWhileComputing > 0);
    QCOMPARE(controller.evaluationCurveAnalysedCount(), -1);

    // The graph was refreshed while the worker was still walking the curve, not
    // only when the whole line landed.
    QVERIFY2(progressiveChanges > 0,
             qPrintable(QStringLiteral("%1 changes for %2 points")
                            .arg(curveSpy.count())
                            .arg(expectedPoints)));

    int highestProgress = 0;
    for (const QList<QVariant> &arguments : progressSpy) {
        highestProgress = qMax(highestProgress, arguments.at(0).toInt());
    }
    QCOMPARE(highestProgress, expectedPoints);
    QVERIFY(curveSpy.count() > 0);

    // A game change restarts the computation for the new line.
    QVERIFY(controller.loadPgn(QStringLiteral("1. d4 d5 *")));
    QCOMPARE(controller.evaluationCurve().size(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isEvaluationCurveComputing(),
                             WaitTimeout);
    QCOMPARE(controller.evaluationCurve().size(), 3);
}

void GameControllerTest::testEvaluationCurveCanBeCancelled() {
    GameController controller;
    // A deep curve is slow enough that the cancel below matters, and the
    // single thread keeps the expectations deterministic.
    HeuristicEval::setSearchThreads(1);
    controller.setEvaluationDepth(HeuristicEval::MaxSearchDepth);
    QVERIFY(controller.loadPgn(QStringLiteral(
        "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 "
        "7. Bb3 d6 8. c3 O-O 9. h3 Nb8 10. d4 Nbd7 *")));

    const QVector<double> staticCurve = controller.evaluationCurve();
    QCOMPARE(staticCurve.size(), controller.uciMoves().size() + 1);
    QVERIFY(controller.isEvaluationCurveComputing());

    controller.cancelEvaluationCurve();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isEvaluationCurveComputing(),
                             WaitTimeout);
    // The cancelled computation leaves the instant curve in place, and the
    // graph goes back to drawing it in full.
    QCOMPARE(controller.evaluationCurve(), staticCurve);
    QCOMPARE(controller.evaluationCurveAnalysedCount(), -1);
    HeuristicEval::setSearchThreads(0);
}

QTEST_GUILESS_MAIN(GameControllerTest)
#include "gamecontroller_test.moc"