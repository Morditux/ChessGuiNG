//
// Unit tests for Rules FEN / PGN and SAN parsing.
//

#include <QSignalSpy>
#include <QTest>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QToolBar>
#include <QTemporaryDir>
#include <QWidgetAction>

#include <algorithm>

#include "chessboard.h"
#include "computergamedialog.h"
#include "mainwindow.h"
#include "movelistwidget.h"
#include "pendulumwidget.h"
#include "rules.h"

// Concatenates every cell of the move list widget, mimicking the plain-text
// rendering used before the move list became a table.
QString moveListText(MainWindow &window) {
    QString text;
    const QAbstractItemModel *model = window.moveListWidget()->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        for (int column = 0; column < model->columnCount(); ++column) {
            text += model->index(row, column).data().toString();
        }
    }
    return text;
}

class RulesTest : public QObject {
    Q_OBJECT

private slots:
    void testFenStartPos();
    void testFenMidGame();
    void testFenEndgame();
    void testFenInvalid();
    void testToFenRoundTrip();
    void testSanParsingAndExecution();
    void testSanPromotion();
    void testSanDisambiguation();
    void testPgnLoading();
    void testPgnLoadingWithComments();
    void testMainWindowPasteFen();
    void testMainWindowNewGame();
    void testWhiteToPlayCheckBox();
    void testFlipBoardButton();
    void testMovePreviews();
    void testLastMoveTracking();
    void testLastMoveHighlight();
    void testBoardEmitsMoveIntentWithoutMutating();
    void testMovePreviewControls();
    void testMainWindowLoadPgnContent();
    void testMainWindowRecognizesScreenshotWhenProvided();
    void testComputerGameDialogSettings();
};

void RulesTest::testFenStartPos() {
    Rules rules;
    const QString startFen = QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    QVERIFY(rules.loadFen(startFen));
    QCOMPARE(rules.currentPlayer(), Rules::Color::White);

    const auto wKing = rules.pieceAt({7, 4});
    QVERIFY(wKing.has_value());
    QCOMPARE(wKing->type, Rules::PieceType::King);
    QCOMPARE(wKing->color, Rules::Color::White);
    QVERIFY(!wKing->hasMoved);

    const auto bKing = rules.pieceAt({0, 4});
    QVERIFY(bKing.has_value());
    QCOMPARE(bKing->type, Rules::PieceType::King);
    QCOMPARE(bKing->color, Rules::Color::Black);
    QVERIFY(!bKing->hasMoved);
}

void RulesTest::testFenMidGame() {
    Rules rules;
    const QString midFen = QStringLiteral("r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5");
    QVERIFY(rules.loadFen(midFen));
    QCOMPARE(rules.currentPlayer(), Rules::Color::White);

    // Black bishop at c5 (row 3, col 2)
    const auto bBishop = rules.pieceAt({3, 2});
    QVERIFY(bBishop.has_value());
    QCOMPARE(bBishop->type, Rules::PieceType::Bishop);
    QCOMPARE(bBishop->color, Rules::Color::Black);

    // White bishop at c4 (row 4, col 2)
    const auto wBishop = rules.pieceAt({4, 2});
    QVERIFY(wBishop.has_value());
    QCOMPARE(wBishop->type, Rules::PieceType::Bishop);
    QCOMPARE(wBishop->color, Rules::Color::White);
}

void RulesTest::testFenEndgame() {
    Rules rules;
    const QString endFen = QStringLiteral("8/8/4k3/8/8/4K3/8/8 w - - 0 1");
    QVERIFY(rules.loadFen(endFen));
    QCOMPARE(rules.currentPlayer(), Rules::Color::White);

    // White king at e3 (row 5, col 4)
    const auto wKing = rules.pieceAt({5, 4});
    QVERIFY(wKing.has_value());
    QCOMPARE(wKing->type, Rules::PieceType::King);

    // Black king at e6 (row 2, col 4)
    const auto bKing = rules.pieceAt({2, 4});
    QVERIFY(bKing.has_value());
    QCOMPARE(bKing->type, Rules::PieceType::King);

    // Empty squares
    QVERIFY(!rules.pieceAt({0, 0}).has_value());
    QVERIFY(!rules.pieceAt({7, 7}).has_value());
}

