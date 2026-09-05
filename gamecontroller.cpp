//
// Controller for a chess game: owns the authoritative position, the PGN
// history, the UCI engine, the opening book and the computer-game state
// machine. UI independent.
//

#include "gamecontroller.h"

#include "gatewayclient.h"
#include "pgnfile.h"
#include "uciengine.h"
#include "uciparser.h"

#include <QTimer>

#include <algorithm>
#include <utility>

GameController::GameController(QObject *parent)
    : QObject(parent)
    , engine_(new UciEngine(this))
    , gateway_(new ChessGatewayClient(this)) {
    connect(engine_, &EngineBackend::stateChanged,
            this, [this](EngineBackend::State) { handleEngineStateChanged(); });
    connect(engine_, &EngineBackend::analysisUpdated,
            this, &GameController::onAnalysisLine);
    connect(engine_, &EngineBackend::timedBestMoveReceived,
            this, &GameController::onEngineTimedMove);

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
}

void GameController::handleEngineStateChanged() {
    if (activeBackend_ != nullptr &&
        activeBackend_->state() == EngineBackend::State::Disconnected) {
        activeBackend_ = nullptr;
    }

    const UciEngine::State state = engineState();

    if (state == UciEngine::State::Disconnected) {
        pendingComputerGameStart_ = false;
        pendingAnalysisStart_ = false;
        pendingGatewaySelection_ = false;
        if (computerGameActive_) {
            finishComputerGame(QStringLiteral("*"),
                               tr("The engine disconnected."));
        }
    } else if (state == UciEngine::State::Ready) {
        if (pendingComputerGameStart_) {
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
    historyPrefix_ = QStringLiteral("[FEN \"%1\"]\n\n%2").arg(fen, description);

    rules_ = newRules;
    initialFen_ = fen;
    uciMoves_.clear();
    pgnMoves_.clear();
    pgnHeaders_.clear();
    pgnResult_ = QStringLiteral("*");
    whiteToMove_ = (rules_.currentPlayer() == Rules::Color::White);
    pgnMoveNumber_ = 1;
    moveCursor_ = 0;

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
    historyPrefix_.clear();
    initialFen_.clear();
    clearMovePreviews();
    stopEngineAnalysis();

    rules_.reset();
    uciMoves_.clear();
    pgnMoves_.clear();
    pgnHeaders_.clear();
    pgnResult_ = QStringLiteral("*");
    whiteToMove_ = true;
    pgnMoveNumber_ = 1;
    moveCursor_ = 0;

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

    if (!newRules.loadPgn(pgnContent, &pgnFormatted, &uciList)) {
        return false;
    }

    computerGameActive_ = false;
    pendingComputerGameStart_ = false;
    historyPrefix_.clear();
    clearMovePreviews();

    rules_ = newRules;

    initialFen_ = PgnFile::tagValue(pgnContent, QStringLiteral("FEN"));

    pgnMoves_ = pgnFormatted;
    uciMoves_ = uciList;
    pgnHeaders_ = PgnFile::parseHeaders(pgnContent);
    pgnResult_ = PgnFile::tagValue(pgnContent, QStringLiteral("Result"));
    if (pgnResult_.isEmpty()) {
        pgnResult_ = QStringLiteral("*");
    }
    whiteToMove_ = (rules_.currentPlayer() == Rules::Color::White);
    pgnMoveNumber_ = (uciMoves_.size() / 2) + 1;
    moveCursor_ = uciMoves_.size();

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
    if (!canStepBack()) {
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
    if (!canStepForward()) {
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
    return !computerGameActive_ && !pendingComputerGameStart_ &&
           moveCursor_ > 0;
}

bool GameController::canStepForward() const {
    return !computerGameActive_ && !pendingComputerGameStart_ &&
           moveCursor_ < uciMoves_.size();
}

bool GameController::goToMove(int moveIndex) {
    if (computerGameActive_ || pendingComputerGameStart_) {
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

bool GameController::requestMove(Rules::Position from, Rules::Position to) {
    if (computerGameActive_ && rules_.currentPlayer() == computerColor_) {
        return false;
    }

    if (!rules_.isValidMove(from, to)) {
        return false;
    }

    const auto movedPiece = rules_.pieceAt(from);
    if (!movedPiece.has_value()) {
        return false;
    }

    const Rules::PieceType promotion =
        movedPiece->type == Rules::PieceType::Pawn &&
                (to.row == 0 || to.row == 7)
            ? Rules::PieceType::Queen
            : Rules::PieceType::None;

    if (!recordMove({from, to, promotion})) {
        return false;
    }

    if (computerGameActive_) {
        if (rules_.isGameOver()) {
            const QString result = completedGameResult();
            finishComputerGame(
                result,
                result == QStringLiteral("1/2-1/2")
                    ? tr("Game drawn.")
                    : tr("Game over: %1 wins.")
                          .arg(result == QStringLiteral("1-0")
                                   ? tr("White")
                                   : tr("Black")));
        } else {
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

    const auto move = parseUciMove(bestMove);
    if (!move.has_value() || !rules_.isValidMove(*move) ||
        !recordMove(*move)) {
        finishComputerGame(
            QStringLiteral("*"),
            tr("The engine returned an invalid move: %1")
                .arg(bestMove));
        return;
    }

    if (rules_.isGameOver()) {
        const QString result = completedGameResult();
        finishComputerGame(
            result,
            result == QStringLiteral("1/2-1/2")
                ? tr("Game drawn.")
                : tr("Game over: %1 wins.")
                      .arg(result == QStringLiteral("1-0")
                               ? tr("White")
                               : tr("Black")));
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
    if (computerGameActive_) {
        return;
    }

    if (isEngineAnalyzing()) {
        stopAnalysis();
    } else {
        startAnalysis();
    }
}

void GameController::startAnalysis() {
    if (computerGameActive_) {
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

    stopEngineAnalysis();
    clearMovePreviews();
}

void GameController::startEngineAnalysis() {
    if (EngineBackend *backend = activeBackendForCommands(); backend != nullptr) {
        backend->startAnalysis();
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
        backend->startTimedSearch(whiteTimeMilliseconds, blackTimeMilliseconds);
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
    }
}

void GameController::updateEvaluation() {
    emit evaluationChanged(heuristicEval_.evaluateDisplayPercentage(rules_));
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
    if (!pgnHeaders_.isEmpty()) {
        return pgnHeaders_.join(QChar('\n'));
    }
    return historyPrefix_;
}

QString GameController::pgnText() const {
    if (pgnHeaders_.isEmpty() && pgnMoves_.isEmpty()) {
        return historyPrefix_;
    }

    QString history;
    if (!pgnHeaders_.isEmpty()) {
        history = pgnHeaders_.join(QChar('\n')) + tr("\n\n");
    }
    history += pgnMoves_.join(QChar('\n'));
    if (!pgnHeaders_.isEmpty()) {
        if (!history.endsWith(QChar('\n')) && !history.isEmpty()) {
            history += QChar(' ');
        }
        history += pgnResult_;
    }
    return history;
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
    computerColor_ = computerGameSettings_.enginePlaysWhite
                         ? Rules::Color::White
                         : Rules::Color::Black;

    rules_.reset();
    historyPrefix_.clear();
    initialFen_.clear();
    pgnMoves_.clear();
    uciMoves_.clear();
    pgnMoveNumber_ = 1;
    moveCursor_ = 0;
    whiteToMove_ = true;
    pgnResult_ = QStringLiteral("*");
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

        if (rules_.isGameOver()) {
            const QString result = completedGameResult();
            finishComputerGame(
                result,
                result == QStringLiteral("1/2-1/2")
                    ? tr("Game drawn.")
                    : tr("Game over: %1 wins.")
                          .arg(result == QStringLiteral("1-0")
                                   ? tr("White")
                                   : tr("Black")));
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
    startEngineAnalysis();
}

void GameController::updateMovePreviews(const EngineAnalysisLine &line) {
    if (line.multipv != 1) {
        return;
    }

    const QStringList moves = line.pv.split(QChar(' '), Qt::SkipEmptyParts);
    if (moves.isEmpty()) {
        return;
    }

    std::optional<Rules::Move> recommendedMove;
    if (const auto firstMove = parseUciMove(moves.first());
        firstMove.has_value() && rules_.isValidMove(*firstMove)) {
        recommendedMove = firstMove;
    }

    std::optional<Rules::Move> computerMove;
    if (computerGameActive_) {
        Rules projectedRules = rules_;
        for (const QString &moveText : moves) {
            const auto move = parseUciMove(moveText);
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
    }
}

bool GameController::recordMove(const Rules::Move &move) {
    const Rules::Color movingColor = rules_.currentPlayer();
    const QString san = rules_.toSan(move);
    if (san.isEmpty() || !rules_.tryMove(move)) {
        return false;
    }

    if (moveCursor_ < uciMoves_.size()) {
        // A move played from the middle of the history discards the
        // abandoned continuation.
        uciMoves_.resize(moveCursor_);
        rebuildPgnHistory();
    }

    uciMoves_.append(Rules::toUci(move));
    appendPgnMove(san, movingColor);
    ++moveCursor_;

    refreshMoveHistory();
    updateEvaluation();
    emit positionChanged();
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
        const auto move = parseUciMove(uciMoves_.at(i));
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
        const auto move = parseUciMove(moveText);
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
}

QString GameController::completedGameResult() const {
    if (!rules_.isGameOver()) {
        return QStringLiteral("*");
    }
    if (rules_.isStalemate(rules_.currentPlayer())) {
        return QStringLiteral("1/2-1/2");
    }
    return rules_.currentPlayer() == Rules::Color::White
               ? QStringLiteral("0-1")
               : QStringLiteral("1-0");
}

std::optional<Rules::Move> GameController::parseUciMove(const QString &moveText) {
    const QString move = moveText.trimmed().toLower();
    if (move.size() < 4) {
        return std::nullopt;
    }

    const auto parseFile = [](QChar file) -> int {
        return file >= QChar('a') && file <= QChar('h')
                   ? file.toLatin1() - 'a'
                   : -1;
    };
    const auto parseRank = [](QChar rank) -> int {
        return rank >= QChar('1') && rank <= QChar('8')
                   ? 8 - rank.digitValue()
                   : -1;
    };

    const Rules::Position from{parseRank(move.at(1)), parseFile(move.at(0))};
    const Rules::Position to{parseRank(move.at(3)), parseFile(move.at(2))};
    if (!Rules::isInside(from) || !Rules::isInside(to)) {
        return std::nullopt;
    }

    Rules::PieceType promotion = Rules::PieceType::None;
    if (move.size() >= 5) {
        switch (move.at(4).toLatin1()) {
        case 'q': promotion = Rules::PieceType::Queen; break;
        case 'r': promotion = Rules::PieceType::Rook; break;
        case 'b': promotion = Rules::PieceType::Bishop; break;
        case 'n': promotion = Rules::PieceType::Knight; break;
        default: return std::nullopt;
        }
    }

    return Rules::Move{from, to, promotion};
}
