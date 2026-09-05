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

    [[nodiscard]] bool engineAnalysisSectionVisible() const;
    void setEngineAnalysisSectionVisible(bool visible);

    [[nodiscard]] bool engineLogSectionVisible() const;
    void setEngineLogSectionVisible(bool visible);

    [[nodiscard]] bool engineDetailsVisible() const;
    void setEngineDetailsVisible(bool visible);

    [[nodiscard]] EngineDetailsPage engineDetailsPage() const;
    void setEngineDetailsPage(EngineDetailsPage page);

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
    bool engineAnalysisSectionVisible_ = false;
    bool engineLogSectionVisible_ = false;
    bool engineDetailsVisible_ = false;
    EngineDetailsPage engineDetailsPage_ = EngineDetailsPage::Variations;
};

#endif // CHESSGUI_APPCONFIG_H
