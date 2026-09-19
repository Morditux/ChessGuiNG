//
// Unit tests for the classical heuristic evaluator.
//

#include <QTest>

#include <algorithm>

#include "heuristiceval.h"
#include "rules.h"

namespace {

// Mirrors a position by flipping the board vertically and swapping the
// colours, which is a symmetry of the game. En passant is dropped; castling
// rights follow the colour swap.
QString mirrorFen(const QString &fen) {
    const QStringList parts = fen.split(QLatin1Char(' '));
    QStringList ranks = parts[0].split(QLatin1Char('/'));
    std::reverse(ranks.begin(), ranks.end());
    for (QString &rank : ranks) {
        for (QChar &ch : rank) {
            if (ch.isUpper()) {
                ch = ch.toLower();
            } else if (ch.isLower()) {
                ch = ch.toUpper();
            }
        }
    }

    QString castling;
    if (parts.size() > 2 && parts[2] != QLatin1String("-")) {
        const QString rights = parts[2];
        if (rights.contains(QLatin1Char('k'))) {
            castling += QLatin1Char('K');
        }
        if (rights.contains(QLatin1Char('q'))) {
            castling += QLatin1Char('Q');
        }
        if (rights.contains(QLatin1Char('K'))) {
            castling += QLatin1Char('k');
        }
        if (rights.contains(QLatin1Char('Q'))) {
            castling += QLatin1Char('q');
        }
    }
    if (castling.isEmpty()) {
        castling = QStringLiteral("-");
    }

    const QString side = parts.size() > 1 && parts[1] == QLatin1String("w")
                             ? QStringLiteral("b")
                             : QStringLiteral("w");
    return ranks.join(QLatin1Char('/')) + QLatin1Char(' ') + side +
           QLatin1Char(' ') + castling + QStringLiteral(" - 0 1");
}

} // namespace

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
    void testColorSymmetry();
    void testReferencePositions();
    void testInsufficientMaterialExtended();
    void testOppositeColorBishopsScaled();
    void testPawnOnSeventhRank();
    void testPassedPawnBlocked();
    void testSupportedPawn();
    void testHangingPiecePenalty();
    void testKnightOutpost();
    void testBadBishop();
    void testConnectedRooks();
    void testCastlingRightsBonus();
    void testSearchFindsMateInOne();
    void testSearchSeesFreeCapture();
    void testSearchHandlesDeadDrawnPosition();
    void testSearchIsRepeatableSingleThreaded();
    void testEvalParamsDrivePieceValues();
    void testMaterialDrawsFollowRules();
    void testFiftyMoveAndRepetitionDraws();
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

void HeuristicEvalTest::testColorSymmetry() {
    const QStringList fens = {
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        QStringLiteral("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1"),
        QStringLiteral("4k3/8/8/3r4/8/8/8/R3K3 w - - 0 1"),
        QStringLiteral("4k3/8/8/8/4r3/8/8/R3K3 w - - 0 1"),
        QStringLiteral("k7/8/8/8/8/8/5PPP/6K1 w - - 0 1"),
        QStringLiteral("2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1"),
        QStringLiteral("4k3/p6p/8/3N4/4P3/8/8/4K3 w - - 0 1"),
        QStringLiteral("2b4k/P7/8/8/8/8/8/K1B5 w - - 0 1"),
        QStringLiteral("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1")};

    for (const QString &fen : fens) {
        Rules original;
        QVERIFY2(original.loadFen(fen), qPrintable(fen));
        Rules mirrored;
        QVERIFY2(mirrored.loadFen(mirrorFen(fen)), qPrintable(mirrorFen(fen)));
        QCOMPARE(HeuristicEval::evaluateCentipawns(original),
                 -HeuristicEval::evaluateCentipawns(mirrored));
    }
}

void HeuristicEvalTest::testReferencePositions() {
    struct Reference {
        const char *fen;
        bool whiteBetter;
    };
    const Reference references[] = {
        {"4k3/8/8/8/8/8/3Q4/4K3 w - - 0 1", true},
        {"4k3/3q4/8/8/8/8/8/4K3 b - - 0 1", false},
        {"4k3/8/8/8/8/8/8/R3K3 w - - 0 1", true},
        {"r3k3/8/8/8/8/8/8/4K3 b - - 0 1", false},
        {"q3k3/8/8/8/8/8/8/4K3 b - - 0 1", false},
        {"4k3/8/8/8/8/8/P7/4K2b b - - 0 1", false},
        {"2b1k3/8/8/8/8/8/8/2B1KB2 w - - 0 1", true},
        {"4k3/8/8/8/8/8/P7/4K3 w - - 0 1", true},
        {"4k3/8/8/8/8/8/PP6/4K3 w - - 0 1", true},
    };

    for (const Reference &reference : references) {
        Rules rules;
        QVERIFY(rules.loadFen(QString::fromLatin1(reference.fen)));
        const int cp = HeuristicEval::evaluateCentipawns(rules);
        if (reference.whiteBetter) {
            QVERIFY2(cp > 0, qPrintable(QString::number(cp)));
        } else {
            QVERIFY2(cp < 0, qPrintable(QString::number(cp)));
        }
    }
}

