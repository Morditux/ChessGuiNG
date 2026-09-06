//
// Created by mordicus on 23/08/2026.
//

#include "mainwindow.h"
#include "chessboard.h"
#include "computergamedialog.h"
#include "computergamesettings.h"
#include "engineconfigurationdialog.h"
#include "engineoutputwidget.h"
#include "evaluationbar.h"
#include "gamecontroller.h"
#include "gatewayclient.h"
#include "movelistwidget.h"
#include "pendulumwidget.h"
#include "pgnfile.h"
#include "pgnselectdialog.h"
#include "remoteenginedialog.h"
#include "uciengine.h"
#include "ucioptionsdialog.h"
#include "uciparser.h"
#include "visionworker.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QThread>
#include <QToolButton>
#include <QToolBar>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {
// A closed section must return all space except its header to the move list.
// Recompute the cap when system font/style metrics change.
class HistorySection : public QWidget {
public:
    explicit HistorySection(QWidget *parent) : QWidget(parent) {}

    void updateHeight(QToolButton *header, bool expanded) {
        header_ = header;
        expanded_ = expanded;
        setMaximumHeight(expanded_ ? QWIDGETSIZE_MAX : header_->sizeHint().height());
    }

protected:
    void changeEvent(QEvent *event) override {
        QWidget::changeEvent(event);
        if (header_ && (event->type() == QEvent::FontChange ||
                        event->type() == QEvent::StyleChange)) {
            updateHeight(header_, expanded_);
        }
    }

private:
    QToolButton *header_ = nullptr;
    bool expanded_ = false;
};
} // namespace

MainWindow::MainWindow(QWidget *parent, const QString &configFilePath)
    : QWidget(parent)
    , gameController_(new GameController(this))
    , config_(configFilePath)
    , networkManager_(new QNetworkAccessManager(this))
    , visionWorker_(new VisionWorker(findModelPath()))
    , visionThread_(new QThread(this)) {
    visionWorker_->moveToThread(visionThread_);
    connect(visionThread_, &QThread::finished, visionWorker_, &QObject::deleteLater);
    connect(visionWorker_, &VisionWorker::finished, this, &MainWindow::onVisionResult);
    visionThread_->start();

    setupUi();

    connect(whiteToPlayCheckBox_, &QCheckBox::toggled,
            this, &MainWindow::setWhiteToMove);
    connect(showComputerMoveCheckBox_, &QCheckBox::toggled,
            this, &MainWindow::setComputerMovePreviewEnabled);
    connect(showRecommendedMoveCheckBox_, &QCheckBox::toggled,
            this, &MainWindow::setRecommendedMovePreviewEnabled);
    connect(highlightLastMoveCheckBox_, &QCheckBox::toggled,
            this, &MainWindow::setLastMoveHighlightingEnabled);
    connect(showComputerMoveAction_, &QAction::toggled,
            this, &MainWindow::setComputerMovePreviewEnabled);
    connect(showRecommendedMoveAction_, &QAction::toggled,
            this, &MainWindow::setRecommendedMovePreviewEnabled);

    connect(gameController_, &GameController::positionChanged, this, [this] {
        board_->setRules(gameController_->rules());
        board_->setUserArrows(gameController_->arrowsAtCursor());
        board_->setSquareAnnotations(gameController_->squaresAtCursor());
        board_->setAuditAnnotation(gameController_->auditAt(gameController_->moveCursor()));
        const QSignalBlocker blocker(whiteToPlayCheckBox_);
        whiteToPlayCheckBox_->setChecked(
            gameController_->rules().currentPlayer() == Rules::Color::White);
        moveListWidget_->setCurrentMove(gameController_->moveCursor());
        currentMoveLabel_->setText(moveListWidget_->currentMoveText());
        updateNavigationActions();
        refreshAuditUi();
    });
    connect(gameController_, &GameController::annotationsChanged, this, [this] {
        board_->setUserArrows(gameController_->arrowsAtCursor());
        board_->setSquareAnnotations(gameController_->squaresAtCursor());
        board_->setAuditAnnotation(gameController_->auditAt(gameController_->moveCursor()));
    });
    connect(gameController_, &GameController::historyChanged, this, [this](const QString &text) {
        Q_UNUSED(text)
        pgnHeaderTextEdit_->setPlainText(gameController_->pgnHeaderText());
        moveListWidget_->setPgn(gameController_->pgnMoves(),
                                gameController_->moveCursor());
        QVector<AuditAnnotation> annotations;
        annotations.reserve(gameController_->uciMoves().size() + 1);
        for (int ply = 0; ply <= gameController_->uciMoves().size(); ++ply) {
            annotations.append(gameController_->auditAt(ply));
        }
        moveListWidget_->setAuditAnnotations(annotations);
        currentMoveLabel_->setText(moveListWidget_->currentMoveText());
    });
    connect(moveListWidget_, &MoveListWidget::moveSelected, this, [this](int plyIndex) {
        gameController_->goToMove(plyIndex);
    });
    connect(gameController_, &GameController::evaluationChanged, this, [this](double value) {
        evaluationBar_->setValue(value);
    });
    connect(gameController_, &GameController::statusMessage, this, [this](const QString &message) {
        setActivityMessage(message);
    });
    connect(gameController_, &GameController::auditStateChanged, this, [this](bool) {
        refreshAuditUi();
        updateNavigationActions();
    });
    connect(gameController_, &GameController::computerGameStateChanged, this, [this](bool active) {
        whitePendulum_->stop();
        blackPendulum_->stop();
        whiteToPlayCheckBox_->setEnabled(!active);
        if (active) {
            const ComputerGameSettings &settings =
                gameController_->currentComputerGameSettings();
            whitePendulum_->setRemainingMilliseconds(settings.timeLimitMilliseconds);
            blackPendulum_->setRemainingMilliseconds(settings.timeLimitMilliseconds);
            {
                const QSignalBlocker blocker(whiteToPlayCheckBox_);
                whiteToPlayCheckBox_->setChecked(true);
            }
            engineOutputWidget_->setControlButtonsEnabled(false, false, true);
        }
    });
    connect(gameController_, &GameController::computerTurnBegan, this, [this] {
        PendulumWidget *computerClock =
            gameController_->computerColor() == Rules::Color::White
                ? whitePendulum_
                : blackPendulum_;
        PendulumWidget *humanClock =
            gameController_->computerColor() == Rules::Color::White
                ? blackPendulum_
                : whitePendulum_;
        humanClock->stop();
        computerClock->start();
    });
    connect(gameController_, &GameController::humanTurnBegan, this, [this] {
        PendulumWidget *computerClock =
            gameController_->computerColor() == Rules::Color::White
                ? whitePendulum_
                : blackPendulum_;
        PendulumWidget *humanClock =
            gameController_->computerColor() == Rules::Color::White
                ? blackPendulum_
                : whitePendulum_;
        computerClock->stop();
        humanClock->start();
    });
    connect(gameController_, &GameController::gameFinished, this, [this](const QString &, const QString &message) {
        setActivityMessage(message);
    });
    connect(gameController_, &GameController::computerMovePreviewChanged, this, [this](const std::optional<Rules::Move> &move) {
        board_->setComputerMovePreview(move);
    });
    connect(gameController_, &GameController::recommendedMovePreviewChanged, this, [this](const std::optional<Rules::Move> &move) {
        board_->setRecommendedMovePreview(move);
    });

    if (!visionWorker_->isReady()) {
        setActivityMessage(visionWorker_->errorString());
    }

    updateEvaluation();

    loadConfiguration(configFilePath);
    refreshEngineStateUi();

    connect(uciEngine(), &UciEngine::stateChanged,
            this, [this](UciEngine::State) {
                refreshEngineStateUi();
            });
    connect(gatewayClient(), &ChessGatewayClient::stateChanged,
            this, [this](ChessGatewayClient::State) {
                refreshEngineStateUi();
            });

    connect(engineOutputWidget_, &EngineOutputWidget::startClicked, this, &MainWindow::startEngineAnalysis);
    connect(engineOutputWidget_, &EngineOutputWidget::pauseClicked, this, &MainWindow::pauseEngineAnalysis);
    connect(engineOutputWidget_, &EngineOutputWidget::stopClicked, this, &MainWindow::stopEngineAnalysis);

    connect(uciEngine(), &UciEngine::engineLoaded, this, &MainWindow::onEngineLoaded);
    connect(gatewayClient(), &ChessGatewayClient::engineLoaded,
            this, &MainWindow::onEngineLoaded);
    connect(uciEngine(), &UciEngine::analysisUpdated, this, &MainWindow::onEngineAnalysisUpdated);
    connect(gatewayClient(), &ChessGatewayClient::analysisUpdated,
            this, &MainWindow::onEngineAnalysisUpdated);
    connect(uciEngine(), &UciEngine::rawLineReceived, engineOutputWidget_, &EngineOutputWidget::appendUciLog);
    connect(gatewayClient(), &ChessGatewayClient::rawLineReceived,
            engineOutputWidget_, &EngineOutputWidget::appendUciLog);
    connect(uciEngine(), &UciEngine::rawLineSent, engineOutputWidget_, [this](const QString &line) {
        engineOutputWidget_->appendUciLog(tr(">> ") + line);
    });
    connect(gatewayClient(), &ChessGatewayClient::rawLineSent,
            engineOutputWidget_, [this](const QString &line) {
                engineOutputWidget_->appendUciLog(tr(">> ") + line);
            });
    connect(uciEngine(), &UciEngine::bestMoveReceived, engineOutputWidget_, &EngineOutputWidget::setBestMove);
    connect(gatewayClient(), &ChessGatewayClient::bestMoveReceived,
            engineOutputWidget_, &EngineOutputWidget::setBestMove);
    connect(uciEngine(), &UciEngine::errorOccurred, this, &MainWindow::onEngineError);
    connect(gatewayClient(), &ChessGatewayClient::errorOccurred,
            this, &MainWindow::onEngineError);
}

