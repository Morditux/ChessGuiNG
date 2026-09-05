//
// Client for a chessgateway server: a persistent TCP connection over the
// `chessgateway/1` protocol (JSON Lines) that gives access to UCI engines
// running on the server. The common UCI backend interface is implemented by
// EngineBackend.
//

#ifndef CHESSGUI_GATEWAYCLIENT_H
#define CHESSGUI_GATEWAYCLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QTimer>

#include "enginebackend.h"

class QTcpSocket;

struct GatewayEngineInfo {
    QString id;
    QString name;
    QString version;
};
Q_DECLARE_METATYPE(GatewayEngineInfo)
Q_DECLARE_METATYPE(QList<GatewayEngineInfo>)

class ChessGatewayClient : public EngineBackend {
    Q_OBJECT

public:
    using State = EngineBackend::State;

    explicit ChessGatewayClient(QObject *parent = nullptr);

    void connectToHost(const QString &host, quint16 port,
                       const QString &accessKey = QString());
    void disconnectFromHost();

    void listEngines();
    void selectEngine(const QString &engineId);
    void stopEngine();

    [[nodiscard]] bool isConnected() const override;
    [[nodiscard]] QString selectedEngineId() const;

signals:
    void helloReceived(const QStringList &features);
    void authenticated();
    void enginesListed(const QList<GatewayEngineInfo> &engines);
    void engineSelected(const GatewayEngineInfo &engine);
    void engineStopped(const QString &engineId);
    void gatewayError(const QString &code, const QString &message);

private slots:
    void onDisconnected();
    void onReadyRead();
    void onSocketError();
    void onSelectRetryTimeout();

private:
    enum class PendingOp {
        None,
        ListEngines,
        SelectEngine,
        StopEngine
    };

    void sendJson(const QJsonObject &object);
    [[nodiscard]] bool canSendUciCommands() const override;
    void transmitUciCommand(const QString &command) override;
    void sendRequest(PendingOp operation, const QJsonObject &object);
    void handleMessage(const QJsonObject &message);
    void handleError(const QJsonObject &message);
    void resetRemoteEngineSession();
    void clearPendingSelection();

    QTimer selectRetryTimer_;
    QTcpSocket *socket_ = nullptr;
    QByteArray inputBuffer_;
    QString accessKey_;
    QString engineId_;
    QString pendingSelectEngineId_;
    bool pendingSelectionExited_ = false;

    int nextRequestId_ = 0;
    int selectRetryCount_ = 0;
    QString pendingRequestId_;
    PendingOp pendingOperation_ = PendingOp::None;
};

#endif // CHESSGUI_GATEWAYCLIENT_H
