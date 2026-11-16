//
// Unit tests for PositionEditorDialog and board settings persistence.
//

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

#include "appconfig.h"
#include "chessboard.h"
#include "mainwindow.h"
#include "positioneditordialog.h"

class PositionEditorDialogTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialValidPosition();
    void testPiecePlacementAndValidation();
    void testCastlingAvailabilityTracksPieces();
    void testLiveFenEditing();
    void testEnPassantOptionsTrackPawns();
    void testMainWindowBoardSettingsPersistence();
};

void PositionEditorDialogTest::testInitialValidPosition() {
    const QString startFen = Rules().toFen();
    PositionEditorDialog dialog(startFen);

    QVERIFY(dialog.isPositionValid());
    QCOMPARE(dialog.fen(), startFen);
    QVERIFY(dialog.okButton()->isEnabled());
    QVERIFY(dialog.whiteToMoveRadio()->isChecked());
    QVERIFY(dialog.whiteKingSideCheck()->isChecked());
    QVERIFY(dialog.blackQueenSideCheck()->isChecked());
}

void PositionEditorDialogTest::testPiecePlacementAndValidation() {
    PositionEditorDialog dialog(Rules().toFen());

    // Remove White king on e1 (row 7, col 4) -> invalid position.
    dialog.setBoardPiece({7, 4}, std::nullopt);
    QVERIFY(!dialog.isPositionValid());
    QVERIFY(!dialog.okButton()->isEnabled());
    QVERIFY(!dialog.whiteKingSideCheck()->isEnabled());
    QVERIFY(!dialog.whiteQueenSideCheck()->isEnabled());

    // Restore White king on e1 -> valid again.
    dialog.setBoardPiece({7, 4}, Rules::Piece{Rules::PieceType::King, Rules::Color::White, false});
    QVERIFY(dialog.isPositionValid());
    QVERIFY(dialog.okButton()->isEnabled());

    // Place a pawn on rank 8 (row 0, col 0) -> invalid.
    dialog.setBoardPiece({0, 0}, Rules::Piece{Rules::PieceType::Pawn, Rules::Color::White, true});
    QVERIFY(!dialog.isPositionValid());
    QVERIFY(!dialog.okButton()->isEnabled());
}

void PositionEditorDialogTest::testCastlingAvailabilityTracksPieces() {
    PositionEditorDialog dialog(Rules().toFen());
    QVERIFY(dialog.whiteKingSideCheck()->isEnabled());
    QVERIFY(dialog.whiteKingSideCheck()->isChecked());

    // Remove h1 rook (row 7, col 7) -> K right unchecked & disabled.
    dialog.setBoardPiece({7, 7}, std::nullopt);
    QVERIFY(!dialog.whiteKingSideCheck()->isEnabled());
    QVERIFY(!dialog.whiteKingSideCheck()->isChecked());
    QVERIFY(dialog.whiteQueenSideCheck()->isEnabled());
    QVERIFY(!dialog.fen().contains(QStringLiteral(" K")));
}

void PositionEditorDialogTest::testLiveFenEditing() {
    PositionEditorDialog dialog(Rules().toFen());

    const QString sicilian =
        QStringLiteral("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2");
    dialog.fenEdit()->setText(sicilian);
    emit dialog.fenEdit()->textEdited(sicilian);

    QVERIFY(dialog.isPositionValid());
    QVERIFY(dialog.okButton()->isEnabled());
    const auto blackPawnC5 = dialog.boardPiece({3, 2});
    QVERIFY(blackPawnC5.has_value());
    QCOMPARE(blackPawnC5->type, Rules::PieceType::Pawn);
    QCOMPARE(blackPawnC5->color, Rules::Color::Black);
    QCOMPARE(dialog.enPassantCombo()->currentText(), QStringLiteral("c6"));

    // Malformed FEN disables OK without crashing.
    const QString badFen = QStringLiteral("invalid fen");
    dialog.fenEdit()->setText(badFen);
    emit dialog.fenEdit()->textEdited(badFen);
    QVERIFY(!dialog.isPositionValid());
    QVERIFY(!dialog.okButton()->isEnabled());
}

void PositionEditorDialogTest::testEnPassantOptionsTrackPawns() {
    // Black pawn on d5, White to move -> d6 is offered as en passant target.
    const QString fen = QStringLiteral("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1");
    PositionEditorDialog dialog(fen);
    QVERIFY(dialog.enPassantCombo()->findText(QStringLiteral("d6")) >= 0);

    dialog.blackToMoveRadio()->setChecked(true);
    QCOMPARE(dialog.enPassantCombo()->findText(QStringLiteral("d6")), -1);
}

void PositionEditorDialogTest::testMainWindowBoardSettingsPersistence() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));

    {
        MainWindow window(nullptr, confPath);
        QVERIFY(window.editPositionAction() != nullptr);
        QCOMPARE(window.editPositionAction()->shortcut().toString(QKeySequence::PortableText),
                 QStringLiteral("Ctrl+E"));
        QVERIFY(window.editPositionAction()->isEnabled());
        QVERIFY(window.animateMovesAction()->isChecked());
        window.animateMovesAction()->setChecked(false);
        QVERIFY(!window.chessBoard()->moveAnimationsEnabled());

        for (QAction *action : window.boardThemeGroup()->actions()) {
            if (action->data().toInt() == static_cast<int>(ChessBoard::BoardTheme::Green)) {
                action->trigger();
                break;
            }
        }
        QCOMPARE(window.chessBoard()->boardTheme(), ChessBoard::BoardTheme::Green);
    }

    {
        MainWindow reloaded(nullptr, confPath);
        QVERIFY(!reloaded.animateMovesAction()->isChecked());
        QVERIFY(!reloaded.chessBoard()->moveAnimationsEnabled());
        QCOMPARE(reloaded.chessBoard()->boardTheme(), ChessBoard::BoardTheme::Green);
    }
}

QTEST_MAIN(PositionEditorDialogTest)
#include "positioneditordialog_test.moc"
