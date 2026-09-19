//
// Controller for a chess game: owns the authoritative position, the PGN
// history, the UCI engine, the opening book and the computer-game state
// machine. UI independent.
//

#include "gamecontroller.h"

#include "curveworker.h"

#include "gatewayclient.h"
#include "pgnfile.h"
#include "uciengine.h"
#include "uciparser.h"

#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
    // An engine is assumed to accept a draw offer while it is not ahead by
    // more than half a pawn in the current position.
    constexpr int DrawAcceptanceThresholdCp = 50;
    // Time the heuristic search behind that decision may take: the answer is
    // needed now, and a deep evaluation setting must not freeze the interface.
    constexpr int DrawDecisionBudgetMs = 50;
}

GameController::GameController(QObject *parent)
    : QObject(parent)
    , engine_(new UciEngine(this))
    , gateway_(new ChessGatewayClient(this)) {
    plyAnnotations_.resize(1);

    // The curve runs one search per ply, so it lives on its own thread. The
    // worker can be stopped from here without a queued call because it only
    // polls atomic flags while it searches.
    curveThread_ = new QThread(this);
    curveWorker_ = new CurveWorker;
    curveWorker_->moveToThread(curveThread_);
    connect(curveThread_, &QThread::finished, curveWorker_, &QObject::deleteLater);
    connect(this, &GameController::curveComputeRequested, curveWorker_,
            &CurveWorker::computeCurve);
    connect(this, &GameController::positionEvaluationRequested, curveWorker_,
            &CurveWorker::evaluatePosition);
    connect(curveWorker_, &CurveWorker::progress, this,
            &GameController::onCurveProgress);
    connect(curveWorker_, &CurveWorker::pointScored, this,
            &GameController::onCurvePointScored);
    connect(curveWorker_, &CurveWorker::curveReady, this,
            &GameController::onCurveReady);
    connect(curveWorker_, &CurveWorker::cancelled, this,
            &GameController::onCurveCancelled);
    connect(curveWorker_, &CurveWorker::evaluationReady, this,
            &GameController::onEvaluationReady);
    curveThread_->start();

    requestEvaluationCurve();
    connect(engine_, &EngineBackend::stateChanged,
            this, [this](EngineBackend::State) { handleEngineStateChanged(); });
    connect(engine_, &EngineBackend::analysisUpdated,
            this, &GameController::onAnalysisLine);
    connect(engine_, &EngineBackend::timedBestMoveReceived,
            this, &GameController::onEngineTimedMove);
    connect(engine_, &EngineBackend::bestMoveReceived,
            this, &GameController::onAuditBestMove);

    connect(gateway_, &EngineBackend::stateChanged,
            this, [this](EngineBackend::State state) {
                if (state == EngineBackend::State::Connected &&
                    pendingGatewaySelection_) {
                    pendingGatewaySelection_ = false;
                    gateway_->selectEngine(remoteEngineId_);
                    return;
                }
                handleEngineStateChanged();
            });
    connect(gateway_, &EngineBackend::analysisUpdated,
            this, &GameController::onAnalysisLine);
    connect(gateway_, &EngineBackend::timedBestMoveReceived,
            this, &GameController::onEngineTimedMove);
    connect(gateway_, &EngineBackend::bestMoveReceived,
            this, &GameController::onAuditBestMove);
}

GameController::~GameController() {
    if (curveWorker_ != nullptr) {
        // Thread safe and non-blocking: the worker polls this while it
        // searches, so the thread stops in milliseconds instead of after a
        // whole curve.
        curveWorker_->stop();
    }
    if (curveThread_ != nullptr) {
        curveThread_->quit();
        curveThread_->wait();
    }
}

void GameController::handleEngineStateChanged() {
    if (activeBackend_ != nullptr &&
        activeBackend_->state() == EngineBackend::State::Disconnected) {
        activeBackend_ = nullptr;
    }

    const UciEngine::State state = engineState();

    if (state == UciEngine::State::Disconnected) {
        if (auditActive_) {
            finishGameAudit(false, tr("Game analysis stopped because the engine disconnected."));
        }
        pendingComputerGameStart_ = false;
        pendingAnalysisStart_ = false;
        pendingGatewaySelection_ = false;
        if (computerGameActive_) {
            finishComputerGame(QStringLiteral("*"),
                               tr("The engine disconnected."));
        }
    } else if (state == UciEngine::State::Ready) {
        if (auditActive_ && auditStartPending_) {
            auditStartPending_ = false;
            startNextAuditPosition();
        } else if (auditRestorePending_) {
            restoreAfterGameAudit();
        } else if (pendingComputerGameStart_) {
            beginComputerGame();
        } else if (pendingAnalysisStart_) {
            pendingAnalysisStart_ = false;
            startAnalysis();
        }
    }
}

bool GameController::startEngine(const QString &executablePath) {
    activeBackend_ = nullptr;
    const bool started = engine_->startEngine(executablePath);
    if (started) {
        activeBackend_ = engine_;
    }
    return started;
}

void GameController::stopEngine() {
    engine_->stopEngine();
    gateway_->disconnectFromHost();
    activeBackend_ = nullptr;
}

UciEngine *GameController::engine() const {
    return engine_;
}

ChessGatewayClient *GameController::gatewayClient() const {
    return gateway_;
}

bool GameController::isEngineConnected() const {
    const EngineBackend *backend = activeBackendForCommands();
    return backend != nullptr && backend->isOperational();
}

bool GameController::isEngineAnalyzing() const {
    const EngineBackend *backend = activeBackendForCommands();
    return backend != nullptr && backend->isAnalyzing();
}

UciEngine::State GameController::engineState() const {
    if (const EngineBackend *backend = selectedBackend(); backend != nullptr) {
        return backend->state();
    }
    return UciEngine::State::Disconnected;
}

QString GameController::engineName() const {
    if (const EngineBackend *backend = selectedBackend(); backend != nullptr &&
        !backend->engineName().isEmpty()) {
        return backend->engineName();
    }
    return engine_->engineName();
}

void GameController::setRemoteEngine(const QString &host, quint16 port,
                                     const QString &engineId,
                                     const QString &engineName,
                                     const QString &accessKey) {
    remoteEngineHost_ = host;
    remoteEnginePort_ = port;
    remoteEngineId_ = engineId;
    remoteEngineName_ = engineName;
    remoteEngineAccessKey_ = accessKey.trimmed();
    ensureRemoteEngine();
}

void GameController::clearRemoteEngine() {
    remoteEngineHost_.clear();
    remoteEnginePort_ = 0;
    remoteEngineId_.clear();
    remoteEngineName_.clear();
    remoteEngineAccessKey_.clear();
    gateway_->disconnectFromHost();
    if (activeBackend_ == gateway_) {
        activeBackend_ = nullptr;
    }
}

bool GameController::hasRemoteEngine() const {
    return !remoteEngineHost_.isEmpty() && remoteEnginePort_ != 0 &&
           !remoteEngineId_.isEmpty();
}

void GameController::ensureRemoteEngine() {
    if (engineState() != UciEngine::State::Disconnected || !hasRemoteEngine()) {
        return;
    }
    activeBackend_ = gateway_;
    pendingGatewaySelection_ = true;
    gateway_->connectToHost(remoteEngineHost_, remoteEnginePort_,
                            remoteEngineAccessKey_);
}

bool GameController::loadFen(const QString &fen, const QString &description) {
    Rules newRules;
    if (!newRules.loadFen(fen)) {
        return false;
    }

    computerGameActive_ = false;
    pendingComputerGameStart_ = false;
    computerGameStarted_ = false;
    invalidateAuditReport();
    historyPrefix_ = QStringLiteral("[FEN \"%1\"]\n\n%2").arg(fen, description);

    rules_ = newRules;
    initialFen_ = fen;
    uciMoves_.clear();
    pgnMoves_.clear();
    plyAnnotations_.clear();
    plyAnnotations_.resize(1);
    pgnHeaders_.clear();
    pgnResult_ = QStringLiteral("*");
    auditFindings_.clear();
    whiteToMove_ = (rules_.currentPlayer() == Rules::Color::White);
    pgnMoveNumber_ = 1;
    moveCursor_ = 0;
    requestEvaluationCurve();

    emit computerGameStateChanged(false);
    emit positionChanged();
    refreshMoveHistory();
    updateEvaluation();

    if (isEngineConnected()) {
        sendPositionToEngine();
        if (isEngineAnalyzing()) {
            startEngineAnalysis();
        }
    }

    return true;
}

