//
// Unit tests for ChessGatewayClient against a fake chessgateway server.
//

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QVector>

#include "gatewayclient.h"

namespace {

QJsonObject parseJsonLine(const QByteArray &line) {
    return QJsonDocument::fromJson(line).object();
}

} // namespace

class FakeGatewayServer : public QObject {
    Q_OBJECT

public:
    explicit FakeGatewayServer(QObject *parent = nullptr)
        : QObject(parent) {
        QVERIFY(server_.listen(QHostAddress::LocalHost, 0));
        connect(&server_, &QTcpServer::newConnection,
                this, &FakeGatewayServer::onNewConnection);
    }

    [[nodiscard]] quint16 port() const {
        return server_.serverPort();
    }

    void setRequireAccessKey(const QString &accessKey) {
        requireAccessKey_ = true;
        accessKey_ = accessKey;
    }

    [[nodiscard]] QVector<QJsonObject> uciCommands() const {
        return uciCommands_;
    }

    [[nodiscard]] QVector<QJsonObject> selectRequests() const {
        return selectRequests_;
    }

    void setSelectTooFrequentResponses(int count) {
        selectTooFrequentResponses_ = count;
    }

    void setExitBeforeSelection(bool enabled) {
        exitBeforeSelection_ = enabled;
    }

    void setHoldSearch(bool enabled) {
        holdSearch_ = enabled;
    }

    void sendRawMessage(const QString &line) {
        if (socket_) {
            socket_->write(line.toUtf8() + "\n");
        }
    }

    void sendEngineExited(const QString &engineId) {
        sendObj({
            {QStringLiteral("type"), QStringLiteral("engine_exited")},
            {QStringLiteral("engine_id"), engineId},
        });
        if (selectedEngineId_ == engineId) {
            selectedEngineId_.clear();
            engineExited_ = true;
        }
    }

    void disconnectClient() {
        if (socket_) {
            socket_->disconnectFromHost();
        }
    }

private slots:
    void onNewConnection() {
        socket_ = server_.nextPendingConnection();
        authenticated_ = false;
        connect(socket_, &QTcpSocket::readyRead,
                this, &FakeGatewayServer::onReadyRead);
        QJsonArray features{QStringLiteral("engine_list"),
                            QStringLiteral("engine_selection"),
                            QStringLiteral("engine_stop"),
                            QStringLiteral("uci_stream")};
        if (requireAccessKey_) {
            features.append(QStringLiteral("access_keys"));
        }
        sendObj({
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("protocol"), QStringLiteral("chessgateway/1")},
            {QStringLiteral("features"), features},
        });
    }

    void onReadyRead() {
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
            if (!rawLine.isEmpty()) {
                handleMessage(parseJsonLine(rawLine));
            }
        }
    }

