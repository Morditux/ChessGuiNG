//
// Common UCI engine backend interface and protocol implementation.
//

#include "enginebackend.h"

#include <algorithm>
#include <utility>

#include "rules.h"

EngineBackend::EngineBackend(const QString &defaultEngineName, QObject *parent)
    : QObject(parent)
    , engineName_(defaultEngineName)
    , defaultEngineName_(defaultEngineName) {
}

bool EngineBackend::isOperational() const {
    return state_ == State::Ready || state_ == State::Analyzing ||
           state_ == State::Stopping;
}

bool EngineBackend::isAnalyzing() const {
    return state_ == State::Analyzing;
}

EngineBackend::State EngineBackend::state() const {
    return state_;
}

QString EngineBackend::engineName() const {
    return engineName_;
}

QString EngineBackend::engineAuthor() const {
    return engineAuthor_;
}

QList<UciOption> EngineBackend::options() const {
    return options_;
}

void EngineBackend::sendPosition(const QString &fen, const QStringList &moves) {
    if (!canSendUciCommands()) {
        return;
    }

    const QString trimmedFen = fen.trimmed();
    if (!trimmedFen.isEmpty() && trimmedFen != QStringLiteral("startpos")) {
        Rules rules;
        if (!rules.loadFen(trimmedFen)) {
            emit errorOccurred(tr("Invalid FEN position: %1")
                                   .arg(trimmedFen));
            return;
        }
    }

    QString command;
    if (trimmedFen.isEmpty() || trimmedFen == QStringLiteral("startpos")) {
        command = QStringLiteral("position startpos");
    } else {
        command = QStringLiteral("position fen %1").arg(trimmedFen);
    }

    if (!moves.isEmpty()) {
        command += QStringLiteral(" moves ") + moves.join(QChar(' '));
    }

    positionCommand_ = command;

    if (state_ == State::Analyzing) {
        pendingRestart_ = true;
        pendingSearchType_ = SearchType::Analysis;
        sendUciCommand(QStringLiteral("stop"));
        setState(State::Stopping);
    } else if (state_ == State::Stopping) {
        pendingRestart_ = true;
        pendingSearchType_ = SearchType::Analysis;
    } else if (state_ == State::Ready) {
        sendUciCommand(command);
    }
}

void EngineBackend::startAnalysis(int depth, int multiPv) {
    if (!canSendUciCommands()) {
        return;
    }

    currentDepth_ = depth;
    if (multiPv > 0 && multiPv != currentMultiPv_) {
        setOption(QStringLiteral("MultiPV"), multiPv);
        currentMultiPv_ = multiPv;
    }

    if (state_ == State::Analyzing) {
        pendingRestart_ = true;
        pendingSearchType_ = SearchType::Analysis;
        sendUciCommand(QStringLiteral("stop"));
        setState(State::Stopping);
        return;
    }

    if (state_ == State::Stopping) {
        pendingRestart_ = true;
        pendingSearchType_ = SearchType::Analysis;
        return;
    }

    if (!positionCommand_.isEmpty()) {
        sendUciCommand(positionCommand_);
    }

    if (depth > 0) {
        sendUciCommand(QStringLiteral("go depth %1").arg(depth));
    } else {
        sendUciCommand(QStringLiteral("go infinite"));
    }

    activeSearchType_ = SearchType::Analysis;
    setState(State::Analyzing);
}

void EngineBackend::startTimedSearch(qint64 whiteTimeMilliseconds,
                                     qint64 blackTimeMilliseconds,
                                     qint64 whiteIncrementMilliseconds,
                                     qint64 blackIncrementMilliseconds) {
    if (!canSendUciCommands()) {
        return;
    }

    const qint64 whiteTime = std::max<qint64>(0, whiteTimeMilliseconds);
    const qint64 blackTime = std::max<qint64>(0, blackTimeMilliseconds);
    const qint64 whiteIncrement = std::max<qint64>(0, whiteIncrementMilliseconds);
    const qint64 blackIncrement = std::max<qint64>(0, blackIncrementMilliseconds);
    const QString goCommand = QStringLiteral("go wtime %1 btime %2 winc %3 binc %4")
                                  .arg(whiteTime)
                                  .arg(blackTime)
                                  .arg(whiteIncrement)
                                  .arg(blackIncrement);

    if (state_ == State::Analyzing) {
        pendingGoCommand_ = goCommand;
        pendingRestart_ = false;
        pendingSearchType_ = SearchType::Timed;
        sendUciCommand(QStringLiteral("stop"));
        setState(State::Stopping);
        return;
    }

    if (state_ == State::Stopping) {
        pendingGoCommand_ = goCommand;
        pendingRestart_ = false;
        pendingSearchType_ = SearchType::Timed;
        return;
    }

    if (state_ != State::Ready) {
        return;
    }

    if (!positionCommand_.isEmpty()) {
        sendUciCommand(positionCommand_);
    }
    sendUciCommand(goCommand);
    activeSearchType_ = SearchType::Timed;
    setState(State::Analyzing);
}

