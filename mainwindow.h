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
#include "pgnannotations.h"
#include "pgnfile.h"
#include "rules.h"

class ChessBoard;
class ChessGatewayClient;
class AnalysisReportDialog;
class EngineOutputWidget;
class EvaluationBar;
class EvaluationGraph;
class GameController;
class MoveListWidget;
class PendulumWidget;
class PlayerStrip;
class QThread;
class VisionWorker;
class UciEngine;
class QAction;
class QCheckBox;
class QCloseEvent;
class QEvent;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QMimeData;
class QNetworkAccessManager;
class QNetworkReply;
class QSplitter;
class QToolButton;
class QUrl;
class QVBoxLayout;
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
    [[nodiscard]] QAction *claimDrawAction() const;
    [[nodiscard]] QAction *takeBackAction() const;
    [[nodiscard]] QAction *resignAction() const;
    [[nodiscard]] QAction *offerDrawAction() const;
    [[nodiscard]] QAction *analyzeGameAction() const;
    [[nodiscard]] QAction *analysisReportAction() const;
    [[nodiscard]] QAction *firstMoveAction() const;
    [[nodiscard]] QAction *lastMoveAction() const;
    [[nodiscard]] QAction *copyFenAction() const;
    [[nodiscard]] QAction *copyPgnAction() const;
    [[nodiscard]] QString loadedPgnContent() const;
    [[nodiscard]] QStringList loadedPgnGames() const;
    [[nodiscard]] int selectedPgnGameIndex() const;

    bool loadEngine(const QString &enginePath);
    void loadConfiguration(const QString &configFilePath = QString());
    void saveConfiguration();
    bool loadImageFile(const QString &filePath = QString());

    // Drag-and-drop entry points: a drop carries an image or a local PGN
    // file. They are public so that the drop path can be exercised without a
    // platform drag session.
    [[nodiscard]] bool hasSupportedDrop(const QMimeData *mime) const;
    bool handleMimeData(const QMimeData *mime);

public slots:
    void pieceMoved(QChar piece, Rules::Position oldPosition,
                    Rules::Position newPosition);
    void updateEvaluation();
    void loadPgn();
    bool loadPgnFile(const QString &filePath = QString());
    bool loadPgnContent(const QString &pgnContent, int selectedGameIndex = -1);
    void savePgn();
    bool savePgnFile(const QString &filePath = QString());
    void copyFen();
    void copyPgn();
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
    void goToStart();
    void goToEnd();
    void clearBoardAnnotations();
    void claimDraw();
    void takeBack();
    void resignGame();
    void offerDraw();
    void setAnalysisSettings(int depth, int multiPv);
    void toggleGameAudit();
    // Opens (or raises) the enriched report of the last game analysis.
    void showAnalysisReport();
    void setGameAuditDepth(int depth);

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
    void syncEvaluationBarGeometry();

