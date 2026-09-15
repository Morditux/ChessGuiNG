//
// Dialog showing the enriched result of a whole-game engine audit: the
// per-player accuracy and mistake counts, and the list of flagged moves.
//

#ifndef CHESSGUI_ANALYSISREPORTDIALOG_H
#define CHESSGUI_ANALYSISREPORTDIALOG_H

#include <QDialog>

#include "auditreports.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

class AnalysisReportDialog : public QDialog {
    Q_OBJECT

public:
    explicit AnalysisReportDialog(QWidget *parent = nullptr);

    // Depth applied to the next analysis run, in the range the dialog offers.
    [[nodiscard]] int depth() const;
    void setDepth(int depth);
    void setDepthRange(int minimum, int maximum);

    // Displays the report, or the empty state when it is not valid.
    void setReport(const AuditReport &report);
    // Switches the run button to its cancel form while an audit is running.
    void setAnalysisRunning(bool running);
    void setProgress(int completedPositions, int totalPositions);
    // Explains why the analysis cannot be started right now.
    void setAnalysisAvailable(bool available);

    // Accessors used by the tests and by the caller to keep the state in sync.
    [[nodiscard]] QSpinBox *depthSpin() const;
    [[nodiscard]] QPushButton *runButton() const;
    [[nodiscard]] QLabel *progressLabel() const;
    [[nodiscard]] QLabel *summaryLabel() const;
    [[nodiscard]] QLabel *whiteStatsLabel() const;
    [[nodiscard]] QLabel *blackStatsLabel() const;
    [[nodiscard]] QLabel *emptyStateLabel() const;
    [[nodiscard]] QTableWidget *findingsTable() const;

signals:
    void runRequested();
    void cancelRequested();
    void depthChanged(int depth);
    // The user selected a flagged move: the caller navigates to that ply.
    void plyActivated(int ply);

private:
    // Translated name of an assessment, for the table and the summary.
    static QString severityText(AuditSeverity severity);

    void updateSummary();
    void updateFindings();
    void updateRunButton();

    AuditReport report_;
    bool running_ = false;
    bool analysisAvailable_ = false;
    int completedPositions_ = 0;
    int totalPositions_ = 0;

    QSpinBox *depthSpin_ = nullptr;
    QPushButton *runButton_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QLabel *whiteStatsLabel_ = nullptr;
    QLabel *blackStatsLabel_ = nullptr;
    QLabel *emptyStateLabel_ = nullptr;
    QTableWidget *findingsTable_ = nullptr;
};

#endif // CHESSGUI_ANALYSISREPORTDIALOG_H
