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

// Mirrors a UCI move the same way mirrorFen() mirrors a position.
QString mirrorUci(const QString &uci) {
    QString mirrored = uci;
    for (int i = 1; i < mirrored.size(); i += 2) {
        mirrored[i] = QChar(QLatin1Char('9').unicode() - mirrored.at(i).digitValue());
    }
    return mirrored;
}

// Independent mate oracle used by the reference suite: a plain minimax over the
// rules engine, so the mate distances the evaluator reports are checked against
// something that shares none of its code. `plies` is odd, so the side to move
// delivers the mate.
bool forcedMateIn(Rules &rules, int plies, const Rules::Move *first = nullptr) {
    if (plies <= 0) {
        return false;
    }
    Rules::Move moves[256];
    const int count = rules.generatePseudoLegalMoves(moves, 256);
    for (int i = 0; i < count; ++i) {
        if (first != nullptr && !(moves[i] == *first)) {
            continue;
        }
        Rules::Undo undo;
        if (!rules.makeMove(moves[i], undo)) {
            continue;
        }
        bool mate = false;
        if (rules.isCheckmate(rules.currentPlayer())) {
            mate = true;
        } else if (plies >= 3) {
            // A defender with no legal move and no check is stalemated, not
            // mated, and every answer it does have must lose to a mate one move
            // further on.
            bool anyReply = false;
            bool allLose = true;
            Rules::Move replies[256];
            const int replyCount = rules.generatePseudoLegalMoves(replies, 256);
            for (int j = 0; j < replyCount; ++j) {
                Rules::Undo replyUndo;
                if (!rules.makeMove(replies[j], replyUndo)) {
                    continue;
                }
                anyReply = true;
                const bool loses = forcedMateIn(rules, plies - 2);
                rules.unmakeMove(replies[j], replyUndo);
                if (!loses) {
                    allLose = false;
                    break;
                }
            }
            mate = anyReply && allLose;
        }
        rules.unmakeMove(moves[i], undo);
        if (mate) {
            return true;
        }
    }
    return false;
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
    void testSearchThreadCap();
    void testSearchCacheIsReusedConsistently();
    void testEvaluationCacheIsTransparent();
    void testEvaluationCacheFollowsTheParams();
    void testReferenceScores();
    void testEvalParamsDrivePieceValues();
    void testMaterialDrawsFollowRules();
    void testFiftyMoveAndRepetitionDraws();
    void testEndgameMopUpDrivesTheKingToTheEdge();
    void testWrongColouredRookPawnIsDrawn();
    void testEvalBreakdownExplainsTheScore();
    void testSearchReportsBestMoveAndVariation();
    void testSearchHonoursNodeLimit();
    void testSearchHonoursCancellationAndDeadline();
    void testSearchReferenceSignatures();
    void testSearchMatchesTheMateOracle();
    void testSearchWinsTheFreeMaterial();
    void testSearchDefendsAgainstMateInOne();
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
    // must be bit-identical. The thread cap is what makes it single-threaded,
    // and it works even when the worker pool already exists, which the
    // CHESSGUI_EVAL_THREADS environment variable could not guarantee.
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));

    HeuristicEval::setSearchThreads(1);
    const auto first = HeuristicEval::search(rules, 4, 2);
    const auto second = HeuristicEval::search(rules, 4, 2);
    HeuristicEval::setSearchThreads(0);

    QCOMPARE(first.centipawns, second.centipawns);
    QCOMPARE(first.mateIn.has_value(), second.mateIn.has_value());
    if (first.mateIn.has_value()) {
        QCOMPARE(*first.mateIn, *second.mateIn);
    }
}

void HeuristicEvalTest::testSearchThreadCap() {
    // Zero means "derive it from the hardware"; a positive value caps the root
    // workers and one makes the search single-threaded.
    HeuristicEval::setSearchThreads(0);
    QVERIFY(HeuristicEval::searchThreads() >= 1);

    HeuristicEval::setSearchThreads(3);
    QCOMPARE(HeuristicEval::searchThreads(), 3);

    HeuristicEval::setSearchThreads(1);
    QCOMPARE(HeuristicEval::searchThreads(), 1);

    // A single-worker search must still find a mate.
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4")));
    const auto result = HeuristicEval::search(rules, 3, 2);
    HeuristicEval::setSearchThreads(0);

    QVERIFY(result.mateIn.has_value());
    QCOMPARE(*result.mateIn, 1);
}