MainWindow::~MainWindow() {
    visionThread_->quit();
    visionThread_->wait();
    saveConfiguration();
}

EvaluationBar *MainWindow::evaluationBar() const {
    return evaluationBar_;
}

ChessBoard *MainWindow::chessBoard() const {
    return board_;
}

PendulumWidget *MainWindow::whitePendulum() const {
    return whitePendulum_;
}

PendulumWidget *MainWindow::blackPendulum() const {
    return blackPendulum_;
}

MoveListWidget *MainWindow::moveListWidget() const {
    return moveListWidget_;
}

QTextEdit *MainWindow::pgnHeaderTextEdit() const {
    return pgnHeaderTextEdit_;
}

QTextEdit *MainWindow::messageLogTextEdit() const {
    return messageLog_;
}

EngineOutputWidget *MainWindow::engineOutputWidget() const {
    return engineOutputWidget_;
}

UciEngine *MainWindow::uciEngine() const {
    return gameController_->engine();
}

ChessGatewayClient *MainWindow::gatewayClient() const {
    return gameController_->gatewayClient();
}

GameController *MainWindow::gameController() const {
    return gameController_;
}

QSplitter *MainWindow::mainSplitter() const {
    return mainSplitter_;
}

QSplitter *MainWindow::topSplitter() const {
    return topSplitter_;
}

QSplitter *MainWindow::rightSplitter() const {
    return rightSplitter_;
}

const AppConfig &MainWindow::config() const {
    return config_;
}

AppConfig &MainWindow::config() {
    return config_;
}

QString MainWindow::uciEnginePath() const {
    return uciEnginePath_;
}

QString MainWindow::initialFen() const {
    return gameController_->initialFen();
}

QCheckBox *MainWindow::whiteToPlayCheckBox() const {
    return whiteToPlayCheckBox_;
}

QCheckBox *MainWindow::showComputerMoveCheckBox() const {
    return showComputerMoveCheckBox_;
}

QCheckBox *MainWindow::showRecommendedMoveCheckBox() const {
    return showRecommendedMoveCheckBox_;
}

QCheckBox *MainWindow::highlightLastMoveCheckBox() const {
    return highlightLastMoveCheckBox_;
}

QToolButton *MainWindow::flipBoardButton() const {
    return flipBoardButton_;
}

QLabel *MainWindow::visionStatusLabel() const {
    return visionStatusLabel_;
}

QAction *MainWindow::clearAnnotationsAction() const {
    return clearAnnotationsAction_;
}

QAction *MainWindow::analyzeGameAction() const {
    return analyzeGameAction_;
}

QString MainWindow::loadedPgnContent() const {
    return loadedPgnContent_;
}

QStringList MainWindow::loadedPgnGames() const {
    return loadedPgnGames_;
}

int MainWindow::selectedPgnGameIndex() const {
    return selectedPgnGameIndex_;
}

bool MainWindow::loadEngine(const QString &enginePath) {
    if (enginePath.isEmpty()) {
        return false;
    }

    if (engineOutputWidget_) {
        engineOutputWidget_->clear();
    }

    // A local engine replaces any remote engine session.
    gameController_->stopEngine();

    if (gameController_->startEngine(enginePath)) {
        uciEnginePath_ = enginePath;
        if (engineOutputWidget_) {
            engineOutputWidget_->setEngineName(uciEngine()->engineName());
            engineOutputWidget_->setEngineStatus(tr("Initializing"));
        }
        // A local engine replaces any remote engine selection.
        gameController_->clearRemoteEngine();
        config_.setUciEnginePath(enginePath);
        config_.setRemoteEngine(QString(), 9000, QString());
        config_.save();
        return true;
    }

    return false;
}

void MainWindow::loadConfiguration(const QString &configFilePath) {
    if (!configFilePath.isEmpty()) {
        config_.setFilePath(configFilePath);
    }

    if (!config_.exists()) {
        // Configuration file chessGui.conf does not exist: create it with default settings
        config_.ensureConfigFileExists();
    } else {
        config_.load();
    }

    if (!config_.windowGeometry().isEmpty()) {
        restoreGeometry(config_.windowGeometry());
    } else {
        if (config_.windowSize().isValid() && config_.windowSize().width() > 0 && config_.windowSize().height() > 0) {
            resize(config_.windowSize());
        }
        if (!config_.windowPos().isNull()) {
            move(config_.windowPos());
        }
    }

    if (engineOutputWidget_) {
        // Restore the details view first: its visibility emits a resize signal,
        // so splitter sizes are applied afterwards to preserve the user's layout.
        const auto page = config_.engineDetailsPage() ==
                                  AppConfig::EngineDetailsPage::UciLog
                              ? EngineOutputWidget::DetailsPage::UciLog
                              : EngineOutputWidget::DetailsPage::Variations;
        engineOutputWidget_->setDetailsPage(page);
        engineOutputWidget_->setDetailsVisible(config_.engineDetailsVisible());
    }

    if (mainSplitter_ && !config_.mainSplitterSizes().isEmpty()) {
        mainSplitter_->setSizes(config_.mainSplitterSizes());
    }
    if (topSplitter_ && !config_.topSplitterSizes().isEmpty()) {
        topSplitter_->setSizes(config_.topSplitterSizes());
    }
    // Apply visibility before geometry. Collapsed sections retain their headers.
    for (const int section : {0, 2}) {
        auto *button = section == 0 ? gameInformationButton_ : messageLogButton_;
        const bool expanded = section == 0 ? config_.gameInformationExpanded()
                                            : config_.messageLogExpanded();
        const QSignalBlocker blocker(button);
        button->setChecked(expanded);
        auto *content = section == 0 ? pgnHeaderTextEdit_ : messageLog_;
        content->setVisible(expanded);
        button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        static_cast<HistorySection *>(rightSplitter_->widget(section))->updateHeight(button, expanded);
        rightSplitter_->widget(section)->layout()->activate();
    }
    QList<int> historySizes = config_.rightSplitterSizes();
    if (historySizes.size() != 3) {
        historySizes = config_.rightSplitterExpandedSizes();
    }
    for (const int section : {0, 2}) {
        auto *button = section == 0 ? gameInformationButton_ : messageLogButton_;
        const int previousSize = historySizes.at(section);
        if (!button->isChecked()) {
            historySizes[section] = button->sizeHint().height();
        } else if (historySizes[section] <= button->sizeHint().height()) {
            historySizes[section] = config_.rightSplitterExpandedSizes().at(section);
        }
        historySizes[1] = qMax(1, historySizes.at(1) + previousSize - historySizes.at(section));
    }
    rightSplitter_->setSizes(historySizes);

    setComputerMovePreviewEnabled(config_.computerMovePreviewEnabled());
    setRecommendedMovePreviewEnabled(config_.recommendedMovePreviewEnabled());
    setLastMoveHighlightingEnabled(config_.highlightLastMoveEnabled());

    gameController_->setRemoteEngine(config_.remoteEngineHost(),
                                     config_.remoteEnginePort(),
                                     config_.remoteEngineId(),
                                     config_.remoteEngineName(),
                                     config_.remoteEngineAccessKey());

    if (!config_.uciEnginePath().isEmpty() && QFile::exists(config_.uciEnginePath())) {
        loadEngine(config_.uciEnginePath());
    }
}

void MainWindow::rememberExpandedHistorySizes() {
    auto expandedSizes = config_.rightSplitterExpandedSizes();
    const auto sizes = rightSplitter_->sizes();
    // Explicit visibility also works while the whole window is hidden.
    for (const int section : {0, 2}) {
        const auto *content = section == 0 ? pgnHeaderTextEdit_ : messageLog_;
        if (!content->isHidden() && sizes.at(section) > 0) {
            expandedSizes[section] = sizes.at(section);
        }
    }
    expandedSizes[1] = sizes.at(1);
    config_.setRightSplitterExpandedSizes(expandedSizes);
}

void MainWindow::setHistorySectionExpanded(int section, bool expanded) {
    rememberExpandedHistorySizes();
    auto sizes = rightSplitter_->sizes();
    const int previousSize = sizes.at(section);
    auto *content = section == 0 ? pgnHeaderTextEdit_ : messageLog_;
    auto *button = section == 0 ? gameInformationButton_ : messageLogButton_;
    content->setVisible(expanded);
    button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    static_cast<HistorySection *>(rightSplitter_->widget(section))->updateHeight(button, expanded);
    rightSplitter_->widget(section)->layout()->activate();
    sizes[section] = expanded ? config_.rightSplitterExpandedSizes().at(section)
                              : button->sizeHint().height();
    sizes[1] = qMax(1, sizes.at(1) + previousSize - sizes.at(section));
    rightSplitter_->setSizes(sizes);
    config_.setGameInformationExpanded(gameInformationButton_->isChecked());
    config_.setMessageLogExpanded(messageLogButton_->isChecked());
}

