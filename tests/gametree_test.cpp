#include <QTest>

#include "gametree.h"

class GameTreeTest : public QObject {
    Q_OBJECT

private slots:
    void testAddChildAndLines();
    void testPromoteAndRemove();
    void testParsePgnWithVariations();
    void testParsePgnWithoutVariationsIsLinear();
    void testParseNestedAndMalformedVariations();
};

void GameTreeTest::testAddChildAndLines() {
    GameTree tree;
    const int e4 = tree.addChild(GameTree::RootId, QStringLiteral("e2e4"));
    const int e5 = tree.addChild(e4, QStringLiteral("e7e5"));
    const int c5 = tree.addChild(e4, QStringLiteral("c7c5"));
    // The same move twice is the same node.
    QCOMPARE(tree.addChild(e4, QStringLiteral("e7e5")), e5);

    QCOMPARE(tree.mainLine(), (QVector<int>{e4, e5}));
    QCOMPARE(tree.lineFrom(c5), (QVector<int>{c5}));
    QCOMPARE(tree.pathTo(c5), (QVector<int>{e4, c5}));
    QCOMPARE(tree.depth(c5), 2);
    QCOMPARE(tree.variationCount(), 1);
    QCOMPARE(tree.uciOf(tree.mainLine()),
             (QStringList{QStringLiteral("e2e4"), QStringLiteral("e7e5")}));
}

void GameTreeTest::testPromoteAndRemove() {
    GameTree tree;
    const int e4 = tree.addChild(GameTree::RootId, QStringLiteral("e2e4"));
    const int e5 = tree.addChild(e4, QStringLiteral("e7e5"));
    const int d4 = tree.addChild(GameTree::RootId, QStringLiteral("d2d4"));
    const int d5 = tree.addChild(d4, QStringLiteral("d7d5"));
    const int c6 = tree.addChild(d4, QStringLiteral("c7c6"));

    tree.promote(c6);
    // The line through c6 is now the main line at every branch point.
    QCOMPARE(tree.mainLine(), (QVector<int>{d4, c6}));
    QCOMPARE(tree.node(d4).children, (QVector<int>{c6, d5}));

    tree.removeSubtree(d4);
    QVERIFY(!tree.isValid(d5));
    QCOMPARE(tree.mainLine(), (QVector<int>{e4, e5}));
    QCOMPARE(tree.variationCount(), 0);
}

void GameTreeTest::testParsePgnWithVariations() {
    Rules start;
    start.reset();
    GameTree tree;
    const QString pgn = QStringLiteral(
        "[Event \"x\"]\n\n1. e4 e5 (1... c5 2. Nf3 d6 { sicilian }) 2. Nf3 Nc6 "
        "(2... d6 3. d4) 3. Bb5 1-0");
    QVERIFY(GameTree::parsePgn(pgn, start, &tree));

    const QStringList main = tree.uciOf(tree.mainLine());
    QCOMPARE(main, (QStringList{"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"}));
    QCOMPARE(tree.variationCount(), 2);

    const int e4 = tree.mainLine().at(0);
    const int c5 = tree.findChild(e4, QStringLiteral("c7c5"));
    QVERIFY(c5 >= 0);
    QCOMPARE(tree.uciOf(tree.lineFrom(c5)),
             (QStringList{"c7c5", "g1f3", "d7d6"}));
    const int d6 = tree.lineFrom(c5).last();
    QCOMPARE(tree.node(d6).annotations.comment, QStringLiteral("sicilian"));

    const int e5 = tree.mainLine().at(1);
    const int nf3 = tree.mainLine().at(2);
    QVERIFY(tree.findChild(nf3, QStringLiteral("d7d6")) >= 0);
    QVERIFY(e5 >= 0);
}

void GameTreeTest::testParsePgnWithoutVariationsIsLinear() {
    Rules start;
    start.reset();
    GameTree tree;
    QVERIFY(GameTree::parsePgn(QStringLiteral("1. e4 e5 2. Nf3 *"), start, &tree));
    QCOMPARE(tree.variationCount(), 0);
    QCOMPARE(tree.mainLine().size(), 3);
}

void GameTreeTest::testParseNestedAndMalformedVariations() {
    Rules start;
    start.reset();
    GameTree tree;
    // A variation inside a variation, then a variation with an illegal move
    // and a variation opened before any move: none of them may break the line.
    const QString pgn = QStringLiteral(
        "( 1. d4 ) 1. e4 e5 (1... c5 2. Nf3 (2. Nc3 Nc6) 2... d6) (1... e6 2. Ke5) 2. Nf3 *");
    QVERIFY(GameTree::parsePgn(pgn, start, &tree));
    QCOMPARE(tree.uciOf(tree.mainLine()),
             (QStringList{"e2e4", "e7e5", "g1f3"}));

    const int e4 = tree.mainLine().at(0);
    const int c5 = tree.findChild(e4, QStringLiteral("c7c5"));
    QVERIFY(c5 >= 0);
    const int nf3 = tree.findChild(c5, QStringLiteral("g1f3"));
    const int nc3 = tree.findChild(c5, QStringLiteral("b1c3"));
    QVERIFY(nf3 >= 0);
    QVERIFY(nc3 >= 0);
    QVERIFY(tree.findChild(nf3, QStringLiteral("d7d6")) >= 0);
    QVERIFY(tree.findChild(nc3, QStringLiteral("b8c6")) >= 0);
    // The illegal move left an e6 node without a continuation, at most.
    const int e6 = tree.findChild(e4, QStringLiteral("e7e6"));
    if (e6 >= 0) {
        QVERIFY(tree.node(e6).children.isEmpty());
    }
}

QTEST_MAIN(GameTreeTest)
#include "gametree_test.moc"