void GameController::newGame() {
    computerGameActive_ = false;
    pendingComputerGameStart_ = false;
    computerGameStarted_ = false;
    invalidateAuditReport();
    historyPrefix_.clear();
    initialFen_.clear();
    clearMovePreviews();
    stopEngineAnalysis();

    rules_.reset();
    uciMoves_.clear();
    pgnMoves_.clear();
    plyAnnotations_.clear();
    plyAnnotations_.resize(1);
    pgnHeaders_.clear();
    pgnResult_ = QStringLiteral("*");
    auditFindings_.clear();
    whiteToMove_ = true;
    pgnMoveNumber_ = 1;
    moveCursor_ = 0;
    requestEvaluationCurve();

    emit computerGameStateChanged(false);
    emit positionChanged();
    refreshMoveHistory();
    updateEvaluation();

    if (isEngineConnected()) {
        sendPositionToEngine();
        stopEngineAnalysis();
    }
}

bool GameController::loadPgn(const QString &pgnContent) {
    Rules newRules;
    QStringList pgnFormatted;
    QStringList uciList;
    QStringList rawComments;

    if (!newRules.loadPgn(pgnContent, &pgnFormatted, &uciList, &rawComments)) {
        return false;
    }

    computerGameActive_ = false;
    pendingComputerGameStart_ = false;
    computerGameStarted_ = false;
    invalidateAuditReport();
    historyPrefix_.clear();
    clearMovePreviews();

    rules_ = newRules;

    initialFen_ = PgnFile::tagValue(pgnContent, QStringLiteral("FEN"));

    pgnMoves_ = pgnFormatted;
    uciMoves_ = uciList;
    plyAnnotations_.clear();
    plyAnnotations_.resize(uciMoves_.size() + 1);
    for (int i = 0; i < plyAnnotations_.size(); ++i) {
        if (i < rawComments.size() && !rawComments.at(i).isEmpty()) {
            PgnAnnotations::decode(rawComments.at(i),
                                   plyAnnotations_[i].arrows,
                                   plyAnnotations_[i].squares);
            if (const auto audit = PgnAnnotations::decodeAudit(rawComments.at(i)); audit.has_value()) {
                plyAnnotations_[i].audit = *audit;
            }
            plyAnnotations_[i].comment =
                PgnAnnotations::stripAuditTags(
                    PgnAnnotations::stripAnnotationTags(rawComments.at(i)));
        }
    }

    pgnHeaders_ = PgnFile::parseHeaders(pgnContent);
    pgnResult_ = PgnFile::tagValue(pgnContent, QStringLiteral("Result"));
    if (pgnResult_.isEmpty()) {
        pgnResult_ = QStringLiteral("*");
    }
    whiteToMove_ = (rules_.currentPlayer() == Rules::Color::White);
    pgnMoveNumber_ = (uciMoves_.size() / 2) + 1;
    moveCursor_ = uciMoves_.size();
    auditFindings_.clear();
    for (int ply = 1; ply < plyAnnotations_.size(); ++ply) {
        const AuditAnnotation audit = plyAnnotations_.at(ply).audit;
        if (audit.isValid()) auditFindings_.append({audit.severity, ply, audit.centipawnLoss,
                                                    audit.bestMove, audit.forcedMate});
    }
    requestEvaluationCurve();

    emit computerGameStateChanged(false);
    emit positionChanged();
    refreshMoveHistory();
    updateEvaluation();

    if (isEngineConnected()) {
        sendPositionToEngine();
        if (isEngineAnalyzing()) {
            startEngineAnalysis();
        }
    }

    return true;
}

void GameController::setSideToMove(bool whiteToMove) {
    if (computerGameActive_ || pendingComputerGameStart_) {
        return;
    }

    const QString currentFen = rules_.toFen();
    QStringList fields = currentFen.split(QChar(' '), Qt::SkipEmptyParts);
    if (fields.size() < 2) {
        return;
    }

    fields[1] = whiteToMove ? QStringLiteral("w") : QStringLiteral("b");
    const QString updatedFen = fields.join(QChar(' '));
    if (updatedFen == currentFen) {
        whiteToMove_ = whiteToMove;
        return;
    }

    loadFen(updatedFen,
            tr("Position loaded with %1 to move.")
                .arg(whiteToMove ? tr("White")
                                 : tr("Black")));
}

bool GameController::stepBack() {
    if (auditActive_ || !canStepBack()) {
        return false;
    }

    --moveCursor_;
    rebuildPositionToCursor();
    clearMovePreviews();

    emit positionChanged();
    updateEvaluation();

    if (isEngineConnected()) {
        sendPositionToEngine();
        if (isEngineAnalyzing()) {
            startEngineAnalysis();
        }
    }

    return true;
}

bool GameController::stepForward() {
    if (auditActive_ || !canStepForward()) {
        return false;
    }

    ++moveCursor_;
    rebuildPositionToCursor();
    clearMovePreviews();

    emit positionChanged();
    updateEvaluation();

    if (isEngineConnected()) {
        sendPositionToEngine();
        if (isEngineAnalyzing()) {
            startEngineAnalysis();
        }
    }

    return true;
}

bool GameController::canStepBack() const {
    return !auditActive_ && !computerGameActive_ && !pendingComputerGameStart_ &&
           moveCursor_ > 0;
}

bool GameController::canStepForward() const {
    return !auditActive_ && !computerGameActive_ && !pendingComputerGameStart_ &&
           moveCursor_ < uciMoves_.size();
}

bool GameController::goToStart() {
    return goToMove(0);
}

bool GameController::goToEnd() {
    return goToMove(uciMoves_.size());
}

bool GameController::goToMove(int moveIndex) {
    if (auditActive_ || computerGameActive_ || pendingComputerGameStart_) {
        return false;
    }

    const int target = qBound(0, moveIndex, uciMoves_.size());
    if (target == moveCursor_) {
        return false;
    }

    moveCursor_ = target;
    rebuildPositionToCursor();
    clearMovePreviews();

    emit positionChanged();
    updateEvaluation();

    if (isEngineConnected()) {
        sendPositionToEngine();
        if (isEngineAnalyzing()) {
            startEngineAnalysis();
        }
    }

    return true;
}

int GameController::moveCursor() const {
    return moveCursor_;
}

bool GameController::canTakeBack() const {
    if (auditActive_ || pendingComputerGameStart_ || uciMoves_.isEmpty()) {
        return false;
    }

    // Taking back is only defined for the current end of the game; use the
    // navigation actions to review an earlier position.
    if (moveCursor_ != uciMoves_.size()) {
        return false;
    }

    if (computerGameActive_) {
        // While the engine is to move, only the human's own last move can go
        // back; on the human's turn the engine's reply goes back with it.
        return rules_.currentPlayer() == computerColor_ || uciMoves_.size() >= 2;
    }

    // A finished game against the engine is not resumed by a take-back; the
    // player starts a new game instead. Free play and loaded games may always
    // step back from a result.
    return pgnResult_ == QStringLiteral("*") || !computerGameStarted_;
}

bool GameController::takeBack() {
    if (!canTakeBack()) {
        return false;
    }

    int plies = 1;
    if (computerGameActive_ && rules_.currentPlayer() != computerColor_) {
        plies = 2;
    }

    const bool resumeAnalysis = !computerGameActive_ && isEngineAnalyzing();
    stopEngineAnalysis();
    clearMovePreviews();

    const int remaining = qMax(0, uciMoves_.size() - plies);
    uciMoves_.resize(remaining);
    if (plyAnnotations_.size() > remaining + 1) {
        plyAnnotations_.resize(remaining + 1);
    }
    truncateCurve(remaining + 1);

    moveCursor_ = remaining;
    rebuildPositionToCursor();
    // Recomputes the notation and the move numbers of the surviving moves.
    rebuildPgnHistory();

    if (pgnResult_ != QStringLiteral("*")) {
        pgnResult_ = QStringLiteral("*");
        updatePgnResult(pgnResult_);
    }

    invalidateAuditReport();
    emit evaluationCurveChanged();
    refreshMoveHistory();
    updateEvaluation();
    emit positionChanged();

    emit statusMessage(plies == 2
                           ? tr("Your last move and the engine's reply were taken back.")
                           : tr("The last move was taken back."));

    if (computerGameActive_) {
        emit humanTurnBegan();
        if (isEngineConnected()) {
            sendPositionToEngine();
            startMovePreviewAnalysis();
        }
        return true;
    }

    if (isEngineConnected()) {
        sendPositionToEngine();
        if (resumeAnalysis) {
            startEngineAnalysis();
        }
    }

    return true;
}

bool GameController::isPromotionMove(Rules::Position from, Rules::Position to) const {
    if (auditActive_ || (computerGameActive_ && rules_.currentPlayer() == computerColor_)) {
        return false;
    }

    const auto movedPiece = rules_.pieceAt(from);
    if (!movedPiece.has_value() || movedPiece->type != Rules::PieceType::Pawn ||
        movedPiece->color != rules_.currentPlayer()) {
        return false;
    }

    const bool reachesPromotion = (movedPiece->color == Rules::Color::White && to.row == 0) ||
                                  (movedPiece->color == Rules::Color::Black && to.row == 7);
    if (!reachesPromotion) {
        return false;
    }

    return rules_.isValidMove(from, to);
}

