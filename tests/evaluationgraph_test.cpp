//
// Unit tests for the game evaluation graph widget.
//

#include <QSignalSpy>
#include <QTest>
#include <QPixmap>

#include "evaluationgraph.h"

class EvaluationGraphTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaults();
    void testEvaluations();
    void testCurrentPly();
    void testClickSelectsPly();
    void testTooltip();
    void testRendering();
};

void EvaluationGraphTest::testDefaults() {
    EvaluationGraph graph;

    QVERIFY(graph.evaluations().isEmpty());
    QCOMPARE(graph.currentPly(), 0);
    QVERIFY(graph.auditAnnotations().isEmpty());
    QVERIFY(graph.sizeHint().width() > 0);
    QVERIFY(graph.sizeHint().height() > 0);
    // The curve must give way to the move list when the panel is short, so it
    // declares no minimum of its own.
    QVERIFY(graph.minimumSizeHint().height() <= 0);
}

void EvaluationGraphTest::testEvaluations() {
    EvaluationGraph graph;

    graph.setEvaluations({50.0, 60.0, 45.0});
    QCOMPARE(graph.evaluations().size(), 3);
    QCOMPARE(graph.evaluations().at(1), 60.0);

    graph.setEvaluations({});
    QVERIFY(graph.evaluations().isEmpty());
}

void EvaluationGraphTest::testCurrentPly() {
    EvaluationGraph graph;
    graph.setEvaluations({50.0, 50.0, 50.0});

    graph.setCurrentPly(2);
    QCOMPARE(graph.currentPly(), 2);

    // Out of range values are clamped to the plotted plies.
    graph.setCurrentPly(9);
    QCOMPARE(graph.currentPly(), 2);
    graph.setCurrentPly(-4);
    QCOMPARE(graph.currentPly(), 0);

    // Dropping points pulls the cursor back into the remaining range.
    graph.setCurrentPly(2);
    graph.setEvaluations({50.0});
    QCOMPARE(graph.currentPly(), 0);
}

void EvaluationGraphTest::testClickSelectsPly() {
    EvaluationGraph graph;
    graph.resize(200, 80);
    graph.setEvaluations({50.0, 50.0, 50.0, 50.0, 50.0});
    graph.show();
    QVERIFY(QTest::qWaitForWindowExposed(&graph));

    QSignalSpy spy(&graph, &EvaluationGraph::plySelected);

    QTest::mouseClick(&graph, Qt::LeftButton, Qt::NoModifier, QPoint(1, 40));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
    QCOMPARE(graph.currentPly(), 0);

    QTest::mouseClick(&graph, Qt::LeftButton, Qt::NoModifier,
                      QPoint(graph.width() - 1, 40));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 4);
    QCOMPARE(graph.currentPly(), 4);

    // The middle of the plot is the middle ply.
    QTest::mouseClick(&graph, Qt::LeftButton, Qt::NoModifier,
                      QPoint(graph.width() / 2, 40));
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toInt(), 2);
}

void EvaluationGraphTest::testTooltip() {
    EvaluationGraph graph;
    graph.setEvaluations({50.0, 60.5, 45.0});

    // Plies that are not plotted have no tooltip.
    QVERIFY(graph.tooltipForPly(-1).isEmpty());
    QVERIFY(graph.tooltipForPly(3).isEmpty());

    const QString start = graph.tooltipForPly(0);
    QVERIFY(start.contains(QStringLiteral("Start")));
    QVERIFY(start.contains(QStringLiteral("50.0")));

    const QString whiteMove = graph.tooltipForPly(1);
    QVERIFY(whiteMove.contains(QStringLiteral("Move 1 (White)")));
    QVERIFY(whiteMove.contains(QStringLiteral("60.5")));

    const QString blackMove = graph.tooltipForPly(2);
    QVERIFY(blackMove.contains(QStringLiteral("Move 1 (Black)")));
    QVERIFY(blackMove.contains(QStringLiteral("45.0")));
}

void EvaluationGraphTest::testRendering() {
    EvaluationGraph graph;
    graph.resize(200, 80);
    QPixmap pixmap(graph.size());

    // An empty curve, a single point and a crossing curve must all render.
    graph.render(&pixmap);
    graph.setEvaluations({50.0});
    graph.render(&pixmap);
    graph.setEvaluations({100.0, 0.0, 50.0, 12.0});
    graph.render(&pixmap);

    // White is winning: the band above the equilibrium line is white and the
    // lower half keeps the plot background.
    graph.setEvaluations({90.0, 90.0, 90.0});
    graph.render(&pixmap);
    const QImage whitesWinning = pixmap.toImage();
    QCOMPARE(whitesWinning.pixelColor(20, 20), QColor("#ffffff"));
    QCOMPARE(whitesWinning.pixelColor(20, 60), QColor("#f7f3eb"));

    // Black is winning: the band flips to the lower half.
    graph.setEvaluations({10.0, 10.0, 10.0});
    graph.render(&pixmap);
    const QImage blacksWinning = pixmap.toImage();
    QCOMPARE(blacksWinning.pixelColor(20, 20), QColor("#f7f3eb"));
    QCOMPARE(blacksWinning.pixelColor(20, 60), QColor("#312e2b"));

    // A level game leaves the whole plot empty.
    graph.setEvaluations({50.0, 50.0, 50.0});
    graph.render(&pixmap);
    const QImage level = pixmap.toImage();
    QCOMPARE(level.pixelColor(20, 20), QColor("#f7f3eb"));
    QCOMPARE(level.pixelColor(20, 60), QColor("#f7f3eb"));
}

QTEST_MAIN(EvaluationGraphTest)
#include "evaluationgraph_test.moc"
