//
// Dialog showing the enriched result of a whole-game engine audit.
//

#include "analysisreportdialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// Depth range a game review is useful in: deep enough to see tactics, shallow
// enough to keep a full game analysis reasonably short.
constexpr int MinimumAuditDepth = 6;
constexpr int MaximumAuditDepth = 40;
constexpr int DefaultAuditDepth = 18;

QString formatAccuracy(double accuracy) {
    return QStringLiteral("%1%").arg(QString::number(accuracy, 'f', 1));
}

QString formatLoss(double centipawns) {
    return QCoreApplication::translate("AnalysisReportDialog", "%1 cp")
        .arg(QString::number(centipawns, 'f', 0));
}
} // namespace

AnalysisReportDialog::AnalysisReportDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Analysis report"));
    setModal(false);
    resize(760, 560);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // --- Analysis settings ------------------------------------------------
    auto *settingsGroup = new QGroupBox(tr("Analysis"), this);
    auto *settingsLayout = new QHBoxLayout(settingsGroup);
    settingsLayout->addWidget(new QLabel(tr("Depth:"), settingsGroup));

    depthSpin_ = new QSpinBox(settingsGroup);
    depthSpin_->setObjectName(QStringLiteral("auditDepthSpin"));
    depthSpin_->setRange(MinimumAuditDepth, MaximumAuditDepth);
    depthSpin_->setValue(DefaultAuditDepth);
    depthSpin_->setKeyboardTracking(false);
    depthSpin_->setToolTip(tr("Search depth used by the game analysis"));
    depthSpin_->setAccessibleName(tr("Game analysis depth"));
    settingsLayout->addWidget(depthSpin_);

    runButton_ = new QPushButton(tr("Run analysis"), settingsGroup);
    runButton_->setObjectName(QStringLiteral("runAnalysisButton"));
    settingsLayout->addWidget(runButton_);

    progressLabel_ = new QLabel(settingsGroup);
    progressLabel_->setObjectName(QStringLiteral("auditProgressLabel"));
    progressLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsLayout->addWidget(progressLabel_, 1);

    connect(depthSpin_, &QSpinBox::valueChanged, this,
            [this](int value) { emit depthChanged(value); });
    connect(runButton_, &QPushButton::clicked, this, [this] {
        if (running_) {
            emit cancelRequested();
        } else {
            emit runRequested();
        }
    });
    mainLayout->addWidget(settingsGroup);

    // --- Summary ----------------------------------------------------------
    auto *summaryGroup = new QGroupBox(tr("Summary"), this);
    auto *summaryLayout = new QVBoxLayout(summaryGroup);

    summaryLabel_ = new QLabel(summaryGroup);
    summaryLabel_->setObjectName(QStringLiteral("auditSummaryLabel"));
    summaryLabel_->setWordWrap(true);
    summaryLayout->addWidget(summaryLabel_);

    auto *statsLayout = new QFormLayout();
    statsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    whiteStatsLabel_ = new QLabel(summaryGroup);
    whiteStatsLabel_->setObjectName(QStringLiteral("auditWhiteStatsLabel"));
    blackStatsLabel_ = new QLabel(summaryGroup);
    blackStatsLabel_->setObjectName(QStringLiteral("auditBlackStatsLabel"));
    statsLayout->addRow(tr("White:"), whiteStatsLabel_);
    statsLayout->addRow(tr("Black:"), blackStatsLabel_);
    summaryLayout->addLayout(statsLayout);

    emptyStateLabel_ = new QLabel(
        tr("Run the analysis to build a report for the current game."), summaryGroup);
    emptyStateLabel_->setObjectName(QStringLiteral("auditEmptyStateLabel"));
    emptyStateLabel_->setWordWrap(true);
    summaryLayout->addWidget(emptyStateLabel_);
    mainLayout->addWidget(summaryGroup);

    // --- Flagged moves ----------------------------------------------------
    auto *findingsGroup = new QGroupBox(tr("Flagged moves"), this);
    auto *findingsLayout = new QVBoxLayout(findingsGroup);
    findingsTable_ = new QTableWidget(findingsGroup);
    findingsTable_->setObjectName(QStringLiteral("auditFindingsTable"));
    findingsTable_->setColumnCount(5);
    findingsTable_->setHorizontalHeaderLabels(
        {tr("Ply"), tr("Move"), tr("Assessment"), tr("Loss"), tr("Best move")});
    findingsTable_->verticalHeader()->setVisible(false);
    findingsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    findingsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    findingsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    findingsTable_->horizontalHeader()->setStretchLastSection(true);
    findingsTable_->setAccessibleName(tr("Moves flagged by the analysis"));
    findingsLayout->addWidget(findingsTable_);
    mainLayout->addWidget(findingsGroup, 1);

    connect(findingsTable_, &QTableWidget::cellClicked, this,
            [this](int row, int) {
                const QTableWidgetItem *item = findingsTable_->item(row, 0);
                if (item != nullptr) {
                    emit plyActivated(item->data(Qt::UserRole).toInt());
                }
            });

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(buttonBox);

    updateSummary();
    updateRunButton();
}

int AnalysisReportDialog::depth() const {
    return depthSpin_->value();
}

