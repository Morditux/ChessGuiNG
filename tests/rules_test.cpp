//
// Unit tests for Rules FEN / PGN and SAN parsing.
//

#include <QSignalSpy>
#include <QTest>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QProgressBar>
#include <QPushButton>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QSpinBox>
#include <QToolButton>
#include <QToolBar>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidgetAction>

#include <algorithm>

#include "chessboard.h"
#include "computergamedialog.h"
#include "evaluationbar.h"
#include "evaluationgraph.h"
#include "gamecontroller.h"
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

// Counts the legal move sequences of the given depth using the search's
// make/unmake primitives and its direct move generator.
quint64 perft(Rules &rules, int depth) {
    if (depth == 0) {
        return 1;
    }

    Rules::Move moves[256];
    const int count = rules.generatePseudoLegalMoves(moves, 256);
    quint64 nodes = 0;
    for (int index = 0; index < count; ++index) {
        Rules::Undo undo;
        if (!rules.makeMove(moves[index], undo)) {
            continue;
        }
        nodes += perft(rules, depth - 1);
        rules.unmakeMove(moves[index], undo);
    }
    return nodes;
}

class RulesTest : public QObject {
    Q_OBJECT

private slots:
    void testPerft();
    void testZobristHash();
    void testFenStartPos();
    void testFenMidGame();
    void testFenEndgame();
    void testFenInvalid();
    void testToFenRoundTrip();
    void testFenClocks();
    void testInsufficientMaterial();
    void testMaterialDrawClassification();
    void testThreefoldRepetition();
    void testCastlingRightsBreakRepetition();
    void testFiftyMoveRule();
    void testSanParsingAndExecution();
    void testSanPromotion();
    void testSanDisambiguation();
    void testPgnLoading();
    void testPgnLoadingWithComments();
    void testMainWindowPasteFen();
    void testMainWindowNewGame();
    void testMainWindowClaimDrawAction();
    void testWhiteToPlayCheckBox();
    void testFlipBoardButton();
    void testMovePreviews();
    void testLastMoveTracking();
    void testLastMoveHighlight();
    void testBoardEmitsMoveIntentWithoutMutating();
    void testMovePreviewControls();
    void testMainWindowLoadPgnContent();
    void testMainWindowCopyFenAndPgn();
    void testMainWindowNavigationHomeAndEnd();
    void testMainWindowMenuLayoutAndShortcuts();
    void testMainWindowAcceptsDroppedPgnFile();
    void testMainWindowRecognizesScreenshotWhenProvided();
    void testComputerGameDialogSettings();
    void testMainWindowExplainsTheEvaluation();
    void testMainWindowSettingsMenu();
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

void RulesTest::testFenClocks() {
    Rules rules;

    // A pawn move resets the halfmove clock; the fullmove number is still 1
    // after White's first move.
    QVERIFY(rules.tryMoveSan(QStringLiteral("e4")));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(4), QStringLiteral("0"));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(5), QStringLiteral("1"));

    QVERIFY(rules.tryMoveSan(QStringLiteral("e5")));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(4), QStringLiteral("0"));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(5), QStringLiteral("2"));

    // A non-pawn, non-capture move increments the halfmove clock.
    QVERIFY(rules.tryMoveSan(QStringLiteral("Nf3")));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(4), QStringLiteral("1"));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(5), QStringLiteral("2"));

    QVERIFY(rules.tryMoveSan(QStringLiteral("Nc6")));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(4), QStringLiteral("2"));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(5), QStringLiteral("3"));

    QVERIFY(rules.tryMoveSan(QStringLiteral("d4")));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(4), QStringLiteral("0"));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(5), QStringLiteral("3"));

    // A capture resets the halfmove clock and advances the fullmove number
    // once Black replies.
    QVERIFY(rules.tryMoveSan(QStringLiteral("exd4")));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(4), QStringLiteral("0"));
    QCOMPARE(rules.toFen().split(QLatin1Char(' ')).at(5), QStringLiteral("4"));

    // Non-zero clocks survive a FEN round trip.
    const QString clockFen = QStringLiteral("4k3/8/8/8/8/8/4P3/4K3 b - - 12 34");
    QVERIFY(rules.loadFen(clockFen));
    QCOMPARE(rules.toFen(), clockFen);
}

