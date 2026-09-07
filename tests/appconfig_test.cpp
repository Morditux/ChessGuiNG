//
// Unit tests for AppConfig and application configuration management.
//

#include <QSignalSpy>
#include <QTest>
#include <QDir>
#include <QFile>
#include <QSplitter>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QLabel>
#include <QAction>
#include <QScopeGuard>
#include <QScreen>
#include "movelistwidget.h"
#include "gamecontroller.h"

#include "appconfig.h"
#include "engineoutputwidget.h"
#include "mainwindow.h"
#include "uciengine.h"

class AppConfigTest : public QObject {
    Q_OBJECT

private slots:
    void testHistoryPreferences();
    void testHistorySections();
    void testHistoryNavigation();
    void testHistoryLayout_data();
    void testHistoryLayout();
    void testDefaultConfigFilePath();
    void testFileCreatedIfMissing();
    void testLoadAndSaveValues();
    void testLoadAndSaveEngineOptions();
    void testLoadAndSaveSplitterSizes();
    void testLegacyCsvSplitterConfig();
    void testLoadAndSaveMovePreviewSettings();
    void testLoadAndSaveEngineSectionVisibility();
    void testLegacyEngineSectionMigration();
    void testLoadAndSaveRemoteEngineSettings();
    void testResetToDefaults();
    void testLoadNonExistentFileReturnsFalse();
    void testMainWindowAutoCreatesConfigFile();
    void testMainWindowSaveAndRestoreState();
    void testMainWindowRestoresSplitterSizesWithHiddenSections();
    void testMainWindowSaveWritesCurrentSplitterSizes();
    void testAutoLoadUciEngineFromConfig();
    void testEngineSelectionUpdatesConfig();
    void testCorruptedConfigFileFallback();
};

void AppConfigTest::testHistoryPreferences() {
    QTemporaryDir dir;
    const auto path = dir.filePath("history.conf");
    QFile legacy(path);
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write("[Splitters]\nrightSplitter=180, 400, 160\n");
    legacy.close();
    AppConfig config(path);
    QVERIFY(config.load());
    QVERIFY(!config.gameInformationExpanded());
    QVERIFY(!config.messageLogExpanded());
    QCOMPARE(config.rightSplitterExpandedSizes(), (QList<int>{180, 400, 160}));
    config.setGameInformationExpanded(true);
    config.setMessageLogExpanded(true);
    config.setRightSplitterExpandedSizes({200, 350, 170});
    QVERIFY(config.save());
    AppConfig loaded(path);
    QVERIFY(loaded.load());
    QVERIFY(loaded.gameInformationExpanded());
    QVERIFY(loaded.messageLogExpanded());
    QCOMPARE(loaded.rightSplitterExpandedSizes(), (QList<int>{200, 350, 170}));
    loaded.resetToDefaults();
    QVERIFY(!loaded.gameInformationExpanded());
    QVERIFY(!loaded.messageLogExpanded());
    QCOMPARE(loaded.rightSplitterExpandedSizes(), (QList<int>{140, 320, 120}));
}