void HeuristicEvalTest::testInsufficientMaterialExtended() {
    HeuristicEval eval;

    // Two knights against a lone king cannot force mate.
    Rules twoKnights;
    QVERIFY(twoKnights.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/1N2K1N1 w - - 0 1")));
    QCOMPARE(eval.evaluateCentipawns(twoKnights), 0);

    // A bishop against a knight is also a dead draw.
    Rules bishopVsKnight;
    QVERIFY(bishopVsKnight.loadFen(
        QStringLiteral("4k3/8/8/8/8/2n5/8/2B1K3 w - - 0 1")));
    QCOMPARE(eval.evaluateCentipawns(bishopVsKnight), 0);
}

void HeuristicEvalTest::testOppositeColorBishopsScaled() {
    // Same material and pawn placement; only the black bishop's square colour
    // changes. Opposite-coloured bishops make the position drawish, so the
    // evaluation must shrink.
    Rules opposite;
    QVERIFY(opposite.loadFen(
        QStringLiteral("2b4k/P7/8/8/8/8/8/K1B5 w - - 0 1")));
    Rules same;
    QVERIFY(same.loadFen(
        QStringLiteral("5b1k/P7/8/8/8/8/8/K1B5 w - - 0 1")));

    const int oppositeCp = HeuristicEval::evaluateCentipawns(opposite);
    const int sameCp = HeuristicEval::evaluateCentipawns(same);
    QVERIFY(oppositeCp > 0);
    QVERIFY(oppositeCp < sameCp);
}

void HeuristicEvalTest::testPawnOnSeventhRank() {
    Rules seventh;
    QVERIFY(seventh.loadFen(
        QStringLiteral("4k3/4P3/8/8/8/8/8/4K3 w - - 0 1")));
    Rules sixth;
    QVERIFY(sixth.loadFen(
        QStringLiteral("4k3/8/4P3/8/8/8/8/4K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(seventh) >
            HeuristicEval::evaluateCentipawns(sixth));
}

void HeuristicEvalTest::testPassedPawnBlocked() {
    // The black knight blocks the e-pawn on e6; the d-pawn is unobstructed.
    Rules blocked;
    QVERIFY(blocked.loadFen(
        QStringLiteral("4k3/8/4n3/4P3/8/8/8/4K3 w - - 0 1")));
    Rules open;
    QVERIFY(open.loadFen(
        QStringLiteral("4k3/8/4n3/3P4/8/8/8/4K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(blocked) <
            HeuristicEval::evaluateCentipawns(open));
}

void HeuristicEvalTest::testSupportedPawn() {
    // In the first position c3 defends d4; in the second the e-pawn has no
    // supporter. Both have the same pawn count and ranks.
    Rules supported;
    QVERIFY(supported.loadFen(
        QStringLiteral("4k3/8/8/8/3P4/2P5/8/4K3 w - - 0 1")));
    Rules unsupported;
    QVERIFY(unsupported.loadFen(
        QStringLiteral("4k3/8/8/8/4P3/2P5/8/4K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(supported) >
            HeuristicEval::evaluateCentipawns(unsupported));
}

void HeuristicEvalTest::testHangingPiecePenalty() {
    // The white knight on d4 is attacked by the e5 pawn. In the first position
    // it is undefended, in the second c3 defends it.
    Rules hanging;
    QVERIFY(hanging.loadFen(
        QStringLiteral("4k3/7p/8/4p3/3N4/8/P7/4K3 w - - 0 1")));
    Rules defended;
    QVERIFY(defended.loadFen(
        QStringLiteral("4k3/7p/8/4p3/3N4/2P5/8/4K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(defended) >
            HeuristicEval::evaluateCentipawns(hanging));
}

void HeuristicEvalTest::testKnightOutpost() {
    // The knight on d5 is protected by the e4 pawn and cannot be attacked by
    // an enemy pawn; the knight on d4 has no pawn protection.
    Rules outpost;
    QVERIFY(outpost.loadFen(
        QStringLiteral("4k3/p6p/8/3N4/4P3/8/8/4K3 w - - 0 1")));
    Rules noOutpost;
    QVERIFY(noOutpost.loadFen(
        QStringLiteral("4k3/p6p/8/8/3NP3/8/8/4K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(outpost) >
            HeuristicEval::evaluateCentipawns(noOutpost));
}

void HeuristicEvalTest::testBadBishop() {
    // The white bishop on c1 is light-squared. In the first position the pawns
    // sit on light squares and block it, in the second they sit on dark ones.
    Rules bad;
    QVERIFY(bad.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/1P1P1P1P/2B1K3 w - - 0 1")));
    Rules good;
    QVERIFY(good.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/P1P1P1P1/2B1K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(bad) <
            HeuristicEval::evaluateCentipawns(good));
}

void HeuristicEvalTest::testConnectedRooks() {
    // On e1 the rook is connected to a1; on e2 it is not.
    Rules connected;
    QVERIFY(connected.loadFen(
        QStringLiteral("7k/8/8/8/8/8/8/R3R1K1 w - - 0 1")));
    Rules separate;
    QVERIFY(separate.loadFen(
        QStringLiteral("7k/8/8/8/8/8/4R3/R5K1 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(connected) >
            HeuristicEval::evaluateCentipawns(separate));
}

void HeuristicEvalTest::testCastlingRightsBonus() {
    // Identical boards; only the castling rights differ.
    Rules canCastle;
    QVERIFY(canCastle.loadFen(
        QStringLiteral("r3k2r/8/8/8/8/8/8/R3K2R w KQ - 0 1")));
    Rules cannotCastle;
    QVERIFY(cannotCastle.loadFen(
        QStringLiteral("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(canCastle) >
            HeuristicEval::evaluateCentipawns(cannotCastle));
}

void HeuristicEvalTest::testSearchFindsMateInOne() {
    // 1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6: Qxf7 is mate in one, but the mate is not
    // yet on the board, so only the search can see it.
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4")));

    const auto result = HeuristicEval::search(rules);
    QVERIFY(result.mateIn.has_value());
    QCOMPARE(*result.mateIn, 1);
    QCOMPARE(result.centipawns, HeuristicEval::MateScore);
}

void HeuristicEvalTest::testSearchSeesFreeCapture() {
    // Black's queen on d5 is en prise to exd5; the classical evaluation still
    // reports a large black advantage, the search must reverse it.
    Rules rules;
    QVERIFY(rules.loadFen(
        QStringLiteral("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1")));

    const int staticScore = HeuristicEval::evaluateCentipawns(rules);
    const auto result = HeuristicEval::search(rules);

    QVERIFY(staticScore < 0);
    QVERIFY(result.centipawns > 0);
    QVERIFY(result.centipawns > staticScore);
    QVERIFY(!result.mateIn.has_value());
}

void HeuristicEvalTest::testSearchHandlesDeadDrawnPosition() {
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));

    const auto result = HeuristicEval::search(rules);
    QCOMPARE(result.centipawns, 0);
    QVERIFY(!result.mateIn.has_value());
}

void HeuristicEvalTest::testSearchIsRepeatableSingleThreaded() {
    // A single-threaded search has no dependence on scheduling, so two runs
    // must be bit-identical. (The parallel search shares its alpha and table
    // across workers, so late-move reductions can make its exact value depend
    // on thread timing; that is only exercised here for repeatability.)
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));

    qputenv("CHESSGUI_EVAL_THREADS", "1");
    const auto first = HeuristicEval::search(rules, 4, 2);
    const auto second = HeuristicEval::search(rules, 4, 2);
    qunsetenv("CHESSGUI_EVAL_THREADS");

    QCOMPARE(first.centipawns, second.centipawns);
    QCOMPARE(first.mateIn.has_value(), second.mateIn.has_value());
    if (first.mateIn.has_value()) {
        QCOMPARE(*first.mateIn, *second.mateIn);
    }
}

void HeuristicEvalTest::testEvalParamsDrivePieceValues() {
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/P7/4K3 w - - 0 1")));

    HeuristicEval::EvalParams tuned = HeuristicEval::params();
    tuned.pawnValue = 2 * HeuristicEval::params().pawnValue;
    HeuristicEval::setParams(tuned);

    const int tunedPawnValue = HeuristicEval::pieceValue(Rules::PieceType::Pawn);
    const int tunedScore = HeuristicEval::evaluateCentipawns(rules);

    // The parameters are process-wide, so the defaults must come back before
    // any assertion can abort the test.
    HeuristicEval::resetParams();
    const int defaultPawnValue = HeuristicEval::pieceValue(Rules::PieceType::Pawn);
    const int defaultScore = HeuristicEval::evaluateCentipawns(rules);

    QCOMPARE(tunedPawnValue, 200);
    QCOMPARE(defaultPawnValue, 100);
    QVERIFY2(tunedScore > defaultScore,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(tunedScore)
                            .arg(defaultScore)));
}

void HeuristicEvalTest::testMaterialDrawsFollowRules() {
    // The evaluator delegates the material-draw test to the rules engine, so
    // every dead position has to score zero. The two bishops on the same square
    // colour used to be missed by the evaluator's own copy of the rule.
    const QStringList deadPositions = {
        QStringLiteral("4k3/8/8/8/8/8/8/K1B1B3 w - - 0 1"),
        QStringLiteral("4k3/8/8/8/8/2b1B3/8/4K3 w - - 0 1"),
        QStringLiteral("4k3/8/8/8/8/8/2N5/4K3 w - - 0 1"),
        QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 1"),
    };

    for (const QString &fen : deadPositions) {
        Rules rules;
        QVERIFY2(rules.loadFen(fen), qPrintable(fen));
        QVERIFY2(rules.isInsufficientMaterial(), qPrintable(fen));
        QCOMPARE(HeuristicEval::evaluateCentipawns(rules), 0);
        QCOMPARE(HeuristicEval::search(rules, 2, 2).centipawns, 0);
    }

    // The evaluator also treats material that cannot force mate as drawn, even
    // though the game itself is not adjudicated by the rules engine.
    Rules loneMinorEach;
    QVERIFY(loneMinorEach.loadFen(
        QStringLiteral("2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1")));
    QVERIFY(!loneMinorEach.isInsufficientMaterial());
    QCOMPARE(HeuristicEval::evaluateCentipawns(loneMinorEach), 0);

    // Material that can still mate must not be swallowed by the draw test.
    Rules twoBishopsOpposite;
    QVERIFY(twoBishopsOpposite.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/8/KBB4b w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateCentipawns(twoBishopsOpposite) != 0);
}

void HeuristicEvalTest::testFiftyMoveAndRepetitionDraws() {
    // White is a rook up, but the fifty-move clock has run out: the position is
    // drawn and both the static score and the search have to say so.
    Rules drawn;
    QVERIFY(drawn.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/R3K3 w - - 100 60")));
    QVERIFY(drawn.isFiftyMoveRule());
    QCOMPARE(HeuristicEval::evaluateCentipawns(drawn), 0);
    QCOMPARE(HeuristicEval::search(drawn, 2, 2).centipawns, 0);
    QVERIFY(!HeuristicEval::search(drawn, 2, 2).mateIn.has_value());

    // One half-move earlier every quiet move runs the clock out, so the search
    // must see the draw inside its horizon and stop counting the rook.
    Rules nearly;
    QVERIFY(nearly.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/R3K3 w - - 99 60")));
    QVERIFY(HeuristicEval::evaluateCentipawns(nearly) > 400);
    QCOMPARE(HeuristicEval::search(nearly, 2, 2).centipawns, 0);

    // Capturing the pawn resets the clock, and that move is not a draw.
    Rules captureResets;
    QVERIFY(captureResets.loadFen(
        QStringLiteral("4k3/8/8/p7/8/8/8/R3K3 w - - 99 60")));
    QVERIFY(HeuristicEval::search(captureResets, 2, 2).centipawns > 300);

    // Two shuffling knights with pawns on the board: the third occurrence of
    // the same position is a draw, and both entry points must report it.
    Rules repetition;
    QVERIFY(repetition.loadFen(
        QStringLiteral("4k1n1/6p1/8/8/8/8/8/1N2K3 w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateCentipawns(repetition) != 0);

    for (int cycle = 0; cycle < 2; ++cycle) {
        QVERIFY(repetition.tryMove({7, 1}, {6, 3})); // Nb1-d2
        QVERIFY(repetition.tryMove({0, 6}, {2, 5})); // Ng8-f6
        QVERIFY(repetition.tryMove({6, 3}, {7, 1})); // Nd2-b1
        QVERIFY(repetition.tryMove({2, 5}, {0, 6})); // Nf6-g8
    }

    QVERIFY(repetition.isThreefoldRepetition());
    QCOMPARE(HeuristicEval::evaluateCentipawns(repetition), 0);
    QCOMPARE(HeuristicEval::search(repetition, 2, 2).centipawns, 0);
}

QTEST_MAIN(HeuristicEvalTest)
#include "heuristiceval_test.moc"
