//
// Unit tests for ChessBoard interactive annotations and drawing.
//

#include <QSignalSpy>
#include <QTest>

#include "chessboard.h"
#include "pgnannotations.h"

class ChessBoardTest : public QObject {
    Q_OBJECT

private slots:
    void testSquareAnnotationToggle();
    void testArrowAnnotationToggle();
    void testPgnAnnotationsEncodingDecoding();
    void testColorModifiers();
    void testMouseRightClickSquare();
    void testMouseRightDragArrow();
    void testLeftClickClearsAnnotations();
    void testResetClearsAnnotations();
    void testStripAnnotationTags();
    void testFormatComment();
};

void ChessBoardTest::testSquareAnnotationToggle() {
    ChessBoard board;
    QVERIFY(!board.hasUserAnnotations());
    QVERIFY(board.squareAnnotations().empty());

    QSignalSpy spy(&board, &ChessBoard::userAnnotationsChanged);

    const Rules::Position e4{4, 4};
    const QColor green = PgnAnnotations::greenColor();
    const QColor red = PgnAnnotations::redColor();

    // Add e4 (green)
    board.toggleSquareAnnotation(e4, green);
    QCOMPARE(board.squareAnnotations().size(), 1);
    QCOMPARE(board.squareAnnotations().front().position, e4);
    QCOMPARE(board.squareAnnotations().front().color, green);
    QVERIFY(board.hasUserAnnotations());
    QCOMPARE(spy.count(), 1);

    // Toggle same square with different color -> replaces color
    board.toggleSquareAnnotation(e4, red);
    QCOMPARE(board.squareAnnotations().size(), 1);
    QCOMPARE(board.squareAnnotations().front().color, red);
    QCOMPARE(spy.count(), 2);

    // Toggle same square with same color -> removes it
    board.toggleSquareAnnotation(e4, red);
    QVERIFY(board.squareAnnotations().empty());
    QVERIFY(!board.hasUserAnnotations());
    QCOMPARE(spy.count(), 3);
}

void ChessBoardTest::testArrowAnnotationToggle() {
    ChessBoard board;
    QVERIFY(board.userArrows().empty());

    QSignalSpy spy(&board, &ChessBoard::userAnnotationsChanged);

    const Rules::Position e2{6, 4};
    const Rules::Position e4{4, 4};
    const QColor green = PgnAnnotations::greenColor();
    const QColor blue = PgnAnnotations::blueColor();

    // Add arrow e2 -> e4 (green)
    board.toggleUserArrow(e2, e4, green);
    QCOMPARE(board.userArrows().size(), 1);
    QCOMPARE(board.userArrows().front().from, e2);
    QCOMPARE(board.userArrows().front().to, e4);
    QCOMPARE(board.userArrows().front().color, green);
    QVERIFY(board.hasUserAnnotations());
    QCOMPARE(spy.count(), 1);

    // Toggle same arrow with blue color -> replaces color
    board.toggleUserArrow(e2, e4, blue);
    QCOMPARE(board.userArrows().size(), 1);
    QCOMPARE(board.userArrows().front().color, blue);
    QCOMPARE(spy.count(), 2);

    // Toggle same arrow with same color -> removes it
    board.toggleUserArrow(e2, e4, blue);
    QVERIFY(board.userArrows().empty());
    QVERIFY(!board.hasUserAnnotations());
    QCOMPARE(spy.count(), 3);
}