void AppConfigTest::testHistorySections() {
    QTemporaryDir dir;
    const auto path = dir.filePath("history.conf");
    QList<int> expandedSizes;
    {
        MainWindow window(nullptr, path);
        window.resize(1280, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *info = window.findChild<QToolButton *>("gameInformationButton");
        auto *log = window.findChild<QToolButton *>("messageLogButton");
        QVERIFY(info && log);
        QVERIFY(!info->isChecked());
        QVERIFY(!log->isChecked());
        QVERIFY(!window.pgnHeaderTextEdit()->isVisible());
        QVERIFY(!window.messageLogTextEdit()->isVisible());
        const int collapsedMoveHeight = window.moveListWidget()->height();
        window.gameController()->statusMessage(QStringLiteral("Activity while closed"));
        QVERIFY(window.messageLogTextEdit()->toPlainText().contains("Activity while closed"));
        QCOMPARE(window.visionStatusLabel()->text(), QStringLiteral("Activity while closed"));
        QTest::mouseClick(info, Qt::LeftButton);
        log->setFocus();
        QTest::keyClick(log, Qt::Key_Space);
        QTRY_VERIFY(window.pgnHeaderTextEdit()->isVisible());
        QTRY_VERIFY(window.messageLogTextEdit()->isVisible());
        window.rightSplitter()->setSizes({160, 320, 140});
        qApp->processEvents();
        expandedSizes = window.rightSplitter()->sizes();
        QVERIFY(window.moveListWidget()->height() < collapsedMoveHeight);
        QTest::mouseClick(info, Qt::LeftButton);
        QTest::mouseClick(log, Qt::LeftButton);
        qApp->processEvents();
        QVERIFY(window.moveListWidget()->height() >= collapsedMoveHeight);
        QCOMPARE(window.config().rightSplitterExpandedSizes().at(0), expandedSizes.at(0));
        QCOMPARE(window.config().rightSplitterExpandedSizes().at(2), expandedSizes.at(2));
    }
    {
        MainWindow window(nullptr, path);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *info = window.findChild<QToolButton *>("gameInformationButton");
        auto *log = window.findChild<QToolButton *>("messageLogButton");
        QVERIFY(!info->isChecked() && !log->isChecked());
        info->click();
        log->click();
        qApp->processEvents();
        QVERIFY(qAbs(window.rightSplitter()->sizes().at(0) - expandedSizes.at(0)) <= 2);
        QVERIFY(qAbs(window.rightSplitter()->sizes().at(2) - expandedSizes.at(2)) <= 2);
    }
    MainWindow restored(nullptr, path);
    restored.show();
    QVERIFY(QTest::qWaitForWindowExposed(&restored));
    QVERIFY(restored.pgnHeaderTextEdit()->isVisible());
    QVERIFY(restored.messageLogTextEdit()->isVisible());
}

void AppConfigTest::testHistoryNavigation() {
    QTemporaryDir dir;
    MainWindow window(nullptr, dir.filePath("history.conf"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.loadPgnContent("1. e4 e5 2. Nf3 Nc6 *"));
    auto *moves = window.moveListWidget();
    auto *label = window.findChild<QLabel *>("currentMoveLabel");
    QVERIFY(label);
    moves->setFocus();
    const auto cell = moves->model()->index(1, 1);
    QTest::mouseClick(moves->viewport(), Qt::LeftButton, {}, moves->visualRect(cell).center());
    QCOMPARE(window.gameController()->moveCursor(), 1);
    QCOMPARE(label->text(), QStringLiteral("1. e4"));
    QTest::keyClick(moves, Qt::Key_Right);
    QCOMPARE(window.gameController()->moveCursor(), 2);
    QCOMPARE(label->text(), QStringLiteral("1… e5"));
    QTest::keyClick(moves, Qt::Key_Left);
    QCOMPARE(window.gameController()->moveCursor(), 1);
    QTest::keyClick(moves, Qt::Key_Down);
    QCOMPARE(window.gameController()->moveCursor(), 1);
    QTest::keyClick(moves, Qt::Key_Return);
    QCOMPARE(window.gameController()->moveCursor(), 3);
    QCOMPARE(label->text(), QStringLiteral("2. Nf3"));
    window.gameController()->goToMove(0);
    QCOMPARE(label->text(), QStringLiteral("Start"));
}

void AppConfigTest::testHistoryLayout_data() {
    QTest::addColumn<QSize>("windowSize");
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("largeFont");
    for (const auto size : {QSize(800, 600), QSize(1280, 800)}) {
        for (bool dark : {false, true}) {
            for (bool large : {false, true}) {
                const auto name = QString("%1x%2-%3-%4").arg(size.width()).arg(size.height())
                                      .arg(dark ? "dark" : "light", large ? "large" : "normal");
                QTest::newRow(qPrintable(name)) << size << dark << large;
            }
        }
    }
}

void AppConfigTest::testHistoryLayout() {
    QFETCH(QSize, windowSize);
    QFETCH(bool, dark);
    QFETCH(bool, largeFont);
    QTemporaryDir dir;
    const auto originalFont = qApp->font();
    const auto originalPalette = qApp->palette();
    const auto restoreAppearance = qScopeGuard([&] {
        qApp->setFont(originalFont);
        qApp->setPalette(originalPalette);
    });
    if (largeFont) {
        auto font = originalFont;
        font.setPointSizeF(font.pointSizeF() * 1.5);
        qApp->setFont(font);
    }
    if (dark) {
        QPalette palette = originalPalette;
        palette.setColor(QPalette::Window, QColor("#242424"));
        palette.setColor(QPalette::Base, QColor("#181818"));
        palette.setColor(QPalette::Button, QColor("#303030"));
        for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
            palette.setColor(role, QColor("#eeeeee"));
        }
        palette.setColor(QPalette::Highlight, QColor("#345b84"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        qApp->setPalette(palette);
    }
    MainWindow window(nullptr, dir.filePath("history.conf"));
    window.resize(windowSize);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.loadPgnContent("[Event \"Example game\"]\n[White \"White player\"]\n[Black \"Black player\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O *"));
    window.gameController()->goToMove(8);
    auto *moves = window.moveListWidget();
    moves->setFocus();
    QTest::keyClick(moves, Qt::Key_Up); // Focus is distinct from the displayed move.
    qApp->processEvents();
    const QString outputDir = qEnvironmentVariable("CHESSGUI_HISTORY_SCREENSHOTS");
    if (!outputDir.isEmpty()) {
        QDir().mkpath(outputDir);
        QVERIFY(window.grab().save(outputDir + '/' + QString::fromLatin1(QTest::currentDataTag()) + ".png"));
    }
    // A window manager may clamp a top-level window to the available desktop
    // size. This happens on the Windows runner for the 1280x800 data rows.
    // Keep the exact assertion where the requested size fits, while accepting
    // an OS-imposed reduction when the requested size is too large.
    const QSize actualWindowSize = window.size();
    if (actualWindowSize != windowSize) {
        const QScreen *screen = window.screen();
        QVERIFY(screen != nullptr);
        const QSize availableSize = screen->availableGeometry().size();
        QVERIFY2(windowSize.width() > availableSize.width() ||
                     windowSize.height() > availableSize.height(),
                 qPrintable(QStringLiteral("unexpected window size clamp: actual %1x%2, requested %3x%4, available %5x%6")
                                .arg(actualWindowSize.width())
                                .arg(actualWindowSize.height())
                                .arg(windowSize.width())
                                .arg(windowSize.height())
                                .arg(availableSize.width())
                                .arg(availableSize.height())));
        QVERIFY(actualWindowSize.width() <= windowSize.width());
        QVERIFY(actualWindowSize.height() <= windowSize.height());
    } else {
        QCOMPARE(actualWindowSize, windowSize);
    }
    QVERIFY(qAbs(moves->columnWidth(1) - moves->columnWidth(2)) <= 1);
    QVERIFY(moves->columnWidth(1) >= moves->fontMetrics().horizontalAdvance("White") + 8);
    QVERIFY2(moves->height() > 100, qPrintable(QString::number(moves->height())));
    const auto *label = window.findChild<QLabel *>("currentMoveLabel");
    QVERIFY(label->width() >= label->fontMetrics().horizontalAdvance(label->text()));
    for (const auto name : {"gameInformationButton", "messageLogButton"}) {
        auto *button = window.findChild<QToolButton *>(name);
        QVERIFY(button);
        QCOMPARE(button->parentWidget()->height(), button->sizeHint().height());
        button->click();
    }
    qApp->processEvents();
    QVERIFY(window.pgnHeaderTextEdit()->isVisible());
    QVERIFY(window.messageLogTextEdit()->isVisible());
    QVERIFY(moves->viewport()->height() >= moves->rowHeight(0));
    QVERIFY(moves->viewport()->rect().intersects(moves->visualRect(moves->model()->index(4, 2))));
    if (!outputDir.isEmpty()) {
        QVERIFY(window.grab().save(outputDir + '/' + QString::fromLatin1(QTest::currentDataTag()) + "-expanded.png"));
    }
}

void AppConfigTest::testDefaultConfigFilePath() {
    const QString defaultPath = AppConfig::defaultConfigFilePath();
    QVERIFY(!defaultPath.isEmpty());
    QVERIFY(defaultPath.endsWith(QStringLiteral("chessGui.conf")));

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!configDir.isEmpty()) {
        QVERIFY(defaultPath.startsWith(configDir) || defaultPath.contains(QStringLiteral("ChessGui")));
    }
}

void AppConfigTest::testFileCreatedIfMissing() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    QVERIFY(!QFile::exists(confPath));

    AppConfig config(confPath);
    QCOMPARE(config.filePath(), confPath);
    QVERIFY(!config.exists());

    QVERIFY(config.ensureConfigFileExists());
    QVERIFY(config.exists());
    QVERIFY(QFile::exists(confPath));

    // Verify default contents can be loaded
    AppConfig verifyConfig(confPath);
    QVERIFY(verifyConfig.load());
    QCOMPARE(verifyConfig.uciEnginePath(), QString());
    QVERIFY(verifyConfig.uciEngineOptions().isEmpty());
    QCOMPARE(verifyConfig.windowSize(), QSize(800, 600));
    QCOMPARE(verifyConfig.windowPos(), QPoint(100, 100));
    QCOMPARE(verifyConfig.mainSplitterSizes(), (QList<int>{420, 180}));
    QCOMPARE(verifyConfig.topSplitterSizes(), (QList<int>{600, 200}));
    QCOMPARE(verifyConfig.rightSplitterSizes(), (QList<int>{140, 320, 120}));
    QVERIFY(!verifyConfig.computerMovePreviewEnabled());
    QVERIFY(!verifyConfig.recommendedMovePreviewEnabled());
    QVERIFY(verifyConfig.highlightLastMoveEnabled());
    QVERIFY(!verifyConfig.engineAnalysisSectionVisible());
    QVERIFY(!verifyConfig.engineLogSectionVisible());
    QVERIFY(!verifyConfig.engineDetailsVisible());
    QCOMPARE(verifyConfig.engineDetailsPage(), AppConfig::EngineDetailsPage::Variations);
}

