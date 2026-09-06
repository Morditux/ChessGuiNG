//
// Created by mordicus on 23/08/2026.
//

#ifndef CHESSGUI_MAINWINDOW_H
#define CHESSGUI_MAINWINDOW_H

#include <QImage>
#include <QStringList>
#include <QTextEdit>
#include <QWidget>

#include "appconfig.h"
#include "pgnfile.h"
#include "rules.h"

class ChessBoard;
class ChessGatewayClient;
class EngineOutputWidget;
class EvaluationBar;
class GameController;
class MoveListWidget;
class PendulumWidget;
class QThread;
class VisionWorker;
class UciEngine;
class QAction;
class QCheckBox;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QMimeData;
class QNetworkAccessManager;
class QNetworkReply;
class QSplitter;
class QToolButton;
class QUrl;
struct EngineAnalysisLine;
struct VisionResult;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &configFilePath = QString());
    ~MainWindow() override;

    [[nodiscard]] EvaluationBar *evaluationBar() const;
    [[nodiscard]] ChessBoard *chessBoard() const;
    [[nodiscard]] PendulumWidget *whitePendulum() const;
    [[nodiscard]] PendulumWidget *blackPendulum() const;
    [[nodiscard]] MoveListWidget *moveListWidget() const;
    [[nodiscard]] QTextEdit *pgnHeaderTextEdit() const;
    [[nodiscard]] QTextEdit *messageLogTextEdit() const;
    [[nodiscard]] EngineOutputWidget *engineOutputWidget() const;
    [[nodiscard]] UciEngine *uciEngine() const;
    [[nodiscard]] ChessGatewayClient *gatewayClient() const;
    [[nodiscard]] GameController *gameController() const;
    [[nodiscard]] QSplitter *mainSplitter() const;
    [[nodiscard]] QSplitter *topSplitter() const;
    [[nodiscard]] QSplitter *rightSplitter() const;
    [[nodiscard]] const AppConfig &config() const;
    [[nodiscard]] AppConfig &config();
    [[nodiscard]] QString uciEnginePath() const;
    [[nodiscard]] QString initialFen() const;
    [[nodiscard]] QCheckBox *whiteToPlayCheckBox() const;
    [[nodiscard]] QCheckBox *showComputerMoveCheckBox() const;
    [[nodiscard]] QCheckBox *showRecommendedMoveCheckBox() const;
    [[nodiscard]] QCheckBox *highlightLastMoveCheckBox() const;
    [[nodiscard]] QToolButton *flipBoardButton() const;
    [[nodiscard]] QLabel *visionStatusLabel() const;
    [[nodiscard]] QAction *clearAnnotationsAction() const;
    [[nodiscard]] QAction *analyzeGameAction() const;
    [[nodiscard]] QString loadedPgnContent() const;
    [[nodiscard]] QStringList loadedPgnGames() const;
    [[nodiscard]] int selectedPgnGameIndex() const;

    bool loadEngine(const QString &enginePath);
    void loadConfiguration(const QString &configFilePath = QString());
    void saveConfiguration();
    bool loadImageFile(const QString &filePath = QString());

public slots:
    void pieceMoved(QChar piece, Rules::Position oldPosition,
                    Rules::Position newPosition);
    void updateEvaluation();
    void loadPgn();
    bool loadPgnFile(const QString &filePath = QString());
    bool loadPgnContent(const QString &pgnContent, int selectedGameIndex = -1);
    void savePgn();
    bool savePgnFile(const QString &filePath = QString());
    bool pasteFen(const QString &fenText = QString());
    void pasteFromClipboard();
    void playAgainstComputer();
    void loadUciEngine();
    void configureEngine();
    void configureRemoteEngine();
    void configureUciOptions();
    void toggleAnalysis();
    void startEngineAnalysis();
    void pauseEngineAnalysis();
    void stopEngineAnalysis();
    void newGame();
    void stopEngine();
    void setComputerMovePreviewEnabled(bool enabled);
    void setRecommendedMovePreviewEnabled(bool enabled);
    void setLastMoveHighlightingEnabled(bool enabled);
    void setWhiteToMove(bool whiteToMove);
    void stepBack();
    void stepForward();
    void clearBoardAnnotations();
    void toggleGameAudit();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onEngineLoaded(const QString &name, const QString &author);
    void onEngineAnalysisUpdated(const EngineAnalysisLine &line);
    void onEngineError(const QString &errorMessage);
    void onVisionResult(const VisionResult &result);
    void adjustEnginePanelSize();