void ChessBoardTest::testPgnAnnotationsEncodingDecoding() {
    const Rules::Position e4{4, 4};
    const Rules::Position c4{4, 2};
    const Rules::Position g5{3, 6};

    std::vector<SquareAnnotation> squares{
        {e4, PgnAnnotations::greenColor()},
        {c4, PgnAnnotations::redColor()}
    };

    std::vector<UserArrow> arrows{
        {e4, g5, PgnAnnotations::blueColor()}
    };

    const QString encoded = PgnAnnotations::encode(arrows, squares);
    QVERIFY(encoded.contains(QStringLiteral("[%csl Ge4,Rc4]")));
    QVERIFY(encoded.contains(QStringLiteral("[%cal Be4g5]")));

    std::vector<UserArrow> decodedArrows;
    std::vector<SquareAnnotation> decodedSquares;
    const bool ok = PgnAnnotations::decode(encoded, decodedArrows, decodedSquares);
    QVERIFY(ok);
    QCOMPARE(decodedSquares.size(), 2);
    QCOMPARE(decodedArrows.size(), 1);
    QCOMPARE(decodedSquares[0].position, e4);
    QCOMPARE(decodedSquares[0].color, PgnAnnotations::greenColor());
    QCOMPARE(decodedSquares[1].position, c4);
    QCOMPARE(decodedSquares[1].color, PgnAnnotations::redColor());
    QCOMPARE(decodedArrows[0].from, e4);
    QCOMPARE(decodedArrows[0].to, g5);
    QCOMPARE(decodedArrows[0].color, PgnAnnotations::blueColor());
}

void ChessBoardTest::testColorModifiers() {
    QCOMPARE(PgnAnnotations::colorToCode(PgnAnnotations::greenColor()), 'G');
    QCOMPARE(PgnAnnotations::colorToCode(PgnAnnotations::redColor()), 'R');
    QCOMPARE(PgnAnnotations::colorToCode(PgnAnnotations::blueColor()), 'B');
    QCOMPARE(PgnAnnotations::colorToCode(PgnAnnotations::orangeColor()), 'Y');

    QCOMPARE(PgnAnnotations::codeToColor('G'), PgnAnnotations::greenColor());
    QCOMPARE(PgnAnnotations::codeToColor('R'), PgnAnnotations::redColor());
    QCOMPARE(PgnAnnotations::codeToColor('B'), PgnAnnotations::blueColor());
    QCOMPARE(PgnAnnotations::codeToColor('Y'), PgnAnnotations::orangeColor());
}

void ChessBoardTest::testMouseRightClickSquare() {
    ChessBoard board;
    board.resize(480, 480);
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));

    const Rules::Position e4{4, 4};
    // Center point of e4
    // We can simulate right click by querying square position
    // Since board is 480x480, ranks and files are laid out cleanly
    // File e is column 4, rank 4 is row 4 in displayed coordinates
    // Let's compute a point inside the e4 square
    // Total side ~ 444, squareSize ~ 55, leftMargin = 28, topMargin = 8
    const int squareSize = (480 - 28 - 8) / 8;
    const QPoint e4Point(28 + 4 * squareSize + squareSize / 2, 8 + 4 * squareSize + squareSize / 2);

    QTest::mouseClick(&board, Qt::RightButton, Qt::NoModifier, e4Point);
    QCOMPARE(board.squareAnnotations().size(), 1);
    QCOMPARE(board.squareAnnotations().front().position, e4);
    QCOMPARE(board.squareAnnotations().front().color, PgnAnnotations::greenColor());

    // Click again to toggle off
    QTest::mouseClick(&board, Qt::RightButton, Qt::NoModifier, e4Point);
    QVERIFY(board.squareAnnotations().empty());
}