void AppConfigTest::testLoadAndSaveValues() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("nested/dir/chessGui.conf"));
    AppConfig config(confPath);

    config.setUciEnginePath(QStringLiteral("/usr/bin/stockfish"));
    config.setWindowSize(QSize(1280, 720));
    config.setWindowPos(QPoint(250, 150));
    config.setMainSplitterSizes({520, 200});
    config.setTopSplitterSizes({750, 250});
    config.setRightSplitterSizes({150, 350, 150});

    QVERIFY(config.save());
    QVERIFY(QFile::exists(confPath));

    AppConfig loadedConfig(confPath);
    QVERIFY(loadedConfig.load());
    QCOMPARE(loadedConfig.uciEnginePath(), QStringLiteral("/usr/bin/stockfish"));
    QCOMPARE(loadedConfig.windowSize(), QSize(1280, 720));
    QCOMPARE(loadedConfig.windowPos(), QPoint(250, 150));
    QCOMPARE(loadedConfig.mainSplitterSizes(), (QList<int>{520, 200}));
    QCOMPARE(loadedConfig.topSplitterSizes(), (QList<int>{750, 250}));
    QCOMPARE(loadedConfig.rightSplitterSizes(), (QList<int>{150, 350, 150}));
}

void AppConfigTest::testLoadAndSaveEngineOptions() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    AppConfig config(confPath);
    const QMap<QString, QString> options = {
        {QStringLiteral("Threads"), QStringLiteral("4")},
        {QStringLiteral("Use NNUE"), QStringLiteral("true")},
        {QStringLiteral("SyzygyPath"), QStringLiteral("/data/tablebases")}
    };
    config.setUciEngineOptions(options);
    QVERIFY(config.save());

    AppConfig loadedConfig(confPath);
    QVERIFY(loadedConfig.load());
    QCOMPARE(loadedConfig.uciEngineOptions(), options);
}

