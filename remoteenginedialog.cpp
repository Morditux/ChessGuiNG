//
// Dialog for selecting a remote UCI engine behind a chessgateway/1 server.
//

#include "remoteenginedialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kListTimeoutMs = 8000;

} // namespace

RemoteEngineDialog::RemoteEngineDialog(const QString &host, quint16 port,
                                       const QString &engineId,
                                       const QString &accessKey,
                                       QWidget *parent)
    : QDialog(parent) {
    Q_UNUSED(engineId)

    setWindowTitle(tr("Remote Engine"));
    setModal(true);
    resize(520, 320);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *description = new QLabel(
        tr("Connect to a chessgateway server and select one of its UCI engines. "
           "The engine is launched on the server when an analysis or a game starts."),
        this);
    description->setWordWrap(true);
    mainLayout->addWidget(description);

    auto *serverGroup = new QGroupBox(tr("Server"), this);
    auto *formLayout = new QFormLayout(serverGroup);

    hostEdit_ = new QLineEdit(serverGroup);
    hostEdit_->setObjectName(QStringLiteral("hostEdit"));
    hostEdit_->setText(host);
    hostEdit_->setPlaceholderText(tr("Host name or IP address"));
    hostEdit_->setClearButtonEnabled(true);
    hostEdit_->setAccessibleName(tr("Gateway server address"));
    formLayout->addRow(tr("Address:"), hostEdit_);

    portSpin_ = new QSpinBox(serverGroup);
    portSpin_->setObjectName(QStringLiteral("portSpin"));
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(port > 0 ? port : 9000);
    portSpin_->setAccessibleName(tr("Gateway server port"));
    formLayout->addRow(tr("Port:"), portSpin_);

    accessKeyEdit_ = new QLineEdit(serverGroup);
    accessKeyEdit_->setObjectName(QStringLiteral("accessKeyEdit"));
    accessKeyEdit_->setText(accessKey.trimmed());
    accessKeyEdit_->setPlaceholderText(tr("Optional"));
    accessKeyEdit_->setClearButtonEnabled(true);
    accessKeyEdit_->setAccessibleName(tr("Gateway access key"));
    accessKeyEdit_->setToolTip(
        tr("Client access key required only when the server asks for one."));
    formLayout->addRow(tr("Access key:"), accessKeyEdit_);

    listButton_ = new QPushButton(tr("List available engines"), serverGroup);
    listButton_->setObjectName(QStringLiteral("listButton"));
    listButton_->setAutoDefault(false);
    connect(listButton_, &QPushButton::clicked,
            this, &RemoteEngineDialog::listEngines);
    formLayout->addRow(QString(), listButton_);

    mainLayout->addWidget(serverGroup);

    auto *engineGroup = new QGroupBox(tr("Engine"), this);
    auto *engineLayout = new QVBoxLayout(engineGroup);

    engineCombo_ = new QComboBox(engineGroup);
    engineCombo_->setObjectName(QStringLiteral("engineCombo"));
    engineCombo_->setEnabled(false);
    engineCombo_->setAccessibleName(tr("Available remote engines"));
    engineCombo_->setToolTip(tr("Engines offered by the server"));
    connect(engineCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RemoteEngineDialog::onSelectionChanged);
    engineLayout->addWidget(engineCombo_);

    statusLabel_ = new QLabel(engineGroup);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setWordWrap(true);
    statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    engineLayout->addWidget(statusLabel_);

    mainLayout->addWidget(engineGroup, 1);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox_->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox_);

    updateOkButton();
    hostEdit_->setFocus();
}

RemoteEngineDialog::~RemoteEngineDialog() = default;

QString RemoteEngineDialog::host() const {
    return hostEdit_ != nullptr ? hostEdit_->text().trimmed() : QString();
}

quint16 RemoteEngineDialog::port() const {
    return portSpin_ != nullptr
               ? static_cast<quint16>(portSpin_->value())
               : 0;
}

QString RemoteEngineDialog::accessKey() const {
    return accessKeyEdit_ != nullptr ? accessKeyEdit_->text().trimmed() : QString();
}

QString RemoteEngineDialog::engineId() const {
    const int index = engineCombo_ != nullptr ? engineCombo_->currentIndex() : -1;
    if (index < 0 || index >= engineInfos_.size()) {
        return {};
    }
    return engineInfos_.at(index).id;
}

QString RemoteEngineDialog::engineName() const {
    const int index = engineCombo_ != nullptr ? engineCombo_->currentIndex() : -1;
    if (index < 0 || index >= engineInfos_.size()) {
        return {};
    }
    return engineInfos_.at(index).name;
}

