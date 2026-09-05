//
// Controller for managing a UCI chess engine process.
//

#ifndef CHESSGUI_UCIENGINE_H
#define CHESSGUI_UCIENGINE_H

#include <QByteArray>
#include <QProcess>

#include "enginebackend.h"

class UciEngine : public EngineBackend {
    Q_OBJECT

public:
    using State = EngineBackend::State;

    explicit UciEngine(QObject *parent = nullptr);
    ~UciEngine() override;

    bool startEngine(const QString &executablePath);
    void stopEngine();

    [[nodiscard]] bool isConnected() const override;
    [[nodiscard]] QString enginePath() const;

private slots:
    void onReadyRead();
    void onErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    [[nodiscard]] bool canSendUciCommands() const override;
    void transmitUciCommand(const QString &command) override;

    QProcess *process_ = nullptr;
    QByteArray outputBuffer_;
    QString enginePath_;
};

#endif // CHESSGUI_UCIENGINE_H