void AppConfigTest::testLoadAndSaveSplitterSizes() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    AppConfig config(confPath);
    config.setMainSplitterSizes({520, 200, 90});
    config.setTopSplitterSizes({750});
    config.setRightSplitterSizes({160, 380, 140});
    QVERIFY(config.save());

    AppConfig loadedConfig(confPath);
    QVERIFY(loadedConfig.load());
    QCOMPARE(loadedConfig.mainSplitterSizes(), (QList<int>{520, 200, 90}));
    QCOMPARE(loadedConfig.topSplitterSizes(), (QList<int>{750}));
    QCOMPARE(loadedConfig.rightSplitterSizes(), (QList<int>{160, 380, 140}));
}

void AppConfigTest::testLegacyCsvSplitterConfig() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Config written by older versions stored splitter sizes as a CSV string.
    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    QFile file(confPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[Splitters]\nmainSplitter=550, 200\ntopSplitter=650, 350\nrightSplitter=400, 150\n");
    file.close();

    AppConfig config(confPath);
    QVERIFY(config.load());
    // Legacy CSV strings are read back natively: sizes are preserved.
    QCOMPARE(config.mainSplitterSizes(), (QList<int>{550, 200}));
    QCOMPARE(config.topSplitterSizes(), (QList<int>{650, 350}));
    QCOMPARE(config.rightSplitterSizes(), (QList<int>{400, 150}));
}