QString RemoteEngineDialog::engineVersion() const {
    const int index = engineCombo_ != nullptr ? engineCombo_->currentIndex() : -1;
    if (index < 0 || index >= engineInfos_.size()) {
        return {};
    }
    return engineInfos_.at(index).version;
}

QString RemoteEngineDialog::engineDisplayName(const GatewayEngineInfo &engine) {
    if (engine.version.isEmpty()) {
        return engine.name.isEmpty() ? engine.id : engine.name;
    }
    return engine.name.isEmpty()
               ? QStringLiteral("%1 (%2)").arg(engine.id, engine.version)
               : QStringLiteral("%1 (%2)").arg(engine.name, engine.version);
}

void RemoteEngineDialog::listEngines() {
    const QString serverHost = host();
    if (serverHost.isEmpty()) {
        statusLabel_->setText(tr("Enter the server address first."));
        return;
    }
    const quint16 serverPort = port();
    if (serverPort == 0) {
        statusLabel_->setText(tr("Enter a valid server port."));
        return;
    }

    engineCombo_->clear();
    engineInfos_.clear();
    updateOkButton();
    setBusy(true);
    statusLabel_->setText(tr("Connecting to %1:%2…").arg(serverHost).arg(serverPort));

    if (client_ != nullptr) {
        client_->disconnectFromHost();
        client_->deleteLater();
    }
    client_ = new ChessGatewayClient(this);
    connect(client_, &ChessGatewayClient::stateChanged,
            this, [this](ChessGatewayClient::State state) {
                if (state == ChessGatewayClient::State::Connected) {
                    statusLabel_->setText(tr("Connected. Listing engines…"));
                    client_->listEngines();
                } else if (state == ChessGatewayClient::State::Disconnected &&
                           busy_) {
                    listTimer_->stop();
                    setBusy(false);
                    statusLabel_->setText(tr("Connection closed."));
                }
            });
    connect(client_, &ChessGatewayClient::enginesListed,
            this, &RemoteEngineDialog::onEnginesReceived);
    connect(client_, &ChessGatewayClient::errorOccurred,
            this, &RemoteEngineDialog::onClientError);
    connect(client_, &ChessGatewayClient::gatewayError,
            this, [this](const QString &code, const QString &message) {
                onClientError(code + tr(": ") + message);
            });

    if (listTimer_ == nullptr) {
        listTimer_ = new QTimer(this);
        listTimer_->setSingleShot(true);
        connect(listTimer_, &QTimer::timeout,
                this, &RemoteEngineDialog::onListTimeout);
    }
    listTimer_->start(kListTimeoutMs);

    client_->connectToHost(serverHost, serverPort, accessKey());
}

void RemoteEngineDialog::onEnginesReceived(const QList<GatewayEngineInfo> &engines) {
    listTimer_->stop();

    engineInfos_ = engines;
    for (const GatewayEngineInfo &engine : engineInfos_) {
        engineCombo_->addItem(engineDisplayName(engine));
    }

    setBusy(false);
    updateOkButton();
    client_->disconnectFromHost();

    if (engineInfos_.isEmpty()) {
        statusLabel_->setText(tr("The server offers no engines."));
        return;
    }

    statusLabel_->setText(tr("%n engine(s) available.", "", engineInfos_.size()));
}

void RemoteEngineDialog::onClientError(const QString &message) {
    listTimer_->stop();
    setBusy(false);
    updateOkButton();
    statusLabel_->setText(message);
}

void RemoteEngineDialog::onListTimeout() {
    setBusy(false);
    updateOkButton();
    statusLabel_->setText(tr("The server did not answer. Check the address and port."));
    if (client_ != nullptr) {
        client_->disconnectFromHost();
    }
}

void RemoteEngineDialog::onSelectionChanged(int index) {
    Q_UNUSED(index)
    updateOkButton();
}

void RemoteEngineDialog::setBusy(bool busy) {
    busy_ = busy;
    hostEdit_->setEnabled(!busy);
    portSpin_->setEnabled(!busy);
    accessKeyEdit_->setEnabled(!busy);
    listButton_->setEnabled(!busy);
    engineCombo_->setEnabled(!busy && !engineInfos_.isEmpty());
}

void RemoteEngineDialog::updateOkButton() {
    buttonBox_->button(QDialogButtonBox::Ok)->setEnabled(
        engineCombo_ != nullptr && engineCombo_->currentIndex() >= 0);
}
