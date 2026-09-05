//
// Unit tests for UciParser.
//

#include <QTest>

#include "rules.h"
#include "uciparser.h"

class UciParserTest : public QObject {
    Q_OBJECT

private slots:
    void testParseInfoStandard();
    void testParseInfoMate();
    void testParseInfoMultiPv();
    void testParseBestMove();
    void testParseOption();
    void testParseId();
    void testKeywords();
    void testToUciMove();
    void testScoreToWinningPercentage();
};

void UciParserTest::testParseInfoStandard() {
    const QString line = QStringLiteral("info depth 20 seldepth 28 multipv 1 score cp 45 nodes 1245600 nps 1500000 time 830 currmove e2e4 currmovenumber 1 pv e2e4 e7e5 g1f3 b8c6");
    const auto result = UciParser::parseInfoLine(line);

    QVERIFY(result.has_value());
    QCOMPARE(result->depth, 20);
    QCOMPARE(result->seldepth, 28);
    QCOMPARE(result->multipv, 1);
    QCOMPARE(result->scoreCp, 45.0);
    QVERIFY(!result->mateIn.has_value());
    QCOMPARE(result->nodes, 1245600);
    QCOMPARE(result->nps, 1500000);
    QCOMPARE(result->timeMs, 830);
    QCOMPARE(result->currmove, QStringLiteral("e2e4"));
    QCOMPARE(result->currmovenumber, 1);
    QCOMPARE(result->pv, QStringLiteral("e2e4 e7e5 g1f3 b8c6"));
}

void UciParserTest::testParseInfoMate() {
    const QString lineMatePositive = QStringLiteral("info depth 12 score mate 3 nodes 45000 nps 900000 time 50 pv d1h5 g7g6 h5e5");
    const auto resultPos = UciParser::parseInfoLine(lineMatePositive);

    QVERIFY(resultPos.has_value());
    QCOMPARE(resultPos->depth, 12);
    QVERIFY(resultPos->mateIn.has_value());
    QCOMPARE(*resultPos->mateIn, 3);
    QCOMPARE(resultPos->pv, QStringLiteral("d1h5 g7g6 h5e5"));

    const QString lineMateNegative = QStringLiteral("info depth 8 score mate -2 nodes 12000 pv g8h8 d8f8");
    const auto resultNeg = UciParser::parseInfoLine(lineMateNegative);

    QVERIFY(resultNeg.has_value());
    QVERIFY(resultNeg->mateIn.has_value());
    QCOMPARE(*resultNeg->mateIn, -2);
}

void UciParserTest::testParseInfoMultiPv() {
    const QString line = QStringLiteral("info multipv 3 depth 16 score cp -80 pv c2c4 e7e5");
    const auto result = UciParser::parseInfoLine(line);

    QVERIFY(result.has_value());
    QCOMPARE(result->multipv, 3);
    QCOMPARE(result->depth, 16);
    QCOMPARE(result->scoreCp, -80.0);
    QCOMPARE(result->pv, QStringLiteral("c2c4 e7e5"));
}

void UciParserTest::testParseBestMove() {
    const QString lineWithPonder = QStringLiteral("bestmove e2e4 ponder e7e5");
    const auto bmPonder = UciParser::parseBestMoveLine(lineWithPonder);

    QVERIFY(bmPonder.has_value());
    QCOMPARE(bmPonder->move, QStringLiteral("e2e4"));
    QCOMPARE(bmPonder->ponder, QStringLiteral("e7e5"));

    const QString lineNoPonder = QStringLiteral("bestmove e7e8q");
    const auto bmNoPonder = UciParser::parseBestMoveLine(lineNoPonder);

    QVERIFY(bmNoPonder.has_value());
    QCOMPARE(bmNoPonder->move, QStringLiteral("e7e8q"));
    QVERIFY(bmNoPonder->ponder.isEmpty());

    QVERIFY(!UciParser::parseBestMoveLine(QStringLiteral("info depth 10")).has_value());
}