void AppConfigTest::testLoadAndSaveMovePreviewSettings() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    AppConfig config(confPath);
    config.setComputerMovePreviewEnabled(true);
    config.setRecommendedMovePreviewEnabled(true);
    config.setHighlightLastMoveEnabled(false);
    QVERIFY(config.save());

    AppConfig loadedConfig(confPath);
    QVERIFY(loadedConfig.load());
    QVERIFY(loadedConfig.computerMovePreviewEnabled());
    QVERIFY(loadedConfig.recommendedMovePreviewEnabled());
    QVERIFY(!loadedConfig.highlightLastMoveEnabled());
}

void AppConfigTest::testLoadAndSaveEngineSectionVisibility() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    AppConfig config(confPath);
    config.setEngineDetailsPage(AppConfig::EngineDetailsPage::UciLog);
    config.setEngineDetailsVisible(true);
    QVERIFY(config.save());

    AppConfig loadedConfig(confPath);
    QVERIFY(loadedConfig.load());
    QVERIFY(!loadedConfig.engineAnalysisSectionVisible());
    QVERIFY(loadedConfig.engineLogSectionVisible());
    QVERIFY(loadedConfig.engineDetailsVisible());
    QCOMPARE(loadedConfig.engineDetailsPage(), AppConfig::EngineDetailsPage::UciLog);
}

void AppConfigTest::testLegacyEngineSectionMigration() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));

    QSettings settings(confPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("EngineOutput"));
    settings.setValue(QStringLiteral("analysisSection"), false);
    settings.setValue(QStringLiteral("logSection"), true);
    settings.endGroup();
    settings.sync();

    AppConfig config(confPath);
    QVERIFY(config.load());
    QVERIFY(config.engineDetailsVisible());
    QCOMPARE(config.engineDetailsPage(), AppConfig::EngineDetailsPage::UciLog);
    QVERIFY(!config.engineAnalysisSectionVisible());
    QVERIFY(config.engineLogSectionVisible());

    QVERIFY(config.save());
    QSettings migrated(confPath, QSettings::IniFormat);
    migrated.beginGroup(QStringLiteral("EngineOutput"));
    QVERIFY(migrated.contains(QStringLiteral("detailsVisible")));
    QCOMPARE(migrated.value(QStringLiteral("detailsPage")).toInt(),
             static_cast<int>(AppConfig::EngineDetailsPage::UciLog));
    migrated.endGroup();
}

