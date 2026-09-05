//
// Dialog for configuring the selected UCI chess engine.
//

#ifndef CHESSGUI_ENGINECONFIGURATIONDIALOG_H
#define CHESSGUI_ENGINECONFIGURATIONDIALOG_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

#include "uciparser.h"

class QLineEdit;

class EngineConfigurationDialog : public QDialog {
    Q_OBJECT

public:
    explicit EngineConfigurationDialog(const QString &enginePath,
                                       const QList<UciOption> &options,
                                       const QMap<QString, QString> &values,
                                       QWidget *parent = nullptr);

    [[nodiscard]] QString enginePath() const;
    [[nodiscard]] QMap<QString, QString> optionValues() const;

private slots:
    void browseForEngine();
    void configureOptions();

private:
    QLineEdit *enginePathEdit_ = nullptr;
    QList<UciOption> options_;
    QMap<QString, QString> optionValues_;
};

#endif // CHESSGUI_ENGINECONFIGURATIONDIALOG_H
