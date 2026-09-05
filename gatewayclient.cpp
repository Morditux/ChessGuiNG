//
// Client for a chessgateway server: a persistent TCP connection over the
// `chessgateway/1` protocol (JSON Lines) that gives access to UCI engines
// running on the server. The interface mirrors UciEngine so the client can
// act as a drop-in engine backend.
//

#include "gatewayclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaType>
#include <QTcpSocket>

namespace {

constexpr int kSelectRetryDelayMs = 600;

QJsonObject jsonMessage(const QByteArray &type, const QByteArray &requestId) {
    QJsonObject message;
    message.insert(QStringLiteral("type"), QString::fromUtf8(type));
    if (!requestId.isEmpty()) {
        message.insert(QStringLiteral("request_id"),
                       QString::fromUtf8(requestId));
    }
    return message;
}

QJsonObject engineToJson(const GatewayEngineInfo &engine) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), engine.id);
    object.insert(QStringLiteral("name"), engine.name);
    object.insert(QStringLiteral("version"), engine.version);
    return object;
}

GatewayEngineInfo engineFromJson(const QJsonObject &object) {
    GatewayEngineInfo engine;
    engine.id = object.value(QStringLiteral("id")).toString();
    engine.name = object.value(QStringLiteral("name")).toString();
    engine.version = object.value(QStringLiteral("version")).toString();
    return engine;
}

} // namespace

ChessGatewayClient::ChessGatewayClient(QObject *parent)
    : EngineBackend(QString(), parent)
    , selectRetryTimer_(this) {
    qRegisterMetaType<GatewayEngineInfo>();
    qRegisterMetaType<QList<GatewayEngineInfo>>();

    selectRetryTimer_.setSingleShot(true);
    connect(&selectRetryTimer_, &QTimer::timeout,
            this, &ChessGatewayClient::onSelectRetryTimeout);

    socket_ = new QTcpSocket(this);

    connect(socket_, &QTcpSocket::disconnected, this, &ChessGatewayClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &ChessGatewayClient::onReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this, &ChessGatewayClient::onSocketError);
}

void ChessGatewayClient::connectToHost(const QString &host, quint16 port,
                                       const QString &accessKey) {
    if (state() != State::Disconnected && state() != State::Connecting) {
        disconnectFromHost();
    }
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }
    accessKey_ = accessKey.trimmed();
    resetRemoteEngineSession();
    inputBuffer_.clear();
    pendingOperation_ = PendingOp::None;
    pendingRequestId_.clear();
    clearPendingSelection();
    setState(State::Connecting);
    socket_->connectToHost(host, port);
}

void ChessGatewayClient::disconnectFromHost() {
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            socket_->abort();
        }
    }
    resetRemoteEngineSession();
    inputBuffer_.clear();
    pendingOperation_ = PendingOp::None;
    pendingRequestId_.clear();
    clearPendingSelection();
    setState(State::Disconnected);
}

bool ChessGatewayClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState &&
           state_ != State::Disconnected && state_ != State::Connecting;
}

QString ChessGatewayClient::selectedEngineId() const {
    return engineId_;
}

void ChessGatewayClient::onDisconnected() {
    resetRemoteEngineSession();
    inputBuffer_.clear();
    pendingOperation_ = PendingOp::None;
    pendingRequestId_.clear();
    clearPendingSelection();
    setState(State::Disconnected);
}

void ChessGatewayClient::onSocketError() {
    emit errorOccurred(socket_->errorString());
}

void ChessGatewayClient::sendJson(const QJsonObject &object) {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    socket_->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    socket_->write("\n");
}