void RulesTest::testFenInvalid() {
    Rules rules;
    // Empty
    QVERIFY(!rules.loadFen(QString()));
    // A complete FEN must contain all six fields.
    QVERIFY(!rules.loadFen(QStringLiteral("8/8/8/8/8/8/4k3/4K3 w - -")));
    // Only 7 ranks
    QVERIFY(!rules.loadFen(QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1")));
    // Rank overflow (9 cols)
    QVERIFY(!rules.loadFen(QStringLiteral("rnbqkbnrr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")));
    // Invalid piece character
    QVERIFY(!rules.loadFen(QStringLiteral("xnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")));
    // Invalid active color
    QVERIFY(!rules.loadFen(QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1")));
    // The reported position has no white king.
    QVERIFY(!rules.loadFen(QStringLiteral("r2qk2r/pp2bppb/2p1p2p/2n5/4nP1N/2N3P1/PPP1Q1BP/R4R2 b kq - 0 1")));
    // Pawns may not occupy the first or eighth rank.
    QVERIFY(!rules.loadFen(QStringLiteral("P3k3/8/8/8/8/8/8/4K3 w - - 0 1")));
    // Castling rights must be unique and backed by the relevant pieces.
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w KK - 0 1")));
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w K - 0 1")));
    // En-passant must describe the immediately preceding double pawn move.
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - e4 0 1")));
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - e6 0 1")));
    // The side that just moved cannot still have its king in check.
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4r1K1 b - - 0 1")));
    // Halfmove is non-negative and fullmove starts at one.
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - -1 1")));
    QVERIFY(!rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 0")));

    // A valid en-passant target is accepted.
    QVERIFY(rules.loadFen(QStringLiteral(
        "rnbqkbnr/pppp1ppp/8/4p3/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 2")));
}

void RulesTest::testToFenRoundTrip() {
    Rules rules;
    const QString startFen = QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    QVERIFY(rules.loadFen(startFen));
    QCOMPARE(rules.toFen(), startFen);
}

void RulesTest::testSanParsingAndExecution() {
    Rules rules;
    // 1. e4
    QVERIFY(rules.tryMoveSan(QStringLiteral("e4")));
    QCOMPARE(rules.currentPlayer(), Rules::Color::Black);

    // 1... e5
    QVERIFY(rules.tryMoveSan(QStringLiteral("e5")));
    QCOMPARE(rules.currentPlayer(), Rules::Color::White);

    // 2. Nf3
    QVERIFY(rules.tryMoveSan(QStringLiteral("Nf3")));
    // 2... Nc6
    QVERIFY(rules.tryMoveSan(QStringLiteral("Nc6")));
    // 3. Bc4
    QVERIFY(rules.tryMoveSan(QStringLiteral("Bc4")));
    // 3... Bc5
    QVERIFY(rules.tryMoveSan(QStringLiteral("Bc5")));
    // 4. O-O
    QVERIFY(rules.tryMoveSan(QStringLiteral("O-O")));

    // White king should be at g1 (row 7, col 6)
    const auto wKing = rules.pieceAt({7, 6});
    QVERIFY(wKing.has_value());
    QCOMPARE(wKing->type, Rules::PieceType::King);

    // White rook should be at f1 (row 7, col 5)
    const auto wRook = rules.pieceAt({7, 5});
    QVERIFY(wRook.has_value());
    QCOMPARE(wRook->type, Rules::PieceType::Rook);
}

void RulesTest::testSanPromotion() {
    Rules rules;
    // Setup position with pawn on 7th rank ready to promote
    const QString promoFen = QStringLiteral("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
    QVERIFY(rules.loadFen(promoFen));

    QVERIFY(rules.tryMoveSan(QStringLiteral("e8=Q+")));
    const auto queen = rules.pieceAt({0, 4});
    QVERIFY(queen.has_value());
    QCOMPARE(queen->type, Rules::PieceType::Queen);
    QCOMPARE(queen->color, Rules::Color::White);
}

void RulesTest::testSanDisambiguation() {
    Rules rules;
    // Setup position with two knights that can move to d4
    const QString knightsFen = QStringLiteral("8/8/8/8/8/2N1N3/8/4K2k w - - 0 1");
    QVERIFY(rules.loadFen(knightsFen));

    // Ncd5 moves knight from c3 (col 2) to d5
    QVERIFY(rules.tryMoveSan(QStringLiteral("Ncd5")));
    QVERIFY(rules.pieceAt({3, 3}).has_value());
    QVERIFY(!rules.pieceAt({5, 2}).has_value()); // c3 is now empty
    QVERIFY(rules.pieceAt({5, 4}).has_value());  // e3 knight remained
}

void RulesTest::testPgnLoading() {
    Rules rules;
    const QString pgn = QStringLiteral(
        "[Event \"Test Game\"]\n"
        "[Site \"Local\"]\n"
        "[Date \"2026.08.24\"]\n"
        "[White \"White Player\"]\n"
        "[Black \"Black Player\"]\n"
        "[Result \"1-0\"]\n\n"
        "1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0\n"
    );

    QStringList pgnMoves;
    QStringList uciMoves;
    QVERIFY(rules.loadPgn(pgn, &pgnMoves, &uciMoves));

    QCOMPARE(uciMoves.size(), 7);
    QVERIFY(rules.isCheckmate(Rules::Color::Black));
    QVERIFY(rules.isGameOver());
}

void RulesTest::testPgnLoadingWithComments() {
    Rules rules;
    const QString pgn = QStringLiteral(
        "[Event \"Commented Game\"]\n"
        "[Result \"*\"]\n\n"
        "{ Root note [%csl Ge4] } 1. e4 { [%cal Ge2e4] King pawn } e5 { [%csl Re5] } 2. Nf3 (2. Bc4) Nc6 ; line note\n *"
    );

    QStringList pgnMoves;
    QStringList uciMoves;
    QStringList comments;
    QVERIFY(rules.loadPgn(pgn, &pgnMoves, &uciMoves, &comments));

    QCOMPARE(uciMoves.size(), 4);
    QCOMPARE(comments.size(), 5); // ply 0 to 4
    QCOMPARE(comments.at(0), QStringLiteral("Root note [%csl Ge4]"));
    QCOMPARE(comments.at(1), QStringLiteral("[%cal Ge2e4] King pawn"));
    QCOMPARE(comments.at(2), QStringLiteral("[%csl Re5]"));
    QCOMPARE(comments.at(3), QString());
    QCOMPARE(comments.at(4), QStringLiteral("line note"));
}

void RulesTest::testMainWindowPasteFen() {
    MainWindow window;
    const QString customFen = QStringLiteral("r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5");

    QVERIFY(window.pasteFen(customFen));
    QCOMPARE(window.initialFen(), customFen);
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::White);

    // Verify bishop on c4 in MainWindow
    const auto bishop = window.chessBoard()->rules().pieceAt({4, 2});
    QVERIFY(bishop.has_value());
    QCOMPARE(bishop->type, Rules::PieceType::Bishop);
}

void RulesTest::testMainWindowNewGame() {
    MainWindow window;
    const QString customFen = QStringLiteral(
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5");

    QVERIFY(window.pasteFen(customFen));
    QCOMPARE(window.initialFen(), customFen);
    window.flipBoardButton()->click();
    QVERIFY(window.chessBoard()->boardFlipped());

    auto *newGameAction = window.findChild<QAction *>(QStringLiteral("newGameAction"));
    QVERIFY(newGameAction != nullptr);
    newGameAction->trigger();

    QVERIFY(window.initialFen().isEmpty());
    QVERIFY(!window.chessBoard()->boardFlipped());
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::White);
    const auto king = window.chessBoard()->rules().pieceAt({7, 4});
    QVERIFY(king.has_value());
    QCOMPARE(king->type, Rules::PieceType::King);
    QCOMPARE(king->color, Rules::Color::White);
}

void RulesTest::testWhiteToPlayCheckBox() {
    MainWindow window;
    QVERIFY(window.whiteToPlayCheckBox() != nullptr);
    QVERIFY(window.whiteToPlayCheckBox()->isChecked());

    window.whiteToPlayCheckBox()->setChecked(false);
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::Black);
    QVERIFY(window.initialFen().contains(QStringLiteral(" b ")));

    window.whiteToPlayCheckBox()->setChecked(true);
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::White);
    QVERIFY(window.initialFen().contains(QStringLiteral(" w ")));
}

void RulesTest::testFlipBoardButton() {
    MainWindow window;
    window.resize(800, 600);
    window.show();
    QCoreApplication::processEvents();

    QVERIFY(window.flipBoardButton() != nullptr);
    QVERIFY(!window.flipBoardButton()->icon().isNull());
    QVERIFY(!window.chessBoard()->boardFlipped());
    QVERIFY(!window.flipBoardButton()->isChecked());
    QVERIFY(window.whitePendulum() != nullptr);
    QVERIFY(window.blackPendulum() != nullptr);

    auto *gameControlBar = window.findChild<QFrame *>(
        QStringLiteral("gameControlBar"));
    QVERIFY(gameControlBar != nullptr);
    QVERIFY(gameControlBar->isVisible());
    QVERIFY(gameControlBar->isAncestorOf(window.blackPendulum()));
    QVERIFY(gameControlBar->isAncestorOf(window.whitePendulum()));
    QVERIFY(window.blackPendulum()->isVisible());
    QVERIFY(window.whitePendulum()->isVisible());
    QVERIFY(gameControlBar->mapTo(&window,
                                  QPoint(0, gameControlBar->height())).y() <=
            window.chessBoard()->mapTo(&window, QPoint()).y());
    QVERIFY(window.visionStatusLabel()->toolTip() ==
            window.visionStatusLabel()->text());

    const auto labels = gameControlBar->findChildren<QLabel *>();
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("Black");
    }));
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
        return label->text() == QStringLiteral("White");
    }));

    window.flipBoardButton()->click();
    QVERIFY(window.chessBoard()->boardFlipped());
    QVERIFY(window.flipBoardButton()->isChecked());
    QCoreApplication::processEvents();
    QVERIFY(window.blackPendulum()->isVisible());
    QVERIFY(window.whitePendulum()->isVisible());
    QVERIFY(gameControlBar->isAncestorOf(window.blackPendulum()));
    QVERIFY(gameControlBar->isAncestorOf(window.whitePendulum()));

    window.flipBoardButton()->click();
    QVERIFY(!window.chessBoard()->boardFlipped());
    QVERIFY(!window.flipBoardButton()->isChecked());
    QCoreApplication::processEvents();
    QVERIFY(window.blackPendulum()->isVisible());
    QVERIFY(window.whitePendulum()->isVisible());
}