private:
    void sendObj(const QJsonObject &object) {
        if (socket_) {
            socket_->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
            socket_->write("\n");
        }
    }

    static QJsonObject engineJson(const QString &id, const QString &name,
                                  const QString &version) {
        return {
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("version"), version},
        };
    }

    void sendUciOutput(const QString &line) {
        sendObj({
            {QStringLiteral("type"), QStringLiteral("uci_output")},
            {QStringLiteral("engine_id"), selectedEngineId_},
            {QStringLiteral("line"), line},
        });
    }

    void handleMessage(const QJsonObject &message) {
        const QString type = message.value(QStringLiteral("type")).toString();
        const QString requestId = message.value(QStringLiteral("request_id")).toString();

        if (requireAccessKey_ && !authenticated_) {
            if (type == QStringLiteral("authenticate")) {
                if (message.value(QStringLiteral("access_key")).toString() == accessKey_) {
                    authenticated_ = true;
                    sendObj({
                        {QStringLiteral("type"), QStringLiteral("authenticated")},
                        {QStringLiteral("request_id"), requestId},
                    });
                    return;
                }
                sendObj({
                    {QStringLiteral("type"), QStringLiteral("error")},
                    {QStringLiteral("request_id"), requestId},
                    {QStringLiteral("code"), QStringLiteral("invalid_access_key")},
                    {QStringLiteral("message"), QStringLiteral("invalid access key")},
                });
                disconnectClient();
                return;
            }
            sendObj({
                {QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("request_id"), requestId},
                {QStringLiteral("code"), QStringLiteral("authentication_required")},
                {QStringLiteral("message"), QStringLiteral("authenticate first")},
            });
            return;
        }

        if (type == QStringLiteral("list_engines")) {
            sendObj({
                {QStringLiteral("type"), QStringLiteral("engines")},
                {QStringLiteral("request_id"), requestId},
                {QStringLiteral("engines"), QJsonArray{
                                                  engineJson(QStringLiteral("sf"),
                                                             QStringLiteral("Stockfish"),
                                                             QStringLiteral("17")),
                                                  engineJson(QStringLiteral("kom"),
                                                             QStringLiteral("Komodo"),
                                                             QStringLiteral("15")),
                                              }},
            });
            return;
        }

        if (type == QStringLiteral("select_engine")) {
            const QString engineId = message.value(QStringLiteral("engine_id")).toString();
            selectRequests_.append(message);
            if (selectTooFrequentResponses_ > 0) {
                --selectTooFrequentResponses_;
                sendObj({
                    {QStringLiteral("type"), QStringLiteral("error")},
                    {QStringLiteral("request_id"), requestId},
                    {QStringLiteral("code"), QStringLiteral("select_too_frequent")},
                    {QStringLiteral("message"),
                     QStringLiteral("wait before selecting another engine")},
                });
            } else if (engineId == QStringLiteral("sf") ||
                       engineId == QStringLiteral("kom")) {
                if (exitBeforeSelection_) {
                    sendEngineExited(engineId);
                }
                selectedEngineId_ = engineId;
                engineExited_ = false;
                sendObj({
                    {QStringLiteral("type"), QStringLiteral("engine_selected")},
                    {QStringLiteral("request_id"), requestId},
                    {QStringLiteral("engine_id"), engineId},
                    {QStringLiteral("engine"),
                     engineJson(engineId,
                                engineId == QStringLiteral("sf")
                                    ? QStringLiteral("Stockfish")
                                    : QStringLiteral("Komodo"),
                                engineId == QStringLiteral("sf")
                                    ? QStringLiteral("17")
                                    : QStringLiteral("15"))},
                });
            } else {
                sendObj({
                    {QStringLiteral("type"), QStringLiteral("error")},
                    {QStringLiteral("request_id"), requestId},
                    {QStringLiteral("code"), QStringLiteral("unknown_engine")},
                    {QStringLiteral("message"),
                     QStringLiteral("unknown engine '%1'").arg(engineId)},
                });
            }
            return;
        }

        if (type == QStringLiteral("stop_engine")) {
            sendObj({
                {QStringLiteral("type"), QStringLiteral("engine_stopped")},
                {QStringLiteral("request_id"), requestId},
                {QStringLiteral("engine_id"), selectedEngineId_},
            });
            selectedEngineId_.clear();
            engineExited_ = false;
            return;
        }

        if (type == QStringLiteral("uci")) {
            const QString command = message.value(QStringLiteral("command")).toString();
            if (selectedEngineId_.isEmpty() || engineExited_) {
                sendObj({
                    {QStringLiteral("type"), QStringLiteral("error")},
                    {QStringLiteral("code"), QStringLiteral("no_engine_selected")},
                    {QStringLiteral("message"),
                     QStringLiteral("select an engine before sending UCI commands")},
                });
                return;
            }
            uciCommands_.append(message);

            if (command == QStringLiteral("uci")) {
                sendUciOutput(QStringLiteral("id name Stockfish 17"));
                sendUciOutput(QStringLiteral("id author The Stockfish developers"));
                sendUciOutput(QStringLiteral(
                    "option name Threads type spin default 1 min 1 max 16"));
                sendUciOutput(QStringLiteral("uciok"));
            } else if (command == QStringLiteral("isready")) {
                sendUciOutput(QStringLiteral("readyok"));
            } else if (command.startsWith(QStringLiteral("go "))) {
                sendUciOutput(QStringLiteral(
                    "info depth 15 score cp 65 nodes 51234 nps 1000000 time 51 pv e2e4 e7e5"));
                if (!holdSearch_) {
                    sendUciOutput(QStringLiteral("bestmove e2e4"));
                }
            } else if (command == QStringLiteral("stop")) {
                sendUciOutput(QStringLiteral("bestmove e2e4"));
            }
            return;
        }

        sendObj({
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("code"), QStringLiteral("invalid_request")},
            {QStringLiteral("message"), QStringLiteral("unknown request type")},
        });
    }

    QTcpServer server_;
    QTcpSocket *socket_ = nullptr;
    QByteArray inputBuffer_;
    QString selectedEngineId_;
    QVector<QJsonObject> uciCommands_;
    QVector<QJsonObject> selectRequests_;
    bool requireAccessKey_ = false;
    QString accessKey_;
    bool authenticated_ = false;
    int selectTooFrequentResponses_ = 0;
    bool exitBeforeSelection_ = false;
    bool holdSearch_ = false;
    bool engineExited_ = false;
};

class GatewayClientTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialState();
    void testConnectAndHello();
    void testListEngines();
    void testAuthenticateWithValidKey();
    void testAuthenticateMissingKey();
    void testAuthenticateWithInvalidKey();
    void testSelectEngineInitializesUciDialogue();
    void testEngineExitedAfterSelection();
    void testEngineExitedDuringSearch();
    void testEngineExitedBeforeSelectionAcknowledgement();
    void testUciCommandWithoutSelectionFails();
    void testEmptyUciCommandsAreIgnored();
    void testStartAnalysis();
    void testStartTimedSearch();
    void testStopEngineKeepsConnection();
    void testServerDisconnect();
    void testUnknownEngineError();
    void testSelectTooFrequentRetries();
    void testSelectTooFrequentRetryFailure();
};

void GatewayClientTest::testInitialState() {
    ChessGatewayClient client;
    QCOMPARE(client.state(), ChessGatewayClient::State::Disconnected);
    QVERIFY(!client.isConnected());
    QVERIFY(!client.isAnalyzing());
    QVERIFY(client.engineName().isEmpty());
    QVERIFY(client.selectedEngineId().isEmpty());
}

void GatewayClientTest::testConnectAndHello() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QSignalSpy stateSpy(&client, &ChessGatewayClient::stateChanged);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QVERIFY(helloSpy.wait(2000));
    QCOMPARE(helloSpy.count(), 1);
    QCOMPARE(helloSpy.first().at(0).toStringList(),
             (QStringList{QStringLiteral("engine_list"),
                          QStringLiteral("engine_selection"),
                          QStringLiteral("engine_stop"),
                          QStringLiteral("uci_stream")}));
    QTRY_COMPARE(client.state(), ChessGatewayClient::State::Connected);
}

void GatewayClientTest::testListEngines() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    QSignalSpy enginesSpy(&client, &ChessGatewayClient::enginesListed);
    client.listEngines();
    QTRY_COMPARE_WITH_TIMEOUT(enginesSpy.count(), 1, 2000);

    const auto engines = qvariant_cast<QList<GatewayEngineInfo>>(enginesSpy.last().at(0));
    QCOMPARE(engines.size(), 2);
    QCOMPARE(engines.at(0).id, QStringLiteral("sf"));
    QCOMPARE(engines.at(0).name, QStringLiteral("Stockfish"));
    QCOMPARE(engines.at(0).version, QStringLiteral("17"));
    QCOMPARE(engines.at(1).id, QStringLiteral("kom"));
}

