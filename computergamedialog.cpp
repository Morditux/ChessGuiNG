// Dialog for configuring a new game against the current UCI engine.

#include "computergamedialog.h"

#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

ComputerGameDialog::ComputerGameDialog(const QString &engineName,
                                       QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Play against computer"));
    setModal(true);
    setMinimumWidth(440);
    resize(520, 420);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *engineLabel = new QLabel(
        tr("The game will use the currently loaded UCI engine: %1")
            .arg(engineName.isEmpty() ? tr("UCI Engine") : engineName),
        this);
    engineLabel->setWordWrap(true);
    mainLayout->addWidget(engineLabel);

    auto *pgnGroup = new QGroupBox(tr("PGN information"), this);
    auto *pgnLayout = new QFormLayout(pgnGroup);
    pgnLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    eventEdit_ = new QLineEdit(tr("Casual Game"), pgnGroup);
    eventEdit_->setObjectName(QStringLiteral("eventEdit"));
    pgnLayout->addRow(tr("Event:"), eventEdit_);

    siteEdit_ = new QLineEdit(tr("ChessGui"), pgnGroup);
    siteEdit_->setObjectName(QStringLiteral("siteEdit"));
    pgnLayout->addRow(tr("Site:"), siteEdit_);

    dateEdit_ = new QLineEdit(
        QDate::currentDate().toString(Qt::ISODate).replace(QChar('-'), QChar('.')),
        pgnGroup);
    dateEdit_->setObjectName(QStringLiteral("dateEdit"));
    dateEdit_->setPlaceholderText(tr("YYYY.MM.DD"));
    pgnLayout->addRow(tr("Date:"), dateEdit_);

    roundEdit_ = new QLineEdit(tr("1"), pgnGroup);
    roundEdit_->setObjectName(QStringLiteral("roundEdit"));
    pgnLayout->addRow(tr("Round:"), roundEdit_);

    playerNameEdit_ = new QLineEdit(tr("Player"), pgnGroup);
    playerNameEdit_->setObjectName(QStringLiteral("playerNameEdit"));
    pgnLayout->addRow(tr("Your name:"), playerNameEdit_);

    engineNameEdit_ = new QLineEdit(engineName, pgnGroup);
    engineNameEdit_->setObjectName(QStringLiteral("engineNameEdit"));
    pgnLayout->addRow(tr("Engine name:"), engineNameEdit_);

    mainLayout->addWidget(pgnGroup);

    auto *gameGroup = new QGroupBox(tr("Game settings"), this);
    auto *gameLayout = new QFormLayout(gameGroup);
    gameLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    colorCombo_ = new QComboBox(gameGroup);
    colorCombo_->setObjectName(QStringLiteral("colorCombo"));
    colorCombo_->addItem(tr("White"));
    colorCombo_->addItem(tr("Black"));
    colorCombo_->addItem(tr("Random"));
    colorCombo_->setToolTip(tr("Choose the side controlled by you"));
    gameLayout->addRow(tr("You play:"), colorCombo_);

    timeControlCombo_ = new QComboBox(gameGroup);
    timeControlCombo_->setObjectName(QStringLiteral("timeControlCombo"));
    timeControlCombo_->addItem(tr("2 hours / player"),
                               QVariant::fromValue<qint64>(7200000));
    timeControlCombo_->addItem(tr("1 hour / player"),
                               QVariant::fromValue<qint64>(3600000));
    timeControlCombo_->addItem(tr("30 minutes / player"),
                               QVariant::fromValue<qint64>(1800000));
    timeControlCombo_->addItem(tr("15 minutes / player"),
                               QVariant::fromValue<qint64>(900000));
    timeControlCombo_->addItem(tr("Blitz — 5 minutes / player"),
                               QVariant::fromValue<qint64>(300000));
    timeControlCombo_->setCurrentIndex(0);
    gameLayout->addRow(tr("Time control:"), timeControlCombo_);

    auto *timeDescription = new QLabel(
        tr("Each player receives the selected amount of time. No increment is used."),
        gameGroup);
    timeDescription->setWordWrap(true);
    gameLayout->addRow(QString(), timeDescription);
    mainLayout->addWidget(gameGroup);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Start"));
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    eventEdit_->setFocus();
}

ComputerGameSettings ComputerGameDialog::settings() const {
    ComputerGameSettings result;
    result.event = eventEdit_->text();
    result.site = siteEdit_->text();
    result.date = dateEdit_->text();
    result.round = roundEdit_->text();
    result.playerName = playerNameEdit_->text();
    result.engineName = engineNameEdit_->text();
    result.timeLimitMilliseconds = timeControlCombo_->currentData().toLongLong();
    result.timeControl = QString::number(result.timeLimitMilliseconds / 1000);

    switch (colorCombo_->currentIndex()) {
    case 0:
        result.enginePlaysWhite = false;
        break;
    case 1:
        result.enginePlaysWhite = true;
        break;
    default:
        result.enginePlaysWhite = QRandomGenerator::global()->bounded(2) == 0;
        break;
    }

    return result;
}
