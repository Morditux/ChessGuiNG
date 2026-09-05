//
// Application configuration manager for ChessGui.
//

#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

// Reads a stored splitter size list. QSettings stores QStringList natively
// in INI files; legacy files written by older versions stored a CSV string,
// which the INI reader also turns into a string list. The member default is
// kept when nothing usable is stored.
QList<int> readSizes(const QVariant &var, const QList<int> &defaultSizes) {
    if (!var.isValid()) {
        return defaultSizes;
    }

    const QStringList items = var.toStringList();
    QList<int> result;
    result.reserve(items.size());
    for (const QString &item : items) {
        bool ok = false;
        const int value = item.trimmed().toInt(&ok);
        if (ok) {
            result.append(value);
        }
    }

    return result.isEmpty() ? defaultSizes : result;
}

QStringList sizesToStringList(const QList<int> &sizes) {
    QStringList items;
    items.reserve(sizes.size());
    for (int size : sizes) {
        items.append(QString::number(size));
    }
    return items;
}

} // namespace

QString AppConfig::defaultConfigFilePath() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                    QStringLiteral("/ChessGui");
    }
    return QDir(configDir).filePath(QStringLiteral("chessGui.conf"));
}

AppConfig::AppConfig(QString filePath)
    : filePath_(std::move(filePath)) {
    if (filePath_.isEmpty()) {
        filePath_ = defaultConfigFilePath();
    }
}

QString AppConfig::filePath() const {
    return filePath_;
}

void AppConfig::setFilePath(const QString &filePath) {
    filePath_ = filePath.isEmpty() ? defaultConfigFilePath() : filePath;
}

bool AppConfig::exists() const {
    return QFile::exists(filePath_);
}

bool AppConfig::ensureConfigFileExists() {
    if (exists()) {
        return true;
    }
    return save();
}

bool AppConfig::load() {
    if (!exists()) {
        return false;
    }

    QSettings settings(filePath_, QSettings::IniFormat);

    settings.beginGroup(QStringLiteral("Engine"));
    uciEnginePath_ = settings.value(QStringLiteral("uciEnginePath"), uciEnginePath_).toString();
    settings.endGroup();

    uciEngineOptions_.clear();
    settings.beginGroup(QStringLiteral("EngineOptions"));
    for (const QString &key : settings.childKeys()) {
        uciEngineOptions_.insert(key, settings.value(key).toString());
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("RemoteEngine"));
    remoteEngineHost_ = settings.value(QStringLiteral("host"), remoteEngineHost_).toString();
    remoteEnginePort_ = static_cast<quint16>(
        settings.value(QStringLiteral("port"), remoteEnginePort_).toUInt());
    remoteEngineId_ = settings.value(QStringLiteral("engineId"), remoteEngineId_).toString();
    remoteEngineName_ = settings.value(QStringLiteral("engineName"), remoteEngineName_).toString();
    remoteEngineVersion_ = settings.value(QStringLiteral("engineVersion"), remoteEngineVersion_).toString();
    remoteEngineAccessKey_ = settings.value(QStringLiteral("accessKey"), remoteEngineAccessKey_).toString();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("MainWindow"));
    const int x = settings.value(QStringLiteral("x"), windowPos_.x()).toInt();
    const int y = settings.value(QStringLiteral("y"), windowPos_.y()).toInt();
    const int w = settings.value(QStringLiteral("width"), windowSize_.width()).toInt();
    const int h = settings.value(QStringLiteral("height"), windowSize_.height()).toInt();
    windowPos_ = QPoint(x, y);
    windowSize_ = QSize(w, h);
    windowGeometry_ = settings.value(QStringLiteral("geometry"), windowGeometry_).toByteArray();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Splitters"));
    mainSplitterSizes_ = readSizes(settings.value(QStringLiteral("mainSplitter")), mainSplitterSizes_);
    topSplitterSizes_ = readSizes(settings.value(QStringLiteral("topSplitter")), topSplitterSizes_);
    rightSplitterSizes_ = readSizes(settings.value(QStringLiteral("rightSplitter")), rightSplitterSizes_);
    rightSplitterExpandedSizes_ = readSizes(
        settings.value(QStringLiteral("rightSplitterExpanded")), rightSplitterSizes_);
    if (rightSplitterExpandedSizes_.size() != 3) {
        rightSplitterExpandedSizes_ = {140, 320, 120};
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("GameHistory"));
    gameInformationExpanded_ = settings.value(QStringLiteral("informationExpanded"), false).toBool();
    messageLogExpanded_ = settings.value(QStringLiteral("logExpanded"), false).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("MovePreviews"));
    computerMovePreviewEnabled_ = settings.value(
        QStringLiteral("computerMove"), computerMovePreviewEnabled_).toBool();
    recommendedMovePreviewEnabled_ = settings.value(
        QStringLiteral("recommendedMove"), recommendedMovePreviewEnabled_).toBool();
    highlightLastMoveEnabled_ = settings.value(
        QStringLiteral("lastMoveHighlight"), highlightLastMoveEnabled_).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("EngineOutput"));
    const bool hasDetailsState = settings.contains(QStringLiteral("detailsVisible"));
    if (hasDetailsState) {
        engineDetailsVisible_ = settings.value(
            QStringLiteral("detailsVisible"), engineDetailsVisible_).toBool();
        const int page = settings.value(QStringLiteral("detailsPage"),
                                        static_cast<int>(engineDetailsPage_)).toInt();
        engineDetailsPage_ = page == static_cast<int>(EngineDetailsPage::UciLog)
                                 ? EngineDetailsPage::UciLog
                                 : EngineDetailsPage::Variations;
        engineAnalysisSectionVisible_ = engineDetailsVisible_ &&
                                        engineDetailsPage_ == EngineDetailsPage::Variations;
        engineLogSectionVisible_ = engineDetailsVisible_ &&
                                   engineDetailsPage_ == EngineDetailsPage::UciLog;
    } else {
        // Migrate the former independent collapsible sections. If both were
        // open, keep the analysis page as the useful default detail view.
        engineAnalysisSectionVisible_ = settings.value(
            QStringLiteral("analysisSection"), engineAnalysisSectionVisible_).toBool();
        engineLogSectionVisible_ = settings.value(
            QStringLiteral("logSection"), engineLogSectionVisible_).toBool();
        engineDetailsVisible_ = engineAnalysisSectionVisible_ || engineLogSectionVisible_;
        engineDetailsPage_ = !engineAnalysisSectionVisible_ && engineLogSectionVisible_
                                 ? EngineDetailsPage::UciLog
                                 : EngineDetailsPage::Variations;
    }
    settings.endGroup();

    return settings.status() == QSettings::NoError;
}