void GatewayClientTest::testAuthenticateWithValidKey() {
    FakeGatewayServer server;
    server.setRequireAccessKey(QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    ChessGatewayClient client;
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QSignalSpy authenticatedSpy(&client, &ChessGatewayClient::authenticated);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port(),
                         QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    QVERIFY(helloSpy.wait(2000));
    QVERIFY(helloSpy.first().at(0).toStringList().contains(
        QStringLiteral("access_keys")));
    QCOMPARE(client.state(), ChessGatewayClient::State::Authenticating);

    QVERIFY(authenticatedSpy.wait(2000));
    QCOMPARE(authenticatedSpy.count(), 1);
    QTRY_COMPARE(client.state(), ChessGatewayClient::State::Connected);

    QSignalSpy enginesSpy(&client, &ChessGatewayClient::enginesListed);
    client.listEngines();
    QTRY_COMPARE_WITH_TIMEOUT(enginesSpy.count(), 1, 2000);
    const auto engines = qvariant_cast<QList<GatewayEngineInfo>>(enginesSpy.last().at(0));
    QCOMPARE(engines.size(), 2);
}

void GatewayClientTest::testAuthenticateMissingKey() {
    FakeGatewayServer server;
    server.setRequireAccessKey(QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    ChessGatewayClient client;
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QSignalSpy errorSpy(&client, &ChessGatewayClient::errorOccurred);
    QSignalSpy stateSpy(&client, &ChessGatewayClient::stateChanged);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QVERIFY(helloSpy.wait(2000));
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() == 1, 2000);
    QVERIFY(errorSpy.first().at(0).toString().contains(
        QStringLiteral("access key")));
    QTRY_COMPARE(client.state(), ChessGatewayClient::State::Disconnected);
}

void GatewayClientTest::testAuthenticateWithInvalidKey() {
    FakeGatewayServer server;
    server.setRequireAccessKey(QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    ChessGatewayClient client;
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QSignalSpy gatewayErrorSpy(&client, &ChessGatewayClient::gatewayError);
    QSignalSpy stateSpy(&client, &ChessGatewayClient::stateChanged);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.port(),
                         QStringLiteral("00000000-0000-0000-0000-000000000000"));
    QVERIFY(helloSpy.wait(2000));
    QVERIFY(gatewayErrorSpy.wait(2000));
    QCOMPARE(gatewayErrorSpy.first().at(0).toString(),
             QStringLiteral("invalid_access_key"));
    QTRY_COMPARE(client.state(), ChessGatewayClient::State::Disconnected);
}

void GatewayClientTest::testSelectEngineInitializesUciDialogue() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    QSignalSpy selectedSpy(&client, &ChessGatewayClient::engineSelected);
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QSignalSpy stateSpy(&client, &ChessGatewayClient::stateChanged);

    client.selectEngine(QStringLiteral("sf"));
    QVERIFY(selectedSpy.wait(2000));
    QCOMPARE(selectedSpy.count(), 1);
    const auto info = qvariant_cast<GatewayEngineInfo>(selectedSpy.first().at(0));
    QCOMPARE(info.id, QStringLiteral("sf"));
    QCOMPARE(client.selectedEngineId(), QStringLiteral("sf"));

    QVERIFY(loadedSpy.wait(2000));
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(loadedSpy.first().at(0).toString(), QStringLiteral("Stockfish 17"));
    QCOMPARE(loadedSpy.first().at(1).toString(),
             QStringLiteral("The Stockfish developers"));

    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);
    QCOMPARE(client.engineName(), QStringLiteral("Stockfish 17"));
    QCOMPARE(client.options().size(), 1);
    QCOMPARE(client.options().at(0).name, QStringLiteral("Threads"));
}

void GatewayClientTest::testEngineExitedAfterSelection() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    QSignalSpy rawLineSpy(&client, &EngineBackend::rawLineReceived);
    server.sendRawMessage(QStringLiteral(
        R"({"type":"engine_exited","engine_id":"unrelated"})"));
    QTest::qWait(50);
    QCOMPARE(rawLineSpy.count(), 0);
    QCOMPARE(client.selectedEngineId(), QStringLiteral("sf"));

    server.sendEngineExited(QStringLiteral("sf"));
    QTRY_COMPARE_WITH_TIMEOUT(rawLineSpy.count(), 1, 2000);
    QCOMPARE(rawLineSpy.first().at(0).toString(),
             QStringLiteral("info string gateway engine sf exited"));
    QCOMPARE(client.selectedEngineId(), QString());
    QCOMPARE(client.state(), ChessGatewayClient::State::Connected);
    QVERIFY(client.isConnected());
    QVERIFY(!client.isAnalyzing());
}

void GatewayClientTest::testEngineExitedDuringSearch() {
    FakeGatewayServer server;
    server.setHoldSearch(true);
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    QSignalSpy rawLineSpy(&client, &EngineBackend::rawLineReceived);
    QSignalSpy bestMoveSpy(&client, &EngineBackend::bestMoveReceived);
    client.startAnalysis(15, 1);
    QCOMPARE(client.state(), ChessGatewayClient::State::Analyzing);

    server.sendEngineExited(QStringLiteral("sf"));
    QTRY_COMPARE_WITH_TIMEOUT(rawLineSpy.count(), 1, 2000);
    QCOMPARE(rawLineSpy.first().at(0).toString(),
             QStringLiteral("info string gateway engine sf exited"));
    QCOMPARE(client.state(), ChessGatewayClient::State::Connected);
    QVERIFY(!client.isAnalyzing());
    QCOMPARE(bestMoveSpy.count(), 0);

    server.sendRawMessage(QStringLiteral(
        R"({"type":"uci_output","engine_id":"sf","line":"bestmove e2e4"})"));
    QTest::qWait(50);
    QCOMPARE(bestMoveSpy.count(), 0);
    QCOMPARE(client.state(), ChessGatewayClient::State::Connected);
}