bool GameController::requestMove(Rules::Position from, Rules::Position to,
                                 Rules::PieceType promotion) {
    if (auditActive_ || (computerGameActive_ && rules_.currentPlayer() == computerColor_)) {
        return false;
    }

    if (!rules_.isValidMove(from, to)) {
        return false;
    }

    const auto movedPiece = rules_.pieceAt(from);
    if (!movedPiece.has_value()) {
        return false;
    }

    const bool reachesPromotion = movedPiece->type == Rules::PieceType::Pawn &&
                                  (to.row == 0 || to.row == 7);
    Rules::PieceType effectivePromotion = Rules::PieceType::None;
    if (reachesPromotion) {
        effectivePromotion = (promotion != Rules::PieceType::None)
                                 ? promotion
                                 : Rules::PieceType::Queen;
    }

    if (!recordMove({from, to, effectivePromotion})) {
        return false;
    }

    if (computerGameActive_) {
        if (!finishGameIfOver()) {
            startComputerTurn();
        }
    } else if (isEngineConnected()) {
        sendPositionToEngine();
    }

    return true;
}

void GameController::startComputerGame(const ComputerGameSettings &settings) {
    pendingComputerGameSettings_ = settings;
    pendingComputerGameStart_ = true;
    computerGameActive_ = false;
    pendingAnalysisStart_ = false;

    if (isEngineAnalyzing()) {
        stopEngineAnalysis();
    } else if (engineState() == UciEngine::State::Ready) {
        beginComputerGame();
    } else {
        ensureRemoteEngine();
    }
}

void GameController::cancelComputerGame() {
    if (!computerGameActive_ && !pendingComputerGameStart_) {
        return;
    }
    finishComputerGame(QStringLiteral("*"), tr("Game cancelled."));
}

void GameController::onClockExpired(Rules::Color color) {
    if (!computerGameActive_) {
        return;
    }

    const QString result = color == Rules::Color::White
                               ? QStringLiteral("0-1")
                               : QStringLiteral("1-0");
    finishComputerGame(result,
                       color == Rules::Color::White
                           ? tr("White's time has expired.")
                           : tr("Black's time has expired."));
}

void GameController::onEngineTimedMove(const QString &bestMove,
                                       const QString &ponder) {
    Q_UNUSED(ponder)

    if (!computerGameActive_ ||
        rules_.currentPlayer() != computerColor_) {
        return;
    }

    const auto move = UciParser::parseMove(bestMove);
    if (!move.has_value() || !rules_.isValidMove(*move) ||
        !recordMove(*move)) {
        finishComputerGame(
            QStringLiteral("*"),
            tr("The engine returned an invalid move: %1")
                .arg(bestMove));
        return;
    }

    if (finishGameIfOver()) {
        return;
    }

    emit humanTurnBegan();
    emit statusMessage(tr("Your turn."));
    QTimer::singleShot(0, this, [this] {
        startMovePreviewAnalysis();
    });
}

void GameController::setRemainingTime(qint64 whiteMilliseconds,
                                      qint64 blackMilliseconds) {
    whiteRemainingMs_ = whiteMilliseconds;
    blackRemainingMs_ = blackMilliseconds;
}

void GameController::toggleAnalysis() {
    if (computerGameActive_ || auditActive_) {
        return;
    }

    if (isEngineAnalyzing()) {
        stopAnalysis();
    } else {
        startAnalysis();
    }
}

void GameController::startAnalysis() {
    if (computerGameActive_ || auditActive_) {
        return;
    }

    if (!isEngineConnected()) {
        pendingAnalysisStart_ = true;
        ensureRemoteEngine();
        return;
    }

    sendPositionToEngine();
    startEngineAnalysis();
}

void GameController::stopAnalysis() {
    if (computerGameActive_ || !isEngineConnected()) {
        return;
    }

    if (auditActive_) {
        // The search shown in the engine panel belongs to the game analysis:
        // stopping it cancels the whole run instead of letting it walk on to
        // the next position. A former manual analysis is not resumed, because
        // the user asked for the engine to stop.
        auditResumeManualAnalysis_ = false;
        cancelGameAudit();
        return;
    }

    stopEngineAnalysis();
    clearMovePreviews();
}

void GameController::startEngineAnalysis() {
    startEngineAnalysis(analysisMultiPv_);
}

void GameController::startEngineAnalysis(int multiPv) {
    if (EngineBackend *backend = activeBackendForCommands(); backend != nullptr) {
        backend->startAnalysis(analysisDepth_, multiPv);
    }
}

void GameController::stopEngineAnalysis() {
    if (EngineBackend *backend = activeBackendForCommands(); backend != nullptr) {
        backend->stopAnalysis();
    }
}

void GameController::startEngineTimedSearch(qint64 whiteTimeMilliseconds,
                                            qint64 blackTimeMilliseconds) {
    if (EngineBackend *backend = activeBackendForCommands(); backend != nullptr) {
        backend->startTimedSearch(whiteTimeMilliseconds, blackTimeMilliseconds,
                                  computerGameSettings_.incrementMilliseconds,
                                  computerGameSettings_.incrementMilliseconds);
    }
}

void GameController::clearMovePreviews() {
    emit computerMovePreviewChanged(std::nullopt);
    emit recommendedMovePreviewChanged(std::nullopt);
}

void GameController::setComputerMovePreviewEnabled(bool enabled) {
    computerMovePreviewEnabled_ = enabled;

    if (!enabled) {
        emit computerMovePreviewChanged(std::nullopt);
    }
    if (enabled) {
        startMovePreviewAnalysis();
    }
}

void GameController::setRecommendedMovePreviewEnabled(bool enabled) {
    recommendedMovePreviewEnabled_ = enabled;

    if (!enabled) {
        emit recommendedMovePreviewChanged(std::nullopt);
    }
    if (enabled) {
        startMovePreviewAnalysis();
        requestEvaluationRefinement();
    }
}

int GameController::evaluationDepth() const {
    return evaluationDepth_;
}

void GameController::setEvaluationDepth(int depth) {
    evaluationDepth_ = std::clamp(depth, 1, HeuristicEval::MaxSearchDepth);
}

void GameController::refreshEvaluation() {
    requestEvaluationCurve();
    updateEvaluation();
}

// Centipawns of the heuristic search at the configured evaluation depth, used
// where only the score (and not the mate distance) is needed. The caller has to
// answer now (the draw offer), so a short time budget keeps a deep setting from
// freezing the interface; the search then reports its best completed depth.
int GameController::evaluationCentipawns(const Rules &rules) const {
    HeuristicEval::SearchLimits limits;
    limits.maxMilliseconds = DrawDecisionBudgetMs;
    return HeuristicEval::search(rules, evaluationDepth_, 2, limits).centipawns;
}

void GameController::updateEvaluation() {
    // The static evaluation is instant, so the gauge, the score text and the
    // tooltip answer at once; the search that sees immediate tactics and the
    // mates inside the horizon runs on the worker and refines them a moment
    // later.
    const HeuristicEval::EvalBreakdown breakdown =
        HeuristicEval::evaluateBreakdown(rules_);
    // A checkmate already on the board is reported as exactly MateScore, which
    // the score text labels "Mate" whatever the distance the search would find.
    const std::optional<int> staticMate =
        std::abs(breakdown.total) >= HeuristicEval::MateScore
            ? std::optional<int>(0)
            : std::nullopt;
    emit evaluationChanged(
        HeuristicEval::centipawnsToPercentage(breakdown.total));
    emit evaluationScoreChanged(
        UciParser::formatScore(breakdown.total, staticMate));
    emit evaluationBreakdownChanged(breakdown);
    requestEvaluationRefinement();
}

void GameController::requestEvaluationRefinement() {
    const quint64 requestId = ++evaluationRequestId_;
    // A depth of one answers from the static evaluation and the captures alone,
    // so there is nothing worth a round trip to the worker.
    if (evaluationDepth_ <= 1) {
        updateHintPreview(
            HeuristicEval::search(rules_, evaluationDepth_).bestMove);
        return;
    }
    emit positionEvaluationRequested(requestId, rules_.toFen(), evaluationDepth_,
                                     2);
}

