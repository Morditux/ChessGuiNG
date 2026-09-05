//
// Dialog for selecting a remote UCI engine behind a chessgateway/1 server.
//

#ifndef CHESSGUI_REMOTEENGINEDIALOG_H
#define CHESSGUI_REMOTEENGINEDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

#include "gatewayclient.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class RemoteEngineDialog : public QDialog {
    Q_OBJECT

public:
    explicit RemoteEngineDialog(const QString &host, quint16 port,
                                const QString &engineId = QString(),
                                const QString &accessKey = QString(),
                                QWidget *parent = nullptr);
    ~RemoteEngineDialog() override;

    [[nodiscard]] QString host() const;
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] QString accessKey() const;
    [[nodiscard]] QString engineId() const;
    [[nodiscard]] QString engineName() const;
    [[nodiscard]] QString engineVersion() const;

private slots:
    void listEngines();
    void onEnginesReceived(const QList<GatewayEngineInfo> &engines);
    void onClientError(const QString &message);
    void onListTimeout();
    void onSelectionChanged(int index);

private:
    void setBusy(bool busy);
    void updateOkButton();
    static QString engineDisplayName(const GatewayEngineInfo &engine);

    QLineEdit *hostEdit_ = nullptr;
    QSpinBox *portSpin_ = nullptr;
    QLineEdit *accessKeyEdit_ = nullptr;
    QPushButton *listButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QComboBox *engineCombo_ = nullptr;
    QDialogButtonBox *buttonBox_ = nullptr;

    ChessGatewayClient *client_ = nullptr;
    QTimer *listTimer_ = nullptr;
    QList<GatewayEngineInfo> engineInfos_;
    bool busy_ = false;
};

#endif // CHESSGUI_REMOTEENGINEDIALOG_H
