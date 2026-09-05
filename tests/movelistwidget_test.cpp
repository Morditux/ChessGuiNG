//
// Unit tests for the MoveListWidget move list rendering and navigation.
//

#include <QSignalSpy>
#include <QTest>

#include "movelistwidget.h"

class MoveListWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialState();
    void testRendersMovesOnly();
    void testCurrentMoveSelected();
    void testClickOnMoveEmitsSelection();
    void testClickOnStartEmitsSelection();
    void testClickOnMoveNumberSelectsWhiteMove();
    void testClickOnBlackMoveEmitsBlackSelection();
    void testBlackStartsPair();
    void testSetPgnResetsCurrentMove();
    void testFocusAndActivationDoNotChangeDisplayedMove();
    void testEmptyCellsDoNotNavigate();
};

QString cellText(MoveListWidget &widget, int row, int column) {
    return widget.model()->index(row, column).data().toString();
}

// Clicks inside the cell at the given row and column.
void clickOnCell(MoveListWidget &widget, int row, int column) {
    const QModelIndex index = widget.model()->index(row, column);
    QVERIFY(index.isValid());
    const QPoint clickPos = widget.visualRect(index).center();
    QTest::mouseClick(widget.viewport(), Qt::LeftButton, {}, clickPos);
}

void MoveListWidgetTest::testInitialState() {
    MoveListWidget widget;
    QCOMPARE(widget.currentMove(), 0);
    QCOMPARE(widget.editTriggers(), QAbstractItemView::NoEditTriggers);
    QCOMPARE(widget.model()->rowCount(), 1);
    QCOMPARE(cellText(widget, 0, 0), QStringLiteral("Start"));
}

void MoveListWidgetTest::testRendersMovesOnly() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5"), QStringLiteral("2. Nf3 Nc6")}, 0);

    QCOMPARE(widget.model()->rowCount(), 3);
    QCOMPARE(cellText(widget, 1, 0), QStringLiteral("1"));
    QCOMPARE(cellText(widget, 1, 1), QStringLiteral("e4"));
    QCOMPARE(cellText(widget, 1, 2), QStringLiteral("e5"));
    QCOMPARE(cellText(widget, 2, 0), QStringLiteral("2"));
    QCOMPARE(cellText(widget, 2, 1), QStringLiteral("Nf3"));
    QCOMPARE(cellText(widget, 2, 2), QStringLiteral("Nc6"));

    QString allText;
    for (int row = 0; row < widget.model()->rowCount(); ++row) {
        for (int column = 0; column < widget.model()->columnCount(); ++column) {
            allText += cellText(widget, row, column);
        }
    }
    QVERIFY(!allText.contains(QLatin1Char('[')));
    QVERIFY(!allText.contains(QStringLiteral("1-0")));
}

void MoveListWidgetTest::testCurrentMoveSelected() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5"), QStringLiteral("2. Nf3")}, 0);
    const QList<QPair<int, int>> cells = {{0, 0}, {1, 1}, {1, 2}, {2, 1}};
    const QStringList labels = {"Start", "1. e4", "1… e5", "2. Nf3"};
    for (int ply : {0, 1, 2, 3, 0}) {
        widget.setCurrentMove(ply);
        QCOMPARE(widget.currentMove(), ply);
        QCOMPARE(widget.currentMoveText(), labels.at(ply));
        for (int row = 0; row < widget.model()->rowCount(); ++row) {
            for (int col = 0; col < 3; ++col) {
                const auto cell = widget.model()->index(row, col);
                const bool current = cells.at(ply) == qMakePair(row, col);
                QCOMPARE(cell.data(MoveListWidget::CurrentMoveRole).toBool(), current);
                QCOMPARE(qvariant_cast<QFont>(cell.data(Qt::FontRole)).bold(), current);
            }
        }
    }
}

void MoveListWidgetTest::testClickOnMoveEmitsSelection() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5"), QStringLiteral("2. Nf3 Nc6")}, 0);
    widget.resize(400, 300);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QSignalSpy spy(&widget, &MoveListWidget::moveSelected);
    clickOnCell(widget, 2, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 3);
}

void MoveListWidgetTest::testClickOnStartEmitsSelection() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5")}, 2);
    widget.resize(400, 300);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QSignalSpy spy(&widget, &MoveListWidget::moveSelected);
    clickOnCell(widget, 0, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 0);
}

