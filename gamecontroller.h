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
    struct AuditFinding {
        AuditSeverity severity = AuditSeverity::None;
        int ply = 0;
        int centipawnLoss = 0;
        QString bestMove;
        bool forcedMate = false;
    };

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

    // Fixed-depth (18), mainline-only engine audit. Results are committed as
    // one transaction so cancelling or losing the engine cannot leave a partial PGN.
    [[nodiscard]] bool canStartGameAudit() const;
    bool startGameAudit();
    void cancelGameAudit();
    [[nodiscard]] bool isGameAuditActive() const;
    [[nodiscard]] QVector<AuditFinding> auditFindings() const;

    // State
    [[nodiscard]] const Rules &rules() const;
    [[nodiscard]] QString initialFen() const;
    [[nodiscard]] QStringList uciMoves() const;
    [[nodiscard]] QStringList pgnMoves() const;
    [[nodiscard]] QString pgnHeaderText() const;
    [[nodiscard]] QString pgnText() const;
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
    void statusMessage(const QString &message);
    void computerTurnBegan();
    void humanTurnBegan();
    void gameFinished(const QString &result, const QString &message);
    void computerGameStateChanged(bool active);
    void computerMovePreviewChanged(const std::optional<Rules::Move> &move);
    void recommendedMovePreviewChanged(const std::optional<Rules::Move> &move);
    void annotationsChanged();
    void auditStateChanged(bool active);
    void auditProgressChanged(int completedPositions, int totalPositions);
    void auditCompleted(bool applied);

private:
    void sendPositionToEngine();
    void ensureRemoteEngine();
    void handleEngineStateChanged();
    void startEngineAnalysis();
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
    void finishGameAudit(bool applyResults, const QString &message);
    void restoreAfterGameAudit();
    bool recordMove(const Rules::Move &move);
    void appendPgnMove(const QString &san, Rules::Color movingColor);
    void rebuildPositionToCursor();
    void rebuildPgnHistory();
    void refreshMoveHistory();
    void updatePgnResult(const QString &result);
    [[nodiscard]] QString completedGameResult() const;
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

    ComputerGameSettings pendingComputerGameSettings_;
    ComputerGameSettings computerGameSettings_;
    bool pendingComputerGameStart_ = false;
    bool computerGameActive_ = false;
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
    QVector<AuditScore> auditScores_;
    QStringList auditBestMoves_;
    std::optional<EngineAnalysisLine> auditLatestLine_;
    QVector<AuditFinding> auditFindings_;
};

#endif // CHESSGUI_GAMECONTROLLER_H