void GatewayClientTest::testEngineExitedBeforeSelectionAcknowledgement() {
    FakeGatewayServer server;
    server.setExitBeforeSelection(true);
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    QSignalSpy rawLineSpy(&client, &EngineBackend::rawLineReceived);
    QSignalSpy selectedSpy(&client, &ChessGatewayClient::engineSelected);
    client.selectEngine(QStringLiteral("sf"));

    QTRY_COMPARE_WITH_TIMEOUT(rawLineSpy.count(), 1, 2000);
    QCOMPARE(rawLineSpy.first().at(0).toString(),
             QStringLiteral("info string gateway engine sf exited"));
    QTRY_COMPARE_WITH_TIMEOUT(selectedSpy.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Connected, 2000);
    QCOMPARE(client.selectedEngineId(), QString());
    QVERIFY(client.isConnected());
    QCOMPARE(server.uciCommands().size(), 0);
}

void GatewayClientTest::testUciCommandWithoutSelectionFails() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    QSignalSpy errorSpy(&client, &ChessGatewayClient::errorOccurred);
    QSignalSpy gatewayErrorSpy(&client, &ChessGatewayClient::gatewayError);

    client.sendRawCommand(QStringLiteral("position startpos"));
    QTRY_VERIFY_WITH_TIMEOUT(gatewayErrorSpy.count() == 1, 2000);
    QCOMPARE(gatewayErrorSpy.first().at(0).toString(),
             QStringLiteral("no_engine_selected"));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.first().at(0).toString().contains(QStringLiteral("no_engine_selected")));
    QCOMPARE(client.state(), ChessGatewayClient::State::Connected);
}

void GatewayClientTest::testEmptyUciCommandsAreIgnored() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    const int commandCount = server.uciCommands().size();
    client.sendRawCommand(QString());
    client.sendRawCommand(QStringLiteral(" \t\n "));
    QTest::qWait(100);
    QCOMPARE(server.uciCommands().size(), commandCount);
}

void GatewayClientTest::testStartAnalysis() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    QSignalSpy analysisSpy(&client, &ChessGatewayClient::analysisUpdated);
    QSignalSpy bestMoveSpy(&client, &ChessGatewayClient::bestMoveReceived);
    QSignalSpy rawSentSpy(&client, &ChessGatewayClient::rawLineSent);

    client.startAnalysis(15, 1);
    QCOMPARE(client.state(), ChessGatewayClient::State::Analyzing);
    QVERIFY(analysisSpy.wait(2000));
    const auto line = qvariant_cast<EngineAnalysisLine>(analysisSpy.first().at(0));
    QCOMPARE(line.depth, 15);
    QCOMPARE(line.scoreCp, 65);

    QVERIFY(bestMoveSpy.count() == 1);
    QCOMPARE(bestMoveSpy.first().at(0).toString(), QStringLiteral("e2e4"));
    QVERIFY(rawSentSpy.count() >= 1);
    QCOMPARE(rawSentSpy.at(0).at(0).toString(), QStringLiteral("go depth 15"));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);
}

void GatewayClientTest::testStartTimedSearch() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    QSignalSpy timedSpy(&client, &ChessGatewayClient::timedBestMoveReceived);
    QSignalSpy rawSentSpy(&client, &ChessGatewayClient::rawLineSent);

    client.startTimedSearch(300000, 300000, 2000, 2000);
    QCOMPARE(client.state(), ChessGatewayClient::State::Analyzing);
    QVERIFY(timedSpy.wait(2000));
    QCOMPARE(timedSpy.first().at(0).toString(), QStringLiteral("e2e4"));

    bool foundGo = false;
    for (const auto &command : server.uciCommands()) {
        if (command.value(QStringLiteral("command")).toString().contains(
                QStringLiteral("go wtime 300000 btime 300000 winc 2000 binc 2000"))) {
            foundGo = true;
            break;
        }
    }
    QVERIFY(foundGo);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);
}