void GameController::onEvaluationReady(
    quint64 requestId, int centipawns, bool hasMate, int mateIn,
    const std::optional<Rules::Move> &bestMove) {
    // A superseded request describes another position, and while the engine is
    // analysing it owns the score it reports.
    if (requestId != evaluationRequestId_ || isEngineAnalyzing()) {
        return;
    }
    const std::optional<int> mate =
        hasMate ? std::optional<int>(mateIn) : std::nullopt;
    emit evaluationChanged(HeuristicEval::centipawnsToPercentage(centipawns));
    emit evaluationScoreChanged(UciParser::formatScore(centipawns, mate));
    // The refinement is also the curve point of the position at the cursor.
    updateCurvePoint(moveCursor_,
                     HeuristicEval::centipawnsToPercentage(centipawns));
    updateHintPreview(bestMove);
}

void GameController::updateHintPreview(
    const std::optional<Rules::Move> &bestMove) {
    // The engine draws the recommended-move arrow whenever it is connected, so
    // the heuristic only steps in without one.
    if (!recommendedMovePreviewEnabled_ || isEngineConnected()) {
        return;
    }
    if (rules_.isGameOver() || !bestMove.has_value() ||
        !rules_.isValidMove(*bestMove)) {
        emit recommendedMovePreviewChanged(std::nullopt);
        return;
    }
    emit recommendedMovePreviewChanged(*bestMove);
}

bool GameController::canStartGameAudit() const {
    return !auditActive_ && !computerGameActive_ && !pendingComputerGameStart_ &&
           !uciMoves_.isEmpty() && isEngineConnected();
}

bool GameController::startGameAudit() {
    if (!canStartGameAudit()) {
        return false;
    }

    invalidateAuditReport();
    auditActive_ = true;
    auditSavedCursor_ = moveCursor_;
    auditPosition_ = 0;
    auditScores_.clear();
    auditScores_.resize(uciMoves_.size() + 1);
    auditBestMoves_.fill(QString(), uciMoves_.size() + 1);
    auditLatestLine_.reset();
    auditResumeManualAnalysis_ = isEngineAnalyzing();
    auditStartPending_ = true;
    emit auditStateChanged(true);
    emit auditProgressChanged(0, auditScores_.size());
    emit statusMessage(tr("Analyzing game: 0 of %1 positions.").arg(auditScores_.size()));

    if (isEngineAnalyzing()) {
        stopEngineAnalysis();
    } else if (engineState() == UciEngine::State::Ready) {
        auditStartPending_ = false;
        startNextAuditPosition();
    }
    return true;
}

void GameController::cancelGameAudit() {
    if (!auditActive_) {
        return;
    }
    finishGameAudit(false, tr("Game analysis cancelled."));
}

bool GameController::isGameAuditActive() const {
    return auditActive_;
}

QVector<GameController::AuditFinding> GameController::auditFindings() const {
    return auditFindings_;
}

void GameController::setGameAuditDepth(int depth) {
    gameAuditDepth_ = qBound(1, depth, 99);
}

int GameController::gameAuditDepth() const {
    return gameAuditDepth_;
}

const AuditReport &GameController::auditReport() const {
    return auditReport_;
}

void GameController::invalidateAuditReport() {
    if (!auditReport_.valid) {
        return;
    }
    auditReport_ = AuditReport{};
    emit auditReportChanged();
}

QVector<QString> GameController::mainLineSan() const {
    QVector<QString> sanByPly;
    sanByPly.reserve(uciMoves_.size() + 1);
    sanByPly.append(QString());

    Rules replay;
    if (initialFen_.isEmpty() || !replay.loadFen(initialFen_)) {
        replay.reset();
    }

    for (const QString &moveText : uciMoves_) {
        const auto move = UciParser::parseMove(moveText);
        if (!move.has_value() || !replay.isValidMove(*move)) {
            break;
        }
        sanByPly.append(replay.toSan(*move));
        replay.tryMove(*move);
    }

    return sanByPly;
}

QString GameController::sanForUciMove(const Rules &position,
                                      const QString &uciMove) const {
    const auto move = UciParser::parseMove(uciMove);
    if (!move.has_value() || !position.isValidMove(*move)) {
        return uciMove;
    }
    return position.toSan(*move);
}

const Rules &GameController::rules() const {
    return rules_;
}

QString GameController::initialFen() const {
    return initialFen_;
}

QStringList GameController::uciMoves() const {
    return uciMoves_;
}

QStringList GameController::pgnMoves() const {
    return pgnMoves_;
}

QString GameController::pgnHeaderText() const {
    if (pgnHeaders_.isEmpty()) {
        return historyPrefix_;
    }

    QString text = historyPrefix_;
    if (!text.isEmpty()) {
        text += QChar('\n');
    }
    return text + pgnHeaders_.join(QChar('\n'));
}

int GameController::materialBalance(Rules::Color color) const {
    int white = 0;
    int black = 0;

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const std::optional<Rules::Piece> piece = rules_.pieceAt({row, column});
            if (!piece.has_value()) {
                continue;
            }

            const int value = HeuristicEval::pieceValue(piece->type);
            if (piece->color == Rules::Color::White) {
                white += value;
            } else {
                black += value;
            }
        }
    }

    return color == Rules::Color::White ? white - black : black - white;
}

QVector<double> GameController::evaluationCurve() const {
    return evaluationCurve_;
}

Rules::Color GameController::sideToMoveAtPly(int ply) const {
    bool initialWhite = true;
    if (!initialFen_.isEmpty()) {
        const QStringList fields = initialFen_.split(QChar(' '), Qt::SkipEmptyParts);
        if (fields.size() >= 2) {
            initialWhite = fields.at(1) != QStringLiteral("b");
        }
    }

    const bool white = initialWhite == (ply % 2 == 0);
    return white ? Rules::Color::White : Rules::Color::Black;
}

void GameController::requestEvaluationCurve() {
    // Whatever is running was started for another game or another depth.
    if (curveWorker_ != nullptr) {
        curveWorker_->cancelRequest(curveRequestId_);
    }
    const quint64 requestId = ++curveRequestId_;

    // The static pass is instant, so the graph has a whole curve to draw while
    // the searched one is on its way; refinements made by the engine belong to
    // the curve being replaced, so they go.
    engineCurvePoints_.clear();
    computeStaticCurve();
    rebuildMergedCurve();

    curveComputing_ = true;
    curveCompleted_ = 0;
    curveTotal_ = heuristicCurve_.size();
    // Only the static pass has run so far: the graph draws it as provisional
    // and sharpens it as the worker reports the points it has scored.
    curveAnalysedCount_ = 0;
    emit evaluationCurveProgressChanged(curveCompleted_, curveTotal_);
    emit curveComputeRequested(requestId, initialFen_, uciMoves_,
                               evaluationDepth_, 2);
}

void GameController::cancelEvaluationCurve() {
    if (curveWorker_ != nullptr) {
        curveWorker_->cancelRequest(curveRequestId_);
    }
}

bool GameController::isEvaluationCurveComputing() const {
    return curveComputing_;
}

int GameController::evaluationCurveAnalysedCount() const {
    return curveAnalysedCount_;
}

void GameController::computeStaticCurve() {
    heuristicCurve_.clear();
    heuristicCurve_.reserve(uciMoves_.size() + 1);

    Rules replay;
    if (initialFen_.isEmpty() || !replay.loadFen(initialFen_)) {
        replay.reset();
    }

    const auto curvePoint = [](const Rules &position) {
        return HeuristicEval::centipawnsToPercentage(
            static_cast<double>(HeuristicEval::evaluateCentipawns(position)));
    };
    heuristicCurve_.append(curvePoint(replay));
    for (const QString &uci : std::as_const(uciMoves_)) {
        const auto move = UciParser::parseMove(uci);
        if (!move.has_value() || !replay.tryMove(*move)) {
            break;
        }
        heuristicCurve_.append(curvePoint(replay));
    }
}

void GameController::rebuildMergedCurve() {
    evaluationCurve_ = heuristicCurve_;
    for (auto it = engineCurvePoints_.constBegin();
         it != engineCurvePoints_.constEnd(); ++it) {
        if (it.key() >= 0 && it.key() < evaluationCurve_.size()) {
            evaluationCurve_[it.key()] = it.value();
        }
    }
    emit evaluationCurveChanged();
}

void GameController::truncateCurve(int size) {
    const int bounded = std::max(size, 1);
    if (heuristicCurve_.size() > bounded) {
        heuristicCurve_.resize(bounded);
    }
    if (evaluationCurve_.size() > bounded) {
        evaluationCurve_.resize(bounded);
    }
    if (curveAnalysedCount_ >= 0) {
        curveAnalysedCount_ = qMin(curveAnalysedCount_, bounded);
    }
    for (auto it = engineCurvePoints_.begin(); it != engineCurvePoints_.end();) {
        if (it.key() >= bounded) {
            it = engineCurvePoints_.erase(it);
        } else {
            ++it;
        }
    }
}

