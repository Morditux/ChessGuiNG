//
// Controller for a chess game: owns the authoritative position, the PGN
// history, the UCI engine, the opening book and the computer-game state
// machine. UI independent.
//

#ifndef CHESSGUI_GAMECONTROLLER_H
#define CHESSGUI_GAMECONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "auditreports.h"
#include "book.h"
#include "computergamesettings.h"
#include "enginebackend.h"
#include "heuristiceval.h"
#include "pgnannotations.h"
#include "rules.h"
#include "uciengine.h"
#include "uciparser.h"

class ChessGatewayClient;

class GameController : public QObject {
    Q_OBJECT

public:
    // The finding type lives in auditreports.h so that the report dialog can
    // use it without depending on the controller.
    using AuditFinding = ::AuditFinding;

    explicit GameController(QObject *parent = nullptr);

    // Engine
    bool startEngine(const QString &executablePath);
    void stopEngine();
    [[nodiscard]] UciEngine *engine() const;
    [[nodiscard]] ChessGatewayClient *gatewayClient() const;

    // The local and the remote engines are mutually exclusive: these helpers
    // report the state of whichever backend is active.
    [[nodiscard]] bool isEngineConnected() const;
    [[nodiscard]] bool isEngineAnalyzing() const;
    [[nodiscard]] UciEngine::State engineState() const;
    [[nodiscard]] QString engineName() const;

    // Remote engine: configured and connected immediately; the engine is
    // loaded as soon as it is selected.
    void setRemoteEngine(const QString &host, quint16 port,
                         const QString &engineId,
                         const QString &engineName = QString(),
                         const QString &accessKey = QString());
    void clearRemoteEngine();
    [[nodiscard]] bool hasRemoteEngine() const;

    // Position and game setup
    bool loadFen(const QString &fen,
                 const QString &description = QStringLiteral("Position loaded from FEN."));
    bool loadPgn(const QString &pgnContent);
    void newGame();
    void setSideToMove(bool whiteToMove);

    // Human move intent; validates, records and continues the game flow.
    bool requestMove(Rules::Position from, Rules::Position to,
                     Rules::PieceType promotion = Rules::PieceType::None);
    [[nodiscard]] bool isPromotionMove(Rules::Position from, Rules::Position to) const;

    // Navigation through the played moves: the board position is moved back
    // or forward one ply without altering the recorded history. Disabled
    // while a computer game is active.
    bool stepBack();
    bool stepForward();
    bool goToStart();
    bool goToEnd();
    // Jumps directly to the position after the given ply (0 is the starting
    // position). Follows the same rules as stepBack/stepForward.
    bool goToMove(int moveIndex);
    [[nodiscard]] bool canStepBack() const;
    [[nodiscard]] bool canStepForward() const;
    [[nodiscard]] int moveCursor() const;

    // Removes played moves from the recorded history: one ply in free play,
    // and either the human's move (while the engine is to move) or the human's
    // move together with the engine's reply in a computer game. The engine
    // search in progress is stopped.
    [[nodiscard]] bool canTakeBack() const;
    bool takeBack();

    // Computer game
    void startComputerGame(const ComputerGameSettings &settings);
    void cancelComputerGame();
    void onClockExpired(Rules::Color color);
    void onEngineTimedMove(const QString &bestMove, const QString &ponder);
    void setRemainingTime(qint64 whiteMilliseconds, qint64 blackMilliseconds);

    // Analysis
    void toggleAnalysis();
    void startAnalysis();
    void stopAnalysis();
    void clearMovePreviews();
    void setComputerMovePreviewEnabled(bool enabled);
    void setRecommendedMovePreviewEnabled(bool enabled);
    void updateEvaluation();

    // Applies the limits used by the next analysis: a depth of 0 searches
    // without a depth limit and MultiPV is the number of principal variations.
    // A running analysis is restarted so that the new limits apply at once.
    void setAnalysisSettings(int depth, int multiPv);
    [[nodiscard]] int analysisDepth() const;
    [[nodiscard]] int analysisMultiPv() const;

    // Concessions. Resigning only applies to a game against the engine; a draw
    // offer is decided by the engine there and agreed between the two players
    // in free play.
    [[nodiscard]] bool canResign() const;
    void resign();
    [[nodiscard]] bool canOfferDraw() const;
    void offerDraw();

    // Draw claims (threefold repetition / fifty-move rule). The controller
    // adjudicates these draws automatically in computer games; this explicit
    // path covers free play and positions loaded from FEN or PGN.
    [[nodiscard]] bool canClaimDraw() const;
    void claimDraw();