void RulesTest::testMovePreviews() {
    ChessBoard board;
    const Rules::Move computerMove{{1, 4}, {3, 4}};
    const Rules::Move recommendedMove{{6, 4}, {4, 4}};

    board.resize(640, 640);
    board.show();
    QCoreApplication::processEvents();
    const QImage boardWithoutPreviews = board.grab().toImage();

    board.setComputerMovePreview(computerMove);
    board.setRecommendedMovePreview(recommendedMove);
    QCoreApplication::processEvents();
    const QImage boardWithPreviews = board.grab().toImage();
    QVERIFY(boardWithPreviews != boardWithoutPreviews);

    const auto storedComputerMove = board.computerMovePreview();
    QVERIFY(storedComputerMove.has_value());
    QCOMPARE(storedComputerMove->from, computerMove.from);
    QCOMPARE(storedComputerMove->to, computerMove.to);

    const auto storedRecommendedMove = board.recommendedMovePreview();
    QVERIFY(storedRecommendedMove.has_value());
    QCOMPARE(storedRecommendedMove->from, recommendedMove.from);
    QCOMPARE(storedRecommendedMove->to, recommendedMove.to);

    board.clearMovePreviews();
    QVERIFY(!board.computerMovePreview().has_value());
    QVERIFY(!board.recommendedMovePreview().has_value());
}