void RulesTest::testInsufficientMaterial() {
    Rules rules;

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));
    QVERIFY(rules.isInsufficientMaterial());
    QVERIFY(rules.isGameOver());

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/2B5/4K3 w - - 0 1")));
    QVERIFY(rules.isInsufficientMaterial());

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/2N5/4K3 w - - 0 1")));
    QVERIFY(rules.isInsufficientMaterial());

    // Bishops confined to the same-colored squares cannot deliver mate.
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/2b1B3/8/4K3 w - - 0 1")));
    QVERIFY(rules.isInsufficientMaterial());
    QVERIFY(rules.isGameOver());

    // Bishops on opposite-colored squares still allow checkmate.
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/2bB4/8/4K3 w - - 0 1")));
    QVERIFY(!rules.isInsufficientMaterial());

    // Rooks and queens always allow checkmate.
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/2R5/4K3 w - - 0 1")));
    QVERIFY(!rules.isInsufficientMaterial());
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/2Q5/4K3 w - - 0 1")));
    QVERIFY(!rules.isInsufficientMaterial());
}

void RulesTest::testMaterialDrawClassification() {
    // King versus king is dead and therefore also unforceable.
    const Rules::MaterialCounts bare;
    const Rules::MaterialDraw bareDraw = Rules::classifyMaterialDraw(bare);
    QVERIFY(bareDraw.dead);
    QVERIFY(bareDraw.unforceable);

    // Two knights against a bare king cannot force mate, but mate is possible
    // with the defender's help, so it is unforceable without being dead.
    Rules::MaterialCounts twoKnights;
    twoKnights.knights[0] = 2;
    const Rules::MaterialDraw twoKnightsDraw =
        Rules::classifyMaterialDraw(twoKnights);
    QVERIFY(!twoKnightsDraw.dead);
    QVERIFY(twoKnightsDraw.unforceable);

    // A lone minor piece on each side is unforceable, whatever the square
    // colours.
    Rules::MaterialCounts loneMinorEach;
    loneMinorEach.evenSquaredBishops[0] = 1;
    loneMinorEach.oddSquaredBishops[1] = 1;
    const Rules::MaterialDraw loneMinorDraw =
        Rules::classifyMaterialDraw(loneMinorEach);
    QVERIFY(!loneMinorDraw.dead);
    QVERIFY(loneMinorDraw.unforceable);

    // Two bishops of one side on opposite colours can mate.
    Rules::MaterialCounts oppositeBishops;
    oppositeBishops.evenSquaredBishops[0] = 1;
    oppositeBishops.oddSquaredBishops[0] = 1;
    QVERIFY(!Rules::classifyMaterialDraw(oppositeBishops).unforceable);

    // Any pawn, rook or queen keeps the position playable.
    Rules::MaterialCounts withPawn;
    withPawn.hasPawnOrMajor = true;
    const Rules::MaterialDraw pawnDraw = Rules::classifyMaterialDraw(withPawn);
    QVERIFY(!pawnDraw.dead);
    QVERIFY(!pawnDraw.unforceable);
}

void RulesTest::testPerft() {
    // Known node counts that exercise castling, en passant, promotions and
    // checks. They pin the exact behaviour of generatePseudoLegalMoves and of
    // the make/unmake pair.
    struct Case {
        const char *fen;
        int depth;
        quint64 nodes;
    };
    const Case cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         1, 48},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         3, 97862},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3,
         9467},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         3, 89890},
    };

    for (const Case &testCase : cases) {
        Rules rules;
        QVERIFY2(rules.loadFen(QString::fromLatin1(testCase.fen)),
                 testCase.fen);
        rules.setTrackRepetition(false);
        QCOMPARE(perft(rules, testCase.depth), testCase.nodes);
    }
}