void MoveListWidgetTest::testClickOnMoveNumberSelectsWhiteMove() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5")}, 0);
    widget.resize(400, 300);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QSignalSpy spy(&widget, &MoveListWidget::moveSelected);
    clickOnCell(widget, 1, 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 1);
}

void MoveListWidgetTest::testClickOnBlackMoveEmitsBlackSelection() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5")}, 0);
    widget.resize(400, 300);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QSignalSpy spy(&widget, &MoveListWidget::moveSelected);
    clickOnCell(widget, 1, 2);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 2);
}

void MoveListWidgetTest::testBlackStartsPair() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1... e5"), QStringLiteral("2. Nf3")}, 0);

    QCOMPARE(widget.model()->rowCount(), 3);
    QCOMPARE(cellText(widget, 1, 0), QStringLiteral("1"));
    QVERIFY(cellText(widget, 1, 1).isEmpty());
    QCOMPARE(cellText(widget, 1, 2), QStringLiteral("e5"));
    QCOMPARE(cellText(widget, 2, 0), QStringLiteral("2"));
    QCOMPARE(cellText(widget, 2, 1), QStringLiteral("Nf3"));

    widget.setCurrentMove(1);
    QVERIFY(widget.model()->index(1, 2).data(MoveListWidget::CurrentMoveRole).toBool());
    QCOMPARE(widget.currentMoveText(), QStringLiteral("1… e5"));
}

void MoveListWidgetTest::testSetPgnResetsCurrentMove() {
    MoveListWidget widget;
    widget.setPgn({QStringLiteral("1. e4 e5")}, 2);
    QCOMPARE(widget.currentMove(), 2);

    widget.setPgn({QStringLiteral("1. d4 d5")}, 0);
    QCOMPARE(widget.currentMove(), 0);
    QVERIFY(widget.model()->index(0, 0).data(MoveListWidget::CurrentMoveRole).toBool());

    widget.setCurrentMove(1);
    widget.setCurrentMove(1);
    QCOMPARE(widget.currentMove(), 1);

    widget.setCurrentMove(-3);
    QCOMPARE(widget.currentMove(), 0);
}

void MoveListWidgetTest::testFocusAndActivationDoNotChangeDisplayedMove() {
    MoveListWidget widget;
    widget.setPgn({"1. e4 e5", "2. Nf3 Nc6"}, 1);
    widget.resize(400, 300);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QSignalSpy spy(&widget, &MoveListWidget::moveSelected);
    widget.setFocus();
    QTest::keyClick(&widget, Qt::Key_Down);
    QCOMPARE(widget.currentIndex(), widget.model()->index(2, 1));
    QCOMPARE(spy.count(), 0);
    QTest::keyClick(&widget, Qt::Key_Return);
    QCOMPARE(spy.takeFirst().first().toInt(), 3);
    QCOMPARE(widget.currentMove(), 1); // The controller has not accepted navigation.
    clickOnCell(widget, 1, 2);
    QCOMPARE(spy.takeFirst().first().toInt(), 2);
    QCOMPARE(widget.currentMove(), 1);
    QVERIFY(widget.model()->index(1, 1).data(MoveListWidget::CurrentMoveRole).toBool());
    QVERIFY(!widget.model()->index(1, 2).data(MoveListWidget::CurrentMoveRole).toBool());
    widget.setCurrentMove(2);
    QVERIFY(widget.model()->index(1, 2).data(MoveListWidget::CurrentMoveRole).toBool());
}

void MoveListWidgetTest::testEmptyCellsDoNotNavigate() {
    MoveListWidget widget;
    widget.setPgn({"12... Nf6", "13. Nf3"}, 1);
    widget.resize(400, 300);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QSignalSpy spy(&widget, &MoveListWidget::moveSelected);
    clickOnCell(widget, 1, 1);
    clickOnCell(widget, 2, 2);
    QTest::keyClick(&widget, Qt::Key_Enter);
    QCOMPARE(spy.count(), 0);
    clickOnCell(widget, 1, 0);
    QCOMPARE(spy.takeFirst().first().toInt(), 1);
    QCOMPARE(widget.currentMoveText(), QStringLiteral("12… Nf6"));
}

QTEST_MAIN(MoveListWidgetTest)
#include "movelistwidget_test.moc"