bool ChessGatewayClient::canSendUciCommands() const {
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

void ChessGatewayClient::transmitUciCommand(const QString &command) {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (command.trimmed().isEmpty()) {
        return;
    }
    QJsonObject message = jsonMessage("uci", QByteArray());
    message.insert(QStringLiteral("command"), command);
    sendJson(message);
}

void ChessGatewayClient::sendRequest(PendingOp operation,
                                     const QJsonObject &object) {
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (pendingOperation_ != PendingOp::None || selectRetryTimer_.isActive()) {
        return;
    }
    pendingOperation_ = operation;
    pendingRequestId_ = QString::number(++nextRequestId_);
    QJsonObject message = object;
    message.insert(QStringLiteral("request_id"), pendingRequestId_);
    sendJson(message);
}

void ChessGatewayClient::handleError(const QJsonObject &message) {
    const QString code = message.value(QStringLiteral("code")).toString();
    const QString errorMessage = message.value(QStringLiteral("message")).toString();
    const QString requestId = message.value(QStringLiteral("request_id")).toString();

    if (!requestId.isEmpty() && requestId == pendingRequestId_) {
        const PendingOp failedOperation = pendingOperation_;

        if (failedOperation == PendingOp::SelectEngine &&
            code == QStringLiteral("select_too_frequent") &&
            selectRetryCount_ == 0) {
            ++selectRetryCount_;
            pendingOperation_ = PendingOp::None;
            pendingRequestId_.clear();
            selectRetryTimer_.start(kSelectRetryDelayMs);
            return;
        }

        pendingOperation_ = PendingOp::None;
        pendingRequestId_.clear();
        if (failedOperation == PendingOp::SelectEngine) {
            const bool selectionWasRateLimited =
                code == QStringLiteral("select_too_frequent");
            clearPendingSelection();
            if (!selectionWasRateLimited) {
                resetRemoteEngineSession();
            }
        }
    }

    emit gatewayError(code, errorMessage);
    if (code.isEmpty()) {
        emit errorOccurred(errorMessage);
    } else {
        emit errorOccurred(tr("%1: %2").arg(code, errorMessage));
    }
}

void ChessGatewayClient::resetRemoteEngineSession() {
    engineId_.clear();
    EngineBackend::resetEngineSession();
}

void ChessGatewayClient::clearPendingSelection() {
    selectRetryTimer_.stop();
    pendingSelectEngineId_.clear();
    pendingSelectionExited_ = false;
    selectRetryCount_ = 0;
}

void ChessGatewayClient::listEngines() {
    QJsonObject message = jsonMessage("list_engines", QByteArray());
    sendRequest(PendingOp::ListEngines, message);
}

void ChessGatewayClient::selectEngine(const QString &engineId) {
    if (engineId.isEmpty()) {
        return;
    }
    if (socket_->state() != QAbstractSocket::ConnectedState ||
        pendingOperation_ != PendingOp::None || selectRetryTimer_.isActive()) {
        return;
    }

    QJsonObject message = jsonMessage("select_engine", QByteArray());
    message.insert(QStringLiteral("engine_id"), engineId);
    pendingSelectEngineId_ = engineId;
    pendingSelectionExited_ = false;
    selectRetryCount_ = 0;
    sendRequest(PendingOp::SelectEngine, message);
}

void ChessGatewayClient::stopEngine() {
    QJsonObject message = jsonMessage("stop_engine", QByteArray());
    sendRequest(PendingOp::StopEngine, message);
}

void ChessGatewayClient::onSelectRetryTimeout() {
    if (pendingSelectEngineId_.isEmpty() ||
        socket_->state() != QAbstractSocket::ConnectedState ||
        pendingOperation_ != PendingOp::None) {
        return;
    }

    QJsonObject message = jsonMessage("select_engine", QByteArray());
    message.insert(QStringLiteral("engine_id"), pendingSelectEngineId_);
    sendRequest(PendingOp::SelectEngine, message);
}

void ChessGatewayClient::onReadyRead() {
    inputBuffer_.append(socket_->readAll());

    while (true) {
        const int newlineIndex = inputBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray rawLine = inputBuffer_.left(newlineIndex);
        inputBuffer_.remove(0, newlineIndex + 1);

        if (rawLine.endsWith('\r')) {
            rawLine.chop(1);
        }

        const QString line = QString::fromUtf8(rawLine);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit errorOccurred(tr("Invalid JSON message from server: %1")
                                   .arg(line));
            continue;
        }
        handleMessage(document.object());
    }
}

