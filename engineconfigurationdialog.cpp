//
// Dialog for configuring the selected UCI chess engine.
//

#include "engineconfigurationdialog.h"

#include "ucioptionsdialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

EngineConfigurationDialog::EngineConfigurationDialog(
    const QString &enginePath,
    const QList<UciOption> &options,
    const QMap<QString, QString> &values,
    QWidget *parent)
    : QDialog(parent)
    , options_(options)
    , optionValues_(values) {
    setWindowTitle(tr("Configure UCI Engine"));
    setModal(true);
    resize(560, 300);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *description = new QLabel(
        tr("Select the UCI engine executable. Use \"Configure…\" to adjust "
           "the engine-specific UCI options."),
        this);
    description->setWordWrap(true);
    mainLayout->addWidget(description);

    auto *engineGroup = new QGroupBox(tr("Engine"), this);
    auto *engineLayout = new QHBoxLayout(engineGroup);
    enginePathEdit_ = new QLineEdit(engineGroup);
    enginePathEdit_->setText(enginePath);
    enginePathEdit_->setPlaceholderText(tr("Path to a UCI engine executable"));
    enginePathEdit_->setClearButtonEnabled(true);
    enginePathEdit_->setAccessibleName(tr("UCI engine executable"));
    engineLayout->addWidget(enginePathEdit_, 1);

    auto *browseButton = new QPushButton(tr("Browse…"), engineGroup);
    browseButton->setAutoDefault(false);
    connect(browseButton, &QPushButton::clicked,
            this, &EngineConfigurationDialog::browseForEngine);
    engineLayout->addWidget(browseButton);
    mainLayout->addWidget(engineGroup);

    auto *optionsGroup = new QGroupBox(tr("UCI options"), this);
    auto *optionsLayout = new QVBoxLayout(optionsGroup);
    auto *optionsHint = new QLabel(
        tr("The engine provides %1 UCI option(s).").arg(options_.size()),
        optionsGroup);
    optionsHint->setWordWrap(true);
    optionsLayout->addWidget(optionsHint);

    auto *configureButton = new QPushButton(tr("Configure…"), optionsGroup);
    configureButton->setAutoDefault(false);
    configureButton->setAccessibleName(tr("Configure UCI options"));
    connect(configureButton, &QPushButton::clicked,
            this, &EngineConfigurationDialog::configureOptions);
    optionsLayout->addWidget(configureButton, 0, Qt::AlignLeft);

    mainLayout->addWidget(optionsGroup, 1);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    enginePathEdit_->setFocus();
}

QString EngineConfigurationDialog::enginePath() const {
    return enginePathEdit_ ? enginePathEdit_->text().trimmed() : QString();
}

QMap<QString, QString> EngineConfigurationDialog::optionValues() const {
    return optionValues_;
}

void EngineConfigurationDialog::browseForEngine() {
    const QString currentPath = enginePathEdit_->text().trimmed();
    const QString startDirectory = currentPath.isEmpty()
                                       ? QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)
                                       : currentPath;
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select UCI Chess Engine"),
        startDirectory,
        tr("Executables (*)"));
    if (!path.isEmpty()) {
        enginePathEdit_->setText(path);
    }
}

void EngineConfigurationDialog::configureOptions() {
    UciOptionsDialog dialog(QString(), options_, optionValues_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    optionValues_ = dialog.optionValues();
}
