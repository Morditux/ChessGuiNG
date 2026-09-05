//
// Dialog for editing the UCI options of a chess engine.
//

#ifndef CHESSGUI_UCIOPTIONSDIALOG_H
#define CHESSGUI_UCIOPTIONSDIALOG_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

#include "uciparser.h"

class QFormLayout;
class QWidget;

class UciOptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit UciOptionsDialog(const QString &engineName,
                              const QList<UciOption> &options,
                              const QMap<QString, QString> &values,
                              QWidget *parent = nullptr);

    [[nodiscard]] QMap<QString, QString> optionValues() const;

private:
    void addOption(const UciOption &option);
    static QString optionDescription(const UciOption &option);

    QFormLayout *optionsLayout_ = nullptr;
    QList<UciOption> options_;
    QMap<QString, QString> optionValues_;
    QMap<QString, QWidget *> optionWidgets_;
};

#endif // CHESSGUI_UCIOPTIONSDIALOG_H
