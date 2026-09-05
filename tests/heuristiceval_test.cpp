//
// Unit tests for the classical heuristic evaluator.
//

#include <QTest>

#include "heuristiceval.h"
#include "rules.h"

class HeuristicEvalTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialPositionIsBalanced();
    void testCentipawnsToPercentage();
    void testInsufficientMaterial();
    void testMaterialAdvantage();
    void testPassedPawnAdvantage();
    void testColorAdvantageSign();
    void testFoolsMate();
    void testScholarsMate();
    void testPawnShelterRewardsCastledKing();
    void testCheckPenaltyForSideToMove();
    void testSingleMinorEndgamesAreDrawn();
};

void HeuristicEvalTest::testInitialPositionIsBalanced() {
    HeuristicEval eval;
    Rules rules;

    const int cp = eval.evaluateCentipawns(rules);
    const double displayPct = eval.evaluateDisplayPercentage(rules);

    // White's tempo is deliberately small, so the opening remains visually
    // balanced while still reflecting the side to move.
    QVERIFY(cp > -30 && cp < 30);
    QVERIFY(displayPct > 48.0 && displayPct < 52.0);
}

void HeuristicEvalTest::testCentipawnsToPercentage() {
    QCOMPARE(HeuristicEval::centipawnsToPercentage(0.0), 50.0);
    QCOMPARE(HeuristicEval::centipawnsToPercentage(10000.0), 100.0);
    QCOMPARE(HeuristicEval::centipawnsToPercentage(-10000.0), 0.0);

    const double plus400 = HeuristicEval::centipawnsToPercentage(400.0);
    QVERIFY(plus400 > 90.0 && plus400 < 92.0);

    const double minus400 = HeuristicEval::centipawnsToPercentage(-400.0);
    QVERIFY(minus400 > 8.0 && minus400 < 10.0);

    for (double cp = -2000.0; cp <= 2000.0; cp += 100.0) {
        const double p1 = HeuristicEval::centipawnsToPercentage(cp);
        const double p2 = HeuristicEval::centipawnsToPercentage(-cp);
        QVERIFY(qAbs((p1 + p2) - 100.0) < 1e-4);
    }
}

void HeuristicEvalTest::testInsufficientMaterial() {
    HeuristicEval eval;
    Rules rules;

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/2B5/4K3 w - - 0 1")));
    QCOMPARE(eval.evaluateCentipawns(rules), 0);

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));
    QCOMPARE(eval.evaluateCentipawns(rules), 0);
}

void HeuristicEvalTest::testMaterialAdvantage() {
    HeuristicEval eval;
    Rules rules;

    // 1. e4 e5 2. Nf3 Nc6 3. Nxe5 Nxe5: Black is up a knight for a pawn.
    QVERIFY(rules.tryMove({6, 4}, {4, 4}));
    QVERIFY(rules.tryMove({1, 4}, {3, 4}));
    QVERIFY(rules.tryMove({7, 6}, {5, 5}));
    QVERIFY(rules.tryMove({0, 1}, {2, 2}));
    QVERIFY(rules.tryMove({5, 5}, {3, 4}));
    QVERIFY(rules.tryMove({2, 2}, {3, 4}));

    QVERIFY(eval.evaluateCentipawns(rules) < 0);
    QVERIFY(eval.evaluateDisplayPercentage(rules) < 50.0);
}

void HeuristicEvalTest::testPassedPawnAdvantage() {
    HeuristicEval eval;
    Rules rules;

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/4P3/8/8/4K3 w - - 0 1")));
    QVERIFY(eval.evaluateCentipawns(rules) > 100);
}

void HeuristicEvalTest::testColorAdvantageSign() {
    HeuristicEval eval;
    Rules rules;

    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/3Q4/4K3 w - - 0 1")));
    QVERIFY(eval.evaluateCentipawns(rules) > 500);

    QVERIFY(rules.loadFen(QStringLiteral("4k3/3q4/8/8/8/8/8/4K3 b - - 0 1")));
    QVERIFY(eval.evaluateCentipawns(rules) < -500);
}

