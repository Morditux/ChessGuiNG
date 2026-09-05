//
// Dialog for selecting one game from a PGN file that contains several games.
//

#ifndef CHESSGUI_PGNSELECTDIALOG_H
#define CHESSGUI_PGNSELECTDIALOG_H

#include <QDialog>
#include <QStringList>

class QDialogButtonBox;
class QTableWidget;

class PgnSelectDialog : public QDialog {
    Q_OBJECT

public:
    explicit PgnSelectDialog(const QStringList &games, QWidget *parent = nullptr);

    [[nodiscard]] QString selectedGame() const;

private slots:
    void onSelectionChanged();

private:
    QTableWidget *gameTable_ = nullptr;
    QDialogButtonBox *buttonBox_ = nullptr;
    QStringList games_;
};

#endif // CHESSGUI_PGNSELECTDIALOG_H