void HeuristicEvalTest::testSearchCacheIsReusedConsistently() {
    // The transposition table now survives from one search to the next, so a
    // warm search must return exactly what the cold one did: deeper entries are
    // keyed by the position and only reused up to their depth.
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));

    HeuristicEval::setSearchThreads(1);
    HeuristicEval::clearSearchCache();
    const auto cold = HeuristicEval::search(rules, 5, 2);
    const auto warm = HeuristicEval::search(rules, 5, 2);

    // A different position in between must not disturb the stored bounds.
    Rules other;
    QVERIFY(other.loadFen(QStringLiteral(
        "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2")));
    const auto otherResult = HeuristicEval::search(other, 4, 2);
    QVERIFY(!otherResult.mateIn.has_value());
    const auto warmAgain = HeuristicEval::search(rules, 5, 2);
    HeuristicEval::clearSearchCache();
    HeuristicEval::setSearchThreads(0);

    QCOMPARE(warm.centipawns, cold.centipawns);
    QCOMPARE(warmAgain.centipawns, cold.centipawns);
    QCOMPARE(warm.mateIn.has_value(), cold.mateIn.has_value());
    if (cold.mateIn.has_value()) {
        QCOMPARE(*warm.mateIn, *cold.mateIn);
        QCOMPARE(*warmAgain.mateIn, *cold.mateIn);
    }

    // Mate scores survive the table too, adjusted for the distance.
    Rules mateInOne;
    QVERIFY(mateInOne.loadFen(QStringLiteral(
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4")));
    HeuristicEval::clearSearchCache();
    const auto mateCold = HeuristicEval::search(mateInOne, 5, 2);
    const auto mateWarm = HeuristicEval::search(mateInOne, 5, 2);
    HeuristicEval::clearSearchCache();

    QVERIFY(mateCold.mateIn.has_value());
    QCOMPARE(*mateCold.mateIn, 1);
    QCOMPARE(mateWarm.centipawns, mateCold.centipawns);
    QVERIFY(mateWarm.mateIn.has_value());
    QCOMPARE(*mateWarm.mateIn, 1);
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

void HeuristicEvalTest::testEndgameMopUpDrivesTheKingToTheEdge() {
    // A queen against a bare king: pushing the king into the corner has to
    // raise the evaluation, and the endgame mop-up is what pays for the push.
    Rules cornered;
    QVERIFY(cornered.loadFen(QStringLiteral("k7/8/8/8/8/8/8/K5Q1 w - - 0 1")));
    Rules central;
    QVERIFY(central.loadFen(QStringLiteral("8/8/8/3k4/8/8/8/K5Q1 w - - 0 1")));

    const int withMopUp = HeuristicEval::evaluateCentipawns(cornered) -
                          HeuristicEval::evaluateCentipawns(central);

    // Switching the term off has to remove part of the difference, and the
    // rest of it must not be a mop-up that fires without an advantage.
    HeuristicEval::EvalParams disabled = HeuristicEval::params();
    disabled.mopUpMaterialThreshold = 100'000;
    HeuristicEval::setParams(disabled);
    const int withoutMopUp = HeuristicEval::evaluateCentipawns(cornered) -
                             HeuristicEval::evaluateCentipawns(central);
    HeuristicEval::resetParams();

    QVERIFY(withMopUp > 0);
    QVERIFY2(withMopUp > withoutMopUp,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(withMopUp)
                            .arg(withoutMopUp)));

    // A balanced opening keeps the phase high, where the mop-up is tapered
    // away and must not disturb the score.
    Rules opening;
    const int openingScore = HeuristicEval::evaluateCentipawns(opening);
    QVERIFY(openingScore > -60);
    QVERIFY(openingScore < 60);
}

void HeuristicEvalTest::testWrongColouredRookPawnIsDrawn() {
    // A bishop that cannot control the promotion square of a rook pawn turns
    // the extra pawn into nothing: the defending king just walks to the corner.
    Rules wrongA;
    QVERIFY(wrongA.loadFen(QStringLiteral("7k/8/8/8/8/8/P7/K1B5 w - - 0 1")));
    QCOMPARE(HeuristicEval::evaluateCentipawns(wrongA), 0);

    Rules rightA;
    QVERIFY(rightA.loadFen(QStringLiteral("7k/8/8/8/8/8/P7/KB6 w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateCentipawns(rightA) > 0);

    // The h-pawn promotes on the other square colour, so the two bishops swap
    // roles.
    Rules wrongH;
    QVERIFY(wrongH.loadFen(QStringLiteral("7k/8/8/8/8/8/7P/KB6 w - - 0 1")));
    QCOMPARE(HeuristicEval::evaluateCentipawns(wrongH), 0);

    Rules rightH;
    QVERIFY(rightH.loadFen(QStringLiteral("7k/8/8/8/8/8/7P/K1B5 w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateCentipawns(rightH) > 0);
}

void HeuristicEvalTest::testEvalBreakdownExplainsTheScore() {
    // The breakdown's total is the score itself, whatever the position.
    const QStringList fens = {
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        QStringLiteral("r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9"),
        QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 1"),
        QStringLiteral("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4"),
    };
    for (const QString &fen : fens) {
        Rules rules;
        QVERIFY2(rules.loadFen(fen), qPrintable(fen));
        const HeuristicEval::EvalBreakdown breakdown =
            HeuristicEval::evaluateBreakdown(rules);
        QCOMPARE(breakdown.total, HeuristicEval::evaluateCentipawns(rules));
    }

    // A king and queen against a bare king: the material is the whole story
    // apart from the endgame mop-up, which the balanced opening does not get.
    Rules queen;
    QVERIFY(queen.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/K5Q1 w - - 0 1")));
    const HeuristicEval::EvalBreakdown queenBreakdown =
        HeuristicEval::evaluateBreakdown(queen);
    QCOMPARE(queenBreakdown.material, 900);
    QVERIFY(queenBreakdown.mopUp > 0);

    Rules opening;
    const HeuristicEval::EvalBreakdown openingBreakdown =
        HeuristicEval::evaluateBreakdown(opening);
    QCOMPARE(openingBreakdown.material, 0);
    QCOMPARE(openingBreakdown.mopUp, 0);
    QCOMPARE(openingBreakdown.pawns, 0);

    // Same two pawns, but side by side they are not isolated any more, which
    // is visible in the pawn term alone.
    Rules isolated;
    QVERIFY(isolated.loadFen(QStringLiteral("4k3/8/8/8/8/8/P6P/4K3 w - - 0 1")));
    Rules neighbouring;
    QVERIFY(neighbouring.loadFen(
        QStringLiteral("4k3/8/8/8/8/8/PP6/4K3 w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateBreakdown(isolated).pawns <
            HeuristicEval::evaluateBreakdown(neighbouring).pawns);

    // A hung piece shows up in the threat term.
    Rules hanging;
    QVERIFY(hanging.loadFen(
        QStringLiteral("4k3/7p/8/4p3/3N4/8/P7/4K3 w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateBreakdown(hanging).threats < 0);

    // A drawn position reports only a zero total.
    Rules drawn;
    QVERIFY(drawn.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));
    QCOMPARE(HeuristicEval::evaluateBreakdown(drawn).total, 0);
    QCOMPARE(HeuristicEval::evaluateBreakdown(drawn).material, 0);
}

void HeuristicEvalTest::testSearchReportsBestMoveAndVariation() {
    // The opening report carries a legal move and the depth that was asked for.
    Rules opening;
    const auto openingResult = HeuristicEval::search(opening, 2, 2);
    QVERIFY(!openingResult.aborted);
    QCOMPARE(openingResult.depth, 2);
    QVERIFY(openingResult.nodes > 0);
    QVERIFY(openingResult.bestMove.has_value());
    QVERIFY(opening.isValidMove(*openingResult.bestMove));
    QVERIFY(!openingResult.principalVariation.empty());
    QCOMPARE(openingResult.principalVariation.front(), *openingResult.bestMove);

    // The mate in one is reported with its move.
    Rules mateInOne;
    QVERIFY(mateInOne.loadFen(QStringLiteral(
        "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4")));
    const auto mateResult = HeuristicEval::search(mateInOne, 2, 2);
    QVERIFY(mateResult.mateIn.has_value());
    QCOMPARE(*mateResult.mateIn, 1);
    QVERIFY(mateResult.bestMove.has_value());
    const Rules::Move expected{{3, 7}, {1, 5}, Rules::PieceType::None};
    QCOMPARE(*mateResult.bestMove, expected);
    QCOMPARE(mateResult.principalVariation.front(), expected);

    // A deeper search reads the rest of the line back from the table, and every
    // move of it is legal in the position it is played from.
    Rules middlegame;
    QVERIFY(middlegame.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));
    const auto deepResult = HeuristicEval::search(middlegame, 4, 2);
    QVERIFY(!deepResult.aborted);
    QCOMPARE(deepResult.depth, 4);
    QVERIFY(deepResult.bestMove.has_value());
    QVERIFY(deepResult.principalVariation.size() >= 2);

    Rules replay = middlegame.detachedCopy();
    for (const Rules::Move &move : deepResult.principalVariation) {
        QVERIFY2(replay.isValidMove(move), qPrintable(Rules::toUci(move)));
        Rules::Undo undo;
        QVERIFY(replay.makeMove(move, undo));
    }
}

void HeuristicEvalTest::testSearchHonoursNodeLimit() {
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));

    HeuristicEval::setSearchThreads(1);
    HeuristicEval::SearchLimits limits;
    limits.maxNodes = 20000;
    const auto limited = HeuristicEval::search(rules, 6, 2, limits);
    HeuristicEval::setSearchThreads(0);

    QVERIFY(limited.aborted);
    QVERIFY(limited.depth < 6);
    QVERIFY(limited.depth >= 1);
    QVERIFY(limited.nodes <= 20000 + 1024);
    QVERIFY(limited.bestMove.has_value());

    // The same search without a budget runs to the end.
    const auto full = HeuristicEval::search(rules, 4, 2);
    QVERIFY(!full.aborted);
    QCOMPARE(full.depth, 4);
}

void HeuristicEvalTest::testSearchHonoursCancellationAndDeadline() {
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));

    // A cancellation that is already pending stops the search before it does
    // any work at all.
    int polls = 0;
    HeuristicEval::SearchLimits cancelled;
    cancelled.shouldStop = [&polls] {
        ++polls;
        return true;
    };
    const auto cancelledResult = HeuristicEval::search(rules, 6, 2, cancelled);
    // Every root worker asks once, and the first answer stops them all.
    QVERIFY(polls >= 1);
    QVERIFY(cancelledResult.aborted);
    QCOMPARE(cancelledResult.depth, 0);
    QVERIFY(!cancelledResult.bestMove.has_value());

    // A deadline of a millisecond cannot let a depth eight search finish.
    HeuristicEval::SearchLimits deadline;
    deadline.maxMilliseconds = 1;
    const auto timedOut = HeuristicEval::search(rules, 8, 2, deadline);
    QVERIFY(timedOut.aborted);
    QVERIFY(timedOut.depth < 8);
}

void HeuristicEvalTest::testEvaluationCacheIsTransparent() {
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral(
        "r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9")));

    HeuristicEval::setSearchThreads(1);
    HeuristicEval::clearSearchCache();
    const int coldSearch = HeuristicEval::search(rules, 4, 2).centipawns;
    const int coldStatic = HeuristicEval::evaluateCentipawns(rules);

    // The cache is filled by the search above; a second search and the static
    // evaluation must not notice it.
    const int warmSearch = HeuristicEval::search(rules, 4, 2).centipawns;
    const int warmStatic = HeuristicEval::evaluateCentipawns(rules);

    QCOMPARE(warmSearch, coldSearch);
    QCOMPARE(warmStatic, coldStatic);

    // Dropping it must not change anything either. The cap has to stay at one
    // worker until the end: a parallel search shares alpha between workers and
    // may split a different value out of the same position, which is not what
    // this test is about.
    HeuristicEval::clearSearchCache();
    QCOMPARE(HeuristicEval::search(rules, 4, 2).centipawns, coldSearch);
    HeuristicEval::setSearchThreads(0);
}

void HeuristicEvalTest::testEvaluationCacheFollowsTheParams() {
    Rules rules;
    QVERIFY(rules.loadFen(QStringLiteral("4k3/8/8/8/8/8/P7/4K3 w - - 0 1")));

    HeuristicEval::setSearchThreads(1);
    HeuristicEval::resetParams();
    // The search fills the evaluation cache with the default weights.
    const int before = HeuristicEval::search(rules, 3, 2).centipawns;

    HeuristicEval::EvalParams tuned = HeuristicEval::params();
    tuned.pawnValue = 2 * tuned.pawnValue;
    HeuristicEval::setParams(tuned);
    const int tunedScore = HeuristicEval::search(rules, 3, 2).centipawns;

    HeuristicEval::resetParams();
    const int restored = HeuristicEval::search(rules, 3, 2).centipawns;
    HeuristicEval::setSearchThreads(0);

    // A stale cached score would have kept the old value for both.
    QVERIFY2(tunedScore > before,
             qPrintable(QStringLiteral("%1 vs %2").arg(tunedScore).arg(before)));
    QCOMPARE(restored, before);
}

void HeuristicEvalTest::testReferenceScores() {
    // The exact score of a spread of positions, so that a refactor of the
    // evaluation cannot change it silently. The values were taken from the
    // implementation and have to be updated on purpose when a weight or a term
    // really changes.
    struct Reference {
        const char *fen;
        int centipawns;
    };
    const Reference references[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 10},
        {"r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5", 10},
        {"r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9", -32},
        {"rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
         -HeuristicEval::MateScore},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", -42},
        {"4k3/8/8/8/8/8/P7/4K3 w - - 0 1", 154},
        {"4k3/8/8/8/4P3/8/8/4K3 w - - 0 1", 200},
        {"2b4k/P7/8/8/8/8/8/K1B5 w - - 0 1", 142},
        {"4k3/p6p/8/3N4/4P3/8/8/4K3 w - - 0 1", 292},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 138},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1", -62},
        {"5rk1/1pp2ppp/p1nb4/3p4/2PP4/2N1P3/PP3PPP/2KR3R w - - 0 1", 288},
    };

    for (const Reference &reference : references) {
        Rules rules;
        QVERIFY2(rules.loadFen(QString::fromLatin1(reference.fen)),
                 reference.fen);
        QCOMPARE(HeuristicEval::evaluateCentipawns(rules), reference.centipawns);
    }
}

void HeuristicEvalTest::testSearchReferenceSignatures() {
    // Cold, single-threaded signatures of a spread of positions, so that a change
    // to the search cannot move a score, a root move or a node count without the
    // suite saying so. Each search starts from an empty transposition table and
    // with one worker, which makes it a function of the position, the depth and
    // the code alone; update a value only when the change is intended.
    struct Reference {
        const char *fen;
        int depth;
        int centipawns;
        const char *best; // "-" for a position the search refuses to enter
        int nodes;
        int mateIn; // 0 when the position is not a mate
    };
    const Reference references[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 10, "d2d4", 1946, 0},
        {"r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5", 4, 10, "e1g1",
         5819, 0},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 84, "e2a6",
         8341, 0},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 510, "d7c8q", 1527, 0},
        {"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 2,
         HeuristicEval::MateScore, "h5f7", 85, 1},
        {"6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1", 2, HeuristicEval::MateScore, "e1e8", 67, 1},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 112, "b4f4", 1724, 0},
        {"4k3/8/8/8/8/8/P7/4K3 w - - 0 1", 5, 163, "e1d2", 685, 0},
        {"r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R b KQ - 6 9", 4, -98, "c6d4",
         4472, 0},
        {"8/8/8/4k3/8/8/3Q4/4K3 w - - 0 1", 5, 982, "e1e2", 13912, 0},
        {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4", 4, 63, "b1c3",
         5500, 0},
        {"8/8/8/4k3/8/8/4B3/4K3 w - - 0 1", 3, 0, "-", 0, 0},
        {"4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", 4, 179, "e4d5", 156, 0},
        {"r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1", 4, 0, "d5c7", 368, 0},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", 3, 958, "a7a8q", 173, 0},
        {"4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1", 3, 990, "e1e2", 280, 0},
    };

    HeuristicEval::resetParams();
    HeuristicEval::setSearchThreads(1);
    for (const Reference &reference : references) {
        Rules rules;
        QVERIFY2(rules.loadFen(QString::fromLatin1(reference.fen)), reference.fen);
        HeuristicEval::clearSearchCache();
        const auto result = HeuristicEval::search(rules, reference.depth);
        QVERIFY2(!result.aborted, reference.fen);
        QCOMPARE(result.nodes, reference.nodes);
        QCOMPARE(result.centipawns, reference.centipawns);
        QCOMPARE(result.mateIn.value_or(0), reference.mateIn);
        if (QLatin1String(reference.best) == QLatin1String("-")) {
            // The position is a draw: the search bottoms out before a single
            // node, without a depth, a line or a move.
            QVERIFY2(!result.bestMove.has_value(), reference.fen);
            QVERIFY(result.principalVariation.empty());
            QCOMPARE(result.depth, 0);
            continue;
        }
        QCOMPARE(result.depth, reference.depth);
        QVERIFY2(result.bestMove.has_value(), reference.fen);
        QCOMPARE(Rules::toUci(*result.bestMove), QString::fromLatin1(reference.best));
        QVERIFY(!result.principalVariation.empty());
        QCOMPARE(result.principalVariation.front(), *result.bestMove);
        // The rest of the line is read back from the table and has to be playable.
        Rules replay = rules.detachedCopy();
        for (const Rules::Move &move : result.principalVariation) {
            QVERIFY2(replay.isValidMove(move), qPrintable(Rules::toUci(move)));
            Rules::Undo undo;
            QVERIFY(replay.makeMove(move, undo));
        }
    }
    HeuristicEval::setSearchThreads(0);
}