    // Mainline-only engine audit. Results are committed as one transaction so
    // cancelling or losing the engine cannot leave a partial PGN, and they are
    // also summarised as an enriched report.
    [[nodiscard]] bool canStartGameAudit() const;
    bool startGameAudit();
    void cancelGameAudit();
    [[nodiscard]] bool isGameAuditActive() const;
    [[nodiscard]] QVector<AuditFinding> auditFindings() const;

    // Search depth of the audit, 18 by default. It is used by the next run and
    // by the score gate while the audit runs.
    void setGameAuditDepth(int depth);
    [[nodiscard]] int gameAuditDepth() const;

    // Enriched result of the last completed audit. It is invalidated as soon
    // as the game it describes changes.
    [[nodiscard]] const AuditReport &auditReport() const;

    // State
    [[nodiscard]] const Rules &rules() const;
    [[nodiscard]] QString initialFen() const;
    [[nodiscard]] QStringList uciMoves() const;
    [[nodiscard]] QStringList pgnMoves() const;
    [[nodiscard]] QString pgnHeaderText() const;
    [[nodiscard]] QString pgnText() const;
    // Material difference in centipawns from the given colour's point of view,
    // positive when that colour has more material on the board; kings are
    // ignored. Used by the UI to report each player's material balance.
    [[nodiscard]] int materialBalance(Rules::Color color) const;

    // White's display percentage (0-100) after each ply of the game, index 0
    // being the starting position: the same unit as evaluationChanged, so the
    // curve and the evaluation bar cannot disagree. Computed with the
    // heuristic evaluator and refined ply by ply while a game audit runs.
    [[nodiscard]] QVector<double> evaluationCurve() const;
    [[nodiscard]] bool isComputerGameActive() const;
    [[nodiscard]] bool isComputerGamePending() const;
    [[nodiscard]] bool isComputerMovePreviewEnabled() const;
    [[nodiscard]] bool isRecommendedMovePreviewEnabled() const;
    [[nodiscard]] Rules::Color computerColor() const;
    [[nodiscard]] const ComputerGameSettings &currentComputerGameSettings() const;

    // Visual annotations
    [[nodiscard]] std::vector<UserArrow> arrowsAtCursor() const;
    [[nodiscard]] std::vector<SquareAnnotation> squaresAtCursor() const;
    [[nodiscard]] QString commentAtCursor() const;
    void setAnnotationsAtCursor(const std::vector<UserArrow> &arrows,
                                const std::vector<SquareAnnotation> &squares,
                                const QString &comment = QString());
    void clearAnnotationsAtCursor();
    [[nodiscard]] bool hasAnnotationsAtCursor() const;