void ChessGatewayClient::handleMessage(const QJsonObject &message) {
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("hello")) {
        QStringList features;
        const QJsonValue featuresValue = message.value(QStringLiteral("features"));
        if (featuresValue.isArray()) {
            for (const auto &feature : featuresValue.toArray()) {
                features.append(feature.toString());
            }
        }
        const QString protocol = message.value(QStringLiteral("protocol")).toString();
        if (!protocol.isEmpty() && protocol != QStringLiteral("chessgateway/1")) {
            emit errorOccurred(tr("Unsupported protocol version: %1").arg(protocol));
            disconnectFromHost();
            return;
        }
        emit helloReceived(features);
        if (features.contains(QStringLiteral("access_keys"))) {
            if (accessKey_.isEmpty()) {
                emit errorOccurred(tr(
                    "The server requires an access key; enter it in the remote "
                    "engine configuration."));
                disconnectFromHost();
                return;
            }
            setState(State::Authenticating);
            QJsonObject message;
            message.insert(QStringLiteral("type"), QStringLiteral("authenticate"));
            message.insert(QStringLiteral("request_id"), QStringLiteral("auth"));
            message.insert(QStringLiteral("access_key"), accessKey_);
            sendJson(message);
        } else {
            setState(State::Connected);
        }
        return;
    }

    if (type == QStringLiteral("authenticated")) {
        setState(State::Connected);
        emit authenticated();
        return;
    }

    if (type == QStringLiteral("engines")) {
        if (pendingOperation_ != PendingOp::ListEngines) {
            return;
        }
        pendingOperation_ = PendingOp::None;
        pendingRequestId_.clear();

        QList<GatewayEngineInfo> engines;
        const QJsonValue enginesValue = message.value(QStringLiteral("engines"));
        if (enginesValue.isArray()) {
            for (const auto &engine : enginesValue.toArray()) {
                engines.append(engineFromJson(engine.toObject()));
            }
        }
        emit enginesListed(engines);
        return;
    }

    if (type == QStringLiteral("engine_selected")) {
        const QString requestId = message.value(QStringLiteral("request_id")).toString();
        const QString engineId = message.value(QStringLiteral("engine_id")).toString();
        if (pendingOperation_ != PendingOp::SelectEngine ||
            (!requestId.isEmpty() && requestId != pendingRequestId_) ||
            (!pendingSelectEngineId_.isEmpty() &&
             engineId != pendingSelectEngineId_)) {
            return;
        }

        const bool selectionEngineExited = pendingSelectionExited_;
        pendingOperation_ = PendingOp::None;
        pendingRequestId_.clear();
        clearPendingSelection();

        const GatewayEngineInfo engine = engineFromJson(
            message.value(QStringLiteral("engine")).toObject());

        resetRemoteEngineSession();
        if (selectionEngineExited) {
            emit engineSelected(engine);
            setState(State::Connected);
            return;
        }

        engineId_ = engineId;
        options_.clear();
        emit engineSelected(engine);
        setState(State::Initializing);
        sendRawCommand(QStringLiteral("uci"));
        return;
    }

    if (type == QStringLiteral("engine_exited")) {
        const QString exitedEngineId =
            message.value(QStringLiteral("engine_id")).toString();
        const bool isCurrentEngine = !exitedEngineId.isEmpty() &&
                                     exitedEngineId == engineId_;
        const bool isPendingSelection =
            !exitedEngineId.isEmpty() &&
            exitedEngineId == pendingSelectEngineId_ &&
            (pendingOperation_ == PendingOp::SelectEngine ||
             selectRetryTimer_.isActive());
        if (!isCurrentEngine && !isPendingSelection) {
            return;
        }

        if (isPendingSelection) {
            pendingSelectionExited_ = true;
        }
        resetRemoteEngineSession();
        handleLine(QStringLiteral("info string gateway engine %1 exited")
                       .arg(exitedEngineId));
        setState(State::Connected);
        return;
    }

    if (type == QStringLiteral("engine_stopped")) {
        if (pendingOperation_ != PendingOp::StopEngine) {
            return;
        }
        pendingOperation_ = PendingOp::None;
        pendingRequestId_.clear();

        const QString engineId = message.value(QStringLiteral("engine_id")).toString();
        resetRemoteEngineSession();
        emit engineStopped(engineId);
        setState(State::Connected);
        return;
    }

    if (type == QStringLiteral("uci_output")) {
        const QString outputEngineId =
            message.value(QStringLiteral("engine_id")).toString();
        if (!outputEngineId.isEmpty() && outputEngineId != engineId_) {
            return;
        }
        const QString line = message.value(QStringLiteral("line")).toString();
        if (!line.isEmpty()) {
            handleLine(line);
        }
        return;
    }

    if (type == QStringLiteral("error")) {
        handleError(message);
        return;
    }
}
