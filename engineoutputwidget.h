//
// Widget for displaying chess engine (UCI) analysis output.
//

#ifndef CHESSGUI_ENGINEOUTPUTWIDGET_H
#define CHESSGUI_ENGINEOUTPUTWIDGET_H

#include <QList>
#include <QString>
#include <QWidget>
#include "uciparser.h"

class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;

class EngineOutputWidget : public QWidget {
    Q_OBJECT

public:
    enum class DetailsPage {
        Variations = 0,
        UciLog = 1
    };
    Q_ENUM(DetailsPage)

    explicit EngineOutputWidget(QWidget *parent = nullptr);
    ~EngineOutputWidget() override = default;

    [[nodiscard]] QString engineName() const;
    [[nodiscard]] QString engineStatus() const;
    [[nodiscard]] int depth() const;
    [[nodiscard]] int seldepth() const;
    [[nodiscard]] QString scoreText() const;
    [[nodiscard]] qint64 nodes() const;
    [[nodiscard]] qint64 nodesPerSecond() const;
    [[nodiscard]] qint64 time() const;
    [[nodiscard]] QString bestMove() const;
    [[nodiscard]] QString uciLog() const;
    [[nodiscard]] QList<EngineAnalysisLine> analysisLines() const;

    [[nodiscard]] QPushButton *startButton() const;
    [[nodiscard]] QPushButton *pauseButton() const;
    [[nodiscard]] QPushButton *stopButton() const;

    [[nodiscard]] bool analysisSectionVisible() const;
    [[nodiscard]] bool logSectionVisible() const;
    [[nodiscard]] bool detailsVisible() const;
    [[nodiscard]] DetailsPage detailsPage() const;
    [[nodiscard]] QToolButton *detailsToggleButton() const;
    [[nodiscard]] QToolButton *analysisToggleButton() const;
    [[nodiscard]] QToolButton *logToggleButton() const;
    [[nodiscard]] QTableWidget *pvTable() const;
    [[nodiscard]] QPlainTextEdit *logEdit() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

public slots:
    void setEngineName(const QString &name);
    void setEngineStatus(const QString &status);
    void setDepth(int depth, int seldepth = 0);
    void setScore(double scoreCp, std::optional<int> mateIn = std::nullopt);
    void setScoreText(const QString &scoreText);
    void setNodes(qint64 nodes);
    void setNodesPerSecond(qint64 nps);
    void setTime(qint64 timeMs);
    void setBestMove(const QString &bestMove);
    void setControlButtonsEnabled(bool startEnabled, bool pauseEnabled, bool stopEnabled);

    void updateAnalysisLine(const EngineAnalysisLine &line);
    void setAnalysisLines(const QList<EngineAnalysisLine> &lines);
    void appendUciLog(const QString &line);
    void clearLog();
    void clearAnalysis();
    void clear();
    void setAnalysisSectionVisible(bool visible);
    void setLogSectionVisible(bool visible);
    void setDetailsVisible(bool visible);
    void setDetailsPage(DetailsPage page);

signals:
    void lineSelected(int multipv, const QString &pv);
    void startClicked();
    void pauseClicked();
    void stopClicked();
    void analysisSectionToggled(bool visible);
    void logSectionToggled(bool visible);
    void detailsToggled(bool visible);
    void detailsPageChanged(DetailsPage page);

private:
    void setupUi();
    void updateHeaderSummary();
    void updateTableDisplay();
    [[nodiscard]] int desiredHeight() const;
    static QString formatScore(double scoreCp, std::optional<int> mateIn);
    static QString formatNodes(qint64 nodes);
    static QString formatNps(qint64 nps);
    static QString formatTime(qint64 timeMs);

    QString engineName_ = QStringLiteral("UCI Engine");
    QString engineStatus_ = QStringLiteral("Disconnected");
    int depth_ = 0;
    int seldepth_ = 0;
    QString scoreText_ = QStringLiteral("-");
    qint64 nodes_ = 0;
    qint64 nps_ = 0;
    qint64 timeMs_ = 0;
    QString bestMove_ = QStringLiteral("-");
    QList<EngineAnalysisLine> analysisLines_;
    bool analysisSectionVisible_ = false;
    bool logSectionVisible_ = false;
    bool detailsVisible_ = false;
    DetailsPage detailsPage_ = DetailsPage::Variations;

    // UI elements
    QFrame *headerFrame_ = nullptr;
    QLabel *nameLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *scoreLabel_ = nullptr;
    QLabel *depthLabel_ = nullptr;
    QLabel *nodesLabel_ = nullptr;
    QLabel *npsLabel_ = nullptr;
    QLabel *timeLabel_ = nullptr;
    QLabel *bestMoveLabel_ = nullptr;
    QLabel *primaryPvLabel_ = nullptr;

    QPushButton *startButton_ = nullptr;
    QPushButton *pauseButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;

    QToolButton *analysisToggleButton_ = nullptr;
    QToolButton *logToggleButton_ = nullptr;
    QToolButton *detailsToggleButton_ = nullptr;
    QWidget *analysisSection_ = nullptr;
    QWidget *logSection_ = nullptr;
    QStackedWidget *detailsStack_ = nullptr;
    QTableWidget *pvTable_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;
    QPushButton *clearLogButton_ = nullptr;
};

#endif // CHESSGUI_ENGINEOUTPUTWIDGET_H
