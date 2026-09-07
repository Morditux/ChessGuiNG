//
// Controller for managing a UCI chess engine process.
//

#include "uciengine.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

QString findWindowsBash() {
    QString bashPath = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (!bashPath.isEmpty()) {
        return bashPath;
    }

    const QStringList candidates = {
        qEnvironmentVariable("ProgramW6432") + QStringLiteral("/Git/bin/bash.exe"),
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Git/bin/bash.exe"),
        qEnvironmentVariable("ProgramW6432") + QStringLiteral("/Git/usr/bin/bash.exe"),
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Git/usr/bin/bash.exe"),
        QStringLiteral("C:/Program Files/Git/bin/bash.exe"),
        QStringLiteral("C:/Program Files/Git/usr/bin/bash.exe")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

} // namespace

UciEngine::UciEngine(QObject *parent)
    : EngineBackend(QStringLiteral("UCI Engine"), parent) {
}

UciEngine::~UciEngine() {
    stopEngine();
}

bool UciEngine::startEngine(const QString &executablePath) {
    stopEngine();

    enginePath_ = executablePath;
    setDefaultEngineName(QFileInfo(executablePath).baseName());
    resetEngineSession();
    outputBuffer_.clear();

    process_ = new QProcess(this);

    connect(process_, &QProcess::readyReadStandardOutput,
            this, &UciEngine::onReadyRead);
    connect(process_, &QProcess::errorOccurred,
            this, &UciEngine::onErrorOccurred);
    connect(process_, &QProcess::finished,
            this, &UciEngine::onProcessFinished);

    QString program = executablePath;
    QStringList arguments;
#ifdef Q_OS_WIN
    // The test suite and user tooling may provide POSIX shell wrappers. Windows
    // cannot execute a .sh file directly, but Git for Windows supplies bash.
    if (QFileInfo(executablePath).suffix().compare(QStringLiteral("sh"), Qt::CaseInsensitive) == 0) {
        const QString bashPath = findWindowsBash();
        if (!bashPath.isEmpty()) {
            program = bashPath;
            arguments << QDir::fromNativeSeparators(executablePath);
        }
    }
#endif
    process_->start(program, arguments);
    if (!process_->waitForStarted(3000)) {
        setState(State::Disconnected);
        delete process_;
        process_ = nullptr;
        return false;
    }

    setState(State::Initializing);
    sendRawCommand(QStringLiteral("uci"));
    return true;
}

void UciEngine::stopEngine() {
    clearSearchState();
    if (!process_) {
        setState(State::Disconnected);
        return;
    }

    if (process_->state() != QProcess::NotRunning) {
        if (state_ == State::Analyzing) {
            sendRawCommand(QStringLiteral("stop"));
        }
        sendRawCommand(QStringLiteral("quit"));

        if (!process_->waitForFinished(1000)) {
            process_->kill();
            process_->waitForFinished(500);
        }
    }

    process_->deleteLater();
    process_ = nullptr;
    setState(State::Disconnected);
}

bool UciEngine::isConnected() const {
    return process_ && process_->state() == QProcess::Running &&
           state_ != State::Disconnected && state_ != State::Initializing;
}

QString UciEngine::enginePath() const {
    return enginePath_;
}

bool UciEngine::canSendUciCommands() const {
    return process_ && process_->state() == QProcess::Running;
}

void UciEngine::transmitUciCommand(const QString &command) {
    if (!process_ || process_->state() != QProcess::Running) {
        return;
    }

    const QByteArray data = (command + QStringLiteral("\n")).toUtf8();
    process_->write(data);
}

void UciEngine::onReadyRead() {
    if (!process_) {
        return;
    }

    outputBuffer_.append(process_->readAllStandardOutput());

    while (true) {
        const int newlineIndex = outputBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray rawLine = outputBuffer_.left(newlineIndex);
        outputBuffer_.remove(0, newlineIndex + 1);

        if (rawLine.endsWith('\r')) {
            rawLine.chop(1);
        }

        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (!line.isEmpty()) {
            handleLine(line);
        }
    }
}

void UciEngine::onErrorOccurred(QProcess::ProcessError error) {
    Q_UNUSED(error)
    if (process_) {
        emit errorOccurred(process_->errorString());
    }
    setState(State::Disconnected);
}

void UciEngine::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)
    setState(State::Disconnected);
}