    [[nodiscard]] const std::vector<UserArrow> &arrowsAt(int ply) const;
    [[nodiscard]] const std::vector<SquareAnnotation> &squaresAt(int ply) const;
    [[nodiscard]] QString commentAt(int ply) const;
    [[nodiscard]] bool hasAnnotationsAt(int ply) const;
    [[nodiscard]] int annotationCount() const;
    [[nodiscard]] AuditAnnotation auditAt(int ply) const;

signals:
    void positionChanged();
    void historyChanged(const QString &pgnText);
    void evaluationChanged(double displayPercentage);
    // Same evaluation as evaluationChanged, in engine notation ("+1.35",
    // "-M3"), so the UI can show a number next to the bar. Emitted from the
    // heuristic evaluator when no engine reports a score.
    void evaluationScoreChanged(const QString &scoreText);
    // Emitted whenever the whole-game curve gains or loses a point.
    void evaluationCurveChanged();
    void statusMessage(const QString &message);
    void computerTurnBegan();
    void humanTurnBegan();
    // Emitted after a move played in a computer game, for the color that just
    // moved: the UI adds this increment to that player's clock.
    void clockIncrementGranted(Rules::Color color, qint64 milliseconds);
    void gameFinished(const QString &result, const QString &message);
    void computerGameStateChanged(bool active);
    void computerMovePreviewChanged(const std::optional<Rules::Move> &move);
    void recommendedMovePreviewChanged(const std::optional<Rules::Move> &move);
    void annotationsChanged();
    void auditStateChanged(bool active);
    void auditProgressChanged(int completedPositions, int totalPositions);
    void auditCompleted(bool applied);
    // Emitted when a new report is available or when the current one is
    // invalidated by a change to the game.
    void auditReportChanged();

private:
    void sendPositionToEngine();
    // Recomputes the whole-game curve with the heuristic evaluator. Used when
    // the game changes as a whole, while a single move only appends a point.
    void rebuildEvaluationCurve();
    // Colour to move at the given ply of the current game, which decides the
    // point of view of an engine score reported for that position.
    [[nodiscard]] Rules::Color sideToMoveAtPly(int ply) const;
    void ensureRemoteEngine();
    void handleEngineStateChanged();
    void startEngineAnalysis();
    // Starts an analysis with an explicit MultiPV count; the depth limit is
    // always the configured one. Move previews ask for a single variation.
    void startEngineAnalysis(int multiPv);
    void stopEngineAnalysis();
    void startEngineTimedSearch(qint64 whiteTimeMilliseconds,
                                qint64 blackTimeMilliseconds);
    void beginComputerGame();
    void startComputerTurn();
    void finishComputerGame(const QString &result, const QString &message);
    void startMovePreviewAnalysis();
    void updateMovePreviews(const EngineAnalysisLine &line);
    void onAnalysisLine(const EngineAnalysisLine &line);
    void onAuditBestMove(const QString &bestMove, const QString &ponder);
    void startNextAuditPosition();
    // Moves the board to the position the audit is currently analysing.
    void showAuditPosition();
    void finishGameAudit(bool applyResults, const QString &message);
    void restoreAfterGameAudit();
    // Clears the report of a game that no longer matches it.
    void invalidateAuditReport();
    // Standard algebraic notation of every main-line ply, replayed from the
    // initial position.
    [[nodiscard]] QVector<QString> mainLineSan() const;
    // Converts a UCI move into the notation of the given position, falling
    // back to the UCI text when it cannot be interpreted.
    [[nodiscard]] QString sanForUciMove(const Rules &position,
                                        const QString &uciMove) const;
    bool recordMove(const Rules::Move &move);
    void appendPgnMove(const QString &san, Rules::Color movingColor);
    void rebuildPositionToCursor();
    void rebuildPgnHistory();
    void refreshMoveHistory();
    void updatePgnResult(const QString &result);
    bool finishGameIfOver();
    [[nodiscard]] QString completedGameResult() const;
    [[nodiscard]] QString drawReasonMessage() const;
    [[nodiscard]] QString buildPgnMovetext() const;
    [[nodiscard]] QString formattedCommentAt(int ply) const;
    [[nodiscard]] static std::optional<Rules::Move> parseUciMove(const QString &moveText);
    [[nodiscard]] EngineBackend *selectedBackend() const;
    [[nodiscard]] EngineBackend *activeBackendForCommands() const;

    UciEngine *engine_ = nullptr;
    ChessGatewayClient *gateway_ = nullptr;
    EngineBackend *activeBackend_ = nullptr;
    Book book_;
    HeuristicEval heuristicEval_;
    Rules rules_;
    QString initialFen_;
    QString historyPrefix_;
    QStringList pgnMoves_;
    QStringList uciMoves_;
    QVector<PlyAnnotations> plyAnnotations_;
    QStringList pgnHeaders_;
    int pgnMoveNumber_ = 1;
    int moveCursor_ = 0;
    bool whiteToMove_ = true;
    QString pgnResult_ = QStringLiteral("*");

    QString remoteEngineHost_;
    quint16 remoteEnginePort_ = 0;
    QString remoteEngineId_;
    QString remoteEngineName_;
    QString remoteEngineAccessKey_;
    bool pendingAnalysisStart_ = false;
    bool pendingGatewaySelection_ = false;
    int analysisDepth_ = 0;
    int analysisMultiPv_ = 1;

    ComputerGameSettings pendingComputerGameSettings_;
    ComputerGameSettings computerGameSettings_;
    bool pendingComputerGameStart_ = false;
    bool computerGameActive_ = false;
    // True once a game was started against the engine, including after it
    // ended: a finished computer game is not resumed by a take-back.
    bool computerGameStarted_ = false;
    bool computerMovePreviewEnabled_ = false;
    bool recommendedMovePreviewEnabled_ = false;
    Rules::Color computerColor_ = Rules::Color::Black;
    qint64 whiteRemainingMs_ = 300000;
    qint64 blackRemainingMs_ = 300000;

    struct AuditScore {
        std::optional<double> centipawns;
        std::optional<int> mateIn;
    };
    bool auditActive_ = false;
    bool auditStartPending_ = false;
    bool auditRestorePending_ = false;
    bool auditResumeManualAnalysis_ = false;
    int auditSavedCursor_ = 0;
    int auditPosition_ = 0;
    int gameAuditDepth_ = 18;
    QVector<AuditScore> auditScores_;
    QStringList auditBestMoves_;
    std::optional<EngineAnalysisLine> auditLatestLine_;
    QVector<AuditFinding> auditFindings_;
    AuditReport auditReport_;
    // One display percentage for White per ply, starting position included.
    QVector<double> evaluationCurve_;
};

#endif // CHESSGUI_GAMECONTROLLER_H