bool AppConfig::save() const {
    const QFileInfo fileInfo(filePath_);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            return false;
        }
    }

    QSettings settings(filePath_, QSettings::IniFormat);

    settings.beginGroup(QStringLiteral("Engine"));
    settings.setValue(QStringLiteral("uciEnginePath"), uciEnginePath_);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("EngineOptions"));
    settings.remove(QString());
    for (auto it = uciEngineOptions_.cbegin(); it != uciEngineOptions_.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("RemoteEngine"));
    settings.setValue(QStringLiteral("host"), remoteEngineHost_);
    settings.setValue(QStringLiteral("port"), remoteEnginePort_);
    settings.setValue(QStringLiteral("engineId"), remoteEngineId_);
    settings.setValue(QStringLiteral("engineName"), remoteEngineName_);
    settings.setValue(QStringLiteral("engineVersion"), remoteEngineVersion_);
    settings.setValue(QStringLiteral("accessKey"), remoteEngineAccessKey_);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("x"), windowPos_.x());
    settings.setValue(QStringLiteral("y"), windowPos_.y());
    settings.setValue(QStringLiteral("width"), windowSize_.width());
    settings.setValue(QStringLiteral("height"), windowSize_.height());
    if (!windowGeometry_.isEmpty()) {
        settings.setValue(QStringLiteral("geometry"), windowGeometry_);
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Splitters"));
    settings.setValue(QStringLiteral("mainSplitter"), sizesToStringList(mainSplitterSizes_));
    settings.setValue(QStringLiteral("topSplitter"), sizesToStringList(topSplitterSizes_));
    settings.setValue(QStringLiteral("rightSplitter"), sizesToStringList(rightSplitterSizes_));
    settings.setValue(QStringLiteral("rightSplitterExpanded"), sizesToStringList(rightSplitterExpandedSizes_));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("GameHistory"));
    settings.setValue(QStringLiteral("informationExpanded"), gameInformationExpanded_);
    settings.setValue(QStringLiteral("logExpanded"), messageLogExpanded_);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("MovePreviews"));
    settings.setValue(QStringLiteral("computerMove"), computerMovePreviewEnabled_);
    settings.setValue(QStringLiteral("recommendedMove"), recommendedMovePreviewEnabled_);
    settings.setValue(QStringLiteral("lastMoveHighlight"), highlightLastMoveEnabled_);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("EngineOutput"));
    settings.setValue(QStringLiteral("detailsVisible"), engineDetailsVisible_);
    settings.setValue(QStringLiteral("detailsPage"), static_cast<int>(engineDetailsPage_));
    // Keep the old keys for older ChessGui versions and external tooling.
    settings.setValue(QStringLiteral("analysisSection"),
                      engineDetailsVisible_ && engineDetailsPage_ == EngineDetailsPage::Variations);
    settings.setValue(QStringLiteral("logSection"),
                      engineDetailsVisible_ && engineDetailsPage_ == EngineDetailsPage::UciLog);
    settings.endGroup();

    settings.sync();
    return settings.status() == QSettings::NoError;
}