void GameController::updateCurvePoint(int ply, double winPct) {
    if (ply < 0 || ply >= heuristicCurve_.size() ||
        heuristicCurve_.at(ply) == winPct) {
        return;
    }
    heuristicCurve_[ply] = winPct;
    rebuildMergedCurve();
}

void GameController::setEngineCurvePoint(int ply, double winPct) {
    if (ply < 0 || ply >= heuristicCurve_.size()) {
        return;
    }
    if (engineCurvePoints_.contains(ply) &&
        engineCurvePoints_.value(ply) == winPct) {
        return;
    }
    engineCurvePoints_.insert(ply, winPct);
    rebuildMergedCurve();
}

void GameController::onCurveProgress(quint64 requestId, int completed,
                                     int total) {
    if (requestId != curveRequestId_) {
        return;
    }
    curveCompleted_ = completed;
    curveTotal_ = total;
    curveAnalysedCount_ = qMin(completed, heuristicCurve_.size());
    emit evaluationCurveProgressChanged(completed, total);
}

void GameController::onCurvePointScored(quint64 requestId, int ply,
                                        double winPct) {
    if (requestId != curveRequestId_) {
        return;
    }
    // The curve sharpens as the worker walks the game: every scored position
    // replaces the static estimate the graph is already showing.
    updateCurvePoint(ply, winPct);
}

void GameController::onCurveReady(quint64 requestId,
                                  const QVector<double> &curve) {
    if (requestId != curveRequestId_) {
        return;
    }
    curveComputing_ = false;
    if (curve.size() >= 1) {
        heuristicCurve_ = curve;
        rebuildMergedCurve();
    }
    curveCompleted_ = heuristicCurve_.size();
    curveTotal_ = heuristicCurve_.size();
    curveAnalysedCount_ = -1;
    emit evaluationCurveProgressChanged(curveTotal_, curveTotal_);
}

void GameController::onCurveCancelled(quint64 requestId) {
    if (requestId != curveRequestId_) {
        return;
    }
    curveComputing_ = false;
    // The cancelled pass leaves the instant static values, which are then the
    // whole curve.
    curveAnalysedCount_ = -1;
    emit evaluationCurveProgressChanged(curveTotal_, curveTotal_);
}

QString GameController::formattedCommentAt(int ply) const {
    if (ply < 0 || ply >= plyAnnotations_.size()) {
        return QString();
    }
    const auto &ann = plyAnnotations_.at(ply);
    QString comment = PgnAnnotations::formatComment(ann.arrows, ann.squares, ann.comment);
    const QString audit = PgnAnnotations::formatAuditComment(ann.audit);
    if (!comment.isEmpty() && !audit.isEmpty()) comment += QLatin1Char(' ');
    return comment + audit;
}

QString GameController::buildPgnMovetext() const {
    Rules replay;
    if (!initialFen_.isEmpty() && replay.loadFen(initialFen_)) {
        // Replay from initial FEN
    } else {
        replay.reset();
    }

    int moveNum = 1;
    QString text;

    // Ply 0 comment
    const QString ply0Comment = formattedCommentAt(0);
    if (!ply0Comment.isEmpty()) {
        text += QStringLiteral("{ %1 }").arg(ply0Comment);
    }

    bool previousWhiteHadComment = false;
    for (int i = 0; i < uciMoves_.size(); ++i) {
        const auto move = UciParser::parseMove(uciMoves_.at(i));
        if (!move.has_value() || !replay.isValidMove(*move)) {
            break;
        }
        const Rules::Color movingColor = replay.currentPlayer();
        const QString san = replay.toSan(*move);
        replay.tryMove(*move);

        const int ply = i + 1;
        const QString comment = formattedCommentAt(ply);
        const bool hasComment = !comment.isEmpty();

        if (movingColor == Rules::Color::White) {
            if (!text.isEmpty()) {
                text += QLatin1Char(' ');
            }
            text += QStringLiteral("%1. %2").arg(moveNum).arg(san);
            if (const int nag = PgnAnnotations::auditNag(plyAnnotations_.at(ply).audit.severity); nag != 0) {
                text += QStringLiteral(" $%1").arg(nag);
            }
            if (hasComment) {
                text += QStringLiteral(" { %1 }").arg(comment);
                previousWhiteHadComment = true;
            } else {
                previousWhiteHadComment = false;
            }
        } else {
            if (!text.isEmpty()) {
                text += QLatin1Char(' ');
            }
            if (previousWhiteHadComment || (i == 0)) {
                text += QStringLiteral("%1... %2").arg(moveNum).arg(san);
            } else {
                text += san;
            }
            if (const int nag = PgnAnnotations::auditNag(plyAnnotations_.at(ply).audit.severity); nag != 0) {
                text += QStringLiteral(" $%1").arg(nag);
            }
            if (hasComment) {
                text += QStringLiteral(" { %1 }").arg(comment);
            }
            previousWhiteHadComment = false;
            ++moveNum;
        }
    }

    return text.trimmed();
}

QString GameController::pgnText() const {
    if (pgnHeaders_.isEmpty() && uciMoves_.isEmpty() && !hasAnnotationsAt(0)) {
        return historyPrefix_;
    }

    QString history;
    if (!pgnHeaders_.isEmpty()) {
        history = pgnHeaders_.join(QChar('\n')) + QStringLiteral("\n\n");
    }
    history += buildPgnMovetext();
    if (!pgnHeaders_.isEmpty()) {
        if (!history.endsWith(QChar('\n')) && !history.isEmpty()) {
            history += QChar(' ');
        }
        history += pgnResult_;
    }
    return history;
}

std::vector<UserArrow> GameController::arrowsAtCursor() const {
    return arrowsAt(moveCursor_);
}

std::vector<SquareAnnotation> GameController::squaresAtCursor() const {
    return squaresAt(moveCursor_);
}

QString GameController::commentAtCursor() const {
    return commentAt(moveCursor_);
}

void GameController::setAnnotationsAtCursor(const std::vector<UserArrow> &arrows,
                                            const std::vector<SquareAnnotation> &squares,
                                            const QString &comment) {
    if (moveCursor_ < 0 || moveCursor_ >= plyAnnotations_.size()) {
        return;
    }
    auto &ann = plyAnnotations_[moveCursor_];
    if (ann.arrows == arrows && ann.squares == squares && ann.comment == comment) {
        return;
    }
    ann.arrows = arrows;
    ann.squares = squares;
    ann.comment = comment;
    refreshMoveHistory();
    emit annotationsChanged();
}

void GameController::clearAnnotationsAtCursor() {
    setAnnotationsAtCursor({}, {});
}

bool GameController::hasAnnotationsAtCursor() const {
    return hasAnnotationsAt(moveCursor_);
}

const std::vector<UserArrow> &GameController::arrowsAt(int ply) const {
    static const std::vector<UserArrow> emptyArrows;
    if (ply >= 0 && ply < plyAnnotations_.size()) {
        return plyAnnotations_.at(ply).arrows;
    }
    return emptyArrows;
}

const std::vector<SquareAnnotation> &GameController::squaresAt(int ply) const {
    static const std::vector<SquareAnnotation> emptySquares;
    if (ply >= 0 && ply < plyAnnotations_.size()) {
        return plyAnnotations_.at(ply).squares;
    }
    return emptySquares;
}

QString GameController::commentAt(int ply) const {
    if (ply >= 0 && ply < plyAnnotations_.size()) {
        return plyAnnotations_.at(ply).comment;
    }
    return {};
}

bool GameController::hasAnnotationsAt(int ply) const {
    if (ply >= 0 && ply < plyAnnotations_.size()) {
        const auto &ann = plyAnnotations_.at(ply);
        return !ann.arrows.empty() || !ann.squares.empty() || !ann.comment.isEmpty();
    }
    return false;
}

int GameController::annotationCount() const {
    int count = 0;
    for (const auto &ann : plyAnnotations_) {
        if (!ann.empty()) {
            ++count;
        }
    }
    return count;
}

AuditAnnotation GameController::auditAt(int ply) const {
    return ply >= 0 && ply < plyAnnotations_.size() ? plyAnnotations_.at(ply).audit
                                                      : AuditAnnotation{};
}

bool GameController::isComputerGameActive() const {
    return computerGameActive_;
}

bool GameController::isComputerGamePending() const {
    return pendingComputerGameStart_;
}

bool GameController::isComputerMovePreviewEnabled() const {
    return computerMovePreviewEnabled_;
}

bool GameController::isRecommendedMovePreviewEnabled() const {
    return recommendedMovePreviewEnabled_;
}

Rules::Color GameController::computerColor() const {
    return computerColor_;
}

const ComputerGameSettings &GameController::currentComputerGameSettings() const {
    return computerGameSettings_;
}

