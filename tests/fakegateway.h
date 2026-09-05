//
// Fake chessgateway/1 server used by tests. It answers hello, list_engines,
// select_engine and forwards UCI commands to a scripted engine simulation.
//

#ifndef CHESSGUI_FAKEGATEWAY_H
#define CHESSGUI_FAKEGATEWAY_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

class FakeGateway : public QObject {
public:
    explicit FakeGateway(QObject *parent = nullptr)
        : QObject(parent) {
        connect(&server_, &QTcpServer::newConnection,
                this, &FakeGateway::onNewConnection);
        server_.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] bool isListening() const {
        return server_.isListening();
    }

    [[nodiscard]] quint16 port() const {
        return server_.serverPort();
    }

    [[nodiscard]] QStringList uciCommands() const {
        return uciCommands_;
    }

    [[nodiscard]] QStringList rawRequests() const {
        return rawRequests_;
    }

    void setRespondToSelectWithError(bool enabled) {
        respondToSelectWithError_ = enabled;
    }

    void setEmptyEngineList(bool enabled) {
        emptyEngineList_ = enabled;
    }

    void requireAccessKey(const QString &accessKey) {
        requireAccessKey_ = true;
        accessKey_ = accessKey;
    }

private slots:
    void onNewConnection() {
        QTcpSocket *socket = server_.nextPendingConnection();
        if (socket == nullptr) {
            return;
        }
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            onReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        authenticated_ = false;

        QJsonObject hello;
        hello[QStringLiteral("type")] = QStringLiteral("hello");
        hello[QStringLiteral("protocol")] = QStringLiteral("chessgateway/1");
        if (requireAccessKey_) {
            hello[QStringLiteral("features")] =
                QJsonArray{QStringLiteral("engine_list"),
                           QStringLiteral("engine_selection"),
                           QStringLiteral("engine_stop"),
                           QStringLiteral("uci_stream"),
                           QStringLiteral("access_keys")};
        }
        sendJson(socket, hello);
    }

    void onReadyRead(QTcpSocket *socket) {
        if (socket == nullptr) {
            return;
        }

        buffer_.append(socket->readAll());

        while (true) {
            const int newlineIndex = buffer_.indexOf('\n');
            if (newlineIndex < 0) {
                break;
            }

            QByteArray rawLine = buffer_.left(newlineIndex);
            buffer_.remove(0, newlineIndex + 1);
            if (rawLine.endsWith('\r')) {
                rawLine.chop(1);
            }

            rawRequests_.append(QString::fromUtf8(rawLine));
            const QJsonObject request =
                QJsonDocument::fromJson(rawLine).object();
            handleRequest(socket, request);
        }
    }

private:
    void sendJson(QTcpSocket *socket, const QJsonObject &object) {
        if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        socket->write("\n");
    }