void ChessBoardTest::testMouseRightDragArrow() {
    ChessBoard board;
    board.resize(480, 480);
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));

    const int squareSize = (480 - 28 - 8) / 8;
    // e2: column 4, row 6
    const QPoint e2Point(28 + 4 * squareSize + squareSize / 2, 8 + 6 * squareSize + squareSize / 2);
    // e4: column 4, row 4
    const QPoint e4Point(28 + 4 * squareSize + squareSize / 2, 8 + 4 * squareSize + squareSize / 2);

    QTest::mousePress(&board, Qt::RightButton, Qt::NoModifier, e2Point);
    QTest::mouseMove(&board, e4Point);
    QTest::mouseRelease(&board, Qt::RightButton, Qt::NoModifier, e4Point);

    QCOMPARE(board.userArrows().size(), 1);
    QCOMPARE(board.userArrows().front().from, (Rules::Position{6, 4}));
    QCOMPARE(board.userArrows().front().to, (Rules::Position{4, 4}));
    QCOMPARE(board.userArrows().front().color, PgnAnnotations::greenColor());

    // Drag again to toggle off
    QTest::mousePress(&board, Qt::RightButton, Qt::NoModifier, e2Point);
    QTest::mouseMove(&board, e4Point);
    QTest::mouseRelease(&board, Qt::RightButton, Qt::NoModifier, e4Point);
    QVERIFY(board.userArrows().empty());
}

void ChessBoardTest::testLeftClickClearsAnnotations() {
    ChessBoard board;
    board.resize(480, 480);
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));

    board.toggleSquareAnnotation({4, 4}, PgnAnnotations::greenColor());
    board.toggleUserArrow({6, 4}, {4, 4}, PgnAnnotations::blueColor());
    QVERIFY(board.hasUserAnnotations());

    // Left click on board clears annotations
    const int squareSize = (480 - 28 - 8) / 8;
    const QPoint emptySquarePoint(28 + 0 * squareSize + squareSize / 2, 8 + 4 * squareSize + squareSize / 2); // a4
    QTest::mouseClick(&board, Qt::LeftButton, Qt::NoModifier, emptySquarePoint);

    QVERIFY(!board.hasUserAnnotations());
    QVERIFY(board.squareAnnotations().empty());
    QVERIFY(board.userArrows().empty());
}

void ChessBoardTest::testResetClearsAnnotations() {
    ChessBoard board;
    board.toggleSquareAnnotation({4, 4}, PgnAnnotations::greenColor());
    board.toggleUserArrow({6, 4}, {4, 4}, PgnAnnotations::redColor());
    QVERIFY(board.hasUserAnnotations());

    board.reset();
    QVERIFY(!board.hasUserAnnotations());
}

void ChessBoardTest::testStripAnnotationTags() {
    QCOMPARE(PgnAnnotations::stripAnnotationTags(QStringLiteral("[%csl Ge4] [%cal Ge2e4] Good opening")),
             QStringLiteral("Good opening"));
    QCOMPARE(PgnAnnotations::stripAnnotationTags(QStringLiteral("{ [%csl Ge4] Simple note }")),
             QStringLiteral("Simple note"));
    QCOMPARE(PgnAnnotations::stripAnnotationTags(QStringLiteral("[%cal Gc4e5]")),
             QString());
    QCOMPARE(PgnAnnotations::stripAnnotationTags(QStringLiteral("Pure comment without tags")),
             QStringLiteral("Pure comment without tags"));
}

void ChessBoardTest::testFormatComment() {
    const std::vector<UserArrow> arrows{{Rules::Position{6, 4}, Rules::Position{4, 4}, PgnAnnotations::greenColor()}};
    const std::vector<SquareAnnotation> squares{{Rules::Position{4, 4}, PgnAnnotations::greenColor()}};

    QCOMPARE(PgnAnnotations::formatComment(arrows, squares, QStringLiteral("Strong move")),
             QStringLiteral("[%csl Ge4] [%cal Ge2e4] Strong move"));
    QCOMPARE(PgnAnnotations::formatComment(arrows, {}, QString()),
             QStringLiteral("[%cal Ge2e4]"));
    QCOMPARE(PgnAnnotations::formatComment({}, {}, QStringLiteral("No visual tags")),
             QStringLiteral("No visual tags"));
    QCOMPARE(PgnAnnotations::formatComment({}, {}, QString()),
             QString());
}

QTEST_MAIN(ChessBoardTest)
#include "chessboard_test.moc"