void EngineBackend::stopAnalysis() {
    if (!canSendUciCommands()) {
        return;
    }

    pendingRestart_ = false;
    pendingGoCommand_.clear();
    if (state_ == State::Analyzing) {
        sendUciCommand(QStringLiteral("stop"));
        setState(State::Stopping);
    }
}

void EngineBackend::setOption(const QString &name, const QVariant &value) {
    if (!canSendUciCommands()) {
        return;
    }

    QString command = QStringLiteral("setoption name %1").arg(name);
    if (value.isValid()) {
        command += QStringLiteral(" value %1").arg(value.toString());
    }
    sendUciCommand(command);
}

void EngineBackend::sendRawCommand(const QString &command) {
    sendUciCommand(command);
}

void EngineBackend::setState(State newState) {
    if (state_ == newState) {
        return;
    }
    state_ = newState;
    emit stateChanged(state_);
}

void EngineBackend::handleLine(const QString &line) {
    emit rawLineReceived(line);

    if (const auto option = UciParser::parseOptionLine(line)) {
        for (auto &knownOption : options_) {
            if (knownOption.name == option->name) {
                knownOption = *option;
                return;
            }
        }
        options_.append(*option);
    } else if (const auto idName = UciParser::parseIdName(line)) {
        engineName_ = *idName;
    } else if (const auto idAuthor = UciParser::parseIdAuthor(line)) {
        engineAuthor_ = *idAuthor;
    } else if (UciParser::isUciOk(line)) {
        emit engineLoaded(engineName_, engineAuthor_);
        sendUciCommand(QStringLiteral("isready"));
    } else if (UciParser::isReadyOk(line)) {
        if (state_ == State::Initializing) {
            setState(State::Ready);
        }
    } else if (const auto analysis = UciParser::parseInfoLine(line)) {
        emit analysisUpdated(*analysis);
    } else if (const auto bestMove = UciParser::parseBestMoveLine(line)) {
        const SearchType finishedSearchType = activeSearchType_;
        emit bestMoveReceived(bestMove->move, bestMove->ponder);
        if (finishedSearchType == SearchType::Timed) {
            emit timedBestMoveReceived(bestMove->move, bestMove->ponder);
        }

        if (!pendingGoCommand_.isEmpty()) {
            const QString pendingGoCommand = std::exchange(pendingGoCommand_, QString());
            pendingRestart_ = false;
            if (!positionCommand_.isEmpty()) {
                sendUciCommand(positionCommand_);
            }
            sendUciCommand(pendingGoCommand);
            activeSearchType_ = SearchType::Timed;
            pendingSearchType_ = SearchType::None;
            setState(State::Analyzing);
        } else if (pendingRestart_) {
            pendingRestart_ = false;
            if (!positionCommand_.isEmpty()) {
                sendUciCommand(positionCommand_);
            }
            if (currentDepth_ > 0) {
                sendUciCommand(QStringLiteral("go depth %1").arg(currentDepth_));
            } else {
                sendUciCommand(QStringLiteral("go infinite"));
            }
            activeSearchType_ = pendingSearchType_;
            pendingSearchType_ = SearchType::None;
            setState(State::Analyzing);
        } else {
            activeSearchType_ = SearchType::None;
            pendingSearchType_ = SearchType::None;
            setState(State::Ready);
        }
    }
}

void EngineBackend::resetEngineSession() {
    engineName_ = defaultEngineName_;
    engineAuthor_.clear();
    options_.clear();
    positionCommand_.clear();
    clearSearchState();
}

void EngineBackend::clearSearchState() {
    pendingGoCommand_.clear();
    pendingRestart_ = false;
    activeSearchType_ = SearchType::None;
    pendingSearchType_ = SearchType::None;
}

void EngineBackend::setDefaultEngineName(const QString &name) {
    defaultEngineName_ = name;
}

void EngineBackend::sendUciCommand(const QString &command) {
    if (!canSendUciCommands()) {
        return;
    }

    emit rawLineSent(command);
    transmitUciCommand(command);
}