void HeuristicEvalTest::testFoolsMate() {
    HeuristicEval eval;
    Rules rules;

    QVERIFY(rules.tryMove({6, 5}, {5, 5}));
    QVERIFY(rules.tryMove({1, 4}, {3, 4}));
    QVERIFY(rules.tryMove({6, 6}, {4, 6}));
    QVERIFY(rules.tryMove({0, 3}, {4, 7}));

    QVERIFY(rules.isCheckmate(Rules::Color::White));
    QCOMPARE(eval.evaluateCentipawns(rules), -HeuristicEval::MateScore);
    QCOMPARE(eval.evaluateDisplayPercentage(rules), 0.0);
}

void HeuristicEvalTest::testScholarsMate() {
    HeuristicEval eval;
    Rules rules;

    QVERIFY(rules.tryMove({6, 4}, {4, 4}));
    QVERIFY(rules.tryMove({1, 4}, {3, 4}));
    QVERIFY(rules.tryMove({7, 3}, {3, 7}));
    QVERIFY(rules.tryMove({0, 1}, {2, 2}));
    QVERIFY(rules.tryMove({7, 5}, {4, 2}));
    QVERIFY(rules.tryMove({0, 6}, {2, 5}));
    QVERIFY(rules.tryMove({3, 7}, {1, 5}));

    QVERIFY(rules.isCheckmate(Rules::Color::Black));
    QCOMPARE(eval.evaluateCentipawns(rules), HeuristicEval::MateScore);
    QCOMPARE(eval.evaluateDisplayPercentage(rules), 100.0);
}

void HeuristicEvalTest::testPawnShelterRewardsCastledKing() {
    HeuristicEval eval;

    // Same material; the only difference is where the white king stands.
    // On g1 the king is behind its f2/g2/h2 pawn shelter, on a1 it stands
    // alone in the corner.
    Rules sheltered;
    QVERIFY(sheltered.loadFen(QStringLiteral("k7/8/8/8/8/8/5PPP/6K1 w - - 0 1")));
    Rules bare;
    QVERIFY(bare.loadFen(QStringLiteral("k7/8/8/8/8/8/5PPP/K7 w - - 0 1")));

    const int shelteredCp = eval.evaluateCentipawns(sheltered);
    const int bareCp = eval.evaluateCentipawns(bare);

    QVERIFY(shelteredCp > 100);
    // The castled king must beat the cornered one by more than the raw
    // piece-square difference, i.e. the pawn shelter has to count.
    QVERIFY(shelteredCp - bareCp > 30);
}

void HeuristicEvalTest::testCheckPenaltyForSideToMove() {
    HeuristicEval eval;

    // Same material; in the second position Black's rook checks the white
    // king, so White (to move) must score lower than in the quiet position.
    Rules quiet;
    QVERIFY(quiet.loadFen(QStringLiteral("4k3/8/8/3r4/8/8/8/R3K3 w - - 0 1")));
    Rules check;
    QVERIFY(check.loadFen(QStringLiteral("4k3/8/8/8/4r3/8/8/R3K3 w - - 0 1")));
    QVERIFY(check.isInCheck(Rules::Color::White));

    QVERIFY(eval.evaluateCentipawns(check) < eval.evaluateCentipawns(quiet));
}

void HeuristicEvalTest::testSingleMinorEndgamesAreDrawn() {
    HeuristicEval eval;
    Rules rules;

    // King and bishop against king and bishop cannot be won.
    QVERIFY(rules.loadFen(QStringLiteral("2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1")));
    QCOMPARE(eval.evaluateCentipawns(rules), 0);

    // King and knight against king and knight cannot be won either.
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/3n4/4K1N1 w - - 0 1")));
    QCOMPARE(eval.evaluateCentipawns(rules), 0);
}

QTEST_MAIN(HeuristicEvalTest)
#include "heuristiceval_test.moc"
