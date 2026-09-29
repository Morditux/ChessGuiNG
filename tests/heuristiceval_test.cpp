//
// Unit tests for the classical heuristic evaluator.
//

#include <QTest>

#include <algorithm>
#include <limits>

#include "evalparamfields.h"
#include "heuristiceval.h"
#include "heuristicsearch.h"
#include "kpkbitbase.h"
#include "rules.h"
#include "uciparser.h"
#include "winprobability.h"

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

// Material of a position as a sorted bag of piece letters, so a positional pair
// can be checked to differ by one feature and not by material.
QString materialOf(const QString &fen) {
    QString pieces;
    for (const QChar &ch : fen.section(QLatin1Char(' '), 0, 0)) {
        if (ch.isLetter()) {
            pieces.append(ch);
        }
    }
    std::sort(pieces.begin(), pieces.end());
    return pieces;
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

// Mirrors a position left to right: the files are reversed, which is a symmetry
// of the game when no castling right is left.
QString mirrorFilesFen(const QString &fen) {
    QStringList parts = fen.split(QLatin1Char(' '));
    QStringList ranks = parts[0].split(QLatin1Char('/'));
    for (QString &rank : ranks) {
        std::reverse(rank.begin(), rank.end());
    }
    parts[0] = ranks.join(QLatin1Char('/'));
    return parts.join(QLatin1Char(' '));
}

// The pawn-structure term of a position with every pawn-structure weight zeroed
// but one, which is set to `value` in both phases: the result is `value` times
// what White holds of that feature minus what Black holds, so a test can pin one
// definition without the others.
int pawnTermScore(const QString &fen,
                  const std::vector<std::pair<int HeuristicEval::EvalParams::*, int>> &weights) {
    HeuristicEval::EvalParams params = HeuristicEval::params();
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        if (QLatin1String(field.group) == QLatin1String("Pawn structure")) {
            params.*field.member = 0;
        }
    }
    for (const auto &weight : weights) {
        params.*weight.first = weight.second;
    }
    HeuristicEval::setParams(params);

    Rules rules;
    const bool loaded = rules.loadFen(fen);
    const int pawns = loaded ? HeuristicEval::evaluateBreakdown(rules).pawns
                             : -99999;
    HeuristicEval::resetParams();
    return pawns;
}

int pawnTermScore(const QString &fen,
                  int HeuristicEval::EvalParams::*middleGame,
                  int HeuristicEval::EvalParams::*endGame, int value = 40) {
    return pawnTermScore(fen, {{middleGame, value}, {endGame, value}});
}

// The king-safety term of a position under modified weights, restored afterwards.
int kingSafetyWith(
    const QString &fen,
    const std::vector<std::pair<int HeuristicEval::EvalParams::*, int>> &weights) {
    HeuristicEval::EvalParams params = HeuristicEval::params();
    for (const auto &weight : weights) {
        params.*weight.first = weight.second;
    }
    HeuristicEval::setParams(params);
    Rules rules;
    const bool loaded = rules.loadFen(fen);
    const int safety = loaded ? HeuristicEval::evaluateBreakdown(rules).kingSafety
                              : -99999;
    HeuristicEval::resetParams();
    return safety;
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
    void testPositionalKnowledge();
    void testOnlyLeadingDoubledPawnIsPassed();
    void testKingMobilityRespectsEnemyAttacks();
    void testControlledPassedPawnPenalty();
    void testKingShelterPrefersThePawnInFront();
    void testKingAttackWeightedByAttacker();
    void testCentralSpaceCountsAdvancedPawns();
    void testSeeValueRayCasting();
    void testMoveOrderingEnhancements();
    void testTranspositionTablePreservesMoveOnFailLow();
    void testTornTranspositionEntryIsRejected();
    void testBackwardPawnDefinition();
    void testCandidatePawnDefinition();
    void testPassedPawnBonusGrowsWithRank();
    void testPassedPawnKingsAndRoad();
    void testPhalanxPawns();
    void testPawnCacheIsTransparent();
    void testKingDangerGrowsFasterThanTheAttackers();
    void testKingDangerFallsWithoutAQueen();
    void testSafeChecksAddToTheKingDanger();
    void testOpenFilesNearTheKing();
    void testKingSafetyInTheCentreAndUnderStorm();
    void testEndgameScalingFlattensUnwinnableAdvantages();
    void testRookEndingScaleFollowsThePawnFlanks();
    void testUnstoppablePassedPawn();
    void testKingAndPawnAgainstKingIsSolved();
    void testBishopAndKnightMateAimsForTheRightCorner();
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
    // The gauge, the curve and the accuracy of the game report all read the one
    // model in winprobability.h, whose calibration puts a pawn at about 59% and
    // four pawns at about 81%; the numbers below are that curve, so changing it
    // is a decision and not an accident.
    QCOMPARE(HeuristicEval::centipawnsToPercentage(0.0), 50.0);
    QCOMPARE(HeuristicEval::centipawnsToPercentage(10000.0), 100.0);
    QCOMPARE(HeuristicEval::centipawnsToPercentage(-10000.0), 0.0);

    // Nothing but this model: a second curve would drift away from the report.
    for (const double cp : {0.0, 150.0, -150.0, 900.0}) {
        QCOMPARE(HeuristicEval::centipawnsToPercentage(cp),
                 WinProbability::fromCentipawns(cp));
    }

    const double plus100 = HeuristicEval::centipawnsToPercentage(100.0);
    QVERIFY(plus100 > 58.9 && plus100 < 59.3);

    const double plus400 = HeuristicEval::centipawnsToPercentage(400.0);
    QVERIFY(plus400 > 81.1 && plus400 < 81.6);

    const double minus400 = HeuristicEval::centipawnsToPercentage(-400.0);
    QVERIFY(minus400 > 18.4 && minus400 < 18.9);

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

    // A knight each keeps the position out of the exactly solved king-and-pawn
    // endings, so the pawn is judged by the evaluation terms.
    QVERIFY(rules.loadFen(QStringLiteral("4k1n1/8/8/8/4P3/8/8/4K1N1 w - - 0 1")));
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
        // A rook against a pawn; a bishop against a pawn is a draw (the
        // bishop wins the pawn and cannot mate), which the evaluator now says.
        {"4k2r/8/8/8/8/8/P7/4K3 b - - 0 1", false},
        {"2b1k3/8/8/8/8/8/8/2B1KB2 w - - 0 1", true},
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
        QStringLiteral("1n2k3/4P3/8/8/8/8/8/1N2K3 w - - 0 1")));
    Rules sixth;
    QVERIFY(sixth.loadFen(
        QStringLiteral("1n2k3/8/4P3/8/8/8/8/1N2K3 w - - 0 1")));

    QVERIFY(HeuristicEval::evaluateCentipawns(seventh) >
            HeuristicEval::evaluateCentipawns(sixth));
}