void MainWindow::saveConfiguration() {
    config_.setWindowPos(pos());
    config_.setWindowSize(size());
    config_.setWindowGeometry(saveGeometry());
    if (mainSplitter_) {
        config_.setMainSplitterSizes(mainSplitter_->sizes());
    }
    if (topSplitter_) {
        config_.setTopSplitterSizes(topSplitter_->sizes());
    }
    if (rightSplitter_) {
        rememberExpandedHistorySizes();
        config_.setGameInformationExpanded(gameInformationButton_->isChecked());
        config_.setMessageLogExpanded(messageLogButton_->isChecked());
        config_.setRightSplitterSizes(rightSplitter_->sizes());
    }
    config_.setUciEnginePath(uciEnginePath_);
    config_.setComputerMovePreviewEnabled(gameController_->isComputerMovePreviewEnabled());
    config_.setRecommendedMovePreviewEnabled(gameController_->isRecommendedMovePreviewEnabled());
    config_.setHighlightLastMoveEnabled(board_->lastMoveHighlightingEnabled());
    if (engineOutputWidget_) {
        config_.setEngineDetailsPage(
            engineOutputWidget_->detailsPage() == EngineOutputWidget::DetailsPage::UciLog
                ? AppConfig::EngineDetailsPage::UciLog
                : AppConfig::EngineDetailsPage::Variations);
        config_.setEngineDetailsVisible(engineOutputWidget_->detailsVisible());
    }
    config_.save();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveConfiguration();
    QWidget::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        clearBoardAnnotations();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteFromClipboard();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool MainWindow::isSupportedImagePath(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("png") ||
           suffix == QLatin1String("jpg") ||
           suffix == QLatin1String("jpeg") ||
           suffix == QLatin1String("bmp") ||
           suffix == QLatin1String("webp");
}

bool MainWindow::isRemoteImageUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    return url.isValid() &&
           (scheme == QLatin1String("http") ||
            scheme == QLatin1String("https"));
}

bool MainWindow::hasImagePayload(const QMimeData *mime) {
    if (mime == nullptr) {
        return false;
    }

    if (mime->hasImage()) {
        return true;
    }

    static const char *imageFormats[] = {
        "image/png",
        "image/jpeg",
        "image/webp",
        "image/bmp"
    };

    for (const char *format : imageFormats) {
        if (mime->hasFormat(QLatin1String(format))) {
            return true;
        }
    }

    return false;
}

QImage MainWindow::imageFromMimeData(const QMimeData *mime) {
    if (mime == nullptr) {
        return {};
    }

    if (mime->hasImage()) {
        const QVariant payload = mime->imageData();
        if (payload.canConvert<QImage>()) {
            const QImage image = qvariant_cast<QImage>(payload);
            if (!image.isNull()) {
                return image;
            }
        }

        if (payload.canConvert<QPixmap>()) {
            const QImage image = qvariant_cast<QPixmap>(payload).toImage();
            if (!image.isNull()) {
                return image;
            }
        }
    }

    static const char *imageFormats[] = {
        "image/png",
        "image/jpeg",
        "image/webp",
        "image/bmp"
    };

    for (const char *format : imageFormats) {
        if (!mime->hasFormat(QLatin1String(format))) {
            continue;
        }

        QImage image;
        if (image.loadFromData(mime->data(QLatin1String(format)))) {
            return image;
        }
    }

    return {};
}

QUrl MainWindow::remoteImageUrlFromMimeData(const QMimeData *mime) {
    if (mime == nullptr) {
        return {};
    }

    if (mime->hasHtml()) {
        static const QRegularExpression imageSource(
            tr("<img[^>]+src\\s*=\\s*['\\\"]([^'\\\"]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = imageSource.match(mime->html());
        if (match.hasMatch()) {
            const QUrl url = QUrl::fromUserInput(match.captured(1));
            if (isRemoteImageUrl(url)) {
                return url;
            }
        }
    }

    const QString text = mime->text().trimmed();
    if (!text.isEmpty()) {
        const QUrl url = QUrl::fromUserInput(text);
        if (isRemoteImageUrl(url)) {
            return url;
        }
    }

    return {};
}

QString MainWindow::findModelPath() {
    const QString relativePath = QStringLiteral("model/chess-tiles-v2.onnx");
    QStringList candidates;
    candidates << QCoreApplication::applicationDirPath() + QLatin1Char('/') + relativePath;
    candidates << QDir::currentPath() + QLatin1Char('/') + relativePath;

#ifdef CHESSGUI_SOURCE_MODEL_PATH
    candidates << QStringLiteral(CHESSGUI_SOURCE_MODEL_PATH);
#endif

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return candidates.constFirst();
}

bool MainWindow::hasSupportedImage(const QMimeData *mime) const {
    if (mime == nullptr) {
        return false;
    }

    if (hasImagePayload(mime)) {
        return true;
    }

    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if ((url.isLocalFile() && isSupportedImagePath(url.toLocalFile())) ||
                isRemoteImageUrl(url)) {
                return true;
            }
        }
    }

    return !remoteImageUrlFromMimeData(mime).isEmpty();
}

bool MainWindow::handleMimeData(const QMimeData *mime) {
    if (mime == nullptr) {
        return false;
    }

    const QImage image = imageFromMimeData(mime);
    if (!image.isNull()) {
        processImage(image, tr("Imported image"));
        return true;
    }

    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile() && isSupportedImagePath(url.toLocalFile())) {
                loadImageFile(url.toLocalFile());
                return true;
            }

            if (isRemoteImageUrl(url)) {
                loadRemoteImage(url);
                return true;
            }
        }
    }

    const QUrl remoteUrl = remoteImageUrlFromMimeData(mime);
    if (!remoteUrl.isEmpty()) {
        loadRemoteImage(remoteUrl);
        return true;
    }

    return false;
}

bool MainWindow::loadImageFile(const QString &filePath) {
    QString path = filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(
            this,
            tr("Load chessboard screenshot"),
            QString(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
    }

    if (path.isEmpty()) {
        return false;
    }

    const QImage image(path);
    if (image.isNull()) {
        setActivityMessage(
            tr("Could not load the image:\n%1").arg(path));
        return false;
    }

    processImage(image, QFileInfo(path).fileName());
    return true;
}

void MainWindow::loadRemoteImage(const QUrl &url) {
    if (!isRemoteImageUrl(url)) {
        return;
    }

    if (remoteImageReply_ != nullptr) {
        remoteImageReply_->abort();
        remoteImageReply_->deleteLater();
        remoteImageReply_ = nullptr;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      tr("ChessGui/1.0"));

    setActivityMessage(tr("Downloading image…"));
    remoteImageReply_ = networkManager_->get(request);
    QNetworkReply *reply = remoteImageReply_;

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != remoteImageReply_) {
            reply->deleteLater();
            return;
        }

        remoteImageReply_ = nullptr;
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        const QByteArray payload = reply->readAll();
        const QUrl sourceUrl = reply->url();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            setActivityMessage(
                tr("Could not download image: %1").arg(errorText));
            return;
        }

        const QImage image = QImage::fromData(payload);
        if (image.isNull()) {
            setActivityMessage(
                tr("The remote resource is not a supported image."));
            return;
        }

        processImage(image, sourceUrl.toString());
    });
}

void MainWindow::processImage(const QImage &sourceImage,
                              const QString &displayName) {
    if (sourceImage.isNull()) {
        setActivityMessage(tr("The image is empty."));
        return;
    }

    if (!visionWorker_->isReady()) {
        setActivityMessage(visionWorker_->errorString());
        return;
    }

    if (gameController_->isEngineAnalyzing()) {
        gameController_->stopAnalysis();
        resumeAnalysisAfterVision_ = true;
    }

    setActivityMessage(
        tr("Analyzing %1…").arg(displayName));

    ++visionRequestId_;
    const QImage image = sourceImage.convertToFormat(QImage::Format_ARGB32);
    const quint64 requestId = visionRequestId_;
    QMetaObject::invokeMethod(visionWorker_, [this, image, requestId] {
        visionWorker_->processImage(image, requestId);
    });
}

void MainWindow::adjustEnginePanelSize() {
    // Only respond to section toggles once the window is laid out. Before the
    // window is shown the splitter sizes and the widget size hints are not
    // meaningful, and resizing here would discard the restored configuration.
    if (!isVisible() || !mainSplitter_ || !engineOutputWidget_) {
        return;
    }
    const QList<int> sizes = mainSplitter_->sizes();
    if (sizes.size() != 2) {
        return;
    }
    mainSplitter_->setSizes({sizes.first(), engineOutputWidget_->sizeHint().height()});
}

