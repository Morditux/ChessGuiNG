//
// Application configuration manager for ChessGui.
//

#ifndef CHESSGUI_APPCONFIG_H
#define CHESSGUI_APPCONFIG_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QSize>
#include <QString>

#include "heuristiceval.h"

class AppConfig {
public:
    enum class EngineDetailsPage {
        Variations = 0,
        UciLog = 1
    };

    static QString defaultConfigFilePath();

    explicit AppConfig(QString filePath = defaultConfigFilePath());

    [[nodiscard]] QString filePath() const;
    void setFilePath(const QString &filePath);

    [[nodiscard]] bool exists() const;
    bool ensureConfigFileExists();

    bool load();
    bool save() const;

    [[nodiscard]] QString uciEnginePath() const;
    void setUciEnginePath(const QString &path);

    [[nodiscard]] QMap<QString, QString> uciEngineOptions() const;
    void setUciEngineOptions(const QMap<QString, QString> &options);

    [[nodiscard]] QString remoteEngineHost() const;
    void setRemoteEngineHost(const QString &host);

    [[nodiscard]] quint16 remoteEnginePort() const;
    void setRemoteEnginePort(quint16 port);

    [[nodiscard]] QString remoteEngineId() const;
    void setRemoteEngineId(const QString &id);

    [[nodiscard]] QString remoteEngineName() const;
    void setRemoteEngineName(const QString &name);

    [[nodiscard]] QString remoteEngineVersion() const;
    void setRemoteEngineVersion(const QString &version);

    [[nodiscard]] QString remoteEngineAccessKey() const;
    void setRemoteEngineAccessKey(const QString &accessKey);

    void setRemoteEngine(const QString &host, quint16 port,
                         const QString &engineId,
                         const QString &engineName = QString(),
                         const QString &engineVersion = QString(),
                         const QString &accessKey = QString());

    [[nodiscard]] QSize windowSize() const;
    void setWindowSize(const QSize &size);

    [[nodiscard]] QPoint windowPos() const;
    void setWindowPos(const QPoint &pos);

    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    [[nodiscard]] QList<int> mainSplitterSizes() const;
    void setMainSplitterSizes(const QList<int> &sizes);

    [[nodiscard]] QList<int> topSplitterSizes() const;
    void setTopSplitterSizes(const QList<int> &sizes);

    [[nodiscard]] QList<int> rightSplitterSizes() const;
    void setRightSplitterSizes(const QList<int> &sizes);

    [[nodiscard]] QList<int> rightSplitterExpandedSizes() const;
    void setRightSplitterExpandedSizes(const QList<int> &sizes);

    [[nodiscard]] bool gameInformationExpanded() const;
    void setGameInformationExpanded(bool expanded);

    [[nodiscard]] bool messageLogExpanded() const;
    void setMessageLogExpanded(bool expanded);

    [[nodiscard]] bool computerMovePreviewEnabled() const;
    void setComputerMovePreviewEnabled(bool enabled);

    [[nodiscard]] bool recommendedMovePreviewEnabled() const;
    void setRecommendedMovePreviewEnabled(bool enabled);

    [[nodiscard]] bool highlightLastMoveEnabled() const;
    void setHighlightLastMoveEnabled(bool enabled);

    [[nodiscard]] bool highlightCheckEnabled() const;
    void setHighlightCheckEnabled(bool enabled);

    [[nodiscard]] bool engineAnalysisSectionVisible() const;
    void setEngineAnalysisSectionVisible(bool visible);

    [[nodiscard]] bool engineLogSectionVisible() const;
    void setEngineLogSectionVisible(bool visible);

    [[nodiscard]] bool engineDetailsVisible() const;
    void setEngineDetailsVisible(bool visible);

    [[nodiscard]] EngineDetailsPage engineDetailsPage() const;
    void setEngineDetailsPage(EngineDetailsPage page);

    // Analysis limits applied to the next engine analysis: a depth of 0 means
    // no depth limit, and MultiPV is the number of principal variations.
    [[nodiscard]] int analysisDepth() const;
    void setAnalysisDepth(int depth);

    [[nodiscard]] int analysisMultiPv() const;
    void setAnalysisMultiPv(int multiPv);

    // Search depth used by the whole-game audit that feeds the analysis report.
    [[nodiscard]] int auditDepth() const;
    void setAuditDepth(int depth);

    // Weights of the heuristic evaluator, restored into HeuristicEval at
    // startup and written back when the settings dialog is accepted. Fields
    // missing from the file keep their calibrated default.
    [[nodiscard]] HeuristicEval::EvalParams evalParams() const;
    void setEvalParams(const HeuristicEval::EvalParams &params);

    // Root workers the heuristic search may use; zero follows the hardware.
    [[nodiscard]] int evalSearchThreads() const;
    void setEvalSearchThreads(int threads);

    // Depth of the heuristic search behind the live evaluation and the
    // whole-game curve.
    [[nodiscard]] int evalDepth() const;
    void setEvalDepth(int depth);

    // Interface preferences: the score text and the centre line of the
    // evaluation gauge.
    [[nodiscard]] bool showEvaluationScore() const;
    void setShowEvaluationScore(bool show);

    [[nodiscard]] bool showCentreLine() const;
    void setShowCentreLine(bool show);

    void resetToDefaults();

private:
    QString filePath_;
    QString uciEnginePath_;
    QMap<QString, QString> uciEngineOptions_;
    QString remoteEngineHost_;
    quint16 remoteEnginePort_ = 9000;
    QString remoteEngineId_;
    QString remoteEngineName_;
    QString remoteEngineVersion_;
    QString remoteEngineAccessKey_;
    QSize windowSize_ = QSize(800, 600);
    QPoint windowPos_ = QPoint(100, 100);
    QByteArray windowGeometry_;
    QList<int> mainSplitterSizes_ = {420, 180};
    QList<int> topSplitterSizes_ = {600, 200};
    QList<int> rightSplitterSizes_ = {140, 320, 120};
    QList<int> rightSplitterExpandedSizes_ = {140, 320, 120};
    bool gameInformationExpanded_ = false;
    bool messageLogExpanded_ = false;
    bool computerMovePreviewEnabled_ = false;
    bool recommendedMovePreviewEnabled_ = false;
    bool highlightLastMoveEnabled_ = true;
    bool highlightCheckEnabled_ = true;
    bool engineAnalysisSectionVisible_ = false;
    bool engineLogSectionVisible_ = false;
    bool engineDetailsVisible_ = false;
    EngineDetailsPage engineDetailsPage_ = EngineDetailsPage::Variations;
    int analysisDepth_ = 0;
    int analysisMultiPv_ = 1;
    int auditDepth_ = 18;
    HeuristicEval::EvalParams evalParams_;
    int evalSearchThreads_ = 0;
    int evalDepth_ = HeuristicEval::DefaultSearchDepth;
    bool showEvaluationScore_ = true;
    bool showCentreLine_ = true;
};

#endif // CHESSGUI_APPCONFIG_H