void UciParserTest::testParseOption() {
    const auto spin = UciParser::parseOptionLine(
        QStringLiteral("option name Threads type spin default 1 min 1 max 32"));
    QVERIFY(spin.has_value());
    QCOMPARE(spin->name, QStringLiteral("Threads"));
    QCOMPARE(spin->type, UciOption::Type::Spin);
    QCOMPARE(spin->defaultValue, QStringLiteral("1"));
    QCOMPARE(spin->minValue, QStringLiteral("1"));
    QCOMPARE(spin->maxValue, QStringLiteral("32"));

    const auto combo = UciParser::parseOptionLine(
        QStringLiteral("option name Style type combo default Normal var Normal var Risky"));
    QVERIFY(combo.has_value());
    QCOMPARE(combo->type, UciOption::Type::Combo);
    QCOMPARE(combo->vars, (QStringList{QStringLiteral("Normal"), QStringLiteral("Risky")}));

    QVERIFY(!UciParser::parseOptionLine(QStringLiteral("info depth 10")).has_value());
}

void UciParserTest::testParseId() {
    QCOMPARE(UciParser::parseIdName(QStringLiteral("id name Stockfish 16.1 AVX2")),
             QStringLiteral("Stockfish 16.1 AVX2"));
    QCOMPARE(UciParser::parseIdAuthor(QStringLiteral("id author the Stockfish developers")),
             QStringLiteral("the Stockfish developers"));
    QVERIFY(!UciParser::parseIdName(QStringLiteral("info depth 10")).has_value());
    QVERIFY(!UciParser::parseIdAuthor(QStringLiteral("bestmove e2e4")).has_value());
}

void UciParserTest::testKeywords() {
    QVERIFY(UciParser::isUciOk(QStringLiteral("uciok")));
    QVERIFY(UciParser::isUciOk(QStringLiteral("  uciok  \n")));
    QVERIFY(!UciParser::isUciOk(QStringLiteral("readyok")));

    QVERIFY(UciParser::isReadyOk(QStringLiteral("readyok")));
    QVERIFY(UciParser::isReadyOk(QStringLiteral("  readyok\r\n")));
    QVERIFY(!UciParser::isReadyOk(QStringLiteral("uciok")));
}

void UciParserTest::testToUciMove() {
    // e2 (row 6, col 4) to e4 (row 4, col 4)
    Rules::Position e2{6, 4};
    Rules::Position e4{4, 4};
    QCOMPARE(UciParser::toUciMove(e2, e4), QStringLiteral("e2e4"));

    // g1 (row 7, col 6) to f3 (row 5, col 5)
    Rules::Position g1{7, 6};
    Rules::Position f3{5, 5};
    QCOMPARE(UciParser::toUciMove(g1, f3), QStringLiteral("g1f3"));

    // e7 (row 1, col 4) to e8 (row 0, col 4) with Queen promotion
    Rules::Position e7{1, 4};
    Rules::Position e8{0, 4};
    QCOMPARE(UciParser::toUciMove(e7, e8, Rules::PieceType::Queen), QStringLiteral("e7e8q"));
    QCOMPARE(UciParser::toUciMove(e7, e8, Rules::PieceType::Knight), QStringLiteral("e7e8n"));
}

void UciParserTest::testScoreToWinningPercentage() {
    // 0 cp should be equal to 50%
    QCOMPARE(UciParser::scoreToWinningPercentage(0.0), 50.0);

    // Positive score > 50%
    QVERIFY(UciParser::scoreToWinningPercentage(100.0) > 50.0);

    // Negative score < 50%
    QVERIFY(UciParser::scoreToWinningPercentage(-100.0) < 50.0);

    // Mate positive should be 100%
    QCOMPARE(UciParser::scoreToWinningPercentage(0.0, 3), 100.0);

    // Mate negative should be 0%
    QCOMPARE(UciParser::scoreToWinningPercentage(0.0, -1), 0.0);
}

QTEST_MAIN(UciParserTest)
#include "uciparser_test.moc"