private:
    void setupUi();

    static bool isSupportedImagePath(const QString &path);
    static bool isRemoteImageUrl(const QUrl &url);
    static bool hasImagePayload(const QMimeData *mime);
    static QImage imageFromMimeData(const QMimeData *mime);
    static QUrl remoteImageUrlFromMimeData(const QMimeData *mime);
    static QString findModelPath();

    bool hasSupportedImage(const QMimeData *mime) const;
    bool handleMimeData(const QMimeData *mime);
    void processImage(const QImage &image, const QString &displayName);
    void loadRemoteImage(const QUrl &url);
    void applyConfiguredEngineOptions();
    void refreshEngineStateUi();
    void refreshAuditUi();
    void clearLoadedPgnSource();
    void rememberLoadedPgnSource(const QString &content,
                                 const QVector<PgnFile::GameSegment> &segments,
                                 int selectedGameIndex);
    void updateNavigationActions();
    void setHistorySectionExpanded(int section, bool expanded);
    void rememberExpandedHistorySizes();
    void setActivityMessage(const QString &message);
    [[nodiscard]] QString currentEngineName() const;

    GameController *gameController_ = nullptr;
    AppConfig config_;
    QString uciEnginePath_;

    EvaluationBar *evaluationBar_ = nullptr;
    ChessBoard *board_ = nullptr;
    PendulumWidget *whitePendulum_ = nullptr;
    PendulumWidget *blackPendulum_ = nullptr;
    MoveListWidget *moveListWidget_ = nullptr;
    QTextEdit *pgnHeaderTextEdit_ = nullptr;
    QTextEdit *messageLog_ = nullptr;
    QToolButton *gameInformationButton_ = nullptr;
    QToolButton *messageLogButton_ = nullptr;
    QLabel *currentMoveLabel_ = nullptr;
    EngineOutputWidget *engineOutputWidget_ = nullptr;
    QSplitter *mainSplitter_ = nullptr;
    QSplitter *topSplitter_ = nullptr;
    QSplitter *rightSplitter_ = nullptr;
    QCheckBox *whiteToPlayCheckBox_ = nullptr;
    QCheckBox *showComputerMoveCheckBox_ = nullptr;
    QCheckBox *showRecommendedMoveCheckBox_ = nullptr;
    QCheckBox *highlightLastMoveCheckBox_ = nullptr;
    QToolButton *flipBoardButton_ = nullptr;
    QLabel *visionStatusLabel_ = nullptr;
    QNetworkAccessManager *networkManager_ = nullptr;
    QNetworkReply *remoteImageReply_ = nullptr;

    QAction *loadPgnAction_ = nullptr;
    QAction *savePgnAction_ = nullptr;
    QAction *pasteFenAction_ = nullptr;
    QAction *newGameAction_ = nullptr;
    QAction *stepBackAction_ = nullptr;
    QAction *stepForwardAction_ = nullptr;
    QAction *configureEngineAction_ = nullptr;
    QAction *uciOptionsAction_ = nullptr;
    QAction *configureRemoteEngineAction_ = nullptr;
    QAction *playAgainstComputerAction_ = nullptr;
    QAction *showComputerMoveAction_ = nullptr;
    QAction *showRecommendedMoveAction_ = nullptr;
    QAction *toggleAnalysisAction_ = nullptr;
    QAction *stopEngineAction_ = nullptr;
    QAction *clearAnnotationsAction_ = nullptr;
    QAction *analyzeGameAction_ = nullptr;

    QString loadedPgnContent_;
    QStringList loadedPgnGames_;
    QVector<PgnFile::GameSegment> loadedPgnGameSegments_;
    int selectedPgnGameIndex_ = -1;

    VisionWorker *visionWorker_ = nullptr;
    QThread *visionThread_ = nullptr;
    quint64 visionRequestId_ = 0;
    bool resumeAnalysisAfterVision_ = false;
};

#endif // CHESSGUI_MAINWINDOW_H