void HeuristicEvalTest::testPassedPawnBlocked() {
    // The black knight blocks the e-pawn on e6; the d-pawn is unobstructed. A
    // white rook keeps the position from being a plain knight-against-pawn
    // draw, where the advantage would be scaled away.
    Rules blocked;
    QVERIFY(blocked.loadFen(
        QStringLiteral("4k3/8/4n3/4P3/8/8/8/R3K3 w - - 0 1")));
    Rules open;
    QVERIFY(open.loadFen(
        QStringLiteral("4k3/8/4n3/3P4/8/8/8/R3K3 w - - 0 1")));

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
        QStringLiteral("1r2k3/8/8/3q4/4P3/8/8/R3K3 w - - 0 1")));

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
    QVERIFY(rules.loadFen(QStringLiteral("4k1n1/8/8/8/8/8/P7/4K1N1 w - - 0 1")));

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
    QVERIFY(rules.loadFen(QStringLiteral("4k1n1/8/8/8/8/8/P7/4K1N1 w - - 0 1")));

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
        {"r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R w KQ - 6 9", -38},
        {"rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", -HeuristicEval::MateScore},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", -55},
        {"4k3/8/8/8/8/8/P7/4K3 w - - 0 1", 0},
        {"4k3/8/8/8/4P3/8/8/4K3 w - - 0 1", 0},
        {"2b4k/P7/8/8/8/8/8/K1B5 w - - 0 1", 175},
        {"4k3/p6p/8/3N4/4P3/8/8/4K3 w - - 0 1", 232},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 166},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 0 1", -74},
        {"5rk1/1pp2ppp/p1nb4/3p4/2PP4/2N1P3/PP3PPP/2KR3R w - - 0 1", 299},
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
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 10, "d2d4", 1707, 0},
        {"r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5", 4, 10, "e1g1",
         4750, 0},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 107, "e2a6",
         9712, 0},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 456, "d7c8q", 1539, 0},
        {"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 2, HeuristicEval::MateScore, "h5f7",
         85, 1},
        {"6k1/5ppp/8/8/8/8/8/4R1K1 w - - 0 1", 2, HeuristicEval::MateScore, "e1e8", 69, 1},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 39, "b4f4", 2062, 0},
        {"4k3/8/8/8/8/8/P7/4K3 w - - 0 1", 5, 0, "a2a3", 509, 0},
        {"r2q1rk1/ppp2ppp/2np1n2/2b1p3/2B1P1b1/2NP1N2/PPPBQPPP/R3K2R b KQ - 6 9", 4, -123, "c6d4",
         4053, 0},
        {"8/8/8/4k3/8/8/3Q4/4K3 w - - 0 1", 5, 1108, "e1e2", 20630, 0},
        {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4", 4, 59, "b1c3",
         3362, 0},
        {"8/8/8/4k3/8/8/4B3/4K3 w - - 0 1", 3, 0, "-", 0, 0},
        {"4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", 4, 0, "e4d5", 119, 0},
        {"r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1", 4, 0, "d5c7", 379, 0},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", 3, 1085, "a7a8q", 169, 0},
        {"4k3/8/8/8/8/8/4q3/3QK3 w - - 0 1", 3, 1109, "e1e2", 195, 0},
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

void HeuristicEvalTest::testPositionalKnowledge() {
    // Pairs of positions that differ by one positional feature and carry the
    // same material with the same side to move, so the static evaluation has to
    // rank them by that feature alone. Every direction below was checked against
    // an external engine (Stockfish dev, depth 18), which agreed with a clear
    // margin; the assertion only asks that the evaluator keeps the same order
    // with room to spare, because a static score is not a search score. Each
    // pair is also run mirrored, which puts the good side behind the black
    // pieces and catches a colour asymmetry.
    struct Pair {
        const char *feature;
        const char *better;
        const char *worse;
    };
    const Pair pairs[] = {
        {"a rook on an open file",
         "4k3/pp3ppp/8/8/8/8/PP3PPP/3RK3 w - - 0 1",
         "4k3/pp3ppp/8/8/8/8/PP3PPP/4KR2 w - - 0 1"},
        {"connected central pawns",
         "4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1",
         "4k3/8/8/8/3P3P/8/8/4K3 w - - 0 1"},
        {"an advanced passed pawn",
         "r4rk1/pp3ppp/3P4/8/8/8/PP3PPP/R4RK1 w - - 0 1",
         "r4rk1/pp3ppp/8/8/3P4/8/PP3PPP/R4RK1 w - - 0 1"},
        {"the own king beside a passed pawn and the enemy king far away",
         "1r6/3K1pp1/8/3P4/8/8/3k1PP1/1R6 w - - 0 1",
         "1r6/5pp1/8/2kP4/8/8/5PP1/1R3K2 w - - 0 1"},
        {"the own king beside a passed pawn (second position)",
         "1r6/5pp1/2KP4/8/8/8/4kPP1/1R6 w - - 0 1",
         "1r6/5pp1/3Pk3/8/8/8/5PP1/1RK5 w - - 0 1"},
        {"an attack on the castled king (queen, knight and bishop)",
         "r1b2rk1/ppp2ppp/2nq4/6N1/2B5/8/PPP2PPP/R2Q1RK1 w - - 0 1",
         "r1b2rk1/ppp2ppp/2nq4/8/8/8/PPP2PPP/RNBQ1RK1 w - - 0 1"},
        {"an attack on the castled king (second position)",
         "r1b2rk1/ppp2ppp/2nq4/4N3/2B4Q/8/PPP2PPP/R4RK1 w - - 0 1",
         "r1b2rk1/ppp2ppp/2nq4/8/8/8/PPPNQPPP/R1B2RK1 w - - 0 1"},
        {"an attack on the castled king (bishop and knight)",
         "r1b2rk1/ppp2ppp/2nq4/6N1/8/1B6/PPP2PPP/R2Q1RK1 w - - 0 1",
         "r1b2rk1/ppp2ppp/2nq4/8/8/8/PPPB1PPP/RN1Q1RK1 w - - 0 1"},
        {"a pawn phalanx",
         "r3k3/pp3ppp/8/8/3PP3/8/PP3PPP/R3K3 w - - 0 1",
         "r3k3/pp3ppp/8/8/3P1P2/8/PP3PPP/R3K3 w - - 0 1"},
        {"an active knight",
         "4k3/pp3ppp/8/8/4N3/8/PP3PPP/4K3 w - - 0 1",
         "4k3/pp3ppp/8/8/8/8/PP3PPP/N3K3 w - - 0 1"},
    };

    for (const Pair &pair : pairs) {
        const QString feature = QString::fromLatin1(pair.feature);
        const QString betterFen = QString::fromLatin1(pair.better);
        const QString worseFen = QString::fromLatin1(pair.worse);
        QVERIFY2(materialOf(betterFen) == materialOf(worseFen), qPrintable(feature));

        Rules better;
        Rules worse;
        QVERIFY2(better.loadFen(betterFen), qPrintable(betterFen));
        QVERIFY2(worse.loadFen(worseFen), qPrintable(worseFen));
        const int direct = HeuristicEval::evaluateCentipawns(better) -
                           HeuristicEval::evaluateCentipawns(worse);

        Rules mirroredBetter;
        Rules mirroredWorse;
        QVERIFY2(mirroredBetter.loadFen(mirrorFen(betterFen)), qPrintable(betterFen));
        QVERIFY2(mirroredWorse.loadFen(mirrorFen(worseFen)), qPrintable(worseFen));
        // The good side plays Black in the mirrored pair, so the sign flips.
        const int mirrored = HeuristicEval::evaluateCentipawns(mirroredWorse) -
                             HeuristicEval::evaluateCentipawns(mirroredBetter);

        QVERIFY2(direct >= 20, qPrintable(QStringLiteral("%1: %2")
                                              .arg(feature)
                                              .arg(direct)));
        QVERIFY2(mirrored >= 20, qPrintable(QStringLiteral("%1 mirrored: %2")
                                                .arg(feature)
                                                .arg(mirrored)));
    }
}

void HeuristicEvalTest::testOnlyLeadingDoubledPawnIsPassed() {
    // A doubled pawn pair holds one passed pawn: the leading one. The rear pawn
    // is blocked by its own piece, so adding it under the leader must lower the
    // pawn term (doubled and isolated) rather than collect a passed bonus.
    HeuristicEval::resetParams();
    Rules leading;
    QVERIFY(leading.loadFen(QStringLiteral("1n2k3/P7/8/8/8/8/8/1N2K3 w - - 0 1")));
    Rules doubled;
    QVERIFY(doubled.loadFen(QStringLiteral("1n2k3/P7/8/P7/8/8/8/1N2K3 w - - 0 1")));

    const int leaderPawns = HeuristicEval::evaluateBreakdown(leading).pawns;
    const int doubledPawns = HeuristicEval::evaluateBreakdown(doubled).pawns;
    QVERIFY2(doubledPawns < leaderPawns,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(doubledPawns)
                            .arg(leaderPawns)));
}