void AppConfigTest::testLoadAndSaveRemoteEngineSettings() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    AppConfig config(confPath);
    config.setRemoteEngine(QStringLiteral("engine.example.org"), 9000,
                           QStringLiteral("stockfish-17"),
                           QStringLiteral("Stockfish"), QStringLiteral("17"),
                           QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    QVERIFY(config.save());

    AppConfig loadedConfig(confPath);
    QVERIFY(loadedConfig.load());
    QCOMPARE(loadedConfig.remoteEngineHost(), QStringLiteral("engine.example.org"));
    QCOMPARE(loadedConfig.remoteEnginePort(), static_cast<quint16>(9000));
    QCOMPARE(loadedConfig.remoteEngineId(), QStringLiteral("stockfish-17"));
    QCOMPARE(loadedConfig.remoteEngineName(), QStringLiteral("Stockfish"));
    QCOMPARE(loadedConfig.remoteEngineVersion(), QStringLiteral("17"));
    QCOMPARE(loadedConfig.remoteEngineAccessKey(),
             QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
}

void AppConfigTest::testResetToDefaults() {
    AppConfig config;
    config.setUciEnginePath(QStringLiteral("/usr/bin/stockfish"));
    config.setUciEngineOptions({{QStringLiteral("Threads"), QStringLiteral("4")}});
    config.setRemoteEngine(QStringLiteral("engine.example.org"), 9000,
                           QStringLiteral("stockfish-17"),
                           QStringLiteral("Stockfish"), QStringLiteral("17"),
                           QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    config.setWindowSize(QSize(1920, 1080));
    config.setWindowPos(QPoint(0, 0));
    config.setMainSplitterSizes({800, 400});
    config.setTopSplitterSizes({1000, 300});
    config.setRightSplitterSizes({500, 200, 100});
    config.setComputerMovePreviewEnabled(true);
    config.setRecommendedMovePreviewEnabled(true);
    config.setHighlightLastMoveEnabled(false);
    config.setEngineAnalysisSectionVisible(false);
    config.setEngineLogSectionVisible(false);

    config.resetToDefaults();
    QCOMPARE(config.uciEnginePath(), QString());
    QVERIFY(config.uciEngineOptions().isEmpty());
    QVERIFY(config.remoteEngineHost().isEmpty());
    QCOMPARE(config.remoteEnginePort(), static_cast<quint16>(9000));
    QVERIFY(config.remoteEngineId().isEmpty());
    QVERIFY(config.remoteEngineName().isEmpty());
    QVERIFY(config.remoteEngineVersion().isEmpty());
    QVERIFY(config.remoteEngineAccessKey().isEmpty());
    QCOMPARE(config.windowSize(), QSize(800, 600));
    QCOMPARE(config.windowPos(), QPoint(100, 100));
    QCOMPARE(config.mainSplitterSizes(), (QList<int>{420, 180}));
    QCOMPARE(config.topSplitterSizes(), (QList<int>{600, 200}));
    QCOMPARE(config.rightSplitterSizes(), (QList<int>{140, 320, 120}));
    QVERIFY(!config.computerMovePreviewEnabled());
    QVERIFY(!config.recommendedMovePreviewEnabled());
    QVERIFY(config.highlightLastMoveEnabled());
    QVERIFY(!config.engineAnalysisSectionVisible());
    QVERIFY(!config.engineLogSectionVisible());
    QVERIFY(!config.engineDetailsVisible());
}

void AppConfigTest::testLoadNonExistentFileReturnsFalse() {
    AppConfig config(QStringLiteral("/non/existent/path/chessGui.conf"));
    QVERIFY(!config.load());
}

void AppConfigTest::testMainWindowAutoCreatesConfigFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    QVERIFY(!QFile::exists(confPath));

    {
        MainWindow window(nullptr, confPath);
        QVERIFY(QFile::exists(confPath));
        QVERIFY(window.mainSplitter() != nullptr);
        QVERIFY(window.topSplitter() != nullptr);
        QVERIFY(window.rightSplitter() != nullptr);
        QCOMPARE(window.mainSplitter()->count(), 2);
        QCOMPARE(window.topSplitter()->count(), 2);
        QCOMPARE(window.rightSplitter()->count(), 3);
    }

    QVERIFY(QFile::exists(confPath));
}

void AppConfigTest::testMainWindowSaveAndRestoreState() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));

    // Prepare config file with custom sizes
    {
        AppConfig initialConfig(confPath);
        initialConfig.setWindowSize(QSize(1000, 750));
        initialConfig.setWindowPos(QPoint(150, 120));
        initialConfig.setMainSplitterSizes({550, 200});
        initialConfig.setTopSplitterSizes({650, 350});
        initialConfig.setRightSplitterSizes({150, 400, 200});
        initialConfig.setEngineAnalysisSectionVisible(false);
        initialConfig.setEngineLogSectionVisible(false);
        QVERIFY(initialConfig.save());
    }

    // Verify MainWindow loads this config
    {
        MainWindow window(nullptr, confPath);
        QCOMPARE(window.config().windowSize(), QSize(1000, 750));
        QCOMPARE(window.config().windowPos(), QPoint(150, 120));
        QCOMPARE(window.config().mainSplitterSizes(), (QList<int>{550, 200}));
        QCOMPARE(window.config().topSplitterSizes(), (QList<int>{650, 350}));
        QCOMPARE(window.config().rightSplitterSizes(), (QList<int>{150, 400, 200}));
        QVERIFY(!window.engineOutputWidget()->analysisSectionVisible());
        QVERIFY(!window.engineOutputWidget()->logSectionVisible());
    }
}