void GatewayClientTest::testStopEngineKeepsConnection() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    QSignalSpy stoppedSpy(&client, &ChessGatewayClient::engineStopped);
    QSignalSpy stateSpy(&client, &ChessGatewayClient::stateChanged);

    client.stopEngine();
    QVERIFY(stoppedSpy.wait(2000));
    QCOMPARE(stoppedSpy.first().at(0).toString(), QStringLiteral("sf"));
    QCOMPARE(client.selectedEngineId(), QString());
    QVERIFY(client.isConnected());
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Connected, 2000);
}

void GatewayClientTest::testServerDisconnect() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    QSignalSpy stateSpy(&client, &ChessGatewayClient::stateChanged);
    server.disconnectClient();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Disconnected, 2000);
    QVERIFY(!client.isConnected());
}

void GatewayClientTest::testUnknownEngineError() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    QSignalSpy gatewayErrorSpy(&client, &ChessGatewayClient::gatewayError);
    QSignalSpy errorSpy(&client, &ChessGatewayClient::errorOccurred);

    client.selectEngine(QStringLiteral("nope"));
    QVERIFY(gatewayErrorSpy.wait(2000));
    QCOMPARE(gatewayErrorSpy.first().at(0).toString(),
             QStringLiteral("unknown_engine"));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.first().at(0).toString().contains(QStringLiteral("unknown engine 'nope'")));
    QVERIFY(client.selectedEngineId().isEmpty());

    // The failed request does not block the following one.
    QSignalSpy enginesSpy(&client, &ChessGatewayClient::enginesListed);
    client.listEngines();
    QTRY_COMPARE_WITH_TIMEOUT(enginesSpy.count(), 1, 2000);
}

void GatewayClientTest::testSelectTooFrequentRetries() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    server.setSelectTooFrequentResponses(1);
    QSignalSpy selectedSpy(&client, &ChessGatewayClient::engineSelected);
    QSignalSpy gatewayErrorSpy(&client, &ChessGatewayClient::gatewayError);
    QSignalSpy errorSpy(&client, &EngineBackend::errorOccurred);
    const int selectCount = server.selectRequests().size();

    client.selectEngine(QStringLiteral("kom"));
    QTest::qWait(100);
    QCOMPARE(client.selectedEngineId(), QStringLiteral("sf"));
    QCOMPARE(client.state(), ChessGatewayClient::State::Ready);
    QCOMPARE(gatewayErrorSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(selectedSpy.count(), 1, 2500);
    QTRY_COMPARE_WITH_TIMEOUT(server.selectRequests().size(), selectCount + 2, 2500);
    QCOMPARE(client.selectedEngineId(), QStringLiteral("kom"));
    QVERIFY(gatewayErrorSpy.isEmpty());
    QVERIFY(errorSpy.isEmpty());
}

void GatewayClientTest::testSelectTooFrequentRetryFailure() {
    FakeGatewayServer server;
    ChessGatewayClient client;
    client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QSignalSpy helloSpy(&client, &ChessGatewayClient::helloReceived);
    QVERIFY(helloSpy.wait(2000));

    client.selectEngine(QStringLiteral("sf"));
    QSignalSpy loadedSpy(&client, &ChessGatewayClient::engineLoaded);
    QVERIFY(loadedSpy.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ChessGatewayClient::State::Ready, 2000);

    server.setSelectTooFrequentResponses(2);
    QSignalSpy gatewayErrorSpy(&client, &ChessGatewayClient::gatewayError);
    QSignalSpy errorSpy(&client, &EngineBackend::errorOccurred);
    client.selectEngine(QStringLiteral("kom"));

    QVERIFY(gatewayErrorSpy.wait(2500));
    QCOMPARE(gatewayErrorSpy.count(), 1);
    QCOMPARE(gatewayErrorSpy.first().at(0).toString(),
             QStringLiteral("select_too_frequent"));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.first().at(0).toString().contains(
        QStringLiteral("select_too_frequent")));
    QCOMPARE(client.selectedEngineId(), QStringLiteral("sf"));
    QCOMPARE(client.state(), ChessGatewayClient::State::Ready);
}

QTEST_MAIN(GatewayClientTest)
#include "gatewayclient_test.moc"