void MainWindow::onVisionResult(const VisionResult &result) {
    if (result.requestId != visionRequestId_) {
        // A newer image was requested while this one was being processed.
        return;
    }

    if (!result.result.has_value()) {
        resumeAnalysisAfterVision_ = false;
        setActivityMessage(result.errorMessage);
        return;
    }

    const FenRecognizer::Result &recognized = *result.result;

    QStringList fenFields = recognized.fen.split(QChar(' '), Qt::SkipEmptyParts);
    if (fenFields.size() >= 2) {
        fenFields[1] = whiteToPlayCheckBox_->isChecked()
                           ? tr("w")
                           : tr("b");
    }
    const QString fen = fenFields.join(QChar(' '));

    if (!gameController_->loadFen(fen,
                                  tr("Position detected from image."))) {
        resumeAnalysisAfterVision_ = false;
        setActivityMessage(tr("The detected FEN is invalid."));
        return;
    }

    clearLoadedPgnSource();

    if (resumeAnalysisAfterVision_) {
        resumeAnalysisAfterVision_ = false;
        gameController_->startAnalysis();
    }

        const QString confidence =
            tr("mean confidence %1%, minimum %2%")
                .arg(recognized.meanConfidence * 100.0f, 0, 'f', 1)
                .arg(recognized.minConfidence * 100.0f, 0, 'f', 1);
        const QString warning = recognized.minConfidence < 0.70f
                                    ? tr(" — please verify")
                                    : QString();
        setActivityMessage(
            tr("Chessboard detected (%1), orientation %2, %3%4")
                .arg(result.snapped ? tr("aligned")
                                    : tr("raw"))
                .arg(recognized.orientation == FenRecognizer::Orientation::Flipped180
                         ? tr("flipped 180°")
                         : tr("auto"))
                .arg(confidence)
                .arg(warning));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::DragEnter ||
        event->type() == QEvent::DragMove) {
        auto *dragEvent = static_cast<QDropEvent *>(event);
        if (hasSupportedImage(dragEvent->mimeData())) {
            dragEvent->acceptProposedAction();
        } else {
            dragEvent->ignore();
        }
        return true;
    }

    if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (!hasSupportedImage(dropEvent->mimeData())) {
            dropEvent->ignore();
            return true;
        }

        dropEvent->acceptProposedAction();
        handleMimeData(dropEvent->mimeData());
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::updateEvaluation() {
    gameController_->updateEvaluation();
}

void MainWindow::loadPgn() {
    loadPgnFile();
}

bool MainWindow::loadPgnFile(const QString &filePath) {
    QString path = filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(
            this,
            tr("Load PGN File"),
            QString(),
            tr("PGN Files (*.pgn);;All Files (*)")
        );
    }

    if (path.isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file:\n%1").arg(path));
        return false;
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    return loadPgnContent(content);
}

bool MainWindow::loadPgnContent(const QString &pgnContent, int selectedGameIndex) {
    const QVector<PgnFile::GameSegment> segments =
        PgnFile::splitGameSegments(pgnContent);
    QStringList games;
    for (const PgnFile::GameSegment &segment : segments) {
        games.append(segment.text);
    }

    QString game;
    int selectedIndex = -1;
    if (games.size() <= 1) {
        if (selectedGameIndex > 0) {
            return false;
        }
        game = games.isEmpty() ? pgnContent : games.first();
        selectedIndex = segments.isEmpty() ? -1 : 0;
    } else if (selectedGameIndex >= 0) {
        if (selectedGameIndex >= games.size()) {
            return false;
        }
        selectedIndex = selectedGameIndex;
        game = games.at(selectedIndex);
    } else {
        PgnSelectDialog dialog(games, this);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        selectedIndex = dialog.selectedIndex();
        if (selectedIndex < 0 || selectedIndex >= games.size()) {
            return false;
        }
        game = games.at(selectedIndex);
    }

    if (!gameController_->loadPgn(game)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to parse PGN content."));
        return false;
    }

    rememberLoadedPgnSource(pgnContent, segments, selectedIndex);
    return true;
}

void MainWindow::clearLoadedPgnSource() {
    loadedPgnContent_.clear();
    loadedPgnGames_.clear();
    loadedPgnGameSegments_.clear();
    selectedPgnGameIndex_ = -1;
}

void MainWindow::rememberLoadedPgnSource(
    const QString &content,
    const QVector<PgnFile::GameSegment> &segments,
    int selectedGameIndex) {
    loadedPgnContent_ = content;
    loadedPgnGames_.clear();
    loadedPgnGames_.reserve(segments.size());
    for (const PgnFile::GameSegment &segment : segments) {
        loadedPgnGames_.append(segment.text);
    }
    loadedPgnGameSegments_ = segments;
    selectedPgnGameIndex_ = selectedGameIndex >= 0 &&
                                    selectedGameIndex < segments.size()
                                ? selectedGameIndex
                                : -1;
}

void MainWindow::savePgn() {
    savePgnFile();
}

bool MainWindow::savePgnFile(const QString &filePath) {
    const QString currentGame = gameController_->pgnText();
    if (currentGame.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Save PGN"),
                                 tr("There is no game to save."));
        return false;
    }

    QString path = filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this,
            tr("Save PGN File"),
            QString(),
            tr("PGN Files (*.pgn);;All Files (*)")
        );
    }

    if (path.isEmpty()) {
        return false;
    }

    if (!path.endsWith(QStringLiteral(".pgn"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".pgn");
    }

    const bool replacingLoadedGame =
        loadedPgnGameSegments_.size() > 1 &&
        selectedPgnGameIndex_ >= 0 &&
        selectedPgnGameIndex_ < loadedPgnGameSegments_.size();
    QString pgnToSave = currentGame;
    if (replacingLoadedGame) {
        pgnToSave = PgnFile::replaceGame(loadedPgnContent_,
                                         loadedPgnGameSegments_,
                                         selectedPgnGameIndex_,
                                         currentGame);
    }

    QFile file(path);
    const QIODevice::OpenMode writeMode =
        replacingLoadedGame ? QIODevice::WriteOnly
                            : QIODevice::WriteOnly | QIODevice::Text;
    if (!file.open(writeMode)) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Could not open file for writing:\n%1").arg(path));
        return false;
    }

    file.write(pgnToSave.toUtf8());
    file.close();

    if (!loadedPgnGameSegments_.isEmpty()) {
        const QVector<PgnFile::GameSegment> savedSegments =
            PgnFile::splitGameSegments(pgnToSave);
        if (savedSegments.size() == loadedPgnGameSegments_.size()) {
            rememberLoadedPgnSource(pgnToSave, savedSegments,
                                    selectedPgnGameIndex_);
        }
    }

    setActivityMessage(tr("Game saved to %1").arg(path));
    return true;
}

bool MainWindow::pasteFen(const QString &fenText) {
    QString fen = fenText;
    if (fen.isEmpty()) {
        fen = QGuiApplication::clipboard()->text().trimmed();
    }

    if (fen.isEmpty()) {
        return false;
    }

    if (!gameController_->loadFen(fen, tr("Position loaded from FEN."))) {
        return false;
    }

    clearLoadedPgnSource();
    return true;
}

void MainWindow::pasteFromClipboard() {
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (handleMimeData(mime)) {
        return;
    }

    const QString text = mime != nullptr ? mime->text().trimmed() : QString();
    if (!text.isEmpty() && pasteFen(text)) {
        setActivityMessage(tr("FEN pasted from clipboard."));
        return;
    }

    setActivityMessage(
        tr("The clipboard does not contain an image or a valid FEN."));
}

void MainWindow::setWhiteToMove(bool whiteToMove) {
    if (gameController_->isComputerGameActive() ||
        gameController_->isComputerGamePending()) {
        const QSignalBlocker blocker(whiteToPlayCheckBox_);
        whiteToPlayCheckBox_->setChecked(
            gameController_->rules().currentPlayer() == Rules::Color::White);
        return;
    }

    if ((whiteToMove && gameController_->rules().currentPlayer() == Rules::Color::Black) ||
        (!whiteToMove && gameController_->rules().currentPlayer() == Rules::Color::White)) {
        clearLoadedPgnSource();
    }
    gameController_->setSideToMove(whiteToMove);
}