private:
    void setupUi();

    // True when the current game has moves that differ from the last saved or
    // loaded PGN. Used to avoid losing a game on close.
    [[nodiscard]] bool hasUnsavedGame() const;
    // Returns false when the user cancels closing the window.
    bool confirmDiscardingUnsavedGame();

    static bool isSupportedImagePath(const QString &path);
    static bool isRemoteImageUrl(const QUrl &url);
    static bool hasImagePayload(const QMimeData *mime);
    static QImage imageFromMimeData(const QMimeData *mime);
    static QUrl remoteImageUrlFromMimeData(const QMimeData *mime);
    static QString findModelPath();

    bool hasSupportedImage(const QMimeData *mime) const;
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
    void setHistorySectionExpanded(int section, bool expanded, bool persist = true);
    // Collapses the PGN header box while it is empty and restores the stored
    // preference once a game brings headers.
    void updateGameInformationSection();
    // Applies the board orientation to the player strips and to the evaluation
    // gauge, which both read from the camp displayed at the bottom.
    void applyBoardOrientation();
    // Fills the strips from the controller: turn, names, material and clocks.
    void refreshPlayerStrips();
    // Fills the whole-game curve, its cursor and the audit markers.
    void refreshEvaluationGraph();
    // Hides the curve when the move panel is too short to keep both usable.
    void updateEvaluationGraphVisibility();
    // One annotation per ply, shared by the move list and the graph.
    [[nodiscard]] QVector<AuditAnnotation> auditAnnotationsByPly() const;
    [[nodiscard]] QString playerNameFor(Rules::Color color) const;
    void rememberExpandedHistorySizes();
    void setActivityMessage(const QString &message);
    [[nodiscard]] QString currentEngineName() const;

    GameController *gameController_ = nullptr;
    AppConfig config_;
    QString uciEnginePath_;

    EvaluationBar *evaluationBar_ = nullptr;
    EvaluationGraph *evaluationGraph_ = nullptr;
    ChessBoard *board_ = nullptr;
    PlayerStrip *whiteStrip_ = nullptr;
    PlayerStrip *blackStrip_ = nullptr;
    PendulumWidget *whitePendulum_ = nullptr;
    PendulumWidget *blackPendulum_ = nullptr;
    MoveListWidget *moveListWidget_ = nullptr;
    QWidget *movesSection_ = nullptr;
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
    QVBoxLayout *gaugeLayout_ = nullptr;
    QVBoxLayout *boardPanelLayout_ = nullptr;
    QHBoxLayout *activityBarLayout_ = nullptr;
    // True once the user opened or closed the PGN header box themselves: the
    // window then stops collapsing it while it is empty.
    bool gameInformationTouched_ = false;
    QLabel *visionStatusLabel_ = nullptr;
    QNetworkAccessManager *networkManager_ = nullptr;
    QNetworkReply *remoteImageReply_ = nullptr;

    QAction *loadPgnAction_ = nullptr;
    QAction *savePgnAction_ = nullptr;
    QAction *copyFenAction_ = nullptr;
    QAction *copyPgnAction_ = nullptr;
    QAction *pasteFenAction_ = nullptr;
    QAction *newGameAction_ = nullptr;
    QAction *firstMoveAction_ = nullptr;
    QAction *stepBackAction_ = nullptr;
    QAction *stepForwardAction_ = nullptr;
    QAction *lastMoveAction_ = nullptr;
    QAction *configureEngineAction_ = nullptr;
    QAction *uciOptionsAction_ = nullptr;
    QAction *configureRemoteEngineAction_ = nullptr;
    QAction *playAgainstComputerAction_ = nullptr;
    QAction *showComputerMoveAction_ = nullptr;
    QAction *showRecommendedMoveAction_ = nullptr;
    QAction *toggleAnalysisAction_ = nullptr;
    QAction *stopEngineAction_ = nullptr;
    QAction *clearAnnotationsAction_ = nullptr;
    QAction *claimDrawAction_ = nullptr;
    QAction *takeBackAction_ = nullptr;
    QAction *resignAction_ = nullptr;
    QAction *offerDrawAction_ = nullptr;
    QAction *analyzeGameAction_ = nullptr;
    QAction *analysisReportAction_ = nullptr;
    AnalysisReportDialog *analysisReportDialog_ = nullptr;

    QString loadedPgnContent_;
    QStringList loadedPgnGames_;
    QVector<PgnFile::GameSegment> loadedPgnGameSegments_;
    int selectedPgnGameIndex_ = -1;
    // PGN text as last saved to or loaded from a file; anything else means the
    // game has unsaved moves.
    QString persistedPgnText_;

    VisionWorker *visionWorker_ = nullptr;
    QThread *visionThread_ = nullptr;
    quint64 visionRequestId_ = 0;
    bool resumeAnalysisAfterVision_ = false;
};

#endif // CHESSGUI_MAINWINDOW_H
