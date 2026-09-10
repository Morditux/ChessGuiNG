//
// Contract tests for the common UCI engine backend.
//

#include <QSignalSpy>
#include <QTest>

#include "enginebackend.h"

class FakeEngineBackend : public EngineBackend {
    Q_OBJECT

public:
    FakeEngineBackend()
        : EngineBackend(QStringLiteral("Fake Engine")) {
    }

    [[nodiscard]] bool isConnected() const override {
        return connected_;
    }

    [[nodiscard]] QStringList commands() const {
        return commands_;
    }

    void setReady() {
        connected_ = true;
        setState(State::Ready);
    }

    void feedLine(const QString &line) {
        handleLine(line);
    }

protected:
    [[nodiscard]] bool canSendUciCommands() const override {
        return connected_;
    }

    void transmitUciCommand(const QString &command) override {
        commands_.append(command);
    }

private:
    bool connected_ = false;
    QStringList commands_;
};

class EngineBackendTest : public QObject {
    Q_OBJECT

private slots:
    void testInitialState();
    void testBuildsPositionAndAnalysisCommands();
    void testRejectsInvalidFen();
    void testTimedSearchAndBestMove();
    void testPositionChangeRestartsAnalysis();
};

void EngineBackendTest::testInitialState() {
    FakeEngineBackend backend;

    QCOMPARE(backend.state(), EngineBackend::State::Disconnected);
    QVERIFY(!backend.isConnected());
    QVERIFY(!backend.isOperational());
    QVERIFY(!backend.isAnalyzing());
    QCOMPARE(backend.engineName(), QStringLiteral("Fake Engine"));
}

void EngineBackendTest::testBuildsPositionAndAnalysisCommands() {
    FakeEngineBackend backend;
    backend.setReady();

    backend.sendPosition(QString(), {QStringLiteral("e2e4")});
    QCOMPARE(backend.commands(), QStringList{QStringLiteral("position startpos moves e2e4")});

    backend.startAnalysis(12, 2);
    const QStringList expectedCommands{
        QStringLiteral("position startpos moves e2e4"),
        QStringLiteral("setoption name MultiPV value 2"),
        QStringLiteral("position startpos moves e2e4"),
        QStringLiteral("go depth 12"),
    };
    QCOMPARE(backend.commands(), expectedCommands);
    QCOMPARE(backend.state(), EngineBackend::State::Analyzing);
}

void EngineBackendTest::testRejectsInvalidFen() {
    FakeEngineBackend backend;
    backend.setReady();
    QSignalSpy errorSpy(&backend, &EngineBackend::errorOccurred);

    backend.sendPosition(QStringLiteral("not a FEN"));

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(backend.commands().isEmpty());
    QCOMPARE(backend.state(), EngineBackend::State::Ready);
}

void EngineBackendTest::testTimedSearchAndBestMove() {
    FakeEngineBackend backend;
    backend.setReady();
    QSignalSpy bestMoveSpy(&backend, &EngineBackend::bestMoveReceived);
    QSignalSpy timedBestMoveSpy(&backend, &EngineBackend::timedBestMoveReceived);

    backend.startTimedSearch(300000, 600000, 1000, 2000);
    const QStringList expectedCommands{
        QStringLiteral("go wtime 300000 btime 600000 winc 1000 binc 2000"),
    };
    QCOMPARE(backend.commands(), expectedCommands);
    QCOMPARE(backend.state(), EngineBackend::State::Analyzing);

    backend.feedLine(QStringLiteral("bestmove e7e5 ponder g1f3"));

    QCOMPARE(bestMoveSpy.count(), 1);
    QCOMPARE(timedBestMoveSpy.count(), 1);
    QCOMPARE(bestMoveSpy.at(0).at(0).toString(), QStringLiteral("e7e5"));
    QCOMPARE(bestMoveSpy.at(0).at(1).toString(), QStringLiteral("g1f3"));
    QCOMPARE(backend.state(), EngineBackend::State::Ready);

    // Without explicit increments the search still advertises zero increments.
    backend.startTimedSearch(300000, 600000);
    QCOMPARE(backend.commands().last(),
             QStringLiteral("go wtime 300000 btime 600000 winc 0 binc 0"));
}

void EngineBackendTest::testPositionChangeRestartsAnalysis() {
    FakeEngineBackend backend;
    backend.setReady();
    backend.startAnalysis();

    backend.sendPosition(QString(), {QStringLiteral("d2d4")});
    QCOMPARE(backend.state(), EngineBackend::State::Stopping);
    QCOMPARE(backend.commands().last(), QStringLiteral("stop"));

    backend.feedLine(QStringLiteral("bestmove e7e5"));

    QCOMPARE(backend.state(), EngineBackend::State::Analyzing);
    const QStringList expectedCommands{
        QStringLiteral("position startpos moves d2d4"),
        QStringLiteral("go infinite"),
    };
    QCOMPARE(backend.commands().mid(backend.commands().size() - 2), expectedCommands);
}

QTEST_MAIN(EngineBackendTest)
#include "enginebackend_test.moc"