void RulesTest::testLastMoveTracking() {
    Rules rules;
    QVERIFY(!rules.lastMove().has_value());

    QVERIFY(rules.tryMove(Rules::Position{6, 4}, Rules::Position{4, 4}));
    QVERIFY(rules.lastMove().has_value());
    QCOMPARE(rules.lastMove()->from, (Rules::Position{6, 4}));
    QCOMPARE(rules.lastMove()->to, (Rules::Position{4, 4}));

    rules.reset();
    QVERIFY(!rules.lastMove().has_value());

    // A FEN with an en passant target square reconstructs the last move.
    QVERIFY(rules.loadFen(QStringLiteral(
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3")));
    QVERIFY(rules.lastMove().has_value());
    QCOMPARE(rules.lastMove()->from, (Rules::Position{1, 3}));
    QCOMPARE(rules.lastMove()->to, (Rules::Position{3, 3}));

    // A FEN without an en passant square has no known last move.
    QVERIFY(rules.loadFen(QStringLiteral(
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3")));
    QVERIFY(!rules.lastMove().has_value());
}

void RulesTest::testLastMoveHighlight() {
    ChessBoard board;
    QVERIFY(board.lastMoveHighlightingEnabled());

    Rules moved;
    QVERIFY(moved.tryMove(Rules::Position{6, 4}, Rules::Position{4, 4}));
    board.setRules(moved);
    board.resize(640, 640);
    board.show();
    QCoreApplication::processEvents();
    const QImage withHighlight = board.grab().toImage();

    board.setLastMoveHighlightingEnabled(false);
    QVERIFY(!board.lastMoveHighlightingEnabled());
    QCoreApplication::processEvents();
    const QImage withoutHighlight = board.grab().toImage();
    QVERIFY(withHighlight != withoutHighlight);

    board.setLastMoveHighlightingEnabled(true);
    QCoreApplication::processEvents();
    QVERIFY(board.grab().toImage() == withHighlight);
}

void RulesTest::testBoardEmitsMoveIntentWithoutMutating() {
    qRegisterMetaType<Rules::Position>("Rules::Position");

    ChessBoard board;
    board.setRules(Rules{});
    board.resize(640, 640);
    board.show();
    QCoreApplication::processEvents();

    // Square centers in widget coordinates, mirroring the board margins.
    constexpr int leftMargin = 28;
    constexpr int topMargin = 8;
    const int squareSize = (640 - 28 - 8 - 8 - 28) / 8;
    const QPoint e2(leftMargin + 4 * squareSize + squareSize / 2,
                    topMargin + 6 * squareSize + squareSize / 2);
    const QPoint e4(leftMargin + 4 * squareSize + squareSize / 2,
                    topMargin + 4 * squareSize + squareSize / 2);

    QSignalSpy moveSpy(&board, &ChessBoard::pieceMoved);

    QTest::mouseClick(&board, Qt::LeftButton, Qt::NoModifier, e2);
    QTest::mouseClick(&board, Qt::LeftButton, Qt::NoModifier, e4);

    QCOMPARE(moveSpy.count(), 1);
    QCOMPARE(moveSpy.first().at(1).value<Rules::Position>(), (Rules::Position{6, 4}));
    QCOMPARE(moveSpy.first().at(2).value<Rules::Position>(), (Rules::Position{4, 4}));

    // The board is a read-only view: its position cache is untouched by the
    // move intent; the controller refreshes it through setRules().
    QCOMPARE(board.rules().currentPlayer(), Rules::Color::White);
    QVERIFY(board.rules().pieceAt({6, 4}).has_value());
    QVERIFY(!board.rules().pieceAt({4, 4}).has_value());
}

void RulesTest::testMovePreviewControls() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    MainWindow window(nullptr, tempDir.filePath(QStringLiteral("chessGui.conf")));
    QCheckBox *computerMoveCheckBox = window.showComputerMoveCheckBox();
    QCheckBox *recommendedMoveCheckBox = window.showRecommendedMoveCheckBox();
    QCheckBox *highlightLastMoveCheckBox = window.highlightLastMoveCheckBox();
    QVERIFY(computerMoveCheckBox != nullptr);
    QVERIFY(recommendedMoveCheckBox != nullptr);
    QVERIFY(highlightLastMoveCheckBox != nullptr);
    QVERIFY(!computerMoveCheckBox->isChecked());
    QVERIFY(!recommendedMoveCheckBox->isChecked());
    QVERIFY(highlightLastMoveCheckBox->isChecked());

    QAction *computerMoveAction = window.findChild<QAction *>(
        QStringLiteral("showComputerMoveAction"));
    QAction *recommendedMoveAction = window.findChild<QAction *>(
        QStringLiteral("showRecommendedMoveAction"));
    QVERIFY(computerMoveAction != nullptr);
    QVERIFY(recommendedMoveAction != nullptr);

    QToolBar *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolBar != nullptr);
    QVERIFY(!toolBar->actions().contains(computerMoveAction));
    QVERIFY(!toolBar->actions().contains(recommendedMoveAction));

    auto *positionToolsButton = window.findChild<QToolButton *>(
        QStringLiteral("positionToolsButton"));
    auto *positionToolsMenu = window.findChild<QMenu *>(
        QStringLiteral("positionToolsMenu"));
    QVERIFY(positionToolsButton != nullptr);
    QVERIFY(positionToolsMenu != nullptr);
    QCOMPARE(positionToolsButton->menu(), positionToolsMenu);
    QCOMPARE(positionToolsMenu->actions().size(), 4);

    const auto widgetActions = positionToolsMenu->findChildren<QWidgetAction *>();
    QCOMPARE(widgetActions.size(), 4);
    QVERIFY(std::any_of(widgetActions.cbegin(), widgetActions.cend(),
                        [computerMoveCheckBox](const QWidgetAction *action) {
        return action->defaultWidget() == computerMoveCheckBox;
    }));
    QVERIFY(std::any_of(widgetActions.cbegin(), widgetActions.cend(),
                        [recommendedMoveCheckBox](const QWidgetAction *action) {
        return action->defaultWidget() == recommendedMoveCheckBox;
    }));
    QVERIFY(std::any_of(widgetActions.cbegin(), widgetActions.cend(),
                        [highlightLastMoveCheckBox](const QWidgetAction *action) {
        return action->defaultWidget() == highlightLastMoveCheckBox;
    }));

    window.resize(1000, 800);
    window.show();
    QCoreApplication::processEvents();

    positionToolsMenu->popup(
        positionToolsButton->mapToGlobal(positionToolsButton->rect().bottomLeft()));
    QTRY_VERIFY(positionToolsMenu->isVisible());
    QVERIFY(window.whiteToPlayCheckBox()->isVisible());
    window.whiteToPlayCheckBox()->click();
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::Black);

    positionToolsMenu->popup(
        positionToolsButton->mapToGlobal(positionToolsButton->rect().bottomLeft()));
    QTRY_VERIFY(positionToolsMenu->isVisible());
    computerMoveCheckBox->click();
    QVERIFY(computerMoveAction->isChecked());
    QVERIFY(window.config().computerMovePreviewEnabled());

    positionToolsMenu->popup(
        positionToolsButton->mapToGlobal(positionToolsButton->rect().bottomLeft()));
    QTRY_VERIFY(positionToolsMenu->isVisible());
    recommendedMoveCheckBox->click();
    QVERIFY(recommendedMoveCheckBox->isChecked());
    QVERIFY(recommendedMoveAction->isChecked());
    QVERIFY(window.config().recommendedMovePreviewEnabled());

    positionToolsMenu->popup(
        positionToolsButton->mapToGlobal(positionToolsButton->rect().bottomLeft()));
    QTRY_VERIFY(positionToolsMenu->isVisible());
    highlightLastMoveCheckBox->click();
    QVERIFY(!highlightLastMoveCheckBox->isChecked());
    QVERIFY(!window.chessBoard()->lastMoveHighlightingEnabled());
    QVERIFY(!window.config().highlightLastMoveEnabled());

    positionToolsMenu->popup(
        positionToolsButton->mapToGlobal(positionToolsButton->rect().bottomLeft()));
    QTRY_VERIFY(positionToolsMenu->isVisible());
    QTest::keyClick(positionToolsMenu, Qt::Key_Escape);
    QTRY_VERIFY(!positionToolsMenu->isVisible());

    AppConfig savedConfig(window.config().filePath());
    QVERIFY(savedConfig.load());
    QVERIFY(savedConfig.computerMovePreviewEnabled());
    QVERIFY(savedConfig.recommendedMovePreviewEnabled());
    QVERIFY(!savedConfig.highlightLastMoveEnabled());
}

void RulesTest::testMainWindowLoadPgnContent() {
    MainWindow window;
    const QString pgn = QStringLiteral(
        "[Event \"Scholar's Mate\"]\n"
        "1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0"
    );

    QVERIFY(window.loadPgnContent(pgn));
    QVERIFY(window.pgnHeaderTextEdit()->toPlainText().contains(
        QStringLiteral("[Event \"Scholar's Mate\"]")));
    QVERIFY(!moveListText(window).contains(QStringLiteral("[Event")));
    QVERIFY(moveListText(window).contains(QStringLiteral("Qxf7#")));
    QVERIFY(window.chessBoard()->rules().isCheckmate(Rules::Color::Black));
}

void RulesTest::testMainWindowRecognizesScreenshotWhenProvided() {
    const QString path = qEnvironmentVariable("CHESSGUI_SAMPLE_IMAGE");
    if (path.isEmpty()) {
        QSKIP("Set CHESSGUI_SAMPLE_IMAGE to run the MainWindow image check");
    }

    MainWindow window;
    QVERIFY(window.loadImageFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(window.chessBoard()->rules().pieceAt({0, 7}).has_value(), 3000);
    QCOMPARE(window.chessBoard()->rules().currentPlayer(), Rules::Color::White);
    QTRY_VERIFY_WITH_TIMEOUT(
        window.messageLogTextEdit()->toPlainText().contains(
            QStringLiteral("Chessboard detected")),
        3000);
}

void RulesTest::testComputerGameDialogSettings() {
    ComputerGameDialog dialog(QStringLiteral("Test Engine"));
    const ComputerGameSettings settings = dialog.settings();

    QCOMPARE(settings.event, QStringLiteral("Casual Game"));
    QCOMPARE(settings.site, QStringLiteral("ChessGui"));
    QCOMPARE(settings.playerName, QStringLiteral("Player"));
    QCOMPARE(settings.engineName, QStringLiteral("Test Engine"));
    QVERIFY(!settings.enginePlaysWhite);
    QCOMPARE(settings.timeLimitMilliseconds, 7200000LL);
    QCOMPARE(settings.timeControl, QStringLiteral("7200"));

    const QStringList headers = settings.pgnHeaders();
    QCOMPARE(headers.size(), 8);
    QVERIFY(headers.at(0).startsWith(QStringLiteral("[Event \"Casual Game\"]")));
    QVERIFY(headers.at(4).contains(QStringLiteral("Player")));
    QVERIFY(headers.at(5).contains(QStringLiteral("Test Engine")));
    QVERIFY(headers.at(6).contains(QStringLiteral("7200")));

    auto *timeControl = dialog.findChild<QComboBox *>(QStringLiteral("timeControlCombo"));
    QVERIFY(timeControl != nullptr);
    QCOMPARE(timeControl->count(), 5);
    QCOMPARE(timeControl->itemData(0).toLongLong(), 7200000LL);
    QCOMPARE(timeControl->itemData(1).toLongLong(), 3600000LL);
    QCOMPARE(timeControl->itemData(2).toLongLong(), 1800000LL);
    QCOMPARE(timeControl->itemData(3).toLongLong(), 900000LL);
    QCOMPARE(timeControl->itemData(4).toLongLong(), 300000LL);
}

QTEST_MAIN(RulesTest)
#include "rules_test.moc"