QString AppConfig::uciEnginePath() const {
    return uciEnginePath_;
}

void AppConfig::setUciEnginePath(const QString &path) {
    uciEnginePath_ = path;
}

QMap<QString, QString> AppConfig::uciEngineOptions() const {
    return uciEngineOptions_;
}

void AppConfig::setUciEngineOptions(const QMap<QString, QString> &options) {
    uciEngineOptions_ = options;
}

QString AppConfig::remoteEngineHost() const {
    return remoteEngineHost_;
}

void AppConfig::setRemoteEngineHost(const QString &host) {
    remoteEngineHost_ = host;
}

quint16 AppConfig::remoteEnginePort() const {
    return remoteEnginePort_;
}

void AppConfig::setRemoteEnginePort(quint16 port) {
    remoteEnginePort_ = port;
}

QString AppConfig::remoteEngineId() const {
    return remoteEngineId_;
}

void AppConfig::setRemoteEngineId(const QString &id) {
    remoteEngineId_ = id;
}

QString AppConfig::remoteEngineName() const {
    return remoteEngineName_;
}

void AppConfig::setRemoteEngineName(const QString &name) {
    remoteEngineName_ = name;
}

QString AppConfig::remoteEngineVersion() const {
    return remoteEngineVersion_;
}

void AppConfig::setRemoteEngineVersion(const QString &version) {
    remoteEngineVersion_ = version;
}

QString AppConfig::remoteEngineAccessKey() const {
    return remoteEngineAccessKey_;
}

void AppConfig::setRemoteEngineAccessKey(const QString &accessKey) {
    remoteEngineAccessKey_ = accessKey;
}

void AppConfig::setRemoteEngine(const QString &host, quint16 port,
                                const QString &engineId,
                                const QString &engineName,
                                const QString &engineVersion,
                                const QString &accessKey) {
    remoteEngineHost_ = host;
    remoteEnginePort_ = port;
    remoteEngineId_ = engineId;
    remoteEngineName_ = engineName;
    remoteEngineVersion_ = engineVersion;
    remoteEngineAccessKey_ = accessKey;
}

QSize AppConfig::windowSize() const {
    return windowSize_;
}

void AppConfig::setWindowSize(const QSize &size) {
    windowSize_ = size;
}

QPoint AppConfig::windowPos() const {
    return windowPos_;
}

void AppConfig::setWindowPos(const QPoint &pos) {
    windowPos_ = pos;
}

QByteArray AppConfig::windowGeometry() const {
    return windowGeometry_;
}

void AppConfig::setWindowGeometry(const QByteArray &geometry) {
    windowGeometry_ = geometry;
}

QList<int> AppConfig::mainSplitterSizes() const {
    return mainSplitterSizes_;
}

void AppConfig::setMainSplitterSizes(const QList<int> &sizes) {
    mainSplitterSizes_ = sizes;
}

QList<int> AppConfig::topSplitterSizes() const {
    return topSplitterSizes_;
}

void AppConfig::setTopSplitterSizes(const QList<int> &sizes) {
    topSplitterSizes_ = sizes;
}

QList<int> AppConfig::rightSplitterSizes() const {
    return rightSplitterSizes_;
}

void AppConfig::setRightSplitterSizes(const QList<int> &sizes) {
    rightSplitterSizes_ = sizes;
}

QList<int> AppConfig::rightSplitterExpandedSizes() const {
    return rightSplitterExpandedSizes_;
}

void AppConfig::setRightSplitterExpandedSizes(const QList<int> &sizes) {
    if (sizes.size() == 3) {
        rightSplitterExpandedSizes_ = sizes;
    }
}