void GameController::sendPositionToEngine() {
    EngineBackend *backend = activeBackendForCommands();
    if (backend == nullptr) {
        return;
    }
    const QStringList movesToSend = uciMoves_.mid(0, moveCursor_);
    backend->sendPosition(initialFen_, movesToSend);
}

EngineBackend *GameController::selectedBackend() const {
    if (activeBackend_ != nullptr) {
        return activeBackend_;
    }
    if (engine_->state() != EngineBackend::State::Disconnected) {
        return engine_;
    }
    if (gateway_->state() != EngineBackend::State::Disconnected) {
        return gateway_;
    }
    return nullptr;
}

EngineBackend *GameController::activeBackendForCommands() const {
    EngineBackend *backend = selectedBackend();
    return backend != nullptr && backend->isOperational() ? backend : nullptr;
}

void GameController::beginComputerGame() {
    if (!pendingComputerGameStart_ || !isEngineConnected()) {
        return;
    }

    computerGameSettings_ = pendingComputerGameSettings_;
    pendingComputerGameStart_ = false;
    computerGameActive_ = true;
    computerGameStarted_ = true;
    invalidateAuditReport();
    computerColor_ = computerGameSettings_.enginePlaysWhite
                         ? Rules::Color::White
                         : Rules::Color::Black;

    rules_.reset();
    historyPrefix_.clear();
    initialFen_.clear();
    pgnMoves_.clear();
    uciMoves_.clear();
    plyAnnotations_.clear();
    plyAnnotations_.resize(1);
    pgnMoveNumber_ = 1;
    moveCursor_ = 0;
    whiteToMove_ = true;
    pgnResult_ = QStringLiteral("*");
    auditFindings_.clear();
    pgnHeaders_ = computerGameSettings_.pgnHeaders(pgnResult_);

    emit computerGameStateChanged(true);
    emit positionChanged();
    refreshMoveHistory();
    updateEvaluation();

    if (computerColor_ == Rules::Color::White) {
        startComputerTurn();
    } else {
        emit humanTurnBegan();
        emit statusMessage(tr("Your turn — White to move."));
        startMovePreviewAnalysis();
    }
}

void GameController::startComputerTurn() {
    if (!computerGameActive_ || !isEngineConnected() ||
        rules_.currentPlayer() != computerColor_) {
        return;
    }

    if (const auto bookMove = book_.pickMove(rules_); bookMove.has_value()) {
        if (!recordMove(*bookMove)) {
            finishComputerGame(
                QStringLiteral("*"),
                tr("The opening book returned an invalid move."));
            return;
        }

        if (finishGameIfOver()) {
            return;
        }

        sendPositionToEngine();

        emit humanTurnBegan();
        emit statusMessage(tr("Your turn (opening book)."));
        startMovePreviewAnalysis();
        return;
    }

    emit computerTurnBegan();
    emit statusMessage(tr("Computer is thinking…"));

    sendPositionToEngine();
    startEngineTimedSearch(whiteRemainingMs_, blackRemainingMs_);
}

void GameController::finishComputerGame(const QString &result,
                                        const QString &message) {
    if (!computerGameActive_ && !pendingComputerGameStart_) {
        return;
    }

    computerGameActive_ = false;
    pendingComputerGameStart_ = false;
    clearMovePreviews();
    emit computerGameStateChanged(false);

    if (isEngineAnalyzing()) {
        stopEngineAnalysis();
    }

    pgnResult_ = result;
    updatePgnResult(result);
    refreshMoveHistory();
    emit gameFinished(result, message);
}

void GameController::startMovePreviewAnalysis() {
    if (!computerGameActive_ || !isEngineConnected() ||
        rules_.currentPlayer() == computerColor_ ||
        (!computerMovePreviewEnabled_ && !recommendedMovePreviewEnabled_)) {
        return;
    }

    sendPositionToEngine();
    // Previews only need the best line, whatever the MultiPV setting is.
    startEngineAnalysis(1);
}

void GameController::updateMovePreviews(const EngineAnalysisLine &line) {
    // During a computer turn the engine is performing the actual timed
    // search. Its intermediate PV must not be published as a planned move:
    // doing so races with the preview analysis that starts after bestmove.
    if (computerGameActive_ && rules_.currentPlayer() == computerColor_) {
        return;
    }

    if (line.multipv != 1) {
        return;
    }

    const QStringList moves = line.pv.split(QChar(' '), Qt::SkipEmptyParts);
    if (moves.isEmpty()) {
        return;
    }

    std::optional<Rules::Move> recommendedMove;
    if (const auto firstMove = UciParser::parseMove(moves.first());
        firstMove.has_value() && rules_.isValidMove(*firstMove)) {
        recommendedMove = firstMove;
    }

    std::optional<Rules::Move> computerMove;
    if (computerGameActive_) {
        Rules projectedRules = rules_;
        for (const QString &moveText : moves) {
            const auto move = UciParser::parseMove(moveText);
            if (!move.has_value() || !projectedRules.isValidMove(*move)) {
                break;
            }

            if (projectedRules.currentPlayer() == computerColor_) {
                computerMove = move;
                break;
            }

            if (!projectedRules.tryMove(*move)) {
                break;
            }
        }
    }

    if (recommendedMovePreviewEnabled_ && recommendedMove.has_value()) {
        emit recommendedMovePreviewChanged(recommendedMove);
    }
    if (computerMovePreviewEnabled_ && computerMove.has_value()) {
        emit computerMovePreviewChanged(computerMove);
    }
}

void GameController::onAnalysisLine(const EngineAnalysisLine &line) {
    // The last scored line of the position being audited is kept whatever
    // depth it reports: a position that is already mate (or that the engine
    // solves early) legitimately stops below the requested depth, and the
    // whole audit must not be discarded because of it.
    if (auditActive_ && auditPosition_ >= 0 && auditPosition_ < auditScores_.size() &&
        line.multipv == 1 && (line.scoreCp.has_value() || line.mateIn.has_value())) {
        auditLatestLine_ = line;
    }
    updateMovePreviews(line);

    if (line.multipv <= 1 &&
        (line.scoreCp.has_value() || line.mateIn.has_value())) {
        const double score = line.scoreCp.value_or(0.0);
        // Adapt score to White's perspective for the evaluation bar.
        const double whiteScoreCp = whiteToMove_ ? score : -score;
        std::optional<int> mateForWhite = line.mateIn;
        if (mateForWhite.has_value() && !whiteToMove_) {
            mateForWhite = -(*mateForWhite);
        }
        const double winPct =
            UciParser::scoreToWinningPercentage(whiteScoreCp, mateForWhite);
        emit evaluationChanged(winPct);
        emit evaluationScoreChanged(
            UciParser::formatScore(whiteScoreCp, mateForWhite));

        // Outside the audit the engine is analysing the position at the
        // cursor, so the curve takes the engine score for that ply: wherever
        // the engine has looked, the curve and the evaluation bar agree.
        // During an audit the lines belong to another position.
        if (!auditActive_) {
            setEngineCurvePoint(moveCursor_, winPct);
        }
    }
}

void GameController::onAuditBestMove(const QString &bestMove, const QString &ponder) {
    Q_UNUSED(ponder)
    if (!auditActive_ || auditPosition_ < 0 || auditPosition_ >= auditScores_.size()) return;
    if (!auditLatestLine_.has_value()) {
        finishGameAudit(false,
                        tr("Game analysis stopped: the engine did not return a score for position %1.")
                            .arg(auditPosition_ + 1));
        return;
    }
    auditScores_[auditPosition_] = {auditLatestLine_->scoreCp, auditLatestLine_->mateIn};
    const QStringList pv = auditLatestLine_->pv.split(QChar(' '), Qt::SkipEmptyParts);
    auditBestMoves_[auditPosition_] = pv.isEmpty() ? bestMove : pv.first();

    // The curve sharpens as the audit walks the main line: the engine score
    // replaces the heuristic one for the position just analysed.
    if (auditLatestLine_->scoreCp.has_value() ||
        auditLatestLine_->mateIn.has_value()) {
        const bool whiteToMoveHere =
            sideToMoveAtPly(auditPosition_) == Rules::Color::White;
        const double centipawns = auditLatestLine_->scoreCp.value_or(0.0);
        std::optional<int> mateForWhite = auditLatestLine_->mateIn;
        if (mateForWhite.has_value() && !whiteToMoveHere) {
            mateForWhite = -(*mateForWhite);
        }
        setEngineCurvePoint(
            auditPosition_,
            UciParser::scoreToWinningPercentage(
                whiteToMoveHere ? centipawns : -centipawns, mateForWhite));
    }

    ++auditPosition_;
    emit auditProgressChanged(auditPosition_, auditScores_.size());
    emit statusMessage(tr("Analyzing game: %1 of %2 positions.").arg(auditPosition_).arg(auditScores_.size()));
    if (auditPosition_ == auditScores_.size()) {
        finishGameAudit(true, tr("Game analysis completed."));
        return;
    }
    auditLatestLine_.reset();
    QTimer::singleShot(0, this, [this] {
        if (!auditActive_) return;
        if (engineState() == UciEngine::State::Ready) startNextAuditPosition();
        else auditStartPending_ = true;
    });
}