void HeuristicEvalTest::testSearchMatchesTheMateOracle() {
    // Every position is a forced mate whose exact distance is established by the
    // independent minimax in `forcedMateIn()`, which shares none of the
    // evaluator's code, so this pins the mate reporting instead of trusting it.
    // One ply past the mate is enough for the search to see it. Each case runs
    // again on its mirrored position, which turns White's mate into Black's and
    // checks that the search is colour-symmetric.
    struct Reference {
        const char *fen;
        int mateIn; // always positive: the sign follows the side to move
    };
    const Reference references[] = {
        {"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 1},
        {"6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1", 1},
        {"7k/6pp/8/8/8/8/5PPP/4R1K1 w - - 0 1", 1},
        {"6rk/6pp/8/6N1/8/8/8/6K1 w - - 0 1", 1},
        {"1k6/8/1K6/8/8/8/8/6R1 w - - 0 1", 1},
        {"7k/8/6K1/8/8/8/8/3Q4 w - - 0 1", 1},
        {"7k/8/5K2/8/8/8/8/1Q6 w - - 0 1", 2},
        {"6k1/8/5K2/8/8/8/8/1Q6 w - - 0 1", 2},
    };

    HeuristicEval::resetParams();
    HeuristicEval::setSearchThreads(1);
    for (const Reference &reference : references) {
        for (const bool mirror : {false, true}) {
            const QString fen = mirror ? mirrorFen(QString::fromLatin1(reference.fen))
                                       : QString::fromLatin1(reference.fen);
            Rules rules;
            QVERIFY2(rules.loadFen(fen), qPrintable(fen));
            const int plies = 2 * reference.mateIn - 1;
            // The oracle says the mate is exactly as long as the table claims.
            QVERIFY2(forcedMateIn(rules, plies), qPrintable(fen));
            QVERIFY2(!forcedMateIn(rules, plies - 2), qPrintable(fen));

            const int side = rules.currentPlayer() == Rules::Color::White ? 1 : -1;
            HeuristicEval::clearSearchCache();
            const auto result = HeuristicEval::search(rules, plies + 1);
            QVERIFY2(result.mateIn.has_value(), qPrintable(fen));
            QCOMPARE(*result.mateIn, side * reference.mateIn);
            QCOMPARE(result.centipawns, side * HeuristicEval::MateScore);
            QVERIFY(result.bestMove.has_value());
            // The move it played really starts the mate, and the line it reports
            // ends on the mate itself.
            QVERIFY2(forcedMateIn(rules, plies, &*result.bestMove), qPrintable(fen));
            QCOMPARE(result.principalVariation.size(), static_cast<size_t>(plies));
        }
    }
    HeuristicEval::setSearchThreads(0);
}

void HeuristicEvalTest::testSearchWinsTheFreeMaterial() {
    // Tactics whose answer is unambiguous, with no mate involved: a pawn takes a
    // hanging queen or knight, a knight forks king and rook, a pawn promotes and
    // a king takes the queen that checks it. Each case also runs on its mirrored
    // position, so both colours are covered.
    struct Reference {
        const char *fen;
        int depth;
        const char *best;
    };
    const Reference references[] = {
        {"4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", 3, "e4d5"},
        {"4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1", 3, "e4d5"},
        {"r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1", 4, "d5c7"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", 3, "a7a8q"},
        {"4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1", 3, "e1e2"},
        {"4k3/8/8/8/8/8/1p6/4K3 b - - 0 1", 3, "b2b1q"},
    };

    HeuristicEval::resetParams();
    HeuristicEval::setSearchThreads(1);
    for (const Reference &reference : references) {
        for (const bool mirror : {false, true}) {
            const QString fen = mirror ? mirrorFen(QString::fromLatin1(reference.fen))
                                       : QString::fromLatin1(reference.fen);
            Rules rules;
            QVERIFY2(rules.loadFen(fen), qPrintable(fen));
            HeuristicEval::clearSearchCache();
            const auto result = HeuristicEval::search(rules, reference.depth);
            QVERIFY2(result.bestMove.has_value(), qPrintable(fen));
            const QString expected = mirror ? mirrorUci(QString::fromLatin1(reference.best))
                                            : QString::fromLatin1(reference.best);
            QCOMPARE(Rules::toUci(*result.bestMove), expected);
        }
    }
    HeuristicEval::setSearchThreads(0);
}

void HeuristicEvalTest::testSearchDefendsAgainstMateInOne() {
    // A threat the search has to answer rather than a move it has to find: White
    // threatens Qxf7#, so whatever Black plays must leave White without a mate in
    // one, which the oracle confirms on the position the search actually reached.
    // The mirrored position tests White answering the same threat.
    HeuristicEval::resetParams();
    HeuristicEval::setSearchThreads(1);
    const QString threats[] = {
        QStringLiteral("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 4 4"),
        mirrorFen(QStringLiteral(
            "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 4 4")),
    };

    for (const QString &fen : threats) {
        Rules rules;
        QVERIFY2(rules.loadFen(fen), qPrintable(fen));
        // The side to move is not the one that mates, so the threat is real only
        // after its move.
        QVERIFY(!forcedMateIn(rules, 1));
        const auto result = HeuristicEval::search(rules, 3);
        QVERIFY2(result.bestMove.has_value(), qPrintable(fen));
        // A mate scored against the side to move would mean it walked into the
        // threat instead of answering it.
        const int side = rules.currentPlayer() == Rules::Color::White ? 1 : -1;
        QVERIFY2(result.centipawns != -side * HeuristicEval::MateScore, qPrintable(fen));
        Rules::Undo undo;
        QVERIFY(rules.makeMove(*result.bestMove, undo));
        QVERIFY2(!forcedMateIn(rules, 1), qPrintable(Rules::toUci(*result.bestMove)));
        rules.unmakeMove(*result.bestMove, undo);
    }
    HeuristicEval::setSearchThreads(0);
}

QTEST_MAIN(HeuristicEvalTest)
#include "heuristiceval_test.moc"
