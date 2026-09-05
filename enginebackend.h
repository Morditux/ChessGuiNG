//
// Common UCI engine backend interface and protocol implementation.
//

#ifndef CHESSGUI_ENGINEBACKEND_H
#define CHESSGUI_ENGINEBACKEND_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "uciparser.h"

class EngineBackend : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Authenticating,
        Connected,
        Initializing,
        Ready,
        Analyzing,
        Stopping
    };
    Q_ENUM(State)

    explicit EngineBackend(const QString &defaultEngineName = QString(),
                           QObject *parent = nullptr);
    ~EngineBackend() override = default;

    [[nodiscard]] virtual bool isConnected() const = 0;
    [[nodiscard]] bool isOperational() const;
    [[nodiscard]] bool isAnalyzing() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] QString engineName() const;
    [[nodiscard]] QString engineAuthor() const;
    [[nodiscard]] QList<UciOption> options() const;

public slots:
    void sendPosition(const QString &fen = QString(),
                      const QStringList &moves = {});
    void startAnalysis(int depth = 0, int multiPv = 1);
    void startTimedSearch(qint64 whiteTimeMilliseconds,
                          qint64 blackTimeMilliseconds,
                          qint64 whiteIncrementMilliseconds = 0,
                          qint64 blackIncrementMilliseconds = 0);
    void stopAnalysis();
    void setOption(const QString &name, const QVariant &value);
    void sendRawCommand(const QString &command);

signals:
    void stateChanged(EngineBackend::State state);
    void engineLoaded(const QString &name, const QString &author);
    void analysisUpdated(const EngineAnalysisLine &line);
    void bestMoveReceived(const QString &bestMove, const QString &ponder);
    void timedBestMoveReceived(const QString &bestMove, const QString &ponder);
    void rawLineReceived(const QString &line);
    void rawLineSent(const QString &line);
    void errorOccurred(const QString &errorMessage);

protected:
    [[nodiscard]] virtual bool canSendUciCommands() const = 0;
    virtual void transmitUciCommand(const QString &command) = 0;

    void setState(State newState);
    void handleLine(const QString &line);
    void resetEngineSession();
    void clearSearchState();
    void setDefaultEngineName(const QString &name);

    QString engineName_;
    QString engineAuthor_;
    QList<UciOption> options_;
    State state_ = State::Disconnected;
    QString positionCommand_;
    QString pendingGoCommand_;
    bool pendingRestart_ = false;
    int currentDepth_ = 0;
    int currentMultiPv_ = 1;

private:
    enum class SearchType {
        None,
        Analysis,
        Timed
    };

    void sendUciCommand(const QString &command);

    QString defaultEngineName_;
    SearchType activeSearchType_ = SearchType::None;
    SearchType pendingSearchType_ = SearchType::None;
};

#endif // CHESSGUI_ENGINEBACKEND_H