void HeuristicEvalTest::testKingMobilityRespectsEnemyAttacks() {
    // The black rook on a5 attacks e5, one of the white king's escape squares,
    // which is therefore not a real move. On a1 the same rook leaves the king's
    // mobility untouched (both rooks have the same mobility themselves).
    HeuristicEval::resetParams();
    Rules attacked;
    QVERIFY(attacked.loadFen(QStringLiteral("4k3/8/8/r7/4K3/8/8/8 w - - 0 1")));
    Rules free;
    QVERIFY(free.loadFen(QStringLiteral("4k3/8/8/8/4K3/8/8/r7 w - - 0 1")));

    const int attackedMobility =
        HeuristicEval::evaluateBreakdown(attacked).mobility;
    const int freeMobility = HeuristicEval::evaluateBreakdown(free).mobility;
    QVERIFY2(attackedMobility < freeMobility,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(attackedMobility)
                            .arg(freeMobility)));

    // Mirroring hands the rook to White and the king to Black, so the sign of
    // the mobility gap has to flip.
    Rules mirroredAttacked;
    Rules mirroredFree;
    QVERIFY(mirroredAttacked.loadFen(mirrorFen(
        QStringLiteral("4k3/8/8/r7/4K3/8/8/8 w - - 0 1"))));
    QVERIFY(mirroredFree.loadFen(mirrorFen(
        QStringLiteral("4k3/8/8/8/4K3/8/8/r7 w - - 0 1"))));
    QVERIFY(HeuristicEval::evaluateBreakdown(mirroredAttacked).mobility >
            HeuristicEval::evaluateBreakdown(mirroredFree).mobility);
}

void HeuristicEvalTest::testControlledPassedPawnPenalty() {
    // The white e6 pawn is passed in both positions, but only in the first does
    // the black rook control e7, the square the pawn must step onto. An empty
    // but controlled advance square has to cost the pawn term something.
    HeuristicEval::resetParams();
    Rules controlled;
    QVERIFY(controlled.loadFen(
        QStringLiteral("1k2r3/8/4P3/8/8/8/8/4K3 w - - 0 1")));
    Rules free;
    QVERIFY(free.loadFen(
        QStringLiteral("rk6/8/4P3/8/8/8/8/4K3 w - - 0 1")));

    const int controlledPawns =
        HeuristicEval::evaluateBreakdown(controlled).pawns;
    const int freePawns = HeuristicEval::evaluateBreakdown(free).pawns;
    QVERIFY2(controlledPawns < freePawns,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(controlledPawns)
                            .arg(freePawns)));

    // Mirrored, Black owns the passed pawn, so the penalty appears with the
    // opposite sign.
    Rules mirroredControlled;
    Rules mirroredFree;
    QVERIFY(mirroredControlled.loadFen(mirrorFen(
        QStringLiteral("1k2r3/8/4P3/8/8/8/8/4K3 w - - 0 1"))));
    QVERIFY(mirroredFree.loadFen(mirrorFen(
        QStringLiteral("rk6/8/4P3/8/8/8/8/4K3 w - - 0 1"))));
    QVERIFY(HeuristicEval::evaluateBreakdown(mirroredControlled).pawns >
            HeuristicEval::evaluateBreakdown(mirroredFree).pawns);
}

