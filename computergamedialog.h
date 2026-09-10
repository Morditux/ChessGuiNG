// Dialog for configuring a new game against the current UCI engine.

#ifndef CHESSGUI_COMPUTERGAMEDIALOG_H
#define CHESSGUI_COMPUTERGAMEDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

#include "computergamesettings.h"

class QComboBox;
class QLineEdit;

class ComputerGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit ComputerGameDialog(const QString &engineName,
                                QWidget *parent = nullptr);

    [[nodiscard]] ComputerGameSettings settings() const;

private:
    QLineEdit *eventEdit_ = nullptr;
    QLineEdit *siteEdit_ = nullptr;
    QLineEdit *dateEdit_ = nullptr;
    QLineEdit *roundEdit_ = nullptr;
    QLineEdit *playerNameEdit_ = nullptr;
    QLineEdit *engineNameEdit_ = nullptr;
    QComboBox *colorCombo_ = nullptr;
    QComboBox *timeControlCombo_ = nullptr;
    QComboBox *incrementCombo_ = nullptr;
};

#endif // CHESSGUI_COMPUTERGAMEDIALOG_H