void AppConfigTest::testMainWindowRestoresSplitterSizesWithHiddenSections() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));

    // Collapsed engine output sections used to trigger adjustEnginePanelSize()
    // during loadConfiguration(), overwriting the saved splitter sizes.
    {
        AppConfig initialConfig(confPath);
        initialConfig.setMainSplitterSizes({600, 350});
        initialConfig.setTopSplitterSizes({650, 300});
        initialConfig.setEngineAnalysisSectionVisible(true);
        initialConfig.setEngineLogSectionVisible(false);
        QVERIFY(initialConfig.save());
    }

    MainWindow window(nullptr, confPath);
    window.resize(1200, 1000);
    window.show();
    qApp->processEvents();

    // The saved sizes are scaled proportionally to the available space, so
    // verify the proportions instead of exact pixel values. With the stored
    // log section collapsed, the engine panel used to be resized to its
    // collapsed size hint during loadConfiguration(), breaking the ratio.
    const QList<int> mainSizes = window.mainSplitter()->sizes();
    QCOMPARE(mainSizes.size(), 2);
    const double mainPanelFraction =
        static_cast<double>(mainSizes.at(1)) / (mainSizes.at(0) + mainSizes.at(1));
    QVERIFY2(qAbs(mainPanelFraction - 350.0 / 950.0) < 0.02,
             qPrintable(QStringLiteral("main splitter proportion lost: %1")
                            .arg(mainPanelFraction)));

    const QList<int> topSizes = window.topSplitter()->sizes();
    QCOMPARE(topSizes.size(), 2);
    const double topPanelFraction =
        static_cast<double>(topSizes.at(1)) / (topSizes.at(0) + topSizes.at(1));
    QVERIFY2(qAbs(topPanelFraction - 300.0 / 950.0) < 0.02,
             qPrintable(QStringLiteral("top splitter proportion lost: %1")
                            .arg(topPanelFraction)));
}

