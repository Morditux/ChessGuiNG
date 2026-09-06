// Unit tests for PgnFile helpers: game splitting, tag extraction, move count.

#include <QTest>

#include "pgnfile.h"

class PgnFileTest : public QObject {
    Q_OBJECT

private slots:
    void testSplitSingleGame();
    void testSplitMultipleGames();
    void testSplitGameWithBlankLinesInMovetext();
    void testTagValue();
    void testParseHeaders();
    void testMoveCount();
    void testMoveCountIgnoresCommentsAndNags();
    void testReplaceGamePreservesOtherGames();
};

void PgnFileTest::testSplitSingleGame() {
    const QString pgn = QStringLiteral(
        "[Event \"Test\"]\n"
        "[Result \"1-0\"]\n"
        "\n"
        "1. e4 e5 2. Nf3 1-0\n");

    const QStringList games = PgnFile::splitGames(pgn);
    QCOMPARE(games.size(), 1);
    QVERIFY(games.first().contains(QStringLiteral("[Event \"Test\"]")));
    QVERIFY(games.first().contains(QStringLiteral("1. e4 e5")));
}

void PgnFileTest::testSplitMultipleGames() {
    const QString pgn = QStringLiteral(
        "[Event \"Game 1\"]\n"
        "[White \"Alice\"]\n"
        "[Result \"1-0\"]\n"
        "\n"
        "1. e4 e5 2. Qh5 1-0\n"
        "\n"
        "[Event \"Game 2\"]\n"
        "[White \"Bob\"]\n"
        "[Result \"0-1\"]\n"
        "\n"
        "1. d4 d5 2. c4 0-1\n");

    const QStringList games = PgnFile::splitGames(pgn);
    QCOMPARE(games.size(), 2);
    QVERIFY(games.at(0).contains(QStringLiteral("[Event \"Game 1\"]")));
    QVERIFY(games.at(0).contains(QStringLiteral("e4")));
    QVERIFY(games.at(1).contains(QStringLiteral("[Event \"Game 2\"]")));
    QVERIFY(games.at(1).contains(QStringLiteral("d4")));
}

void PgnFileTest::testSplitGameWithBlankLinesInMovetext() {
    const QString pgn = QStringLiteral(
        "[Event \"Long\"]\n"
        "\n"
        "1. e4 e5\n"
        "\n"
        "2. Nf3 Nc6\n"
        "\n"
        "[Event \"Next\"]\n"
        "\n"
        "1. d4 d5\n");

    const QStringList games = PgnFile::splitGames(pgn);
    QCOMPARE(games.size(), 2);
    QVERIFY(games.at(0).contains(QStringLiteral("2. Nf3")));
    QVERIFY(!games.at(0).contains(QStringLiteral("d4")));
    QVERIFY(games.at(1).contains(QStringLiteral("d4")));
}

void PgnFileTest::testTagValue() {
    const QString game = QStringLiteral(
        "[Event \"Casual\"]\n"
        "[Site \"Home\"]\n"
        "[Date \"2026.08.27\"]\n"
        "[Result \"1/2-1/2\"]\n"
        "\n"
        "1. e4 e5 *\n");

    QCOMPARE(PgnFile::tagValue(game, QStringLiteral("Event")),
             QStringLiteral("Casual"));
    QCOMPARE(PgnFile::tagValue(game, QStringLiteral("Result")),
             QStringLiteral("1/2-1/2"));
    QCOMPARE(PgnFile::tagValue(game, QStringLiteral("White")), QString());
}

void PgnFileTest::testParseHeaders() {
    const QString game = QStringLiteral(
        "[Event \"Casual\"]\n"
        "[White \"Alice\"]\n"
        "\n"
        "1. e4 e5\n");

    const QStringList headers = PgnFile::parseHeaders(game);
    QCOMPARE(headers.size(), 2);
    QCOMPARE(headers.at(0), QStringLiteral("[Event \"Casual\"]"));
    QCOMPARE(headers.at(1), QStringLiteral("[White \"Alice\"]"));
}

void PgnFileTest::testMoveCount() {
    QCOMPARE(PgnFile::moveCount(QStringLiteral("1. e4 e5 2. Nf3 Nc6")), 4);
    QCOMPARE(PgnFile::moveCount(QStringLiteral("1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7#")), 7);
    QCOMPARE(PgnFile::moveCount(QStringLiteral("1. O-O O-O-O 2. exd5 axb5")), 4);
}

void PgnFileTest::testMoveCountIgnoresCommentsAndNags() {
    const QString game = QStringLiteral(
        "1. e4 {King's pawn} e5 $1 2. Nf3 ; a comment\n"
        "2... Nc6 1-0");
    QCOMPARE(PgnFile::moveCount(game), 4);
}

void PgnFileTest::testReplaceGamePreservesOtherGames() {
    const QString pgn = QStringLiteral(
        "[Event \"First\"]\r\n"
        "[White \"Alice\"]\r\n"
        "\r\n"
        "1. e4 e5 *\r\n"
        "\r\n\r\n"
        "[Event \"Second\"]\r\n"
        "[White \"Bob\"]\r\n"
        "\r\n"
        "1. d4 d5 *\r\n\r\n");
    const QVector<PgnFile::GameSegment> segments =
        PgnFile::splitGameSegments(pgn);

    QCOMPARE(segments.size(), 2);
    const QString secondHeader = QStringLiteral("[Event \"Second\"]");
    const int secondStart = pgn.indexOf(secondHeader);
    QVERIFY(secondStart >= 0);

    const QString replacement = QStringLiteral(
        "[Event \"First\"]\n\n1. e4 $4 { analysis } *");
    const QString replaced = PgnFile::replaceGame(pgn, segments, 0, replacement);
    const int replacedSecondStart = replaced.indexOf(secondHeader);
    QVERIFY(replacedSecondStart >= 0);

    QCOMPARE(replaced.mid(replacedSecondStart), pgn.mid(secondStart));
    QCOMPARE(replaced.left(segments.at(0).start),
             pgn.left(segments.at(0).start));
    QVERIFY(replaced.contains(replacement));
}

QTEST_GUILESS_MAIN(PgnFileTest)

#include "pgnfile_test.moc"