void MainWindow::playAgainstComputer() {
    if (!gameController_->isEngineConnected() &&
        !gameController_->hasRemoteEngine()) {
        QMessageBox::information(
            this,
            tr("Play against computer"),
            tr("Load and start a UCI engine before starting a game."));
        return;
    }

    ComputerGameDialog dialog(currentEngineName(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    whitePendulum_->stop();
    blackPendulum_->stop();
    clearLoadedPgnSource();
    gameController_->startComputerGame(dialog.settings());
}

void MainWindow::loadUciEngine() {
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Select UCI Chess Engine"),
        QString(),
        tr("Executables (*)")
    );

    if (fileName.isEmpty()) {
        return;
    }

    loadEngine(fileName);
}

void MainWindow::configureEngine() {
    const QString currentPath = uciEnginePath_.isEmpty()
                                     ? config_.uciEnginePath()
                                     : uciEnginePath_;
    const QList<UciOption> availableOptions = uciEngine()
                                                  ? uciEngine()->options()
                                                  : QList<UciOption>();
    EngineConfigurationDialog dialog(
        currentPath,
        availableOptions,
        config_.uciEngineOptions(),
        this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newPath = dialog.enginePath();
    config_.setUciEnginePath(newPath);
    config_.setUciEngineOptions(dialog.optionValues());

    if (newPath.isEmpty()) {
        stopEngine();
        uciEnginePath_.clear();
        if (!config_.save()) {
            QMessageBox::warning(this, tr("Engine configuration"),
                                 tr("Could not save the engine configuration."));
        }
        return;
    }

    // A local engine replaces any remote engine selection.
    gameController_->clearRemoteEngine();
    config_.setRemoteEngine(QString(), 9000, QString());

    const bool sameLoadedEngine = uciEngine()->isConnected() &&
                                  newPath == uciEnginePath_;
    if (sameLoadedEngine) {
        applyConfiguredEngineOptions();
        if (!config_.save()) {
            QMessageBox::warning(this, tr("Engine configuration"),
                                 tr("Could not save the engine configuration."));
        }
        return;
    }

    if (!loadEngine(newPath)) {
        // Keep the selected path in the configuration so it can be corrected
        // from this dialog and retried later.
        uciEnginePath_ = newPath;
        config_.save();
        QMessageBox::warning(
            this,
            tr("Engine configuration"),
            tr("Could not start the selected UCI engine."));
    }
}

void MainWindow::configureRemoteEngine() {
    RemoteEngineDialog dialog(config_.remoteEngineHost(),
                              config_.remoteEnginePort(),
                              config_.remoteEngineId(),
                              config_.remoteEngineAccessKey(),
                              this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString host = dialog.host();
    const quint16 port = dialog.port();
    const QString engineId = dialog.engineId();
    const QString accessKey = dialog.accessKey();
    if (host.isEmpty() || port == 0 || engineId.isEmpty()) {
        return;
    }

    stopEngine();
    uciEnginePath_.clear();
    gameController_->setRemoteEngine(host, port, engineId, dialog.engineName(),
                                     accessKey);

    // The remote engine replaces any local engine selection.
    config_.setUciEnginePath(QString());
    config_.setRemoteEngine(host, port, engineId,
                            dialog.engineName(), dialog.engineVersion(),
                            accessKey);

    if (!config_.save()) {
        QMessageBox::warning(this, tr("Remote engine configuration"),
                             tr("Could not save the remote engine configuration."));
    }
}

void MainWindow::configureUciOptions() {
    QList<UciOption> options;
    QString engineName;
    if (uciEngine() != nullptr && uciEngine()->isConnected()) {
        options = uciEngine()->options();
        engineName = uciEngine()->engineName();
    } else if (gatewayClient() != nullptr && gatewayClient()->isConnected()) {
        options = gatewayClient()->options();
        engineName = gatewayClient()->engineName();
    } else if (gameController_->hasRemoteEngine()) {
        engineName = config_.remoteEngineName();
    }

    UciOptionsDialog dialog(engineName, options, config_.uciEngineOptions(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    config_.setUciEngineOptions(dialog.optionValues());
    if (!config_.save()) {
        QMessageBox::warning(this, tr("Engine configuration"),
                             tr("Could not save the engine configuration."));
    }
    applyConfiguredEngineOptions();
}

void MainWindow::toggleAnalysis() {
    gameController_->toggleAnalysis();
}

void MainWindow::toggleGameAudit() {
    if (gameController_->isGameAuditActive()) {
        gameController_->cancelGameAudit();
    } else if (!gameController_->startGameAudit()) {
        setActivityMessage(tr("Game analysis is unavailable. Load a completed game and connect an engine."));
    }
}
void MainWindow::startEngineAnalysis() {
    gameController_->startAnalysis();
}

void MainWindow::pauseEngineAnalysis() {
    gameController_->stopAnalysis();
}

void MainWindow::stopEngineAnalysis() {
    if (gameController_->isComputerGameActive()) {
        return;
    }

    gameController_->stopAnalysis();
    if (engineOutputWidget_) {
        engineOutputWidget_->clearAnalysis();
    }
}

void MainWindow::newGame() {
    gameController_->newGame();
    clearLoadedPgnSource();
    if (engineOutputWidget_) {
        engineOutputWidget_->clearAnalysis();
    }
    board_->setBoardFlipped(false);
}

void MainWindow::stepBack() {
    gameController_->stepBack();
}

void MainWindow::stepForward() {
    gameController_->stepForward();
}

void MainWindow::clearBoardAnnotations() {
    if (board_) {
        board_->clearUserAnnotations();
    }
}

void MainWindow::updateNavigationActions() {
    if (stepBackAction_) {
        stepBackAction_->setEnabled(gameController_->canStepBack());
    }
    if (stepForwardAction_) {
        stepForwardAction_->setEnabled(gameController_->canStepForward());
    }
}

void MainWindow::stopEngine() {
    gameController_->stopEngine();
    if (engineOutputWidget_) {
        engineOutputWidget_->setEngineName(tr("UCI Engine"));
        engineOutputWidget_->setEngineStatus(tr("Disconnected"));
    }
    board_->clearMovePreviews();
}

void MainWindow::setComputerMovePreviewEnabled(bool enabled) {
    if (showComputerMoveCheckBox_ != nullptr) {
        const QSignalBlocker blocker(showComputerMoveCheckBox_);
        showComputerMoveCheckBox_->setChecked(enabled);
    }
    if (showComputerMoveAction_ != nullptr) {
        const QSignalBlocker blocker(showComputerMoveAction_);
        showComputerMoveAction_->setChecked(enabled);
    }

    gameController_->setComputerMovePreviewEnabled(enabled);

    config_.setComputerMovePreviewEnabled(enabled);
    config_.save();
}

void MainWindow::setRecommendedMovePreviewEnabled(bool enabled) {
    if (showRecommendedMoveCheckBox_ != nullptr) {
        const QSignalBlocker blocker(showRecommendedMoveCheckBox_);
        showRecommendedMoveCheckBox_->setChecked(enabled);
    }
    if (showRecommendedMoveAction_ != nullptr) {
        const QSignalBlocker blocker(showRecommendedMoveAction_);
        showRecommendedMoveAction_->setChecked(enabled);
    }

    gameController_->setRecommendedMovePreviewEnabled(enabled);

    config_.setRecommendedMovePreviewEnabled(enabled);
    config_.save();
}

void MainWindow::setLastMoveHighlightingEnabled(bool enabled) {
    if (highlightLastMoveCheckBox_ != nullptr) {
        const QSignalBlocker blocker(highlightLastMoveCheckBox_);
        highlightLastMoveCheckBox_->setChecked(enabled);
    }

    board_->setLastMoveHighlightingEnabled(enabled);

    config_.setHighlightLastMoveEnabled(enabled);
    config_.save();
}

void MainWindow::onEngineLoaded(const QString &name, const QString &author) {
    Q_UNUSED(author)
    engineOutputWidget_->setEngineName(name);
    engineOutputWidget_->setEngineStatus(tr("Ready"));
    applyConfiguredEngineOptions();
}

void MainWindow::applyConfiguredEngineOptions() {
    const QMap<QString, QString> configuredOptions = config_.uciEngineOptions();

    if (uciEngine() != nullptr && uciEngine()->isConnected()) {
        for (const UciOption &option : uciEngine()->options()) {
            if (option.type != UciOption::Type::Button && configuredOptions.contains(option.name)) {
                uciEngine()->setOption(option.name, configuredOptions.value(option.name));
            }
        }
        return;
    }

    if (gatewayClient() != nullptr && gatewayClient()->isConnected()) {
        for (const UciOption &option : gatewayClient()->options()) {
            if (option.type != UciOption::Type::Button && configuredOptions.contains(option.name)) {
                gatewayClient()->setOption(option.name, configuredOptions.value(option.name));
            }
        }
    }
}

void MainWindow::refreshEngineStateUi() {
    const UciEngine::State state = gameController_->engineState();
    const bool connected = gameController_->isEngineConnected();
    const bool analyzing = gameController_->isEngineAnalyzing();

    if (toggleAnalysisAction_) {
        const bool analysisAvailable = connected ||
                                       gameController_->hasRemoteEngine();
        toggleAnalysisAction_->setEnabled(analysisAvailable);
        toggleAnalysisAction_->setText(analyzing
                                           ? tr("Stop Analysis")
                                           : tr("Start Analysis"));
    }
    if (stopEngineAction_) {
        stopEngineAction_->setEnabled(state != UciEngine::State::Disconnected);
    }
    if (uciOptionsAction_) {
        uciOptionsAction_->setEnabled(connected ||
                                      gameController_->hasRemoteEngine());
    }

    if (gameController_->isComputerGameActive()) {
        engineOutputWidget_->setControlButtonsEnabled(false, false, connected);
    } else {
        switch (state) {
            case UciEngine::State::Disconnected:
                engineOutputWidget_->setControlButtonsEnabled(
                    gameController_->hasRemoteEngine(), false, false);
                break;
            case UciEngine::State::Connecting:
            case UciEngine::State::Authenticating:
            case UciEngine::State::Connected:
            case UciEngine::State::Initializing:
                engineOutputWidget_->setControlButtonsEnabled(false, false, false);
                break;
            case UciEngine::State::Ready:
                engineOutputWidget_->setControlButtonsEnabled(true, false, true);
                break;
            case UciEngine::State::Analyzing:
                engineOutputWidget_->setControlButtonsEnabled(false, true, true);
                break;
            case UciEngine::State::Stopping:
                engineOutputWidget_->setControlButtonsEnabled(false, false, false);
                break;
        }
    }

    QString statusStr;
    switch (state) {
        case UciEngine::State::Disconnected:
            statusStr = tr("Disconnected");
            break;
        case UciEngine::State::Connecting:
        case UciEngine::State::Authenticating:
        case UciEngine::State::Connected:
        case UciEngine::State::Initializing:
            statusStr = tr("Initializing");
            break;
        case UciEngine::State::Ready:
            statusStr = tr("Ready");
            break;
        case UciEngine::State::Analyzing:
            statusStr = tr("Analyzing");
            break;
        case UciEngine::State::Stopping:
            statusStr = tr("Stopping");
            break;
    }
    engineOutputWidget_->setEngineStatus(statusStr);
    refreshAuditUi();
}

void MainWindow::refreshAuditUi() {
    if (!analyzeGameAction_) return;
    const bool active = gameController_->isGameAuditActive();
    analyzeGameAction_->setEnabled(active || gameController_->canStartGameAudit());
    analyzeGameAction_->setText(active ? tr("Cancel Game Analysis") : tr("Analyze Game"));
    const QString description = active
        ? tr("Cancel the fixed-depth game analysis")
        : tr("Analyze the main line at depth 18 and mark inaccuracies, mistakes and blunders");
    analyzeGameAction_->setToolTip(description);
    analyzeGameAction_->setStatusTip(description);
}

void MainWindow::setActivityMessage(const QString &message) {
    visionStatusLabel_->setText(message);
    visionStatusLabel_->setToolTip(message);
    visionStatusLabel_->setAccessibleDescription(message);
    if (messageLog_) {
        messageLog_->append(message);
    }
}

QString MainWindow::currentEngineName() const {
    if (gameController_->isEngineConnected()) {
        return gameController_->engineName();
    }
    if (gameController_->hasRemoteEngine()) {
        return config_.remoteEngineName();
    }
    return uciEngine()->engineName();
}

void MainWindow::onEngineAnalysisUpdated(const EngineAnalysisLine &line) {
    engineOutputWidget_->updateAnalysisLine(line);
}

void MainWindow::onEngineError(const QString &errorMessage) {
    engineOutputWidget_->setEngineStatus(tr("Error"));
    engineOutputWidget_->appendUciLog(tr("ERROR: ") + errorMessage);
    board_->clearMovePreviews();
}

void MainWindow::setupUi() {
    setMinimumWidth(800);
    setMinimumHeight(600);
    setAcceptDrops(true);
    installEventFilter(this);

    auto *menubar = new QMenuBar(this);
    const auto configureToolAction = [](QAction *action, const QString &iconPath,
                                        const QString &description) {
        action->setIcon(QIcon(iconPath));
        action->setToolTip(description);
        action->setStatusTip(description);
    };

    // Menu File
    auto *menuFile = new QMenu(tr("File"), this);
    QAction *quitAction = menuFile->addAction(tr("Quit"));
    configureToolAction(quitAction, QStringLiteral(":/icons/toolbar-quit.svg"),
                        tr("Quit ChessGui"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    menubar->addMenu(menuFile);

    // Menu Games
    auto *menuGames = new QMenu(tr("Games"), this);
    newGameAction_ = menuGames->addAction(tr("New game"));
    newGameAction_->setObjectName(QStringLiteral("newGameAction"));
    configureToolAction(newGameAction_, QStringLiteral(":/icons/toolbar-new-game.svg"),
                        tr("Start a new game from the initial position"));
    newGameAction_->setShortcut(QKeySequence::New);
    connect(newGameAction_, &QAction::triggered, this, &MainWindow::newGame);
    menuGames->addSeparator();

    stepBackAction_ = menuGames->addAction(tr("Step back"));
    stepBackAction_->setObjectName(QStringLiteral("stepBackAction"));
    configureToolAction(stepBackAction_, QStringLiteral(":/icons/toolbar-step-back.svg"),
                        tr("Go back one move in the game"));
    stepBackAction_->setShortcut(QKeySequence(Qt::Key_Left));
    stepBackAction_->setShortcutContext(Qt::WindowShortcut);
    stepBackAction_->setEnabled(false);
    addAction(stepBackAction_);
    connect(stepBackAction_, &QAction::triggered, this, &MainWindow::stepBack);

    stepForwardAction_ = menuGames->addAction(tr("Step forward"));
    stepForwardAction_->setObjectName(QStringLiteral("stepForwardAction"));
    configureToolAction(stepForwardAction_, QStringLiteral(":/icons/toolbar-step-forward.svg"),
                        tr("Go forward one move in the game"));
    stepForwardAction_->setShortcut(QKeySequence(Qt::Key_Right));
    stepForwardAction_->setShortcutContext(Qt::WindowShortcut);
    stepForwardAction_->setEnabled(false);
    addAction(stepForwardAction_);
    connect(stepForwardAction_, &QAction::triggered, this, &MainWindow::stepForward);
    menuGames->addSeparator();

    playAgainstComputerAction_ = menuGames->addAction(
        tr("Play against computer"));
    configureToolAction(playAgainstComputerAction_,
                        QStringLiteral(":/icons/toolbar-play-computer.svg"),
                        tr("Start a game against the computer"));
    connect(playAgainstComputerAction_, &QAction::triggered,
            this, &MainWindow::playAgainstComputer);
    menuGames->addSeparator();

    showComputerMoveAction_ = menuGames->addAction(
        tr("Show computer's planned move"));
    showComputerMoveAction_->setObjectName(QStringLiteral("showComputerMoveAction"));
    showComputerMoveAction_->setCheckable(true);
    showComputerMoveAction_->setToolTip(
        tr("Draw the computer's next planned move as a dashed arrow"));
    showComputerMoveAction_->setStatusTip(showComputerMoveAction_->toolTip());

    showRecommendedMoveAction_ = menuGames->addAction(
        tr("Show recommended move"));
    showRecommendedMoveAction_->setObjectName(QStringLiteral("showRecommendedMoveAction"));
    showRecommendedMoveAction_->setCheckable(true);
    showRecommendedMoveAction_->setToolTip(
        tr("Draw the engine's recommended move as a solid arrow"));
    showRecommendedMoveAction_->setStatusTip(showRecommendedMoveAction_->toolTip());
    menuGames->addSeparator();

    loadPgnAction_ = menuGames->addAction(tr("Load PGN..."));
    configureToolAction(loadPgnAction_, QStringLiteral(":/icons/toolbar-load-pgn.svg"),
                        tr("Load a PGN game"));
    loadPgnAction_->setShortcut(QKeySequence::Open);
    connect(loadPgnAction_, &QAction::triggered, this, &MainWindow::loadPgn);

    savePgnAction_ = menuGames->addAction(tr("Save PGN..."));
    configureToolAction(savePgnAction_, QStringLiteral(":/icons/toolbar-save-pgn.svg"),
                        tr("Save the current game to a PGN file"));
    savePgnAction_->setShortcut(QKeySequence::Save);
    savePgnAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(savePgnAction_);
    connect(savePgnAction_, &QAction::triggered, this, &MainWindow::savePgn);

    QAction *loadImageAction = menuGames->addAction(tr("Load screenshot..."));
    configureToolAction(loadImageAction,
                        QStringLiteral(":/icons/toolbar-load-screenshot.svg"),
                        tr("Load a chessboard screenshot"));
    connect(loadImageAction, &QAction::triggered, this, [this] {
        loadImageFile();
    });

    pasteFenAction_ = menuGames->addAction(tr("Paste FEN or screenshot"));
    configureToolAction(pasteFenAction_, QStringLiteral(":/icons/toolbar-paste.svg"),
                        tr("Paste a FEN position or screenshot"));
    pasteFenAction_->setShortcut(QKeySequence::Paste); // Ctrl+V
    pasteFenAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(pasteFenAction_);
    connect(pasteFenAction_, &QAction::triggered,
            this, &MainWindow::pasteFromClipboard);
    menuGames->addSeparator();

    clearAnnotationsAction_ = menuGames->addAction(tr("Clear board annotations"));
    clearAnnotationsAction_->setObjectName(QStringLiteral("clearAnnotationsAction"));
    clearAnnotationsAction_->setShortcut(QKeySequence(Qt::Key_Escape));
    clearAnnotationsAction_->setShortcutContext(Qt::WindowShortcut);
    clearAnnotationsAction_->setToolTip(
        tr("Clear all user-drawn arrows and square highlights"));
    clearAnnotationsAction_->setStatusTip(clearAnnotationsAction_->toolTip());
    addAction(clearAnnotationsAction_);
    connect(clearAnnotationsAction_, &QAction::triggered,
            this, &MainWindow::clearBoardAnnotations);

    menubar->addMenu(menuGames);

    // Menu Engine
    auto *menuEngine = new QMenu(tr("Engine"), this);
    QAction *loadEngineAction = menuEngine->addAction(tr("Load UCI Engine..."));
    configureToolAction(loadEngineAction,
                        QStringLiteral(":/icons/toolbar-load-engine.svg"),
                        tr("Load a UCI chess engine"));
    connect(loadEngineAction, &QAction::triggered, this, &MainWindow::loadUciEngine);

    configureRemoteEngineAction_ = menuEngine->addAction(tr("Remote Engine..."));
    configureRemoteEngineAction_->setObjectName(QStringLiteral("configureRemoteEngineAction"));
    configureToolAction(configureRemoteEngineAction_,
                        QStringLiteral(":/icons/toolbar-configure.svg"),
                        tr("Select a remote engine on a chessgateway server"));
    connect(configureRemoteEngineAction_, &QAction::triggered,
            this, &MainWindow::configureRemoteEngine);

    configureEngineAction_ = menuEngine->addAction(tr("Configure"));
    configureToolAction(configureEngineAction_,
                        QStringLiteral(":/icons/toolbar-configure.svg"),
                        tr("Configure the chess engine"));
    connect(configureEngineAction_, &QAction::triggered,
            this, &MainWindow::configureEngine);

    uciOptionsAction_ = menuEngine->addAction(tr("UCI Options..."));
    uciOptionsAction_->setObjectName(QStringLiteral("uciOptionsAction"));
    configureToolAction(uciOptionsAction_,
                        QStringLiteral(":/icons/toolbar-configure.svg"),
                        tr("Configure the UCI options of the active engine"));
    uciOptionsAction_->setEnabled(false);
    connect(uciOptionsAction_, &QAction::triggered,
            this, &MainWindow::configureUciOptions);

    toggleAnalysisAction_ = menuEngine->addAction(tr("Start Analysis"));
    configureToolAction(toggleAnalysisAction_,
                        QStringLiteral(":/icons/toolbar-analysis.svg"),
                        tr("Start or stop engine analysis"));
    toggleAnalysisAction_->setEnabled(false);
    connect(toggleAnalysisAction_, &QAction::triggered, this, &MainWindow::toggleAnalysis);

    analyzeGameAction_ = menuEngine->addAction(tr("Analyze Game"));
    analyzeGameAction_->setObjectName(QStringLiteral("analyzeGameAction"));
    configureToolAction(analyzeGameAction_,
                        QStringLiteral(":/icons/toolbar-analysis.svg"),
                        tr("Analyze the main line at depth 18"));
    analyzeGameAction_->setEnabled(false);
    connect(analyzeGameAction_, &QAction::triggered, this, &MainWindow::toggleGameAudit);

    stopEngineAction_ = menuEngine->addAction(tr("Disconnect Engine"));
    configureToolAction(stopEngineAction_,
                        QStringLiteral(":/icons/toolbar-disconnect-engine.svg"),
                        tr("Disconnect the chess engine"));
    stopEngineAction_->setEnabled(false);
    connect(stopEngineAction_, &QAction::triggered, this, &MainWindow::stopEngine);

    menubar->addMenu(menuEngine);
    menubar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *toolBar = new QToolBar(tr("Main toolbar"), this);
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setIconSize(QSize(24, 24));
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolBar->addAction(quitAction);
    toolBar->addSeparator();
    toolBar->addAction(newGameAction_);
    toolBar->addAction(stepBackAction_);
    toolBar->addAction(stepForwardAction_);
    toolBar->addAction(playAgainstComputerAction_);
    toolBar->addSeparator();
    toolBar->addAction(loadPgnAction_);
    toolBar->addAction(savePgnAction_);
    toolBar->addAction(loadImageAction);
    toolBar->addAction(pasteFenAction_);
    toolBar->addSeparator();
    toolBar->addAction(loadEngineAction);
    toolBar->addAction(configureEngineAction_);
    toolBar->addAction(toggleAnalysisAction_);
    toolBar->addAction(analyzeGameAction_);
    toolBar->addAction(stopEngineAction_);

    auto *layout = new QGridLayout(this);
    setLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(menubar, 0, 0);
    layout->addWidget(toolBar, 1, 0);

    // Main vertical splitter: splits top area (board + move history) and bottom area (engine output)
    mainSplitter_ = new QSplitter(Qt::Vertical, this);
    layout->addWidget(mainSplitter_, 2, 0);
    layout->setRowStretch(2, 1);

    // Top horizontal splitter: splits board on the left and move history on the right
    topSplitter_ = new QSplitter(Qt::Horizontal, mainSplitter_);

    // Left pane: Evaluation bar + Chess board
    auto *boardContainer = new QWidget(topSplitter_);
    auto *boardLayout = new QHBoxLayout(boardContainer);
    boardLayout->setContentsMargins(8, 8, 8, 8);
    boardLayout->setSpacing(8);

    evaluationBar_ = new EvaluationBar(boardContainer);
    auto *boardPanel = new QWidget(boardContainer);
    auto *boardPanelLayout = new QVBoxLayout(boardPanel);
    boardPanelLayout->setContentsMargins(0, 0, 0, 0);
    boardPanelLayout->setSpacing(4);

    board_ = new ChessBoard(boardPanel);
    board_->setAcceptDrops(true);
    board_->installEventFilter(this);

    blackPendulum_ = new PendulumWidget(PendulumWidget::PieceColor::Black,
                                        boardPanel);
    whitePendulum_ = new PendulumWidget(PendulumWidget::PieceColor::White,
                                        boardPanel);

    connect(whitePendulum_, &PendulumWidget::remainingMillisecondsChanged,
            this, [this](qint64 remaining) {
                gameController_->setRemainingTime(
                    remaining, blackPendulum_->remainingMilliseconds());
                if (remaining <= 0) {
                    gameController_->onClockExpired(Rules::Color::White);
                }
            });
    connect(blackPendulum_, &PendulumWidget::remainingMillisecondsChanged,
            this, [this](qint64 remaining) {
                gameController_->setRemainingTime(
                    whitePendulum_->remainingMilliseconds(), remaining);
                if (remaining <= 0) {
                    gameController_->onClockExpired(Rules::Color::Black);
                }
            });

    auto *gameControlBar = new QFrame(boardPanel);
    gameControlBar->setObjectName(QStringLiteral("gameControlBar"));
    gameControlBar->setFrameShape(QFrame::StyledPanel);
    gameControlBar->setFrameShadow(QFrame::Raised);
    gameControlBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *gameControlLayout = new QHBoxLayout(gameControlBar);
    gameControlLayout->setContentsMargins(4, 2, 4, 2);
    gameControlLayout->setSpacing(6);

    const auto addClock = [gameControlBar, gameControlLayout](
                              const QString &label,
                              PendulumWidget *pendulum,
                              const QString &objectName) {
        auto *clockContainer = new QWidget(gameControlBar);
        clockContainer->setObjectName(objectName);
        auto *clockLayout = new QHBoxLayout(clockContainer);
        clockLayout->setContentsMargins(0, 0, 0, 0);
        clockLayout->setSpacing(3);
        auto *clockLabel = new QLabel(label, clockContainer);
        clockLabel->setBuddy(pendulum);
        clockLayout->addWidget(clockLabel);
        clockLayout->addWidget(pendulum);
        gameControlLayout->addWidget(clockContainer);
    };

    addClock(tr("Black"), blackPendulum_, QStringLiteral("blackClockControl"));
    addClock(tr("White"), whitePendulum_, QStringLiteral("whiteClockControl"));

    visionStatusLabel_ = new QLabel(gameControlBar);
    visionStatusLabel_->setObjectName(QStringLiteral("activityStatusLabel"));
    visionStatusLabel_->setWordWrap(false);
    visionStatusLabel_->setMinimumWidth(0);
    visionStatusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    gameControlLayout->addWidget(visionStatusLabel_, 1);

    flipBoardButton_ = new QToolButton(gameControlBar);
    flipBoardButton_->setText(tr("Flip board"));
    flipBoardButton_->setIcon(QIcon(QStringLiteral(":/icons/flip-board.svg")));
    flipBoardButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    flipBoardButton_->setCheckable(true);
    flipBoardButton_->setToolTip(tr("Flip the board orientation"));
    flipBoardButton_->setStatusTip(flipBoardButton_->toolTip());
    flipBoardButton_->setAccessibleName(tr("Flip board orientation"));
    flipBoardButton_->setAccessibleDescription(
        tr("Display the chessboard from the opposite side"));
    gameControlLayout->addWidget(flipBoardButton_);

    auto *positionToolsButton = new QToolButton(gameControlBar);
    positionToolsButton->setObjectName(QStringLiteral("positionToolsButton"));
    positionToolsButton->setText(tr("Position tools"));
    positionToolsButton->setIcon(
        QIcon(QStringLiteral(":/icons/toolbar-configure.svg")));
    positionToolsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    positionToolsButton->setPopupMode(QToolButton::InstantPopup);
    positionToolsButton->setToolTip(
        tr("Change the side to move, move previews and last move highlighting"));
    positionToolsButton->setStatusTip(positionToolsButton->toolTip());
    positionToolsButton->setAccessibleName(tr("Position tools"));
    positionToolsButton->setAccessibleDescription(
        tr("Open position and move preview settings"));

    auto *positionToolsMenu = new QMenu(positionToolsButton);
    positionToolsMenu->setObjectName(QStringLiteral("positionToolsMenu"));
    positionToolsMenu->setAccessibleName(tr("Position tools"));
    positionToolsMenu->setAccessibleDescription(
        tr("Position and move preview settings"));
    positionToolsButton->setMenu(positionToolsMenu);
    gameControlLayout->addWidget(positionToolsButton);

    whiteToPlayCheckBox_ = new QCheckBox(tr("White to play"), positionToolsMenu);
    whiteToPlayCheckBox_->setChecked(true);
    auto *whiteToPlayAction = new QWidgetAction(positionToolsMenu);
    whiteToPlayAction->setObjectName(QStringLiteral("whiteToPlayWidgetAction"));
    whiteToPlayAction->setDefaultWidget(whiteToPlayCheckBox_);
    positionToolsMenu->addAction(whiteToPlayAction);

    showComputerMoveCheckBox_ = new QCheckBox(
        tr("Show computer's planned move"), positionToolsMenu);
    showComputerMoveCheckBox_->setObjectName(QStringLiteral("showComputerMoveCheckBox"));
    showComputerMoveCheckBox_->setToolTip(
        tr("Draw the computer's next planned move as a dashed arrow"));
    showComputerMoveCheckBox_->setAccessibleDescription(
        tr("Show the computer's next planned move on the chessboard"));
    auto *computerMoveAction = new QWidgetAction(positionToolsMenu);
    computerMoveAction->setObjectName(QStringLiteral("showComputerMoveWidgetAction"));
    computerMoveAction->setDefaultWidget(showComputerMoveCheckBox_);
    positionToolsMenu->addAction(computerMoveAction);

    showRecommendedMoveCheckBox_ = new QCheckBox(
        tr("Show recommended move"), positionToolsMenu);
    showRecommendedMoveCheckBox_->setObjectName(QStringLiteral("showRecommendedMoveCheckBox"));
    showRecommendedMoveCheckBox_->setToolTip(
        tr("Draw the engine's recommended move as a solid arrow"));
    showRecommendedMoveCheckBox_->setAccessibleDescription(
        tr("Show the engine's recommended move on the chessboard"));
    auto *recommendedMoveAction = new QWidgetAction(positionToolsMenu);
    recommendedMoveAction->setObjectName(QStringLiteral("showRecommendedMoveWidgetAction"));
    recommendedMoveAction->setDefaultWidget(showRecommendedMoveCheckBox_);
    positionToolsMenu->addAction(recommendedMoveAction);

    highlightLastMoveCheckBox_ = new QCheckBox(
        tr("Highlight last move"), positionToolsMenu);
    highlightLastMoveCheckBox_->setObjectName(QStringLiteral("highlightLastMoveCheckBox"));
    highlightLastMoveCheckBox_->setChecked(true);
    highlightLastMoveCheckBox_->setToolTip(
        tr("Highlight the squares of the last played move"));
    highlightLastMoveCheckBox_->setAccessibleDescription(
        tr("Highlight the source and destination squares of the last move on the chessboard"));
    auto *highlightLastMoveAction = new QWidgetAction(positionToolsMenu);
    highlightLastMoveAction->setObjectName(QStringLiteral("highlightLastMoveWidgetAction"));
    highlightLastMoveAction->setDefaultWidget(highlightLastMoveCheckBox_);
    positionToolsMenu->addAction(highlightLastMoveAction);

    connect(flipBoardButton_, &QToolButton::toggled,
            board_, &ChessBoard::setBoardFlipped);
    connect(board_, &ChessBoard::boardFlippedChanged,
            flipBoardButton_, &QToolButton::setChecked);

    boardPanelLayout->addWidget(gameControlBar);
    boardPanelLayout->addWidget(board_, 1);

    boardLayout->addWidget(evaluationBar_);
    boardLayout->addWidget(boardPanel, 1);

    boardPanel->setAcceptDrops(true);
    boardPanel->installEventFilter(this);
    boardContainer->setAcceptDrops(true);
    boardContainer->installEventFilter(this);

    // Keep the three splitter sections, with secondary content behind headers.
    rightSplitter_ = new QSplitter(Qt::Vertical, topSplitter_);
    rightSplitter_->setChildrenCollapsible(false);

    const auto addHistorySection = [this](const QString &title, const QString &name,
                                          QToolButton *&button, QTextEdit *&content) {
        auto *section = new HistorySection(rightSplitter_);
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        button = new QToolButton(section);
        button->setObjectName(name);
        button->setText(title);
        button->setAccessibleName(title);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setArrowType(Qt::RightArrow);
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(button);
        content = new QTextEdit(section);
        content->setReadOnly(true);
        content->setAccessibleName(title);
        layout->addWidget(content, 1);
        content->hide();
        section->updateHeight(button, false);
        rightSplitter_->addWidget(section);
    };
    addHistorySection(tr("Game information"), QStringLiteral("gameInformationButton"),
                      gameInformationButton_, pgnHeaderTextEdit_);

    auto *movesSection = new QWidget(rightSplitter_);
    auto *movesLayout = new QVBoxLayout(movesSection);
    movesLayout->setContentsMargins(0, 0, 0, 0);
    auto *navigation = new QHBoxLayout;
    navigation->addWidget(new QLabel(tr("Moves"), movesSection));
    for (auto *action : {stepBackAction_, stepForwardAction_}) {
        auto *button = new QToolButton(movesSection);
        button->setDefaultAction(action);
        button->setArrowType(action == stepBackAction_ ? Qt::LeftArrow : Qt::RightArrow);
        button->setAccessibleName(action->text());
        navigation->addWidget(button);
    }
    currentMoveLabel_ = new QLabel(tr("Start"), movesSection);
    currentMoveLabel_->setObjectName(QStringLiteral("currentMoveLabel"));
    currentMoveLabel_->setAccessibleName(tr("Displayed move"));
    currentMoveLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    navigation->addWidget(currentMoveLabel_, 1);
    movesLayout->addLayout(navigation);
    moveListWidget_ = new MoveListWidget(movesSection);
    movesLayout->addWidget(moveListWidget_, 1);
    rightSplitter_->addWidget(movesSection);

    addHistorySection(tr("Log"), QStringLiteral("messageLogButton"),
                      messageLogButton_, messageLog_);
    connect(gameInformationButton_, &QToolButton::toggled, this, [this](bool expanded) {
        setHistorySectionExpanded(0, expanded);
    });
    connect(messageLogButton_, &QToolButton::toggled, this, [this](bool expanded) {
        setHistorySectionExpanded(2, expanded);
    });
    rightSplitter_->setStretchFactor(0, 0);
    rightSplitter_->setStretchFactor(1, 1);
    rightSplitter_->setStretchFactor(2, 0);
    rightSplitter_->setSizes({140, 320, 120});

    topSplitter_->addWidget(boardContainer);
    topSplitter_->addWidget(rightSplitter_);
    topSplitter_->setAcceptDrops(true);
    topSplitter_->installEventFilter(this);
    topSplitter_->setStretchFactor(0, 3);
    topSplitter_->setStretchFactor(1, 1);

    // Bottom pane: Engine output widget (full width of MainWindow)
    engineOutputWidget_ = new EngineOutputWidget(mainSplitter_);

    connect(engineOutputWidget_, &EngineOutputWidget::analysisSectionToggled,
            this, &MainWindow::adjustEnginePanelSize);
    connect(engineOutputWidget_, &EngineOutputWidget::logSectionToggled,
            this, &MainWindow::adjustEnginePanelSize);
    connect(engineOutputWidget_, &EngineOutputWidget::detailsToggled,
            this, &MainWindow::adjustEnginePanelSize);

    mainSplitter_->addWidget(topSplitter_);
    mainSplitter_->addWidget(engineOutputWidget_);
    mainSplitter_->setAcceptDrops(true);
    mainSplitter_->installEventFilter(this);
    mainSplitter_->setStretchFactor(0, 3);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setSizes({420, 180});

    // Make the splitter handles visible: the default style draws a thin,
    // nearly invisible groove. The color is derived from the palette so it
    // contrasts on both light and dark themes. The stylesheet set on the
    // main splitter is inherited by the nested top splitter.
    const QPalette windowPalette = palette();
    const bool darkTheme = windowPalette.color(QPalette::Window).lightness() < 128;
    QColor handleColor = windowPalette.color(QPalette::Mid);
    handleColor = darkTheme ? handleColor.lighter(140) : handleColor.darker(125);
    const QColor hoverColor = darkTheme ? handleColor.lighter(140)
                                        : handleColor.darker(120);
    mainSplitter_->setStyleSheet(
        QStringLiteral("QSplitter::handle { background-color: %1; }"
                       "QSplitter::handle:hover { background-color: %2; }")
            .arg(handleColor.name(), hoverColor.name()));

    connect(board_, &ChessBoard::pieceMoved, this, &MainWindow::pieceMoved);
    connect(board_, &ChessBoard::userAnnotationsChanged, this, [this] {
        gameController_->setAnnotationsAtCursor(board_->userArrows(),
                                               board_->squareAnnotations());
    });

    setActivityMessage(
        tr("Paste or drop a chessboard screenshot to detect its position."));
}

void MainWindow::pieceMoved(QChar piece, Rules::Position oldPosition,
                            Rules::Position newPosition) {
    Q_UNUSED(piece)

    if (gameController_->isComputerGameActive() &&
        gameController_->rules().currentPlayer() != gameController_->computerColor()) {
        PendulumWidget *humanClock =
            gameController_->computerColor() == Rules::Color::White
                ? blackPendulum_
                : whitePendulum_;
        humanClock->stop();
    }

    // The board never mutated its position cache: the controller is the only
    // source of truth, and a rejected move simply leaves the board unchanged.
    gameController_->requestMove(oldPosition, newPosition);
}