void RulesTest::testZobristHash() {
    // The incrementally maintained key must match a fresh recomputation after
    // every kind of move, and unmake must restore it exactly.
    const auto checkMove = [](const char *fen, const Rules::Move &move) {
        Rules rules;
        QVERIFY(rules.loadFen(QString::fromLatin1(fen)));
        rules.setTrackRepetition(false);
        const quint64 before = rules.zobristKey();

        Rules::Undo undo;
        QVERIFY(rules.makeMove(move, undo));

        Rules recomputed;
        QVERIFY(recomputed.loadFen(rules.toFen()));
        QCOMPARE(rules.zobristKey(), recomputed.zobristKey());
        QVERIFY(rules.zobristKey() != before);

        rules.unmakeMove(move, undo);
        QCOMPARE(rules.zobristKey(), before);
    };

    // Quiet move, capture, castling, en passant and promotion.
    checkMove("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
              {{6, 4}, {4, 4}, Rules::PieceType::None});
    checkMove("4k3/8/8/8/8/8/3b4/3RK3 w - - 0 1",
              {{7, 3}, {6, 3}, Rules::PieceType::None});
    checkMove("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
              {{7, 4}, {7, 6}, Rules::PieceType::None});
    checkMove("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
              {{3, 4}, {2, 3}, Rules::PieceType::None});
    checkMove("4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
              {{1, 0}, {0, 0}, Rules::PieceType::Queen});

    // Transpositions reached by different move orders share the same key. Both
    // lines end on a bishop move, so neither offers an en passant capture.
    Rules first;
    QVERIFY(first.loadFen(
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")));
    QVERIFY(first.tryMove({{6, 4}, {4, 4}})); // e4
    QVERIFY(first.tryMove({{1, 4}, {3, 4}})); // e5
    QVERIFY(first.tryMove({{7, 6}, {5, 5}})); // Nf3
    QVERIFY(first.tryMove({{0, 1}, {2, 2}})); // Nc6
    QVERIFY(first.tryMove({{7, 5}, {3, 1}})); // Bb5

    Rules second;
    QVERIFY(second.loadFen(
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")));
    QVERIFY(second.tryMove({{6, 4}, {4, 4}})); // e4
    QVERIFY(second.tryMove({{0, 1}, {2, 2}})); // Nc6
    QVERIFY(second.tryMove({{7, 6}, {5, 5}})); // Nf3
    QVERIFY(second.tryMove({{1, 4}, {3, 4}})); // e5
    QVERIFY(second.tryMove({{7, 5}, {3, 1}})); // Bb5

    QCOMPARE(first.zobristKey(), second.zobristKey());
}

void RulesTest::testThreefoldRepetition() {
    Rules rules;

    // Two white knights and the two kings are enough material for this test,
    // but not enough for the insufficient-material rule to kick in.
    QVERIFY(rules.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/N3K1N1 w - - 0 1")));

    QVERIFY(rules.tryMoveSan(QStringLiteral("Nc2")));
    QVERIFY(rules.tryMoveSan(QStringLiteral("Kd8")));
    QVERIFY(rules.tryMoveSan(QStringLiteral("Na1")));
    QVERIFY(rules.tryMoveSan(QStringLiteral("Ke8")));
    QVERIFY(!rules.isThreefoldRepetition());

    QVERIFY(rules.tryMoveSan(QStringLiteral("Nc2")));
    QVERIFY(rules.tryMoveSan(QStringLiteral("Kd8")));
    QVERIFY(rules.tryMoveSan(QStringLiteral("Na1")));
    QVERIFY(rules.tryMoveSan(QStringLiteral("Ke8")));
    QVERIFY(rules.isThreefoldRepetition());
    QVERIFY(rules.isDraw());
    QVERIFY(rules.isGameOver());
}

void RulesTest::testCastlingRightsBreakRepetition() {
    Rules rules;

    // The rook moves away and comes back, so the same piece placement is
    // reached again but White has lost the kingside castling right.
    QVERIFY(rules.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1")));

    for (int cycle = 0; cycle < 2; ++cycle) {
        QVERIFY(rules.tryMoveSan(QStringLiteral("Rg1")));
        QVERIFY(rules.tryMoveSan(QStringLiteral("Kd8")));
        QVERIFY(rules.tryMoveSan(QStringLiteral("Rh1")));
        QVERIFY(rules.tryMoveSan(QStringLiteral("Ke8")));
    }

    // The piece placement is back to the initial one, but the castling right
    // is different, so the initial position has not been repeated.
    QVERIFY(!rules.isThreefoldRepetition());
}

void RulesTest::testFiftyMoveRule() {
    Rules rules;

    QVERIFY(rules.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/N3K2R w - - 99 60")));
    QVERIFY(!rules.isFiftyMoveRule());
    QVERIFY(!rules.isDraw());
    QVERIFY(!rules.isGameOver());

    // A quiet knight move reaches 100 halfmoves without a pawn move or a
    // capture: the fifty-move rule is available.
    QVERIFY(rules.tryMoveSan(QStringLiteral("Nc2")));
    QVERIFY(rules.isFiftyMoveRule());
    QVERIFY(rules.isDraw());
    QVERIFY(rules.isGameOver());

    // A pawn move resets the counter, even when it started at 99.
    Rules pawnRules;
    QVERIFY(pawnRules.loadFen(
        QStringLiteral("4k3/8/8/8/8/4P3/8/4K3 w - - 99 60")));
    QVERIFY(pawnRules.tryMoveSan(QStringLiteral("e4")));
    QVERIFY(!pawnRules.isFiftyMoveRule());
    QVERIFY(!pawnRules.isDraw());

    // Checkmate takes precedence over a simultaneously available fifty-move
    // claim: the quiet mating move also brings the halfmove clock to 100.
    Rules mateRules;
    QVERIFY(mateRules.loadFen(
        QStringLiteral("7k/8/6QK/8/8/8/8/8 w - - 99 60")));
    QVERIFY(mateRules.tryMoveSan(QStringLiteral("Qg7#")));
    QVERIFY(mateRules.isCheckmate(Rules::Color::Black));
    QVERIFY(mateRules.isFiftyMoveRule());
    QVERIFY(!mateRules.isDraw());
    QVERIFY(mateRules.isGameOver());
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

void RulesTest::testMainWindowClaimDrawAction() {
    MainWindow window;
    QVERIFY(window.claimDrawAction() != nullptr);

    // The action is disabled until a claimable draw is available.
    QVERIFY(!window.claimDrawAction()->isEnabled());
    QVERIFY(window.gameController()->loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/N3K2R w - - 99 60")));
    QVERIFY(window.gameController()->requestMove({7, 0}, {6, 2}));
    QVERIFY(window.gameController()->canClaimDraw());
    QVERIFY(window.claimDrawAction()->isEnabled());

    QSignalSpy finishedSpy(window.gameController(), &GameController::gameFinished);
    window.claimDrawAction()->trigger();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toString(), QStringLiteral("1/2-1/2"));
    QVERIFY(!window.gameController()->canClaimDraw());
    QVERIFY(!window.claimDrawAction()->isEnabled());
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
    QVERIFY(window.evaluationBar() != nullptr);
    QVERIFY(!window.evaluationBar()->isFlipped());

    auto *blackStrip = window.findChild<QWidget *>(QStringLiteral("blackPlayerStrip"));
    auto *whiteStrip = window.findChild<QWidget *>(QStringLiteral("whitePlayerStrip"));
    QVERIFY(blackStrip != nullptr);
    QVERIFY(whiteStrip != nullptr);
    QVERIFY(blackStrip->isVisible());
    QVERIFY(whiteStrip->isVisible());
    QVERIFY(blackStrip->isAncestorOf(window.blackPendulum()));
    QVERIFY(whiteStrip->isAncestorOf(window.whitePendulum()));
    QVERIFY(window.blackPendulum()->isVisible());
    QVERIFY(window.whitePendulum()->isVisible());

    // The strip above the board belongs to the camp displayed at the top.
    const auto stripIsAboveBoard = [&window](QWidget *strip) {
        return strip->mapTo(&window, QPoint(0, strip->height())).y() <=
               window.chessBoard()->mapTo(&window, QPoint()).y();
    };
    QVERIFY(stripIsAboveBoard(blackStrip));
    QVERIFY(!stripIsAboveBoard(whiteStrip));
    QVERIFY(window.visionStatusLabel()->toolTip() ==
            window.visionStatusLabel()->text());

    auto *blackName = blackStrip->findChild<QLabel *>(
        QStringLiteral("playerNameLabel"));
    auto *whiteName = whiteStrip->findChild<QLabel *>(
        QStringLiteral("playerNameLabel"));
    QVERIFY(blackName != nullptr);
    QVERIFY(whiteName != nullptr);
    // Without PGN headers the strips fall back to the colour names.
    QCOMPARE(blackName->toolTip(), QStringLiteral("Black"));
    QCOMPARE(whiteName->toolTip(), QStringLiteral("White"));

    window.flipBoardButton()->click();
    QVERIFY(window.chessBoard()->boardFlipped());
    QVERIFY(window.flipBoardButton()->isChecked());
    QCoreApplication::processEvents();
    QVERIFY(window.blackPendulum()->isVisible());
    QVERIFY(window.whitePendulum()->isVisible());

    // Flipping the board swaps the strips: the white clock moves to the top.
    QVERIFY(stripIsAboveBoard(whiteStrip));
    QVERIFY(!stripIsAboveBoard(blackStrip));
    QVERIFY(whiteStrip->isAncestorOf(window.whitePendulum()));
    QVERIFY(blackStrip->isAncestorOf(window.blackPendulum()));
    // The gauge follows the board so that it keeps reading like the board.
    QVERIFY(window.evaluationBar()->isFlipped());

    window.flipBoardButton()->click();
    QVERIFY(!window.chessBoard()->boardFlipped());
    QVERIFY(!window.flipBoardButton()->isChecked());
    QCoreApplication::processEvents();
    QVERIFY(window.blackPendulum()->isVisible());
    QVERIFY(window.whitePendulum()->isVisible());
    QVERIFY(stripIsAboveBoard(blackStrip));
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
    QCheckBox *highlightCheckCheckBox = window.highlightCheckCheckBox();
    QVERIFY(computerMoveCheckBox != nullptr);
    QVERIFY(recommendedMoveCheckBox != nullptr);
    QVERIFY(highlightLastMoveCheckBox != nullptr);
    QVERIFY(highlightCheckCheckBox != nullptr);
    QVERIFY(!computerMoveCheckBox->isChecked());
    QVERIFY(!recommendedMoveCheckBox->isChecked());
    QVERIFY(highlightLastMoveCheckBox->isChecked());
    QVERIFY(highlightCheckCheckBox->isChecked());

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
    QCOMPARE(positionToolsMenu->actions().size(), 5);

    const auto widgetActions = positionToolsMenu->findChildren<QWidgetAction *>();
    QCOMPARE(widgetActions.size(), 5);
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
    QVERIFY(std::any_of(widgetActions.cbegin(), widgetActions.cend(),
                        [highlightCheckCheckBox](const QWidgetAction *action) {
        return action->defaultWidget() == highlightCheckCheckBox;
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
    highlightCheckCheckBox->click();
    QVERIFY(!highlightCheckCheckBox->isChecked());
    QVERIFY(!window.chessBoard()->checkHighlightingEnabled());
    QVERIFY(!window.config().highlightCheckEnabled());

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
    QVERIFY(!savedConfig.highlightCheckEnabled());
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

void RulesTest::testMainWindowCopyFenAndPgn() {
    MainWindow window;
    const QString customFen = QStringLiteral("r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5");
    QVERIFY(window.pasteFen(customFen));

    QVERIFY(window.copyFenAction() != nullptr);
    QVERIFY(window.copyPgnAction() != nullptr);

    QGuiApplication::clipboard()->clear();

    window.copyFenAction()->trigger();
    QCOMPARE(QGuiApplication::clipboard()->text().trimmed(), window.gameController()->rules().toFen());

    window.copyPgnAction()->trigger();
    QCOMPARE(QGuiApplication::clipboard()->text().trimmed(), window.gameController()->pgnText());
}

void RulesTest::testMainWindowNavigationHomeAndEnd() {
    MainWindow window;
    const QString pgn = QStringLiteral("1. e4 e5 2. Nf3 Nc6 *");
    QVERIFY(window.loadPgnContent(pgn));

    QVERIFY(window.firstMoveAction() != nullptr);
    QVERIFY(window.lastMoveAction() != nullptr);
    QCOMPARE(window.gameController()->moveCursor(), 4);
    QVERIFY(window.firstMoveAction()->isEnabled());
    QVERIFY(!window.lastMoveAction()->isEnabled());

    window.firstMoveAction()->trigger();
    QCOMPARE(window.gameController()->moveCursor(), 0);
    QVERIFY(!window.firstMoveAction()->isEnabled());
    QVERIFY(window.lastMoveAction()->isEnabled());

    window.lastMoveAction()->trigger();
    QCOMPARE(window.gameController()->moveCursor(), 4);
    QVERIFY(window.firstMoveAction()->isEnabled());
    QVERIFY(!window.lastMoveAction()->isEnabled());
}

void RulesTest::testMainWindowMenuLayoutAndShortcuts() {
    MainWindow window;

    // The crowded Games menu is split: navigation, board and clipboard actions
    // each live in their own menu.
    const auto menuTitles = [&window] {
        QStringList titles;
        const auto menus = window.findChildren<QMenu *>();
        for (const QMenu *menu : menus) {
            if (menu->menuAction() != nullptr &&
                !menu->menuAction()->isSeparator()) {
                titles << menu->title();
            }
        }
        return titles;
    }();
    for (const auto &title : {QStringLiteral("File"), QStringLiteral("Games"),
                              QStringLiteral("Navigation"),
                              QStringLiteral("Board"), QStringLiteral("Edit"),
                              QStringLiteral("Engine")}) {
        QVERIFY2(menuTitles.contains(title), qPrintable(title));
    }

    const auto menuByTitle = [&window](const QString &title) -> QMenu * {
        const auto menus = window.findChildren<QMenu *>();
        for (QMenu *menu : menus) {
            if (menu->menuAction() != nullptr && menu->title() == title) {
                return menu;
            }
        }
        return nullptr;
    };

    const auto actionInMenu = [](QMenu *menu, const QString &text) {
        for (QAction *action : menu->actions()) {
            if (action->text() == text) {
                return action;
            }
        }
        return static_cast<QAction *>(nullptr);
    };

    QMenu *gamesMenu = menuByTitle(QStringLiteral("Games"));
    QMenu *navigationMenu = menuByTitle(QStringLiteral("Navigation"));
    QMenu *boardMenu = menuByTitle(QStringLiteral("Board"));
    QMenu *editMenu = menuByTitle(QStringLiteral("Edit"));
    QVERIFY(gamesMenu != nullptr);
    QVERIFY(navigationMenu != nullptr);
    QVERIFY(boardMenu != nullptr);
    QVERIFY(editMenu != nullptr);

    // Games keeps only the actions that drive a game in progress.
    QVERIFY(gamesMenu->actions().contains(window.takeBackAction()));
    QVERIFY(gamesMenu->actions().contains(window.resignAction()));
    QVERIFY(gamesMenu->actions().contains(window.offerDrawAction()));
    QVERIFY(gamesMenu->actions().contains(window.claimDrawAction()));
    // Navigation actions moved out of Games.
    QVERIFY(!gamesMenu->actions().contains(window.firstMoveAction()));
    QVERIFY(!gamesMenu->actions().contains(window.lastMoveAction()));
    QVERIFY(navigationMenu->actions().contains(window.firstMoveAction()));
    QVERIFY(navigationMenu->actions().contains(window.lastMoveAction()));

    QCOMPARE(gamesMenu->actions().size(), 7);      // 3 separators + 4 actions
    QCOMPARE(navigationMenu->actions().size(), 4); // no separators

    // Every action of the new menus carries a keyboard shortcut.
    const auto shortcutOf = [](QAction *action) {
        return action->shortcut().toString(QKeySequence::PortableText);
    };
    QCOMPARE(shortcutOf(window.takeBackAction()), QStringLiteral("Ctrl+Z"));
    QCOMPARE(shortcutOf(window.resignAction()), QStringLiteral("Ctrl+R"));
    QCOMPARE(shortcutOf(window.offerDrawAction()), QStringLiteral("Ctrl+Shift+R"));
    QCOMPARE(shortcutOf(window.claimDrawAction()), QStringLiteral("Ctrl+D"));
    QCOMPARE(shortcutOf(window.firstMoveAction()), QStringLiteral("Home"));
    QCOMPARE(shortcutOf(window.lastMoveAction()), QStringLiteral("End"));
    QCOMPARE(shortcutOf(window.copyFenAction()), QStringLiteral("Ctrl+Shift+F"));
    QCOMPARE(shortcutOf(window.copyPgnAction()), QStringLiteral("Ctrl+Shift+C"));
    QVERIFY(editMenu->actions().contains(window.copyFenAction()));
    QVERIFY(editMenu->actions().contains(window.copyPgnAction()));

    QVERIFY(window.flipBoardAction() != nullptr);
    QVERIFY(window.highlightLastMoveAction() != nullptr);
    QVERIFY(window.highlightCheckAction() != nullptr);
    QVERIFY(boardMenu->actions().contains(window.flipBoardAction()));
    QVERIFY(boardMenu->actions().contains(window.highlightLastMoveAction()));
    QVERIFY(boardMenu->actions().contains(window.highlightCheckAction()));
    QCOMPARE(shortcutOf(window.flipBoardAction()), QStringLiteral("Ctrl+F"));
    QCOMPARE(shortcutOf(window.highlightLastMoveAction()),
             QStringLiteral("Ctrl+Shift+H"));
    QCOMPARE(shortcutOf(window.highlightCheckAction()),
             QStringLiteral("Ctrl+Shift+K"));

    // The flip action and the status-line button share one orientation.
    QVERIFY(!window.chessBoard()->boardFlipped());
    QVERIFY(!window.flipBoardAction()->isChecked());
    window.flipBoardAction()->trigger();
    QVERIFY(window.chessBoard()->boardFlipped());
    QVERIFY(window.flipBoardButton()->isChecked());
    window.flipBoardButton()->click();
    QVERIFY(!window.chessBoard()->boardFlipped());
    QVERIFY(!window.flipBoardAction()->isChecked());

    // Everything the menus expose is still on the toolbar exactly once.
    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolBar != nullptr);
    QVERIFY(toolBar->actions().contains(window.takeBackAction()));
    QVERIFY(toolBar->actions().contains(window.firstMoveAction()));
}

void RulesTest::testMainWindowAcceptsDroppedPgnFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pgnPath = dir.filePath(QStringLiteral("dropped.pgn"));
    QFile file(pgnPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("1. e4 e5 2. Nf3 Nc6 3. Bb5 *\n");
    file.close();

    MainWindow window;

    // A platform drag session cannot be simulated with sendEvent, so the drop
    // entry point is exercised directly with the payload a file manager sends.
    QMimeData pgnDrop;
    pgnDrop.setUrls({QUrl::fromLocalFile(pgnPath)});
    QVERIFY(window.hasSupportedDrop(&pgnDrop));
    QVERIFY(window.handleMimeData(&pgnDrop));

    // The dropped game is loaded exactly like the "Load PGN..." action.
    QCOMPARE(window.gameController()->uciMoves().size(), 5);
    QCOMPARE(window.gameController()->uciMoves().last(), QStringLiteral("f1b5"));
    QCOMPARE(window.gameController()->moveCursor(), 5);
    QVERIFY(moveListText(window).contains(QStringLiteral("Bb5")));

    // A file that is neither an image nor a PGN is not a supported drop.
    const QString textPath = dir.filePath(QStringLiteral("notes.txt"));
    QFile textFile(textPath);
    QVERIFY(textFile.open(QIODevice::WriteOnly | QIODevice::Text));
    textFile.write("not a game\n");
    textFile.close();

    QMimeData textDrop;
    textDrop.setUrls({QUrl::fromLocalFile(textPath)});
    QVERIFY(!window.hasSupportedDrop(&textDrop));
    QVERIFY(!window.handleMimeData(&textDrop));
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
    QCOMPARE(settings.incrementMilliseconds, 0LL);
    QCOMPARE(settings.pgnTimeControl(), QStringLiteral("7200+0"));

    const QStringList headers = settings.pgnHeaders();
    QCOMPARE(headers.size(), 8);
    QVERIFY(headers.at(0).startsWith(QStringLiteral("[Event \"Casual Game\"]")));
    QVERIFY(headers.at(4).contains(QStringLiteral("Player")));
    QVERIFY(headers.at(5).contains(QStringLiteral("Test Engine")));
    QVERIFY(headers.at(6).contains(QStringLiteral("7200+0")));

    auto *timeControl = dialog.findChild<QComboBox *>(QStringLiteral("timeControlCombo"));
    QVERIFY(timeControl != nullptr);
    QCOMPARE(timeControl->count(), 6);
    QCOMPARE(timeControl->itemData(0).toLongLong(), 7200000LL);
    QCOMPARE(timeControl->itemData(1).toLongLong(), 3600000LL);
    QCOMPARE(timeControl->itemData(2).toLongLong(), 1800000LL);
    QCOMPARE(timeControl->itemData(3).toLongLong(), 900000LL);
    QCOMPARE(timeControl->itemData(4).toLongLong(), 300000LL);
    // The custom entry is the only one without a real base time.
    QCOMPARE(timeControl->itemData(5).toLongLong(), -1LL);

    auto *increment = dialog.findChild<QComboBox *>(QStringLiteral("incrementCombo"));
    QVERIFY(increment != nullptr);
    QCOMPARE(increment->currentIndex(), 0);
    QCOMPARE(increment->itemData(0).toLongLong(), 0LL);
    bool hasThreeSeconds = false;
    for (int index = 0; index < increment->count(); ++index) {
        hasThreeSeconds = hasThreeSeconds || increment->itemData(index).toLongLong() == 3000LL;
    }
    QVERIFY(hasThreeSeconds);

    for (int index = 0; index < increment->count(); ++index) {
        if (increment->itemData(index).toLongLong() != 3000LL) {
            continue;
        }
        increment->setCurrentIndex(index);
        const ComputerGameSettings incremented = dialog.settings();
        QCOMPARE(incremented.incrementMilliseconds, 3000LL);
        QCOMPARE(incremented.pgnTimeControl(), QStringLiteral("7200+3"));
        QVERIFY(incremented.pgnHeaders().at(6).contains(QStringLiteral("7200+3")));
        break;
    }

    // The custom entry replaces both the preset base time and the increment
    // list with free values.
    auto *customMinutes = dialog.findChild<QSpinBox *>(QStringLiteral("customMinutesSpin"));
    QVERIFY(customMinutes != nullptr);
    auto *customIncrement = dialog.findChild<QSpinBox *>(QStringLiteral("customIncrementSpin"));
    QVERIFY(customIncrement != nullptr);
    QVERIFY(!customMinutes->isEnabled());
    QVERIFY(!customIncrement->isEnabled());

    timeControl->setCurrentIndex(5);
    QVERIFY(customMinutes->isEnabled());
    QVERIFY(customIncrement->isEnabled());
    QVERIFY(!increment->isEnabled());

    customMinutes->setValue(42);
    customIncrement->setValue(7);
    const ComputerGameSettings custom = dialog.settings();
    QCOMPARE(custom.timeLimitMilliseconds, 42LL * 60000);
    QCOMPARE(custom.incrementMilliseconds, 7000LL);
    QCOMPARE(custom.pgnTimeControl(), QStringLiteral("2520+7"));

    // Leaving the custom entry restores the preset controls.
    timeControl->setCurrentIndex(4);
    QVERIFY(!customMinutes->isEnabled());
    QVERIFY(!customIncrement->isEnabled());
    QVERIFY(increment->isEnabled());
    QCOMPARE(dialog.settings().timeLimitMilliseconds, 300000LL);
}

void RulesTest::testMainWindowExplainsTheEvaluation() {
    MainWindow window;

    // The gauge carries the heuristic breakdown as its tooltip, naming the
    // terms the evaluator knows about.
    QVERIFY(window.pasteFen(QStringLiteral(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")));
    const QString startTooltip = window.evaluationBar()->toolTip();
    QVERIFY(!startTooltip.isEmpty());
    QVERIFY(startTooltip.contains(QStringLiteral("Material:")));
    QVERIFY(startTooltip.contains(QStringLiteral("Total:")));
    QVERIFY(startTooltip.contains(QStringLiteral("Endgame mop-up:")));

    // It follows the position: material dominates once a queen is on the board.
    QVERIFY(window.pasteFen(
        QStringLiteral("4k3/8/8/8/8/8/8/K5Q1 w - - 0 1")));
    const QString queenTooltip = window.evaluationBar()->toolTip();
    QVERIFY2(queenTooltip.contains(QStringLiteral("+9.00")),
             qPrintable(queenTooltip));
}

void RulesTest::testMainWindowSettingsMenu() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    MainWindow window(nullptr, tempDir.filePath(QStringLiteral("chessGui.conf")));

    // The evaluator dialog and the gauge preferences live in their own menu.
    auto *settingsMenu = static_cast<QMenu *>(nullptr);
    for (QMenu *menu : window.findChildren<QMenu *>()) {
        if (menu->menuAction() != nullptr &&
            menu->title() == QStringLiteral("Settings")) {
            settingsMenu = menu;
        }
    }
    QVERIFY(settingsMenu != nullptr);

    auto *evaluationParams =
        window.findChild<QAction *>(QStringLiteral("evaluationParamsAction"));
    auto *showScore =
        window.findChild<QAction *>(QStringLiteral("showEvaluationScoreAction"));
    auto *showCentreLine =
        window.findChild<QAction *>(QStringLiteral("showCentreLineAction"));
    QVERIFY(evaluationParams != nullptr);
    QVERIFY(showScore != nullptr);
    QVERIFY(showCentreLine != nullptr);
    QVERIFY(evaluationParams->isEnabled());
    QVERIFY(showScore->isCheckable());
    QVERIFY(showCentreLine->isCheckable());
    QVERIFY(settingsMenu->actions().contains(evaluationParams));
    QVERIFY(settingsMenu->actions().contains(showScore));
    QVERIFY(settingsMenu->actions().contains(showCentreLine));

    // The gauge preferences start from the configuration, are applied when they
    // change and are written back.
    QVERIFY(showScore->isChecked());
    QVERIFY(showCentreLine->isChecked());
    QVERIFY(window.evaluationBar()->showEvaluationText());
    QVERIFY(window.evaluationBar()->showCenterLine());

    showScore->setChecked(false);
    showCentreLine->setChecked(false);
    QVERIFY(!window.evaluationBar()->showEvaluationText());
    QVERIFY(!window.evaluationBar()->showCenterLine());
    QVERIFY(!window.config().showEvaluationScore());
    QVERIFY(!window.config().showCentreLine());

    // The background curve reports through a progress bar that stays hidden
    // while nothing is being computed.
    auto *curveProgress =
        window.findChild<QProgressBar *>(QStringLiteral("curveProgressBar"));
    auto *cancelCurve =
        window.findChild<QPushButton *>(QStringLiteral("cancelCurveButton"));
    QVERIFY(curveProgress != nullptr);
    QVERIFY(cancelCurve != nullptr);
    QVERIFY(!curveProgress->isVisible());
    QVERIFY(!cancelCurve->isVisible());

    // The background curve tells the graph how far it got: a freshly loaded
    // game is drawn as provisional until the analysis has walked it.
    window.gameController()->setEvaluationDepth(3);
    QVERIFY(window.gameController()->loadPgn(QStringLiteral(
        "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 *")));
    auto *curveGraph = window.findChild<EvaluationGraph *>();
    QVERIFY(curveGraph != nullptr);
    QVERIFY(curveGraph->evaluations().size() > 1);
    QVERIFY(curveGraph->analysedCount() >= 0);
    QVERIFY(curveGraph->analysedCount() < curveGraph->evaluations().size());
    QTRY_VERIFY_WITH_TIMEOUT(curveGraph->analysedCount() == -1, 5000);
    window.gameController()->setEvaluationDepth(
        HeuristicEval::DefaultSearchDepth);

    // A reopened window restores them, and the evaluator settings survive too.
    HeuristicEval::EvalParams tuned = HeuristicEval::params();
    tuned.rookValue = 505;
    window.gameController()->setEvaluationDepth(3);
    AppConfig stored = window.config();
    stored.setEvalParams(tuned);
    stored.setEvalDepth(3);
    stored.setEvalSearchThreads(2);
    QVERIFY(stored.save());

    MainWindow reopened(nullptr, tempDir.filePath(QStringLiteral("chessGui.conf")));
    QVERIFY(!reopened.evaluationBar()->showEvaluationText());
    QVERIFY(!reopened.evaluationBar()->showCenterLine());
    QCOMPARE(reopened.gameController()->evaluationDepth(), 3);
    QCOMPARE(HeuristicEval::searchThreads(), 2);
    QCOMPARE(HeuristicEval::params().rookValue, 505);

    HeuristicEval::resetParams();
    HeuristicEval::setSearchThreads(0);
}

QTEST_MAIN(RulesTest)
#include "rules_test.moc"
