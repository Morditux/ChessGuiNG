//
// Dialog for editing the weights and the search limits of the heuristic
// evaluator.
//

#include "evalparamsdialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// The label of a weight, with the phase it belongs to appended when the key
// names one. The base label and the group come from the shared field table and
// are looked up in its own translation context.
QString weightLabel(const EvalParamFields::Field &field) {
    const QString base =
        QCoreApplication::translate("EvalParams", field.label);
    const QString key = QString::fromLatin1(field.key);
    if (key.endsWith(QLatin1String("MG"))) {
        return QCoreApplication::translate("EvalParamsDialog", "%1 (middlegame)")
            .arg(base);
    }
    if (key.endsWith(QLatin1String("EG"))) {
        return QCoreApplication::translate("EvalParamsDialog", "%1 (endgame)")
            .arg(base);
    }
    return base;
}

} // namespace

EvalParamsDialog::EvalParamsDialog(const HeuristicEval::EvalParams &params,
                                   int searchThreads, int evaluationDepth,
                                   int defaultEvaluationDepth, QWidget *parent)
    : QDialog(parent)
    , defaultDepth_(defaultEvaluationDepth) {
    setWindowTitle(tr("Evaluation parameters"));
    setModal(true);
    resize(520, 640);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *description = new QLabel(
        tr("Weights are in centipawns (a pawn is 100). Every term carries a "
           "middlegame and an endgame value and the evaluator interpolates "
           "between them with the phase. Changes apply to the live evaluation, "
           "the evaluation curve and the move hints, and are remembered between "
           "sessions."),
        this);
    description->setWordWrap(true);
    mainLayout->addWidget(description);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *fieldsWidget = new QWidget(scrollArea);
    auto *fieldsLayout = new QVBoxLayout(fieldsWidget);
    fieldsLayout->setContentsMargins(4, 4, 4, 4);
    fieldsLayout->setSpacing(10);

    // The search limits come first: they change how the weights are used and
    // are the two knobs that most often need touching.
    auto *searchBox = new QGroupBox(tr("Search"), fieldsWidget);
    auto *searchForm = new QFormLayout(searchBox);
    searchForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    threadsSpin_ = new QSpinBox(searchBox);
    threadsSpin_->setObjectName(QStringLiteral("evalThreadsSpin"));
    threadsSpin_->setRange(0, 16);
    threadsSpin_->setSpecialValueText(tr("Automatic"));
    threadsSpin_->setValue(qBound(0, searchThreads, 16));
    threadsSpin_->setToolTip(
        tr("Root workers the heuristic search may use. Automatic follows the "
           "hardware; one makes a search deterministic."));
    searchForm->addRow(tr("Threads:"), threadsSpin_);

    depthSpin_ = new QSpinBox(searchBox);
    depthSpin_->setObjectName(QStringLiteral("evalDepthSpin"));
    depthSpin_->setRange(1, HeuristicEval::MaxSearchDepth);
    depthSpin_->setValue(
        qBound(1, evaluationDepth, HeuristicEval::MaxSearchDepth));
    depthSpin_->setToolTip(
        tr("Depth of the heuristic search behind the live evaluation and the "
           "whole-game curve. Deeper searches see more tactics and make the "
           "curve slower."));
    searchForm->addRow(tr("Evaluation depth:"), depthSpin_);
    fieldsLayout->addWidget(searchBox);

    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        const QString group =
            QCoreApplication::translate("EvalParams", field.group);
        QFormLayout *form = groupForm(group, fieldsWidget);

        auto *spin = new QSpinBox(fieldsWidget);
        spin->setRange(field.minimum, field.maximum);
        spin->setValue(params.*field.member);
        spin->setObjectName(QStringLiteral("evalParam_%1")
                                .arg(QString::fromLatin1(field.key)));
        spin->setToolTip(tr("Default: %1")
                             .arg(HeuristicEval::EvalParams{}.*field.member));
        form->addRow(weightLabel(field), spin);
        fields_.insert(QString::fromLatin1(field.key), spin);
    }
    fieldsLayout->addStretch(1);

    scrollArea->setWidget(fieldsWidget);
    mainLayout->addWidget(scrollArea, 1);

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
            QDialogButtonBox::RestoreDefaults,
        this);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    QPushButton *restoreButton =
        buttonBox->button(QDialogButtonBox::RestoreDefaults);
    restoreButton->setObjectName(QStringLiteral("restoreDefaultsButton"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(restoreButton, &QPushButton::clicked, this,
            &EvalParamsDialog::restoreDefaults);
    mainLayout->addWidget(buttonBox);
}

QFormLayout *EvalParamsDialog::groupForm(const QString &group,
                                         QWidget *container) {
    QFormLayout *form = groups_.value(group, nullptr);
    if (form != nullptr) {
        return form;
    }

    auto *box = new QGroupBox(group, container);
    form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    qobject_cast<QVBoxLayout *>(container->layout())->addWidget(box);
    groups_.insert(group, form);
    return form;
}

HeuristicEval::EvalParams EvalParamsDialog::params() const {
    HeuristicEval::EvalParams result;
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        const QSpinBox *spin =
            fields_.value(QString::fromLatin1(field.key), nullptr);
        if (spin != nullptr) {
            result.*field.member = spin->value();
        }
    }
    return result;
}

int EvalParamsDialog::searchThreads() const {
    return threadsSpin_ != nullptr ? threadsSpin_->value() : 0;
}

int EvalParamsDialog::evaluationDepth() const {
    return depthSpin_ != nullptr ? depthSpin_->value() : defaultDepth_;
}

void EvalParamsDialog::restoreDefaults() {
    const HeuristicEval::EvalParams defaults;
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        QSpinBox *spin = fields_.value(QString::fromLatin1(field.key), nullptr);
        if (spin != nullptr) {
            spin->setValue(defaults.*field.member);
        }
    }
    if (threadsSpin_ != nullptr) {
        threadsSpin_->setValue(0);
    }
    if (depthSpin_ != nullptr) {
        depthSpin_->setValue(defaultDepth_);
    }
}