    void handleRequest(QTcpSocket *socket, const QJsonObject &request) {
        const QString type = request.value(QStringLiteral("type")).toString();

        if (requireAccessKey_ && !authenticated_) {
            if (type == QStringLiteral("authenticate")) {
                if (request.value(QStringLiteral("access_key")).toString() == accessKey_) {
                    authenticated_ = true;
                    QJsonObject response;
                    response[QStringLiteral("type")] = QStringLiteral("authenticated");
                    response[QStringLiteral("request_id")] =
                        request.value(QStringLiteral("request_id"));
                    sendJson(socket, response);
                    return;
                }
                QJsonObject response;
                response[QStringLiteral("type")] = QStringLiteral("error");
                response[QStringLiteral("request_id")] =
                    request.value(QStringLiteral("request_id"));
                response[QStringLiteral("code")] = QStringLiteral("invalid_access_key");
                response[QStringLiteral("message")] = QStringLiteral("invalid access key");
                sendJson(socket, response);
                socket->disconnectFromHost();
                return;
            }
            QJsonObject response;
            response[QStringLiteral("type")] = QStringLiteral("error");
            response[QStringLiteral("request_id")] =
                request.value(QStringLiteral("request_id"));
            response[QStringLiteral("code")] = QStringLiteral("authentication_required");
            response[QStringLiteral("message")] = QStringLiteral("authenticate first");
            sendJson(socket, response);
            return;
        }

        if (type == QStringLiteral("list_engines")) {
            QJsonObject response;
            response[QStringLiteral("type")] = QStringLiteral("engines");
            if (!emptyEngineList_) {
                QJsonArray engines;
                QJsonObject stockfish;
                stockfish[QStringLiteral("id")] = QStringLiteral("stockfish-17");
                stockfish[QStringLiteral("name")] = QStringLiteral("Stockfish");
                stockfish[QStringLiteral("version")] = QStringLiteral("17");
                engines.append(stockfish);

                QJsonObject komodo;
                komodo[QStringLiteral("id")] = QStringLiteral("komodo-14");
                komodo[QStringLiteral("name")] = QStringLiteral("Komodo");
                komodo[QStringLiteral("version")] = QStringLiteral("14.1");
                engines.append(komodo);

                response[QStringLiteral("engines")] = engines;
            } else {
                response[QStringLiteral("engines")] = QJsonArray();
            }
            sendJson(socket, response);
        } else if (type == QStringLiteral("select_engine")) {
            if (respondToSelectWithError_) {
                QJsonObject response;
                response[QStringLiteral("type")] = QStringLiteral("error");
                response[QStringLiteral("code")] = QStringLiteral("unknown_engine");
                response[QStringLiteral("message")] = QStringLiteral("unknown engine");
                sendJson(socket, response);
                return;
            }

            QJsonObject engine;
            engine[QStringLiteral("id")] =
                request.value(QStringLiteral("engine_id")).toString();
            engine[QStringLiteral("name")] = QStringLiteral("Stockfish");
            engine[QStringLiteral("version")] = QStringLiteral("17");

            QJsonObject response;
            response[QStringLiteral("type")] = QStringLiteral("engine_selected");
            response[QStringLiteral("engine_id")] = engine[QStringLiteral("id")];
            response[QStringLiteral("engine")] = engine;
            sendJson(socket, response);
        } else if (type == QStringLiteral("uci")) {
            const QString command =
                request.value(QStringLiteral("command")).toString();
            uciCommands_.append(command);
            simulateEngine(socket, command);
        }
    }

    void simulateEngine(QTcpSocket *socket, const QString &command) {
        const auto emitLine = [this, socket](const QString &line) {
            QJsonObject event;
            event[QStringLiteral("type")] = QStringLiteral("uci_output");
            event[QStringLiteral("engine_id")] = QStringLiteral("stockfish-17");
            event[QStringLiteral("line")] = line;
            sendJson(socket, event);
        };

        if (command == QStringLiteral("uci")) {
            emitLine(QStringLiteral("id name RemoteMockEngine 1.0"));
            emitLine(QStringLiteral("id author TestAuthor"));
            emitLine(QStringLiteral("uciok"));
        } else if (command == QStringLiteral("isready")) {
            emitLine(QStringLiteral("readyok"));
        } else if (command == QStringLiteral("stop")) {
            emitLine(QStringLiteral("bestmove e7e5 ponder g1f3"));
        } else if (command.startsWith(QStringLiteral("go"))) {
            emitLine(QStringLiteral("info depth 15 score cp 65 nodes 50000 nps 1000000 time 50 pv e7e5 g1f3"));
            emitLine(QStringLiteral("bestmove e7e5 ponder g1f3"));
        }
    }

    QTcpServer server_;
    QByteArray buffer_;
    QStringList uciCommands_;
    QStringList rawRequests_;
    bool respondToSelectWithError_ = false;
    bool emptyEngineList_ = false;
    bool requireAccessKey_ = false;
    bool authenticated_ = false;
    QString accessKey_;
};

#endif // CHESSGUI_FAKEGATEWAY_H
