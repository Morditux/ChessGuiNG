//
// Dialog for editing the UCI options of a chess engine.
//

#include "ucioptionsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

UciOptionsDialog::UciOptionsDialog(const QString &engineName,
                                   const QList<UciOption> &options,
                                   const QMap<QString, QString> &values,
                                   QWidget *parent)
    : QDialog(parent)
    , options_(options)
    , optionValues_(values) {
    setWindowTitle(tr("UCI Options"));
    setModal(true);
    resize(460, 420);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    if (!engineName.isEmpty()) {
        auto *engineLabel = new QLabel(
            tr("Engine: %1").arg(engineName), this);
        engineLabel->setWordWrap(true);
        mainLayout->addWidget(engineLabel);
    }

    auto *description = new QLabel(
        tr("Set the options the engine currently provides. Changes are sent "
           "to the engine when this dialog is accepted."),
        this);
    description->setWordWrap(true);
    mainLayout->addWidget(description);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *optionsWidget = new QWidget(scrollArea);
    optionsLayout_ = new QFormLayout(optionsWidget);
    optionsLayout_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    optionsLayout_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    optionsLayout_->setFormAlignment(Qt::AlignTop);
    optionsLayout_->setContentsMargins(4, 4, 4, 4);

    if (options_.isEmpty()) {
        auto *message = new QLabel(
            tr("No UCI options are available. Load an engine first to see its options."),
            optionsWidget);
        message->setWordWrap(true);
        message->setStyleSheet(tr("color: palette(placeholder-text);"));
        optionsLayout_->addRow(message);
    } else {
        for (const UciOption &option : options_) {
            addOption(option);
        }
    }

    scrollArea->setWidget(optionsWidget);
    mainLayout->addWidget(scrollArea, 1);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

QMap<QString, QString> UciOptionsDialog::optionValues() const {
    QMap<QString, QString> result = optionValues_;

    for (const UciOption &option : options_) {
        if (option.type == UciOption::Type::Button) {
            result.remove(option.name);
            continue;
        }

        QWidget *widget = optionWidgets_.value(option.name, nullptr);
        if (widget == nullptr) {
            continue;
        }

        switch (option.type) {
            case UciOption::Type::Check: {
                const auto *checkBox = qobject_cast<const QCheckBox *>(widget);
                result[option.name] = checkBox->isChecked()
                                           ? tr("true")
                                           : tr("false");
                break;
            }
            case UciOption::Type::Spin: {
                const auto *spinBox = qobject_cast<const QSpinBox *>(widget);
                result[option.name] = QString::number(spinBox->value());
                break;
            }
            case UciOption::Type::Combo: {
                const auto *comboBox = qobject_cast<const QComboBox *>(widget);
                result[option.name] = comboBox->currentText();
                break;
            }
            case UciOption::Type::String:
            case UciOption::Type::Unknown: {
                const auto *lineEdit = qobject_cast<const QLineEdit *>(widget);
                result[option.name] = lineEdit->text();
                break;
            }
            case UciOption::Type::Button:
                break;
        }
    }

    return result;
}

void UciOptionsDialog::addOption(const UciOption &option) {
    if (option.type == UciOption::Type::Button) {
        auto *message = new QLabel(
            tr("%1 is a command and has no persistent value.").arg(option.name),
            this);
        message->setWordWrap(true);
        optionsLayout_->addRow(message);
        return;
    }

    QWidget *widget = nullptr;
    const QString savedValue = optionValues_.value(option.name, option.defaultValue);

    switch (option.type) {
        case UciOption::Type::Check: {
            auto *checkBox = new QCheckBox(this);
            checkBox->setChecked(savedValue.compare(tr("true"), Qt::CaseInsensitive) == 0 ||
                                 savedValue == tr("1"));
            widget = checkBox;
            break;
        }
        case UciOption::Type::Spin: {
            auto *spinBox = new QSpinBox(this);
            bool minOk = false;
            bool maxOk = false;
            const int minValue = option.minValue.toInt(&minOk);
            const int maxValue = option.maxValue.toInt(&maxOk);
            spinBox->setRange(minOk ? minValue : -2147483647, maxOk ? maxValue : 2147483647);
            bool valueOk = false;
            const int value = savedValue.toInt(&valueOk);
            if (valueOk) {
                spinBox->setValue(value);
            } else {
                bool defaultOk = false;
                const int defaultValue = option.defaultValue.toInt(&defaultOk);
                if (defaultOk) {
                    spinBox->setValue(defaultValue);
                }
            }
            widget = spinBox;
            break;
        }
        case UciOption::Type::Combo: {
            auto *comboBox = new QComboBox(this);
            comboBox->addItems(option.vars);
            const int savedIndex = comboBox->findText(savedValue);
            if (savedIndex >= 0) {
                comboBox->setCurrentIndex(savedIndex);
            } else if (!savedValue.isEmpty()) {
                comboBox->addItem(savedValue);
                comboBox->setCurrentText(savedValue);
            }
            widget = comboBox;
            break;
        }
        case UciOption::Type::String:
        case UciOption::Type::Unknown: {
            auto *lineEdit = new QLineEdit(this);
            lineEdit->setText(savedValue);
            widget = lineEdit;
            break;
        }
        case UciOption::Type::Button:
            break;
    }

    if (widget == nullptr) {
        return;
    }

    widget->setAccessibleName(option.name);
    widget->setToolTip(optionDescription(option));
    optionWidgets_.insert(option.name, widget);
    optionsLayout_->addRow(option.name + tr(":"), widget);
}

QString UciOptionsDialog::optionDescription(const UciOption &option) {
    QString description = option.name;
    if (!option.defaultValue.isEmpty()) {
        description += tr(" — default: ") + option.defaultValue;
    }
    if (!option.minValue.isEmpty() || !option.maxValue.isEmpty()) {
        description += tr(" — range: %1…%2")
                           .arg(option.minValue.isEmpty() ? tr("-") : option.minValue,
                                option.maxValue.isEmpty() ? tr("+") : option.maxValue);
    }
    return description;
}
