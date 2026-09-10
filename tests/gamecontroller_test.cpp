//
// Unit tests for GameController.
//

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaType>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>

#include "fakegateway.h"
#include "gamecontroller.h"
#include "gatewayclient.h"
#include "uciengine.h"

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

} // namespace

class GameControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialState();
    void testLoadFen();
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
    void testGameAuditCanBeCancelledWithoutPartialResults();
    void testGameAuditMarksLostForcedMateAsBlunder();
    void testPromotionAndUnderpromotion();
    void testGoToStartAndGoToEnd();
    void testDrawClaims();
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
    QCOMPARE(evaluationSpy.count(), 1);

    QVERIFY(!controller.loadFen(QStringLiteral("not a fen")));
    QCOMPARE(controller.initialFen(), fen);
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
    QCOMPARE(evaluationSpy.count(), 1);
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

QTEST_GUILESS_MAIN(GameControllerTest)
#include "gamecontroller_test.moc"
