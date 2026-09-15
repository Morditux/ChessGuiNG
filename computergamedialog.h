// Dialog for configuring a new game against the current UCI engine.

#ifndef CHESSGUI_COMPUTERGAMEDIALOG_H
#define CHESSGUI_COMPUTERGAMEDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>

#include "computergamesettings.h"

class QComboBox;
class QLineEdit;
class QSpinBox;

class ComputerGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit ComputerGameDialog(const QString &engineName,
                                QWidget *parent = nullptr);

    [[nodiscard]] ComputerGameSettings settings() const;

    [[nodiscard]] QComboBox *timeControlCombo() const;
    [[nodiscard]] QComboBox *incrementCombo() const;
    [[nodiscard]] QSpinBox *customMinutesSpin() const;
    [[nodiscard]] QSpinBox *customIncrementSpin() const;

private:
    // Enables the free base time and increment fields only while the custom
    // time control is selected.
    void updateCustomTimeEnabled();

    QLineEdit *eventEdit_ = nullptr;
    QLineEdit *siteEdit_ = nullptr;
    QLineEdit *dateEdit_ = nullptr;
    QLineEdit *roundEdit_ = nullptr;
    QLineEdit *playerNameEdit_ = nullptr;
    QLineEdit *engineNameEdit_ = nullptr;
    QComboBox *colorCombo_ = nullptr;
    QComboBox *timeControlCombo_ = nullptr;
    QComboBox *incrementCombo_ = nullptr;
    QSpinBox *customMinutesSpin_ = nullptr;
    QSpinBox *customIncrementSpin_ = nullptr;
};

#endif // CHESSGUI_COMPUTERGAMEDIALOG_H