void AppConfigTest::testMainWindowSaveWritesCurrentSplitterSizes() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));

    QList<int> expectedMainSizes;
    QList<int> expectedTopSizes;
    QList<int> expectedRightSizes;
    {
        MainWindow window(nullptr, confPath);
        window.resize(1200, 1000);
        window.show();
        qApp->processEvents();

        // Remember the laid-out splitter sizes: the destructor must save them.
        expectedMainSizes = window.mainSplitter()->sizes();
        expectedTopSizes = window.topSplitter()->sizes();
        expectedRightSizes = window.rightSplitter()->sizes();
        QVERIFY(expectedMainSizes.size() == 2);
        QVERIFY(expectedTopSizes.size() == 2);
        QVERIFY(expectedRightSizes.size() == 3);
    }

    AppConfig reloadedConfig(confPath);
    QVERIFY(reloadedConfig.load());
    QCOMPARE(reloadedConfig.mainSplitterSizes(), expectedMainSizes);
    QCOMPARE(reloadedConfig.topSplitterSizes(), expectedTopSizes);
    QCOMPARE(reloadedConfig.rightSplitterSizes(), expectedRightSizes);
}

void AppConfigTest::testAutoLoadUciEngineFromConfig() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString scriptPath = tempDir.filePath(QStringLiteral("mock_engine.sh"));
    QFile scriptFile(scriptPath);
    QVERIFY(scriptFile.open(QIODevice::WriteOnly | QIODevice::Text));

    const QByteArray script =
        "#!/bin/bash\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then\n"
        "    echo \"id name ConfigMockEngine 1.0\"\n"
        "    echo \"id author TestAuthor\"\n"
        "    echo \"uciok\"\n"
        "  elif [ \"$line\" = \"isready\" ]; then\n"
        "    echo \"readyok\"\n"
        "  elif [ \"$line\" = \"quit\" ]; then\n"
        "    exit 0\n"
        "  fi\n"
        "done\n";

    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    AppConfig config(confPath);
    config.setUciEnginePath(scriptPath);
    QVERIFY(config.save());

    MainWindow window(nullptr, confPath);
    QCOMPARE(window.uciEnginePath(), scriptPath);
    QTRY_COMPARE_WITH_TIMEOUT(window.uciEngine()->state(), UciEngine::State::Ready, 2000);
    QCOMPARE(window.uciEngine()->engineName(), QStringLiteral("ConfigMockEngine 1.0"));
}

void AppConfigTest::testEngineSelectionUpdatesConfig() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString scriptPath = tempDir.filePath(QStringLiteral("mock_engine_dyn.sh"));
    QFile scriptFile(scriptPath);
    QVERIFY(scriptFile.open(QIODevice::WriteOnly | QIODevice::Text));

    const QByteArray script =
        "#!/bin/bash\n"
        "while read line; do\n"
        "  if [ \"$line\" = \"uci\" ]; then\n"
        "    echo \"id name DynEngine 1.0\"\n"
        "    echo \"uciok\"\n"
        "  elif [ \"$line\" = \"isready\" ]; then\n"
        "    echo \"readyok\"\n"
        "  elif [ \"$line\" = \"quit\" ]; then\n"
        "    exit 0\n"
        "  fi\n"
        "done\n";

    scriptFile.write(script);
    scriptFile.close();
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser | QFile::ExeUser |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    MainWindow window(nullptr, confPath);
    QVERIFY(window.uciEnginePath().isEmpty());

    QVERIFY(window.loadEngine(scriptPath));
    QCOMPARE(window.uciEnginePath(), scriptPath);

    // Verify config file updated immediately
    AppConfig reloadedConfig(confPath);
    QVERIFY(reloadedConfig.load());
    QCOMPARE(reloadedConfig.uciEnginePath(), scriptPath);
}

void AppConfigTest::testCorruptedConfigFileFallback() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString confPath = tempDir.filePath(QStringLiteral("chessGui.conf"));
    QFile file(confPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[MainWindow]\nx=invalid\ny=notanumber\nwidth=0\nheight=-10\n\n[Splitters]\nmainSplitter=not,numbers\n");
    file.close();

    AppConfig config(confPath);
    QVERIFY(config.load());
    // Should fallback to default values without crashing
    QCOMPARE(config.mainSplitterSizes(), (QList<int>{420, 180}));
    QCOMPARE(config.topSplitterSizes(), (QList<int>{600, 200}));
    QCOMPARE(config.rightSplitterSizes(), (QList<int>{140, 320, 120}));
}

QTEST_MAIN(AppConfigTest)
#include "appconfig_test.moc"