bool AppConfig::gameInformationExpanded() const {
    return gameInformationExpanded_;
}

void AppConfig::setGameInformationExpanded(bool expanded) {
    gameInformationExpanded_ = expanded;
}

bool AppConfig::messageLogExpanded() const {
    return messageLogExpanded_;
}

void AppConfig::setMessageLogExpanded(bool expanded) {
    messageLogExpanded_ = expanded;
}

bool AppConfig::computerMovePreviewEnabled() const {
    return computerMovePreviewEnabled_;
}

void AppConfig::setComputerMovePreviewEnabled(bool enabled) {
    computerMovePreviewEnabled_ = enabled;
}

bool AppConfig::recommendedMovePreviewEnabled() const {
    return recommendedMovePreviewEnabled_;
}

void AppConfig::setRecommendedMovePreviewEnabled(bool enabled) {
    recommendedMovePreviewEnabled_ = enabled;
}

bool AppConfig::highlightLastMoveEnabled() const {
    return highlightLastMoveEnabled_;
}

void AppConfig::setHighlightLastMoveEnabled(bool enabled) {
    highlightLastMoveEnabled_ = enabled;
}

bool AppConfig::engineAnalysisSectionVisible() const {
    return engineAnalysisSectionVisible_;
}

void AppConfig::setEngineAnalysisSectionVisible(bool visible) {
    engineAnalysisSectionVisible_ = visible;
    if (visible) {
        engineDetailsVisible_ = true;
        engineDetailsPage_ = EngineDetailsPage::Variations;
        engineLogSectionVisible_ = false;
    } else if (engineDetailsPage_ == EngineDetailsPage::Variations) {
        engineDetailsVisible_ = false;
    }
}

bool AppConfig::engineLogSectionVisible() const {
    return engineLogSectionVisible_;
}

void AppConfig::setEngineLogSectionVisible(bool visible) {
    engineLogSectionVisible_ = visible;
    if (visible) {
        engineDetailsVisible_ = true;
        engineDetailsPage_ = EngineDetailsPage::UciLog;
        engineAnalysisSectionVisible_ = false;
    } else if (engineDetailsPage_ == EngineDetailsPage::UciLog) {
        engineDetailsVisible_ = false;
    }
}

bool AppConfig::engineDetailsVisible() const {
    return engineDetailsVisible_;
}

void AppConfig::setEngineDetailsVisible(bool visible) {
    engineDetailsVisible_ = visible;
    engineAnalysisSectionVisible_ = visible &&
                                    engineDetailsPage_ == EngineDetailsPage::Variations;
    engineLogSectionVisible_ = visible &&
                               engineDetailsPage_ == EngineDetailsPage::UciLog;
}

AppConfig::EngineDetailsPage AppConfig::engineDetailsPage() const {
    return engineDetailsPage_;
}

void AppConfig::setEngineDetailsPage(EngineDetailsPage page) {
    engineDetailsPage_ = page == EngineDetailsPage::UciLog
                             ? EngineDetailsPage::UciLog
                             : EngineDetailsPage::Variations;
    engineAnalysisSectionVisible_ = engineDetailsVisible_ &&
                                    engineDetailsPage_ == EngineDetailsPage::Variations;
    engineLogSectionVisible_ = engineDetailsVisible_ &&
                               engineDetailsPage_ == EngineDetailsPage::UciLog;
}

void AppConfig::resetToDefaults() {
    uciEnginePath_.clear();
    uciEngineOptions_.clear();
    remoteEngineHost_.clear();
    remoteEnginePort_ = 9000;
    remoteEngineId_.clear();
    remoteEngineName_.clear();
    remoteEngineVersion_.clear();
    remoteEngineAccessKey_.clear();
    windowSize_ = QSize(800, 600);
    windowPos_ = QPoint(100, 100);
    windowGeometry_.clear();
    mainSplitterSizes_ = {420, 180};
    topSplitterSizes_ = {600, 200};
    rightSplitterSizes_ = {140, 320, 120};
    rightSplitterExpandedSizes_ = {140, 320, 120};
    gameInformationExpanded_ = false;
    messageLogExpanded_ = false;
    computerMovePreviewEnabled_ = false;
    recommendedMovePreviewEnabled_ = false;
    highlightLastMoveEnabled_ = true;
    engineAnalysisSectionVisible_ = false;
    engineLogSectionVisible_ = false;
    engineDetailsVisible_ = false;
    engineDetailsPage_ = EngineDetailsPage::Variations;
}