void GameController::startNextAuditPosition() {
    if (!auditActive_) return;
    EngineBackend *backend = activeBackendForCommands();
    if (backend == nullptr || engineState() != UciEngine::State::Ready) {
        auditStartPending_ = true;
        return;
    }
    if (auditPosition_ >= auditScores_.size()) {
        finishGameAudit(true, tr("Game analysis completed."));
        return;
    }
    auditLatestLine_.reset();
    showAuditPosition();
    backend->sendPosition(initialFen_, uciMoves_.mid(0, auditPosition_));
    backend->startAnalysis(gameAuditDepth_);
}

void GameController::showAuditPosition() {
    // The board follows the analysis so that the reviewer sees the position
    // the engine is scoring, highlighted in the move list and the curve.
    const int target = qBound(0, auditPosition_, uciMoves_.size());
    if (moveCursor_ == target) {
        return;
    }

    moveCursor_ = target;
    rebuildPositionToCursor();
    clearMovePreviews();
    emit positionChanged();
    updateEvaluation();
}

void GameController::finishGameAudit(bool applyResults, const QString &message) {
    if (!auditActive_) return;
    if (applyResults) {
        QVector<AuditFinding> findings;
        QVector<AuditAnnotation> annotations(plyAnnotations_.size());
        bool usable = auditScores_.size() == uciMoves_.size() + 1;
        for (const AuditScore &score : std::as_const(auditScores_))
            usable = usable && (score.centipawns.has_value() || score.mateIn.has_value());

        AuditReport report;
        report.depth = gameAuditDepth_;
        report.plies = uciMoves_.size();
        report.analysedPositions = auditScores_.size();
        report.sanByPly = mainLineSan();
        report.centipawnLossByPly.fill(-1, uciMoves_.size() + 1);

        double whiteAccuracy = 0.0;
        double blackAccuracy = 0.0;
        const auto statsFor = [&report](Rules::Color color) -> AuditPlayerStats & {
            return color == Rules::Color::White ? report.white : report.black;
        };
        const auto accuracySumFor = [&](Rules::Color color) -> double & {
            return color == Rules::Color::White ? whiteAccuracy : blackAccuracy;
        };

        // Walking the main line also gives the notation of the best move of
        // every position, which the report shows next to the played one.
        Rules replay;
        if (initialFen_.isEmpty() || !replay.loadFen(initialFen_)) {
            replay.reset();
        }

        for (int ply = 1; usable && ply <= uciMoves_.size(); ++ply) {
            const AuditScore &before = auditScores_.at(ply - 1);
            const AuditScore &after = auditScores_.at(ply);
            // Both mate values are read from the side to move, so a positive
            // value after the move means the opponent now mates: the mover's
            // own forced mate is gone. A value of exactly zero means the move
            // delivered mate, which is the opposite of a blunder.
            const bool lostForcedMate = before.mateIn.has_value() && *before.mateIn > 0 &&
                (!after.mateIn.has_value() || *after.mateIn > 0);
            const bool allowedForcedMate = after.mateIn.has_value() && *after.mateIn > 0;
            int loss = 0;
            AuditSeverity severity = AuditSeverity::None;
            if (lostForcedMate || allowedForcedMate) severity = AuditSeverity::Blunder;
            else if (before.centipawns.has_value() && after.centipawns.has_value()) {
                loss = qMax(0, qRound(*before.centipawns + *after.centipawns));
                severity = loss >= 200 ? AuditSeverity::Blunder : loss >= 100 ? AuditSeverity::Mistake :
                           loss >= 50 ? AuditSeverity::Inaccuracy : AuditSeverity::None;
            } else if (before.mateIn.has_value() || after.mateIn.has_value()) {
                // A mate score that is not a lost or an allowed forced mate
                // still carries a winning percentage, so the move is scored
                // for accuracy even though it has no centipawn loss.
            } else { usable = false; break; }

            const Rules::Color mover = sideToMoveAtPly(ply - 1);
            AuditPlayerStats &stats = statsFor(mover);

            // Both scores are read from the side to move, so the position
            // after the move has to be turned around before it can be compared
            // with the position before it.
            const double winningBefore = UciParser::scoreToWinningPercentage(
                before.centipawns.value_or(0.0), before.mateIn);
            const double winningAfter =
                100.0 - UciParser::scoreToWinningPercentage(
                            after.centipawns.value_or(0.0), after.mateIn);
            accuracySumFor(mover) +=
                AuditReports::moveAccuracy(winningBefore - winningAfter);
            ++stats.scoredMoves;

            if (before.centipawns.has_value() && after.centipawns.has_value()) {
                ++stats.centipawnMoves;
                stats.totalCentipawnLoss += loss;
                report.centipawnLossByPly[ply] = loss;
            }

            switch (severity) {
            case AuditSeverity::Inaccuracy:
                ++stats.inaccuracies;
                break;
            case AuditSeverity::Mistake:
                ++stats.mistakes;
                break;
            case AuditSeverity::Blunder:
                ++stats.blunders;
                break;
            case AuditSeverity::None:
                break;
            }

            if (severity != AuditSeverity::None) {
                AuditAnnotation annotation{severity, loss, auditBestMoves_.at(ply - 1),
                                           uciMoves_.at(ply - 1), lostForcedMate || allowedForcedMate};
                annotations[ply] = annotation;
                AuditFinding finding{severity, ply, loss, annotation.bestMove, annotation.forcedMate};
                finding.bestMoveSan = sanForUciMove(replay, annotation.bestMove);
                findings.append(finding);
            }

            if (const auto played = UciParser::parseMove(uciMoves_.at(ply - 1));
                played.has_value() && replay.isValidMove(*played)) {
                replay.tryMove(*played);
            }
        }

        if (usable) {
            for (AuditPlayerStats *stats : {&report.white, &report.black}) {
                stats->averageCentipawnLoss =
                    stats->centipawnMoves > 0
                        ? static_cast<double>(stats->totalCentipawnLoss) /
                              stats->centipawnMoves
                        : 0.0;
                stats->accuracy = stats->scoredMoves > 0
                                      ? (stats == &report.white ? whiteAccuracy
                                                                : blackAccuracy) /
                                            stats->scoredMoves
                                      : 0.0;
            }
            report.findings = findings;
            report.valid = true;

            for (PlyAnnotations &annotation : plyAnnotations_) annotation.audit = {};
            for (int ply = 1; ply < annotations.size(); ++ply) plyAnnotations_[ply].audit = annotations.at(ply);
            auditFindings_ = findings;
            auditReport_ = report;
            refreshMoveHistory();
            emit annotationsChanged();
            emit auditReportChanged();
        } else applyResults = false;
    }
    auditActive_ = false;
    auditStartPending_ = false;
    auditLatestLine_.reset();
    auditRestorePending_ = isEngineConnected();
    emit auditStateChanged(false);
    emit auditCompleted(applyResults);
    emit statusMessage(message);
    if (isEngineAnalyzing()) {
        stopEngineAnalysis();
    }
    // Puts the board back on the user's position, with or without an engine.
    restoreAfterGameAudit();
}

void GameController::restoreAfterGameAudit() {
    // The board followed the analysis: it returns to the position the user was
    // looking at as soon as the audit ends, whether an engine is still
    // connected or not.
    const int target = qBound(0, auditSavedCursor_, uciMoves_.size());
    if (moveCursor_ != target) {
        moveCursor_ = target;
        rebuildPositionToCursor();
        clearMovePreviews();
        emit positionChanged();
        updateEvaluation();
    }

    if (!auditRestorePending_ || engineState() != UciEngine::State::Ready) return;
    auditRestorePending_ = false;
    sendPositionToEngine();
    if (auditResumeManualAnalysis_) {
        auditResumeManualAnalysis_ = false;
        startEngineAnalysis();
    }
}