void HeuristicEvalTest::testKingShelterPrefersThePawnInFront() {
    // Same full material and the same castled king; only the surviving seventh
    // white pawn moves between g2 (directly in front of the king) and f2 (on an
    // adjacent file). The phase is maximal, so the middlegame shelter, the half
    // this preference applies to, is what the comparison sees.
    HeuristicEval::resetParams();
    Rules inFront;
    QVERIFY(inFront.loadFen(
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPP1PP/RNBQ1RK1 w kq - 0 1")));
    Rules beside;
    QVERIFY(beside.loadFen(
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPP1P/RNBQ1RK1 w kq - 0 1")));

    const int inFrontSafety =
        HeuristicEval::evaluateBreakdown(inFront).kingSafety;
    const int besideSafety =
        HeuristicEval::evaluateBreakdown(beside).kingSafety;
    QVERIFY2(inFrontSafety > besideSafety,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(inFrontSafety)
                            .arg(besideSafety)));

    // Mirrored, the shelter belongs to Black, so the sign flips.
    Rules mirroredInFront;
    Rules mirroredBeside;
    QVERIFY(mirroredInFront.loadFen(mirrorFen(
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPP1PP/RNBQ1RK1 w kq - 0 1"))));
    QVERIFY(mirroredBeside.loadFen(mirrorFen(
        QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPP1P/RNBQ1RK1 w kq - 0 1"))));
    QVERIFY(HeuristicEval::evaluateBreakdown(mirroredInFront).kingSafety <
            HeuristicEval::evaluateBreakdown(mirroredBeside).kingSafety);
}

void HeuristicEvalTest::testKingAttackWeightedByAttacker() {
    // Both positions attack exactly one square of the white king's
    // neighbourhood, g2 and f2 respectively, and both carry the same phase (a
    // knight is one, a queen four, so the knight position pads with three
    // harmless knights). The queen attack has to cost the king safety more than
    // the knight attack.
    HeuristicEval::resetParams();
    Rules knight;
    QVERIFY(knight.loadFen(
        QStringLiteral("7k/8/8/nnn5/5n2/8/5PPP/6K1 w - - 0 1")));
    Rules queen;
    QVERIFY(queen.loadFen(
        QStringLiteral("7k/8/8/8/3q4/8/5PPP/6K1 w - - 0 1")));

    const int knightSafety =
        HeuristicEval::evaluateBreakdown(knight).kingSafety;
    const int queenSafety =
        HeuristicEval::evaluateBreakdown(queen).kingSafety;
    QVERIFY2(queenSafety < knightSafety,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(queenSafety)
                            .arg(knightSafety)));

    // Mirrored, the attacker is White, so the king safety of the black king is
    // what suffers and the gap flips direction for White.
    Rules mirroredKnight;
    Rules mirroredQueen;
    QVERIFY(mirroredKnight.loadFen(mirrorFen(
        QStringLiteral("7k/8/8/nnn5/5n2/8/5PPP/6K1 w - - 0 1"))));
    QVERIFY(mirroredQueen.loadFen(mirrorFen(
        QStringLiteral("7k/8/8/8/3q4/8/5PPP/6K1 w - - 0 1"))));
    QVERIFY(HeuristicEval::evaluateBreakdown(mirroredQueen).kingSafety >
            HeuristicEval::evaluateBreakdown(mirroredKnight).kingSafety);
}

void HeuristicEvalTest::testCentralSpaceCountsAdvancedPawns() {
    // Same two central pawns; on d5/e5 they control four squares in the enemy
    // half, on d3/e3 none, so only the advanced pair gives space. Nothing else
    // in the position changes, so the space term is the one that reads it.
    HeuristicEval::resetParams();
    Rules advanced;
    QVERIFY(advanced.loadFen(
        QStringLiteral("4k3/8/8/3PP3/8/8/8/4K3 w - - 0 1")));
    Rules home;
    QVERIFY(home.loadFen(
        QStringLiteral("4k3/8/8/8/8/3PP3/8/4K3 w - - 0 1")));

    const int advancedSpace = HeuristicEval::evaluateBreakdown(advanced).space;
    const int homeSpace = HeuristicEval::evaluateBreakdown(home).space;
    QVERIFY2(advancedSpace > homeSpace,
             qPrintable(QStringLiteral("%1 vs %2")
                            .arg(advancedSpace)
                            .arg(homeSpace)));

    // Mirrored, the advanced pair is Black's, so the sign flips.
    Rules mirroredAdvanced;
    Rules mirroredHome;
    QVERIFY(mirroredAdvanced.loadFen(mirrorFen(
        QStringLiteral("4k3/8/8/3PP3/8/8/8/4K3 w - - 0 1"))));
    QVERIFY(mirroredHome.loadFen(mirrorFen(
        QStringLiteral("4k3/8/8/8/8/3PP3/8/4K3 w - - 0 1"))));
    QVERIFY(HeuristicEval::evaluateBreakdown(mirroredAdvanced).space <
            HeuristicEval::evaluateBreakdown(mirroredHome).space);
}

void HeuristicEvalTest::testSeeValueRayCasting() {
    HeuristicEval::resetParams();

    // 1. Undefended piece: Black queen on d5, white pawn on e4.
    Rules undefended;
    QVERIFY(undefended.loadFen(QStringLiteral("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1")));
    const auto pTakesQ = UciParser::parseMove(QStringLiteral("e4d5"));
    QVERIFY(pTakesQ.has_value());
    QCOMPARE(HeuristicSearch::seeValue(undefended, *pTakesQ),
             HeuristicEval::params().queenValue);

    // 2. Defended piece sacrifice: Queen takes d5 defended by pawn on c6.
    Rules sacrifice;
    QVERIFY(sacrifice.loadFen(QStringLiteral("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1")));
    const auto qTakesD5 = UciParser::parseMove(QStringLiteral("d1d5"));
    QVERIFY(qTakesD5.has_value());
    // White Queen (900) takes pawn (100) on d5 defended by c6 pawn. 100 - 900 = -800.
    QCOMPARE(HeuristicSearch::seeValue(sacrifice, *qTakesD5),
             HeuristicEval::params().pawnValue - HeuristicEval::params().queenValue);

    // 3. Equal exchange: White rook takes Black rook defended by Black rook.
    Rules equal;
    QVERIFY(equal.loadFen(QStringLiteral("4k3/4r3/8/8/4r3/8/8/4R1K1 w - - 0 1")));
    const auto rTakesR = UciParser::parseMove(QStringLiteral("e1e4"));
    QVERIFY(rTakesR.has_value());
    QCOMPARE(HeuristicSearch::seeValue(equal, *rTakesR), 0);

    // 4. Battery / X-ray: White rooks on e1 and e2 vs Black rooks on e6 and e7.
    Rules battery;
    QVERIFY(battery.loadFen(QStringLiteral("4k3/4r3/4r3/8/8/8/4R3/4R1K1 w - - 0 1")));
    const auto r2TakesR = UciParser::parseMove(QStringLiteral("e2e6"));
    QVERIFY(r2TakesR.has_value());
    // White plays Rxe6 (+500), Black plays Rxe6 (-500), White x-ray plays Rxe6 (+500). Net +500.
    QCOMPARE(HeuristicSearch::seeValue(battery, *r2TakesR),
             HeuristicEval::params().rookValue);
}

void HeuristicEvalTest::testMoveOrderingEnhancements() {
    HeuristicEval::resetParams();

    // 1. Capture with promotion vs quiet promotion vs underpromotion.
    // White pawn on d7, black knight on c8, black king on a8.
    Rules promo;
    QVERIFY(promo.loadFen(QStringLiteral("k1n5/3P4/8/8/8/8/8/4K3 w - - 0 1")));
    const auto capPromoQ = UciParser::parseMove(QStringLiteral("d7c8q"));
    const auto quietPromoQ = UciParser::parseMove(QStringLiteral("d7d8q"));
    const auto capPromoR = UciParser::parseMove(QStringLiteral("d7c8r"));
    QVERIFY(capPromoQ.has_value() && quietPromoQ.has_value() && capPromoR.has_value());

    const int scoreCapPromoQ = HeuristicSearch::moveOrderingScore(promo, *capPromoQ);
    const int scoreQuietPromoQ = HeuristicSearch::moveOrderingScore(promo, *quietPromoQ);
    const int scoreCapPromoR = HeuristicSearch::moveOrderingScore(promo, *capPromoR);

    // Capture + promotion must score higher than quiet promotion:
    QVERIFY2(scoreCapPromoQ > scoreQuietPromoQ,
             qPrintable(QStringLiteral("%1 vs %2").arg(scoreCapPromoQ).arg(scoreQuietPromoQ)));
    // Queen promotion must score higher than Rook promotion:
    QVERIFY2(scoreCapPromoQ > scoreCapPromoR,
             qPrintable(QStringLiteral("%1 vs %2").arg(scoreCapPromoQ).arg(scoreCapPromoR)));

    // 2. Good capture vs Killer move vs Quiet move vs Bad capture (losing sacrifice).
    // Position: White Queen on h5, Rook on e1, Pawn on e4.
    // Black Queen on d5 (undefended), Black pawn on f7 (defended by King), Black Pawn on a7.
    Rules orderingPos;
    QVERIFY(orderingPos.loadFen(QStringLiteral("rnb1kbnr/p4ppp/8/3q3Q/4P3/8/PPPP1PPP/RNB1K2R w KQkq - 0 1")));
    const auto goodCapture = UciParser::parseMove(QStringLiteral("e4d5")); // PxQ (winning)
    const auto badCapture = UciParser::parseMove(QStringLiteral("h5f7")); // QxP defended by king (losing)
    const auto quietMove = UciParser::parseMove(QStringLiteral("e1g1")); // quiet (castling)
    QVERIFY(goodCapture.has_value() && badCapture.has_value() && quietMove.has_value());

    const Rules::Move killer = *quietMove;
    const int scoreGoodCap = HeuristicSearch::moveOrderingScore(orderingPos, *goodCapture);
    const int scoreKiller = HeuristicSearch::moveOrderingScore(orderingPos, killer, {}, killer);
    const int scoreQuiet = HeuristicSearch::moveOrderingScore(orderingPos, *quietMove);
    const int scoreBadCap = HeuristicSearch::moveOrderingScore(orderingPos, *badCapture);

    // Good capture >= 1'000'000:
    QVERIFY(scoreGoodCap >= 1'000'000);
    // Killer == 800'000:
    QCOMPARE(scoreKiller, 800'000);
    // Quiet move >= 0:
    QVERIFY(scoreQuiet >= 0);
    // Bad capture < 0 (relegated after quiet moves):
    QVERIFY(scoreBadCap < 0);
    // Complete ordering verification:
    QVERIFY(scoreGoodCap > scoreKiller);
    QVERIFY(scoreKiller > scoreQuiet);
    QVERIFY(scoreQuiet > scoreBadCap);
}

void HeuristicEvalTest::testTranspositionTablePreservesMoveOnFailLow() {
    HeuristicEval::resetParams();
    HeuristicSearch::clearCaches();

    const quint64 key = 0x123456789ABCDEF0ULL;
    const auto moveA = UciParser::parseMove(QStringLiteral("e2e4"));
    QVERIFY(moveA.has_value());

    // 1. Store exact entry at depth 2 with best move e2e4.
    constexpr int TtExact = 1;
    constexpr int TtUpper = 3;
    HeuristicSearch::storeTransposition(key, 2, 50, TtExact, *moveA);

    Rules::Move probed{};
    QVERIFY(HeuristicSearch::probeTranspositionMove(key, probed));
    QCOMPARE(Rules::toUci(probed), QStringLiteral("e2e4"));

    // 2. Now store fail-low (TtUpper) at depth 3 with empty move {}.
    HeuristicSearch::storeTransposition(key, 3, -100, TtUpper, Rules::Move{});

    // 3. Probing the key must STILL have preserved move e2e4!
    Rules::Move probedAfterFailLow{};
    QVERIFY(HeuristicSearch::probeTranspositionMove(key, probedAfterFailLow));
    QCOMPARE(Rules::toUci(probedAfterFailLow), QStringLiteral("e2e4"));

    HeuristicSearch::clearCaches();
}

void HeuristicEvalTest::testTornTranspositionEntryIsRejected() {
    HeuristicEval::resetParams();
    HeuristicSearch::clearCaches();

    constexpr int TtExact = 1;
    const quint64 key = 0x0F1E2D3C4B5A6978ULL;
    const auto moveA = UciParser::parseMove(QStringLiteral("e2e4"));
    const auto moveB = UciParser::parseMove(QStringLiteral("d2d4"));
    QVERIFY(moveA.has_value() && moveB.has_value());

    // A well-formed entry is found again.
    HeuristicSearch::storeTransposition(key, 4, 30, TtExact, *moveA);
    Rules::Move probed{};
    QVERIFY(HeuristicSearch::probeTranspositionMove(key, probed));
    QCOMPARE(Rules::toUci(probed), QStringLiteral("e2e4"));

    // The slot two workers left half-written: the key word of position A with
    // the payload of position B. Position A must not receive B's move.
    HeuristicSearch::storeTornTransposition(key, 4, 30, TtExact, *moveA, 9, -80,
                                            TtExact, *moveB);
    Rules::Move torn{};
    QVERIFY(!HeuristicSearch::probeTranspositionMove(key, torn));

    // The same forged slot does not answer for a colliding key either.
    const quint64 collidingKey = key ^ (quint64{1} << 40);
    QVERIFY(!HeuristicSearch::probeTranspositionMove(collidingKey, torn));

    // A torn slot counts as empty for the depth-preferred replacement, so the
    // next store simply overwrites it.
    HeuristicSearch::storeTransposition(key, 1, 10, TtExact, *moveB);
    QVERIFY(HeuristicSearch::probeTranspositionMove(key, probed));
    QCOMPARE(Rules::toUci(probed), QStringLiteral("d2d4"));

    HeuristicSearch::clearCaches();
}

void HeuristicEvalTest::testBackwardPawnDefinition() {
    using P = HeuristicEval::EvalParams;
    const auto backward = [](const char *fen) {
        return pawnTermScore(QString::fromLatin1(fen),
                             &P::backwardPawnPenaltyMG,
                             &P::backwardPawnPenaltyEG);
    };

    // A duo on the same row is no backward pawn: each side can be defended by
    // the other stepping up, and the distant enemy pawns guard nothing.
    QCOMPARE(backward("4k3/2p1p3/8/8/2PP4/8/8/4K3 w - - 0 1"), 0);
    QCOMPARE(backward(mirrorFen(QStringLiteral("4k3/2p1p3/8/8/2PP4/8/8/4K3 w - - 0 1"))
                          .toLatin1().constData()),
             0);

    // The rear pawn of the c2/d4 pair can still come to the support of d4, so
    // neither is backward.
    QCOMPARE(backward("4k3/2p1pp2/8/8/3P4/8/2P2P2/4K3 w - - 0 1"), 0);

    // d4 has only c5 beside it, which is ahead of it, and its stop square d5 is
    // covered by the e6 pawn: it is backward (a penalty for White).
    const char *backwardFen = "4k3/8/4p3/2P5/3P4/8/8/4K3 w - - 0 1";
    QCOMPARE(backward(backwardFen), -40);
    QCOMPARE(pawnTermScore(mirrorFen(QString::fromLatin1(backwardFen)),
                           &P::backwardPawnPenaltyMG, &P::backwardPawnPenaltyEG),
             40);
}

void HeuristicEvalTest::testCandidatePawnDefinition() {
    using P = HeuristicEval::EvalParams;
    const auto candidates = [](const QString &fen) {
        return pawnTermScore(fen, &P::candidatePawnBonusMG,
                             &P::candidatePawnBonusEG);
    };

    // e4 is blocked by e5: a pawn that cannot advance on its own file is no
    // candidate, whichever pawns stand beside it.
    const QString blocked = QStringLiteral("4k3/8/8/4pP2/4P3/3P4/8/4K3 w - - 0 1");
    QCOMPARE(candidates(blocked), 0);
    QCOMPARE(candidates(mirrorFen(blocked)), 0);

    // Two pawns against one on the wing: the c-pawn has as many helpers as it
    // has sentinels and an open file, so it is the candidate.
    const QString majority = QStringLiteral("4k3/1p6/8/8/8/8/1PP5/4K3 w - - 0 1");
    QCOMPARE(candidates(majority), 40);
    QCOMPARE(candidates(mirrorFen(majority)), -40);

    // The lone c2 pawn faces an enemy pawn on its own file: no candidate. Black
    // holds the majority, so its b-pawn is the only one in the position.
    const QString outnumbered =
        QStringLiteral("4k3/1pp5/8/8/8/8/2P5/4K3 w - - 0 1");
    QCOMPARE(candidates(outnumbered), -40);
}

void HeuristicEvalTest::testEndgameScalingFlattensUnwinnableAdvantages() {
    HeuristicEval::resetParams();
    // Endings the static evaluation used to show as won although an engine
    // gives them zero or close to it: the stronger side has no pawn and leads by
    // less than the material it needs, so its advantage is scaled away. Both
    // colours are checked, which also pins the symmetry of the scaling.
    struct Case {
        const char *fen;
        int limit; // the strong side's score has to stay below it
    };
    const Case flat[] = {
        {"4k3/8/8/3b4/8/8/8/R3K3 w - - 0 1", 20},   // rook against bishop
        {"4k3/8/8/3n4/8/8/8/R3K3 w - - 0 1", 20},   // rook against knight
        {"4k3/8/8/3r4/8/8/8/R2BK3 w - - 0 1", 80},  // rook and bishop against rook
        {"8/5pk1/6p1/3R4/1r5P/6P1/5PK1/8 w - - 0 40", 60}, // three pawns against two
    };
    for (const Case &entry : flat) {
        const QString fen = QString::fromLatin1(entry.fen);
        Rules rules;
        QVERIFY2(rules.loadFen(fen), entry.fen);
        const int score = HeuristicEval::evaluateCentipawns(rules);
        QVERIFY2(score > -entry.limit && score < entry.limit,
                 qPrintable(QStringLiteral("%1: %2").arg(fen).arg(score)));
        QVERIFY2(HeuristicEval::evaluateBreakdown(rules).scaleFactor < 64,
                 entry.fen);

        Rules mirrored;
        QVERIFY2(mirrored.loadFen(mirrorFen(fen)), entry.fen);
        const int mirroredScore = HeuristicEval::evaluateCentipawns(mirrored);
        QVERIFY2(mirroredScore > -entry.limit && mirroredScore < entry.limit,
                 qPrintable(QStringLiteral("%1 mirrored: %2")
                                .arg(fen).arg(mirroredScore)));
    }

    // A queen against a rook keeps its edge, and a rook and a bishop against a
    // bare king keeps all of it: only the cases above are flattened.
    for (const char *fen : {"4k3/8/8/8/8/8/8/r2QK3 w - - 0 1",
                            "4k3/8/8/8/8/8/8/R2BK3 w - - 0 1"}) {
        Rules rules;
        QVERIFY2(rules.loadFen(QString::fromLatin1(fen)), fen);
        QVERIFY2(HeuristicEval::evaluateCentipawns(rules) > 200, fen);
    }
    Rules bareKing;
    QVERIFY(bareKing.loadFen(QStringLiteral("4k3/8/8/8/8/8/8/R2BK3 w - - 0 1")));
    QCOMPARE(HeuristicEval::evaluateBreakdown(bareKing).scaleFactor, 64);

    // The scaling is part of the explanation the tooltip shows, and the terms
    // stay unscaled.
    Rules bishop;
    QVERIFY(bishop.loadFen(QStringLiteral("4k3/8/8/3b4/8/8/8/R3K3 w - - 0 1")));
    const auto breakdown = HeuristicEval::evaluateBreakdown(bishop);
    QCOMPARE(breakdown.material, 170);
    QCOMPARE(breakdown.scaleFactor, HeuristicEval::params().noPawnsRookVsMinorScale);
}

void HeuristicEvalTest::testRookEndingScaleFollowsThePawnFlanks() {
    HeuristicEval::resetParams();
    const auto scaleOf = [](const char *fen) {
        Rules rules;
        return rules.loadFen(QString::fromLatin1(fen))
                   ? HeuristicEval::evaluateBreakdown(rules).scaleFactor
                   : -1;
    };
    const HeuristicEval::EvalParams defaults;

    // Pawns on one wing, the defending king beside its pawns: the wing rule.
    QCOMPARE(scaleOf("8/5pk1/6p1/3R4/1r5P/6P1/5PK1/8 w - - 0 40"),
             defaults.rookEndgameOneFlankScale);
    QCOMPARE(scaleOf(mirrorFen(QStringLiteral(
                         "8/5pk1/6p1/3R4/1r5P/6P1/5PK1/8 w - - 0 40"))
                         .toLatin1().constData()),
             defaults.rookEndgameOneFlankScale);

    // An outside pawn on the other wing takes the rule away; only the general
    // reduction for few pawns is left.
    QCOMPARE(scaleOf("8/5pk1/6p1/3R4/1r5P/P5P1/6K1/8 w - - 0 40"),
             defaults.generalScaleBase + 3 * defaults.generalScalePerPawn);

    // A bare king has nothing to defend with: the mop-up keeps its full value.
    QCOMPARE(scaleOf("4k3/8/8/8/8/8/8/K5Q1 w - - 0 1"), 64);
}

void HeuristicEvalTest::testUnstoppablePassedPawn() {
    HeuristicEval::resetParams();
    // The defender has only a king and no piece to stop the a-pawn. With the
    // king in the corner it cannot catch the pawn (rule of the square); on c8 it
    // can. Only the black king moves between the two, so the pawn term shows the
    // unstoppable bonus. A knight for White keeps the position out of the solved
    // king-and-pawn endings.
    const auto pawnTerm = [](const QString &fen) {
        Rules rules;
        return rules.loadFen(fen) ? HeuristicEval::evaluateBreakdown(rules).pawns
                                  : -99999;
    };
    const QString far = QStringLiteral("7k/8/8/P7/8/8/8/K1N5 b - - 0 1");
    const QString near = QStringLiteral("2k5/8/8/P7/8/8/8/K1N5 b - - 0 1");
    const int bonus = HeuristicEval::params().unstoppablePasserBonus;
    QVERIFY2(pawnTerm(far) - pawnTerm(near) > bonus / 2,
             qPrintable(QStringLiteral("%1 vs %2").arg(pawnTerm(far)).arg(pawnTerm(near))));
    // Mirrored: the good side is Black.
    QVERIFY(pawnTerm(mirrorFen(near)) - pawnTerm(mirrorFen(far)) > bonus / 2);

    // With White to move the king gets no head start on the pawn, so the same
    // king square that was too late with Black to move is still too late.
    QVERIFY(pawnTerm(QStringLiteral("2k5/8/8/P7/8/8/8/K1N5 w - - 0 1")) <
            pawnTerm(far));

    // A piece for the defender switches the rule off.
    const QString defended = QStringLiteral("6nk/8/8/P7/8/8/8/K1N5 b - - 0 1");
    QVERIFY(pawnTerm(defended) < pawnTerm(far) - bonus / 2);
}

void HeuristicEvalTest::testKingAndPawnAgainstKingIsSolved() {
    HeuristicEval::resetParams();
    // Results of a spread of positions, each one agreeing with Stockfish at
    // depth 12 (the whole bitbase was compared with it on 1500 random
    // positions when it was written). Wins are for the side with the pawn.
    struct Case {
        const char *fen;
        bool win;
    };
    const Case cases[] = {
        {"8/8/8/8/8/7p/6k1/2K5 w - - 0 1", true},
        {"8/3K4/8/1k6/8/2p5/8/8 b - - 0 1", true},
        {"8/8/8/8/3P4/8/4K3/2k5 b - - 0 1", true},
        {"5K2/8/6k1/4p3/8/8/8/8 w - - 0 1", true},
        {"8/8/8/2K2p2/8/7k/8/8 b - - 0 1", true},
        {"8/6K1/8/8/6k1/8/p7/8 w - - 0 1", true},
        {"1K6/6k1/4p3/8/8/8/8/8 b - - 0 1", true},
        {"8/4k3/8/8/8/1K6/p7/8 b - - 0 1", true},
        {"4k3/7p/8/8/1K6/8/8/8 w - - 0 1", false},
        {"8/K1p5/8/6k1/8/8/8/8 w - - 0 1", false},
        {"6k1/8/3K4/8/8/7P/8/8 w - - 0 1", false},
        {"8/8/8/8/6K1/8/4Pk2/8 b - - 0 1", false},
        {"8/1P1k4/8/8/8/K7/8/8 b - - 0 1", false},
        {"8/8/8/2k5/8/7P/3K4/8 w - - 0 1", false},
        {"8/8/8/2k3K1/3P4/8/8/8 b - - 0 1", false},
        {"8/5K2/8/5P2/4k3/8/8/8 b - - 0 1", false},
    };
    const HeuristicEval::EvalParams defaults;
    for (const Case &entry : cases) {
        // The position, its colour mirror and its file mirror have the same
        // result, which also exercises the normalisation of the table.
        const QString fen = QString::fromLatin1(entry.fen);
        for (const QString &variant :
             {fen, mirrorFen(fen), mirrorFilesFen(fen)}) {
            Rules rules;
            QVERIFY2(rules.loadFen(variant), qPrintable(variant));
            const int score = HeuristicEval::evaluateCentipawns(rules);
            const bool whitePawn = variant.split(QLatin1Char(' '))[0]
                                       .contains(QLatin1Char('P'));
            if (entry.win) {
                const int ownScore = whitePawn ? score : -score;
                QVERIFY2(ownScore >= defaults.knownWinScore &&
                             ownScore <= defaults.knownWinScore +
                                             7 * defaults.knownWinRankBonus,
                         qPrintable(QStringLiteral("%1: %2").arg(variant).arg(score)));
            } else {
                QVERIFY2(score == 0,
                         qPrintable(QStringLiteral("%1: %2").arg(variant).arg(score)));
            }
        }
    }

    // A winning pawn is worth more the further it has advanced, which is the
    // slope the search follows.
    Rules farAway;
    Rules advanced;
    QVERIFY(farAway.loadFen(QStringLiteral("8/8/8/8/2P5/8/8/K6k w - - 0 1")));
    QVERIFY(advanced.loadFen(QStringLiteral("8/8/8/2P5/8/8/8/K6k w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateCentipawns(advanced) >
            HeuristicEval::evaluateCentipawns(farAway));

    // The classic corner draw: the rook pawn cannot promote against a king in
    // its corner, whoever is to move.
    for (const char *fen : {"7k/8/6K1/7P/8/8/8/8 w - - 0 1",
                            "7k/8/6K1/7P/8/8/8/8 b - - 0 1"}) {
        Rules rules;
        QVERIFY2(rules.loadFen(QString::fromLatin1(fen)), fen);
        QCOMPARE(HeuristicEval::evaluateCentipawns(rules), 0);
    }

    // Both pawns and pieces at once: only the exact ending is special-cased.
    Rules extraKnight;
    QVERIFY(extraKnight.loadFen(QStringLiteral("8/6k1/8/7P/8/8/8/K5N1 w - - 0 1")));
    QVERIFY(HeuristicEval::evaluateCentipawns(extraKnight) > 300);
}

void HeuristicEvalTest::testBishopAndKnightMateAimsForTheRightCorner() {
    HeuristicEval::resetParams();
    // The dark-squared bishop mates in a1 or h8 only. The same material with the
    // defending king in h8 (a corner of the bishop's colour) must be worth much
    // more than with it in a8, which is the wrong corner.
    const auto score = [](const char *fen) {
        Rules rules;
        return rules.loadFen(QString::fromLatin1(fen))
                   ? HeuristicEval::evaluateCentipawns(rules)
                   : -99999;
    };
    const int right = score("7k/8/5K2/8/8/8/8/2B1N3 w - - 0 1");
    const int wrong = score("k7/8/2K5/8/8/8/8/2B1N3 w - - 0 1");
    QVERIFY2(right > wrong + 30,
             qPrintable(QStringLiteral("%1 vs %2").arg(right).arg(wrong)));
    // Mirrored, and with the light-squared bishop: the corners swap.
    QVERIFY(score("7k/8/5K2/8/8/8/8/2B1N3 w - - 0 1") >
            score("7k/8/5K2/8/8/8/8/3BN3 w - - 0 1") + 30);
}

void HeuristicEvalTest::testPassedPawnBonusGrowsWithRank() {
    HeuristicEval::resetParams();
    // A lone passed h-pawn, with a knight each so that the position is judged by
    // the terms: the pawn term rises with every rank, and the last two ranks are
    // worth clearly more than the first ones.
    const char *files[] = {"1n2k3/8/8/8/8/8/7P/1N2K3 w - - 0 1",
                           "1n2k3/8/8/8/8/7P/8/1N2K3 w - - 0 1",
                           "1n2k3/8/8/8/7P/8/8/1N2K3 w - - 0 1",
                           "1n2k3/8/8/7P/8/8/8/1N2K3 w - - 0 1",
                           "1n2k3/8/7P/8/8/8/8/1N2K3 w - - 0 1",
                           "1n2k3/7P/8/8/8/8/8/1N2K3 w - - 0 1"};
    int previous = std::numeric_limits<int>::min();
    int values[6] = {};
    for (int rank = 0; rank < 6; ++rank) {
        Rules rules;
        QVERIFY2(rules.loadFen(QString::fromLatin1(files[rank])), files[rank]);
        values[rank] = HeuristicEval::evaluateBreakdown(rules).pawns;
        QVERIFY2(values[rank] > previous,
                 qPrintable(QStringLiteral("rank %1: %2 after %3")
                                .arg(rank + 2).arg(values[rank]).arg(previous)));
        previous = values[rank];
    }
    QVERIFY(values[5] - values[3] > values[3] - values[1]);

    // The table is what carries the rank: nothing else grows with it here.
    using P = HeuristicEval::EvalParams;
    QCOMPARE(pawnTermScore(QString::fromLatin1(files[4]),
                           {{&P::passedPawnRank6MG, 77}, {&P::passedPawnRank6EG, 77}}),
             77);
}

void HeuristicEvalTest::testPassedPawnKingsAndRoad() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;

    // Kings: with only the king-distance weights on, the pawn term rewards the
    // own king near and the enemy king far from the square in front of a passed
    // pawn. Each colour, since the mirrored positions swap the roles.
    const auto kings = [](const QString &fen) {
        return pawnTermScore(fen, {{&P::passedKingDistanceEnemy, 10},
                                   {&P::passedKingDistanceOwn, 4}});
    };
    const QString ownNear = QStringLiteral("1n2k3/8/3P4/3K4/8/8/8/1N6 w - - 0 1");
    const QString ownFar = QStringLiteral("1n2k3/8/3P4/8/8/8/8/1N5K w - - 0 1");
    QVERIFY(kings(ownNear) > kings(ownFar));
    QVERIFY(kings(mirrorFen(ownNear)) < kings(mirrorFen(ownFar)));
    // The enemy king far from the pawn is worth more than the enemy king close.
    const QString enemyFar = QStringLiteral("1n5k/8/3P4/3K4/8/8/8/1N6 w - - 0 1");
    const QString enemyNear = QStringLiteral("1n2k3/8/3P4/3K4/8/8/8/1N6 w - - 0 1");
    QVERIFY(kings(enemyFar) > kings(enemyNear));
    // A pawn that has not passed its third rank gets no such term.
    QCOMPARE(kings(QStringLiteral("1n2k3/8/8/8/8/3P4/3K4/1N6 w - - 0 1")), 0);

    // Road: a passed pawn on the fifth rank earns the free-road bonus per rank
    // past the third when nothing stands on or attacks its road, and the smaller
    // safe-stop bonus when a piece stands further up but the stop square is safe.
    const auto road = [](const QString &fen) {
        return pawnTermScore(fen, {{&P::passedPawnFreePathMG, 10},
                                   {&P::passedPawnFreePathEG, 10},
                                   {&P::passedPawnSafeStopMG, 4},
                                   {&P::passedPawnSafeStopEG, 4}});
    };
    const QString free = QStringLiteral("n6k/8/8/3P4/8/8/8/1N2K3 w - - 0 1");
    const QString stopSafe = QStringLiteral("7k/3n4/8/3P4/8/8/8/1N2K3 w - - 0 1");
    QCOMPARE(road(free), 2 * 10);
    QCOMPARE(road(stopSafe), 2 * 4);
    // Attacked stop square: no bonus at all.
    QCOMPARE(road(QStringLiteral("7k/1n6/8/3P4/8/8/8/1N2K3 w - - 0 1")), 0);
    QCOMPARE(road(mirrorFen(free)), -2 * 10);
}

void HeuristicEvalTest::testPhalanxPawns() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;
    const auto phalanx = [](const QString &fen) {
        return pawnTermScore(fen, {{&P::phalanxPawnBonusMG, 6},
                                   {&P::phalanxPawnBonusEG, 6}});
    };
    // Two pawns side by side on the fourth rank: 3 ranks past the start, 6 each.
    const QString duo = QStringLiteral("4k3/8/8/8/3PP3/8/8/4K3 w - - 0 1");
    QCOMPARE(phalanx(duo), 2 * 3 * 6);
    QCOMPARE(phalanx(mirrorFen(duo)), -2 * 3 * 6);
    // Not side by side: nothing.
    QCOMPARE(phalanx(QStringLiteral("4k3/8/8/8/3P1P2/8/8/4K3 w - - 0 1")), 0);
    // An enemy pawn facing a pawn of the phalanx halves that pawn's bonus.
    QCOMPARE(phalanx(QStringLiteral("4k3/8/3p4/8/3PP3/8/8/4K3 w - - 0 1")),
             3 * 6 + 3 * 6 / 2 - 0);
}

void HeuristicEvalTest::testPawnCacheIsTransparent() {
    HeuristicEval::resetParams();
    const auto sameBreakdown = [](const HeuristicEval::EvalBreakdown &a,
                                  const HeuristicEval::EvalBreakdown &b) {
        return a.material == b.material && a.placement == b.placement &&
               a.pawns == b.pawns && a.space == b.space &&
               a.mobility == b.mobility && a.kingSafety == b.kingSafety &&
               a.threats == b.threats && a.outposts == b.outposts &&
               a.badBishops == b.badBishops &&
               a.connectedRooks == b.connectedRooks && a.mopUp == b.mopUp &&
               a.tempo == b.tempo && a.inCheck == b.inCheck &&
               a.scaleFactor == b.scaleFactor && a.total == b.total;
    };

    // Pairs of positions with the same pawns and different pieces: the second
    // one finds the pawn entry the first one stored, and has to score what it
    // scores from a cold cache.
    const char *pairs[][2] = {
        {"r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5",
         "r1bqk2r/pppp1ppp/5n2/2b1p3/2B1P3/2n2N2/PPPP1PPP/R1BQ1RK1 b kq - 4 5"},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         "8/2p5/3p4/1P5r/1R3p1k/8/4P1P1/K7 w - - 0 1"},
        {"1r4k1/5pp1/3P4/3K4/8/8/5PP1/1R6 w - - 0 1",
         "1r4k1/5pp1/3P4/8/8/8/5PP1/1R4K1 w - - 0 1"},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         "r3k2r/p1ppqpb1/bn2pnp1/3P4/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    };
    for (const auto &pair : pairs) {
        Rules first;
        Rules second;
        QVERIFY2(first.loadFen(QString::fromLatin1(pair[0])), pair[0]);
        QVERIFY2(second.loadFen(QString::fromLatin1(pair[1])), pair[1]);

        HeuristicSearch::clearCaches();
        const auto coldFirst = HeuristicEval::evaluateBreakdown(first);
        HeuristicSearch::clearCaches();
        const auto coldSecond = HeuristicEval::evaluateBreakdown(second);

        // Cold, then warm (the same position again), then through the entry
        // the sibling position left behind.
        HeuristicSearch::clearCaches();
        QVERIFY(sameBreakdown(HeuristicEval::evaluateBreakdown(first), coldFirst));
        QVERIFY(sameBreakdown(HeuristicEval::evaluateBreakdown(first), coldFirst));
        QVERIFY(sameBreakdown(HeuristicEval::evaluateBreakdown(second), coldSecond));
        QVERIFY(sameBreakdown(HeuristicEval::evaluateBreakdown(first), coldFirst));
    }

    // The first pair does share its pawns, which is what makes it a test of the
    // cache and not of two independent lookups.
    Rules a;
    Rules b;
    QVERIFY(a.loadFen(QString::fromLatin1(pairs[0][0])));
    QVERIFY(b.loadFen(QString::fromLatin1(pairs[0][1])));
    QCOMPARE(a.pawnKey(), b.pawnKey());

    // A change of the weights drops the entries, so a new weight is not
    // answered from the old one.
    Rules pawns;
    QVERIFY(pawns.loadFen(QStringLiteral("1n2k3/8/8/8/4P3/8/8/1N2K3 w - - 0 1")));
    const int before = HeuristicEval::evaluateBreakdown(pawns).pawns;
    HeuristicEval::EvalParams tuned = HeuristicEval::params();
    tuned.isolatedPawnPenaltyMG += 30;
    tuned.isolatedPawnPenaltyEG += 30;
    HeuristicEval::setParams(tuned);
    QVERIFY(HeuristicEval::evaluateBreakdown(pawns).pawns < before);
    HeuristicEval::resetParams();
    QCOMPARE(HeuristicEval::evaluateBreakdown(pawns).pawns, before);
}

void HeuristicEvalTest::testKingDangerGrowsFasterThanTheAttackers() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;
    // Only the attackers count, with a weight of 100 each for a knight and a
    // bishop and a divisor of 100, so the penalty is the square of the danger.
    const std::vector<std::pair<int P::*, int>> weights = {
        {&P::kingAttackerWeightKnight, 100}, {&P::kingAttackerWeightBishop, 100},
        {&P::kingAttackerWeightRook, 0}, {&P::kingAttackerWeightQueen, 0},
        {&P::kingAdjacentAttackWeight, 0}, {&P::kingDangerOpenFile, 0},
        {&P::safeCheckKnight, 0}, {&P::safeCheckBishop, 0},
        {&P::safeCheckRook, 0}, {&P::safeCheckQueen, 0},
        {&P::queenlessDangerReduction, 0}, {&P::kingDangerDivisor, 100}};
    // The same white pieces in three places. Far away from the black king they
    // attack nothing; with the knight on g5 one piece attacks its zone (the
    // queen keeps that enough to be charged); with the bishop on d3 as well
    // there are two.
    const QString none = QStringLiteral("6k1/p4ppp/8/8/8/N7/P7/3QK2B w - - 0 1");
    const QString one = QStringLiteral("6k1/p4ppp/8/6N1/8/8/P7/3QK2B w - - 0 1");
    const QString two = QStringLiteral("6k1/p4ppp/8/6N1/8/3B4/P7/3QK3 w - - 0 1");
    for (const bool mirrored : {false, true}) {
        const auto safety = [&](const QString &fen) {
            return kingSafetyWith(mirrored ? mirrorFen(fen) : fen, weights);
        };
        const int sign = mirrored ? -1 : 1;
        const int base = sign * safety(none);
        const int gainOne = sign * safety(one) - base;
        const int gainTwo = sign * safety(two) - base;
        QVERIFY2(gainOne > 0, qPrintable(QString::number(gainOne)));
        // Two attackers cost far more than twice what one costs.
        QVERIFY2(gainTwo > 2 * gainOne,
                 qPrintable(QStringLiteral("%1 vs %2").arg(gainTwo).arg(gainOne)));
    }

    // Without a queen a lone attacker does nothing at all.
    const QString loneKnight =
        QStringLiteral("6k1/p4ppp/8/6N1/8/8/P7/4K2B w - - 0 1");
    const QString farKnight =
        QStringLiteral("6k1/p4ppp/8/8/8/N7/P7/4K2B w - - 0 1");
    QCOMPARE(kingSafetyWith(loneKnight, weights), kingSafetyWith(farKnight, weights));
}

void HeuristicEvalTest::testKingDangerFallsWithoutAQueen() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;
    // A knight and a bishop against the black king: with the reduction for a
    // queenless attacker the white side gains less than without it.
    const QString queenless = QStringLiteral("6k1/p4ppp/8/6N1/8/3B4/P7/4K3 w - - 0 1");
    QVERIFY(kingSafetyWith(queenless, {{&P::queenlessDangerReduction, 400}}) <
            kingSafetyWith(queenless, {{&P::queenlessDangerReduction, 0}}));
    // With a queen on the board the reduction does not apply.
    const QString withQueen = QStringLiteral("6k1/p4ppp/8/6N1/8/3B4/P7/3QK3 w - - 0 1");
    QCOMPARE(kingSafetyWith(withQueen, {{&P::queenlessDangerReduction, 400}}),
             kingSafetyWith(withQueen, {{&P::queenlessDangerReduction, 0}}));
    // The same, seen from the other colour.
    QVERIFY(kingSafetyWith(mirrorFen(queenless), {{&P::queenlessDangerReduction, 400}}) >
            kingSafetyWith(mirrorFen(queenless), {{&P::queenlessDangerReduction, 0}}));
}

void HeuristicEvalTest::testSafeChecksAddToTheKingDanger() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;
    // Black knight on f4 and bishop on b6 against the white king on g1: the
    // knight would give check from e2, which nothing but the king could cover.
    const QString uncovered =
        QStringLiteral("6k1/p4ppp/1b6/8/5n2/8/P4PPP/6K1 w - - 0 1");
    // A white bishop on f1 covers e2, so that check square is not safe.
    const QString covered =
        QStringLiteral("6k1/p4ppp/1b6/8/5n2/8/P4PPP/5BK1 w - - 0 1");
    const auto knightChecks = [&](const QString &fen) {
        return kingSafetyWith(fen, {{&P::safeCheckKnight, 200}}) -
               kingSafetyWith(fen, {{&P::safeCheckKnight, 0}});
    };
    QVERIFY2(knightChecks(uncovered) < 0,
             qPrintable(QString::number(knightChecks(uncovered))));
    QCOMPARE(knightChecks(covered), 0);
    QVERIFY(knightChecks(mirrorFen(uncovered)) > 0);
}

void HeuristicEvalTest::testOpenFilesNearTheKing() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;
    const auto penalty = [&](const QString &fen) {
        return kingSafetyWith(fen, {{&P::openFileNearKingPenalty, 100}}) -
               kingSafetyWith(fen, {{&P::openFileNearKingPenalty, 0}});
    };
    // The g-file is open (no pawn of either colour) and White has a rook: the
    // black king pays, which is a gain for White.
    const QString open = QStringLiteral("6k1/p4p1p/8/8/8/8/P4P1P/R3K3 w - - 0 1");
    // Half open: a white pawn still stands on the file.
    const QString half = QStringLiteral("6k1/p4p1p/8/8/8/8/P4PPP/R3K3 w - - 0 1");
    // The same open file against an opponent with only a knight: no penalty.
    const QString noRook = QStringLiteral("6k1/p4p1p/8/8/8/8/P4P1P/N3K3 w - - 0 1");
    QVERIFY(penalty(open) > 0);
    QVERIFY(penalty(half) > 0);
    QVERIFY(penalty(half) < penalty(open));
    QCOMPARE(penalty(noRook), 0);
    QVERIFY(penalty(mirrorFen(open)) < 0);
}

void HeuristicEvalTest::testKingSafetyInTheCentreAndUnderStorm() {
    HeuristicEval::resetParams();
    using P = HeuristicEval::EvalParams;
    // A black king that stayed on e8 without castling rights and has files
    // without pawns beside it, against a rook.
    const auto centre = [&](const QString &fen) {
        return kingSafetyWith(fen, {{&P::kingCentreOpenFilePenalty, 100}}) -
               kingSafetyWith(fen, {{&P::kingCentreOpenFilePenalty, 0}});
    };
    QVERIFY(centre(QStringLiteral("4k3/p4ppp/8/8/8/8/P4PPP/R3K3 w - - 0 1")) > 0);
    // With its castling rights (White keeps its own too, or its king in the
    // centre would be the one charged), or on the g-file, it does not count.
    QCOMPARE(centre(QStringLiteral("4k2r/p4ppp/8/8/8/8/P4PPP/R3K3 w Qk - 0 1")), 0);
    QCOMPARE(centre(QStringLiteral("6k1/p4ppp/8/8/8/8/P4PPP/R3K3 w - - 0 1")), 0);
    QVERIFY(centre(mirrorFen(QStringLiteral("4k3/p4ppp/8/8/8/8/P4PPP/R3K3 w - - 0 1"))) < 0);

    // A white pawn on g5 storms the black king; a black pawn on g6 right in
    // front of it blocks the storm and reduces the penalty.
    const auto storm = [&](const QString &fen) {
        return kingSafetyWith(fen, {{&P::kingStormBlockedPercent, 100}}) -
               kingSafetyWith(fen, {{&P::kingStormBlockedPercent, 0}});
    };
    QVERIFY(storm(QStringLiteral("q5k1/p4p1p/6p1/6P1/8/8/P7/Q3K3 w - - 0 1")) > 0);
    QCOMPARE(storm(QStringLiteral("q5k1/p4p1p/8/6P1/8/8/P7/Q3K3 w - - 0 1")), 0);
}

QTEST_MAIN(HeuristicEvalTest)
#include "heuristiceval_test.moc"