void AnalysisReportDialog::setDepth(int depth) {
    const QSignalBlocker blocker(depthSpin_);
    depthSpin_->setValue(depth);
}

void AnalysisReportDialog::setDepthRange(int minimum, int maximum) {
    depthSpin_->setRange(minimum, maximum);
}

void AnalysisReportDialog::setReport(const AuditReport &report) {
    report_ = report;
    updateSummary();
}

void AnalysisReportDialog::setAnalysisRunning(bool running) {
    running_ = running;
    depthSpin_->setEnabled(!running);
    updateRunButton();
    if (!running) {
        progressLabel_->clear();
    }
}

void AnalysisReportDialog::setProgress(int completedPositions, int totalPositions) {
    completedPositions_ = completedPositions;
    totalPositions_ = totalPositions;
    if (running_ && totalPositions_ > 0) {
        progressLabel_->setText(tr("Analysing %1 of %2 positions…")
                                    .arg(completedPositions_)
                                    .arg(totalPositions_));
    }
}

void AnalysisReportDialog::setAnalysisAvailable(bool available) {
    analysisAvailable_ = available;
    updateRunButton();
}

QSpinBox *AnalysisReportDialog::depthSpin() const {
    return depthSpin_;
}

QPushButton *AnalysisReportDialog::runButton() const {
    return runButton_;
}

QLabel *AnalysisReportDialog::progressLabel() const {
    return progressLabel_;
}

QLabel *AnalysisReportDialog::summaryLabel() const {
    return summaryLabel_;
}

QLabel *AnalysisReportDialog::whiteStatsLabel() const {
    return whiteStatsLabel_;
}

QLabel *AnalysisReportDialog::blackStatsLabel() const {
    return blackStatsLabel_;
}

QLabel *AnalysisReportDialog::emptyStateLabel() const {
    return emptyStateLabel_;
}

QTableWidget *AnalysisReportDialog::findingsTable() const {
    return findingsTable_;
}

QString AnalysisReportDialog::severityText(AuditSeverity severity) {
    switch (severity) {
    case AuditSeverity::Inaccuracy:
        return tr("Inaccuracy");
    case AuditSeverity::Mistake:
        return tr("Mistake");
    case AuditSeverity::Blunder:
        return tr("Blunder");
    case AuditSeverity::None:
        break;
    }
    return {};
}

void AnalysisReportDialog::updateRunButton() {
    runButton_->setText(running_ ? tr("Cancel analysis") : tr("Run analysis"));
    runButton_->setEnabled(running_ || analysisAvailable_);
    runButton_->setToolTip(
        running_ ? tr("Stop the game analysis")
                 : tr("Analyse the main line and list the mistakes it contains"));
}

void AnalysisReportDialog::updateSummary() {
    const bool hasReport = report_.valid;

    emptyStateLabel_->setVisible(!hasReport);
    summaryLabel_->setVisible(hasReport);
    whiteStatsLabel_->setVisible(hasReport);
    blackStatsLabel_->setVisible(hasReport);

    if (!hasReport) {
        summaryLabel_->clear();
        whiteStatsLabel_->clear();
        blackStatsLabel_->clear();
        findingsTable_->setRowCount(0);
        return;
    }

    summaryLabel_->setText(tr("Depth %1 — %2 positions analysed, %3 flagged moves.")
                               .arg(report_.depth)
                               .arg(report_.analysedPositions)
                               .arg(report_.findingCount()));

    const auto describe = [this](const AuditPlayerStats &stats) {
        return tr("Accuracy %1 — average loss %2\n"
                  "Inaccuracies %3 — mistakes %4 — blunders %5")
            .arg(formatAccuracy(stats.accuracy), formatLoss(stats.averageCentipawnLoss))
            .arg(stats.inaccuracies)
            .arg(stats.mistakes)
            .arg(stats.blunders);
    };
    whiteStatsLabel_->setText(describe(report_.white));
    blackStatsLabel_->setText(describe(report_.black));

    updateFindings();
}

void AnalysisReportDialog::updateFindings() {
    findingsTable_->setRowCount(report_.findings.size());
    for (int row = 0; row < report_.findings.size(); ++row) {
        const AuditFinding &finding = report_.findings.at(row);
        const QString san = finding.ply < report_.sanByPly.size()
                                ? report_.sanByPly.at(finding.ply)
                                : QString();

        auto *plyItem = new QTableWidgetItem(QString::number(finding.ply));
        plyItem->setData(Qt::UserRole, finding.ply);
        findingsTable_->setItem(row, 0, plyItem);
        findingsTable_->setItem(row, 1, new QTableWidgetItem(san));

        auto *assessmentItem = new QTableWidgetItem(severityText(finding.severity));
        if (const QColor color = PgnAnnotations::auditColor(finding.severity);
            color.isValid()) {
            assessmentItem->setForeground(color);
        }
        findingsTable_->setItem(row, 2, assessmentItem);

        const QString loss = finding.forcedMate || finding.centipawnLoss <= 0
                                 ? QStringLiteral("-")
                                 : QString::number(finding.centipawnLoss);
        findingsTable_->setItem(row, 3, new QTableWidgetItem(loss));
        findingsTable_->setItem(row, 4, new QTableWidgetItem(finding.bestMoveSan));
    }
    findingsTable_->resizeColumnsToContents();
}