bool GameController::recordMove(const Rules::Move &move) {
    const Rules::Color movingColor = rules_.currentPlayer();
    const QString san = rules_.toSan(move);
    if (san.isEmpty() || !rules_.tryMove(move)) {
        return false;
    }

    // The game no longer matches any report built for it.
    invalidateAuditReport();

    if (moveCursor_ < uciMoves_.size()) {
        // A move played from the middle of the history discards the
        // abandoned continuation.
        uciMoves_.resize(moveCursor_);
        if (plyAnnotations_.size() > moveCursor_ + 1) {
            plyAnnotations_.resize(moveCursor_ + 1);
        }
        truncateCurve(moveCursor_ + 1);
        rebuildPgnHistory();
    }

    uciMoves_.append(Rules::toUci(move));
    plyAnnotations_.append(PlyAnnotations{});
    appendPgnMove(san, movingColor);
    ++moveCursor_;

    // The point is filled from the static evaluation at once and refined by the
    // background search the live evaluation is about to request, so the curve
    // keeps up with the game without blocking on a search.
    heuristicCurve_.append(HeuristicEval::centipawnsToPercentage(
        static_cast<double>(HeuristicEval::evaluateCentipawns(rules_))));
    curveAnalysedCount_ = -1;
    rebuildMergedCurve();

    refreshMoveHistory();
    updateEvaluation();
    emit positionChanged();

    if (computerGameActive_ && computerGameSettings_.incrementMilliseconds > 0 &&
        !rules_.isGameOver()) {
        emit clockIncrementGranted(movingColor,
                                   computerGameSettings_.incrementMilliseconds);
    }

    return true;
}

void GameController::appendPgnMove(const QString &san, Rules::Color movingColor) {
    if (movingColor == Rules::Color::White) {
        pgnMoves_.append(QStringLiteral("%1. %2")
                             .arg(pgnMoveNumber_)
                             .arg(san));
        whiteToMove_ = false;
    } else {
        if (!pgnMoves_.isEmpty()) {
            pgnMoves_.last() += QStringLiteral(" ") + san;
        } else {
            pgnMoves_.append(QStringLiteral("%1... %2")
                                 .arg(pgnMoveNumber_)
                                 .arg(san));
        }
        ++pgnMoveNumber_;
        whiteToMove_ = true;
    }
}

void GameController::rebuildPositionToCursor() {
    Rules rebuilt;
    if (!initialFen_.isEmpty() && rebuilt.loadFen(initialFen_)) {
        rules_ = rebuilt;
    } else {
        rules_.reset();
    }

    const int target = qBound(0, moveCursor_, uciMoves_.size());
    for (int i = 0; i < target; ++i) {
        const auto move = UciParser::parseMove(uciMoves_.at(i));
        if (!move.has_value() || !rules_.tryMove(*move)) {
            moveCursor_ = i;
            break;
        }
    }

    whiteToMove_ = (rules_.currentPlayer() == Rules::Color::White);
    pgnMoveNumber_ = (moveCursor_ / 2) + 1;
}

void GameController::rebuildPgnHistory() {
    Rules replay;
    if (!initialFen_.isEmpty() && replay.loadFen(initialFen_)) {
        // Replays from the configured initial position.
    } else {
        replay.reset();
    }

    pgnMoves_.clear();
    pgnMoveNumber_ = 1;
    for (const QString &moveText : uciMoves_) {
        const auto move = UciParser::parseMove(moveText);
        if (!move.has_value() || !replay.isValidMove(*move)) {
            break;
        }
        const Rules::Color movingColor = replay.currentPlayer();
        const QString san = replay.toSan(*move);
        if (san.isEmpty()) {
            break;
        }
        replay.tryMove(*move);
        appendPgnMove(san, movingColor);
    }
    whiteToMove_ = (replay.currentPlayer() == Rules::Color::White);
    pgnMoveNumber_ = (uciMoves_.size() / 2) + 1;
}

void GameController::refreshMoveHistory() {
    emit historyChanged(pgnText());
}

void GameController::updatePgnResult(const QString &result) {
    for (QString &header : pgnHeaders_) {
        if (header.startsWith(QStringLiteral("[Result "))) {
            header = QStringLiteral("[Result \"%1\"]").arg(result);
            return;
        }
    }

    // FEN-loaded and free-play games start without PGN headers; add a Result
    // tag so the declared result is preserved when the game is exported.
    pgnHeaders_.append(QStringLiteral("[Result \"%1\"]").arg(result));
}

QString GameController::completedGameResult() const {
    if (!rules_.isGameOver()) {
        return QStringLiteral("*");
    }
    if (rules_.isCheckmate(Rules::Color::White) ||
        rules_.isCheckmate(Rules::Color::Black)) {
        return rules_.currentPlayer() == Rules::Color::White
                   ? QStringLiteral("0-1")
                   : QStringLiteral("1-0");
    }
    return QStringLiteral("1/2-1/2");
}

bool GameController::finishGameIfOver() {
    if (!rules_.isGameOver()) {
        return false;
    }

    const QString result = completedGameResult();
    finishComputerGame(result,
                       result == QStringLiteral("1/2-1/2")
                           ? drawReasonMessage()
                           : tr("Game over: %1 wins.")
                                 .arg(result == QStringLiteral("1-0")
                                          ? tr("White")
                                          : tr("Black")));
    return true;
}

QString GameController::drawReasonMessage() const {
    if (rules_.isStalemate(rules_.currentPlayer())) {
        return tr("Draw by stalemate.");
    }
    if (rules_.isThreefoldRepetition()) {
        return tr("Draw by threefold repetition.");
    }
    if (rules_.isFiftyMoveRule()) {
        return tr("Draw by the fifty-move rule.");
    }
    if (rules_.isInsufficientMaterial()) {
        return tr("Draw by insufficient material.");
    }
    return tr("Game drawn.");
}

bool GameController::canClaimDraw() const {
    if (pgnResult_ != QStringLiteral("*") || computerGameActive_ ||
        pendingComputerGameStart_ || auditActive_ ||
        moveCursor_ != uciMoves_.size()) {
        return false;
    }

    // A checkmate ends the game before a claimable draw can apply.
    if (rules_.isCheckmate(Rules::Color::White) ||
        rules_.isCheckmate(Rules::Color::Black)) {
        return false;
    }

    return rules_.isThreefoldRepetition() || rules_.isFiftyMoveRule();
}

void GameController::claimDraw() {
    if (!canClaimDraw()) {
        return;
    }

    pgnResult_ = QStringLiteral("1/2-1/2");
    const QString message = drawReasonMessage();
    updatePgnResult(pgnResult_);
    refreshMoveHistory();
    emit statusMessage(message);
    emit gameFinished(pgnResult_, message);
}

bool GameController::canResign() const {
    return computerGameActive_ && pgnResult_ == QStringLiteral("*");
}

void GameController::resign() {
    if (!canResign()) {
        return;
    }

    const QString result = computerColor_ == Rules::Color::White
                               ? QStringLiteral("1-0")
                               : QStringLiteral("0-1");
    finishComputerGame(result,
                       tr("You resigned; the engine wins."));
}

bool GameController::canOfferDraw() const {
    if (auditActive_ || pendingComputerGameStart_ ||
        pgnResult_ != QStringLiteral("*")) {
        return false;
    }

    // An offer describes the current position, so a review position has to be
    // left first.
    if (moveCursor_ != uciMoves_.size()) {
        return false;
    }

    if (computerGameActive_) {
        return true;
    }

    // Free play: the two players share the board, so at least one move must
    // have been played before they can agree on a draw.
    return !uciMoves_.isEmpty();
}

void GameController::offerDraw() {
    if (!canOfferDraw()) {
        return;
    }

    if (computerGameActive_) {
        const int centipawns = evaluationCentipawns(rules_);
        const int engineCentipawns = computerColor_ == Rules::Color::White
                                         ? centipawns
                                         : -centipawns;
        if (engineCentipawns > DrawAcceptanceThresholdCp) {
            emit statusMessage(tr("The engine declined the draw offer."));
            return;
        }

        finishComputerGame(QStringLiteral("1/2-1/2"),
                           tr("The engine accepted the draw offer."));
        return;
    }

    pgnResult_ = QStringLiteral("1/2-1/2");
    updatePgnResult(pgnResult_);
    refreshMoveHistory();
    emit statusMessage(tr("Draw agreed."));
    emit gameFinished(pgnResult_, tr("Draw agreed."));
}

void GameController::setAnalysisSettings(int depth, int multiPv) {
    const int clampedDepth = qBound(0, depth, 99);
    const int clampedMultiPv = qBound(1, multiPv, 8);
    if (analysisDepth_ == clampedDepth && analysisMultiPv_ == clampedMultiPv) {
        return;
    }

    analysisDepth_ = clampedDepth;
    analysisMultiPv_ = clampedMultiPv;

    // Restart a running analysis so that the new limits apply immediately.
    if (!computerGameActive_ && !auditActive_ && isEngineAnalyzing()) {
        startEngineAnalysis();
    }
}

int GameController::analysisDepth() const {
    return analysisDepth_;
}

int GameController::analysisMultiPv() const {
    return analysisMultiPv_;
}

