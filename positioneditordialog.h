//
// Interactive dialog for editing a chess position (board setup, flags and FEN).
//

#ifndef CHESSGUI_POSITIONEDITORDIALOG_H
#define CHESSGUI_POSITIONEDITORDIALOG_H

#include <QDialog>
#include <QString>

#include <optional>

#include "chessboard.h"
#include "rules.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace detail {
class PositionEditorBoard;
}

class PositionEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit PositionEditorDialog(
        const QString &initialFen,
        ChessBoard::BoardTheme theme = ChessBoard::BoardTheme::Classic,
        bool flipped = false,
        QWidget *parent = nullptr);
    ~PositionEditorDialog() override = default;

    [[nodiscard]] QString fen() const;
    [[nodiscard]] bool isPositionValid() const;

    void setBoardPiece(Rules::Position pos,
                       const std::optional<Rules::Piece> &piece);
    [[nodiscard]] std::optional<Rules::Piece> boardPiece(Rules::Position pos) const;
    void loadFenIntoEditor(const QString &fenText);

    [[nodiscard]] QLineEdit *fenEdit() const;
    [[nodiscard]] QLabel *validationLabel() const;
    [[nodiscard]] QPushButton *okButton() const;
    [[nodiscard]] QComboBox *enPassantCombo() const;
    [[nodiscard]] QCheckBox *whiteKingSideCheck() const;
    [[nodiscard]] QCheckBox *whiteQueenSideCheck() const;
    [[nodiscard]] QCheckBox *blackKingSideCheck() const;
    [[nodiscard]] QCheckBox *blackQueenSideCheck() const;
    [[nodiscard]] QRadioButton *whiteToMoveRadio() const;
    [[nodiscard]] QRadioButton *blackToMoveRadio() const;

private:
    void setupUi(ChessBoard::BoardTheme theme, bool flipped);
    void syncFromControls(bool updateFenField = true);
    void updateCastlingAvailability();
    void updateEnPassantOptions(const QString &preferredTarget = QString());
    [[nodiscard]] QString buildFenFromControls() const;
    [[nodiscard]] QString diagnoseInvalidPosition() const;
    void validateAndUpdateStatus(const QString &fenText);
    void applyBoardFromFen(const QString &fenText);

    detail::PositionEditorBoard *editorBoard_ = nullptr;
    QRadioButton *whiteToMoveRadio_ = nullptr;
    QRadioButton *blackToMoveRadio_ = nullptr;
    QCheckBox *whiteKingSideCheck_ = nullptr;
    QCheckBox *whiteQueenSideCheck_ = nullptr;
    QCheckBox *blackKingSideCheck_ = nullptr;
    QCheckBox *blackQueenSideCheck_ = nullptr;
    QComboBox *enPassantCombo_ = nullptr;
    QSpinBox *halfmoveSpin_ = nullptr;
    QSpinBox *fullmoveSpin_ = nullptr;
    QLineEdit *fenEdit_ = nullptr;
    QLabel *validationLabel_ = nullptr;
    QDialogButtonBox *buttonBox_ = nullptr;
    bool updating_ = false;
    bool valid_ = false;
};

#endif // CHESSGUI_POSITIONEDITORDIALOG_H
