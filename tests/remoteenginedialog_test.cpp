//
// Unit tests for RemoteEngineDialog.
//

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

#include "fakegateway.h"
#include "remoteenginedialog.h"

class RemoteEngineDialogTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitialState();
    void testListEnginesAndSelect();
    void testListEnginesWithAccessKey();
    void testListEnginesEmpty();
    void testOkDisabledWithoutSelection();
};

void RemoteEngineDialogTest::initTestCase() {
    qRegisterMetaType<QList<GatewayEngineInfo>>();
    qRegisterMetaType<GatewayEngineInfo>();
}

void RemoteEngineDialogTest::testInitialState() {
    RemoteEngineDialog dialog(QStringLiteral("example.org"), 9100,
                              QString(),
                              QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    auto *hostEdit = dialog.findChild<QLineEdit *>(QStringLiteral("hostEdit"));
    auto *portSpin = dialog.findChild<QSpinBox *>(QStringLiteral("portSpin"));
    auto *accessKeyEdit = dialog.findChild<QLineEdit *>(QStringLiteral("accessKeyEdit"));
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("engineCombo"));
    auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(hostEdit != nullptr);
    QVERIFY(portSpin != nullptr);
    QVERIFY(accessKeyEdit != nullptr);
    QVERIFY(combo != nullptr);
    QVERIFY(buttonBox != nullptr);

    QCOMPARE(hostEdit->text(), QStringLiteral("example.org"));
    QCOMPARE(portSpin->value(), 9100);
    QCOMPARE(accessKeyEdit->text(),
             QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    QVERIFY(!combo->isEnabled());
    QVERIFY(!buttonBox->button(QDialogButtonBox::Ok)->isEnabled());
    QCOMPARE(dialog.host(), QStringLiteral("example.org"));
    QCOMPARE(dialog.port(), static_cast<quint16>(9100));
    QCOMPARE(dialog.accessKey(),
             QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    QVERIFY(dialog.engineId().isEmpty());
}

void RemoteEngineDialogTest::testListEnginesAndSelect() {
    FakeGateway gateway;
    QVERIFY(gateway.isListening());

    RemoteEngineDialog dialog(QString(), 0);
    auto *hostEdit = dialog.findChild<QLineEdit *>(QStringLiteral("hostEdit"));
    auto *portSpin = dialog.findChild<QSpinBox *>(QStringLiteral("portSpin"));
    auto *listButton = dialog.findChild<QPushButton *>(QStringLiteral("listButton"));
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("engineCombo"));
    auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(hostEdit != nullptr);
    QVERIFY(portSpin != nullptr);
    QVERIFY(listButton != nullptr);
    QVERIFY(combo != nullptr);
    QVERIFY(buttonBox != nullptr);

    // Listing without a host shows a status message and no connection.
    listButton->click();
    QCOMPARE(combo->count(), 0);

    hostEdit->setText(QStringLiteral("127.0.0.1"));
    portSpin->setValue(gateway.port());
    listButton->click();

    QTRY_COMPARE_WITH_TIMEOUT(combo->count(), 2, 5000);
    QVERIFY(combo->isEnabled());
    QVERIFY(buttonBox->button(QDialogButtonBox::Ok)->isEnabled());

    combo->setCurrentIndex(0);
    QCOMPARE(dialog.engineId(), QStringLiteral("stockfish-17"));
    QCOMPARE(dialog.engineName(), QStringLiteral("Stockfish"));
    QCOMPARE(dialog.engineVersion(), QStringLiteral("17"));
    QCOMPARE(dialog.host(), QStringLiteral("127.0.0.1"));
    QCOMPARE(dialog.port(), gateway.port());

    combo->setCurrentIndex(1);
    QCOMPARE(dialog.engineId(), QStringLiteral("komodo-14"));
    QCOMPARE(dialog.engineName(), QStringLiteral("Komodo"));
    QCOMPARE(dialog.engineVersion(), QStringLiteral("14.1"));

    dialog.accept();
    QCOMPARE(dialog.result(), QDialog::Accepted);
}

void RemoteEngineDialogTest::testListEnginesWithAccessKey() {
    FakeGateway gateway;
    gateway.requireAccessKey(QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    QVERIFY(gateway.isListening());

    RemoteEngineDialog dialog(QStringLiteral("127.0.0.1"), gateway.port());
    auto *accessKeyEdit = dialog.findChild<QLineEdit *>(QStringLiteral("accessKeyEdit"));
    auto *portSpin = dialog.findChild<QSpinBox *>(QStringLiteral("portSpin"));
    auto *listButton = dialog.findChild<QPushButton *>(QStringLiteral("listButton"));
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("engineCombo"));
    auto *statusLabel = dialog.findChild<QLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(accessKeyEdit != nullptr);
    QVERIFY(portSpin != nullptr);
    QVERIFY(listButton != nullptr);
    QVERIFY(combo != nullptr);
    QVERIFY(statusLabel != nullptr);

    portSpin->setValue(gateway.port());
    accessKeyEdit->setText(QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
    listButton->click();

    QTRY_COMPARE_WITH_TIMEOUT(combo->count(), 2, 5000);
    QVERIFY(combo->isEnabled());
    QCOMPARE(dialog.accessKey(),
             QStringLiteral("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"));
}

void RemoteEngineDialogTest::testListEnginesEmpty() {
    FakeGateway gateway;
    gateway.setEmptyEngineList(true);
    QVERIFY(gateway.isListening());

    RemoteEngineDialog dialog(QString(), 0);
    auto *hostEdit = dialog.findChild<QLineEdit *>(QStringLiteral("hostEdit"));
    auto *portSpin = dialog.findChild<QSpinBox *>(QStringLiteral("portSpin"));
    auto *listButton = dialog.findChild<QPushButton *>(QStringLiteral("listButton"));
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("engineCombo"));
    auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
    auto *statusLabel = dialog.findChild<QLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(statusLabel != nullptr);

    hostEdit->setText(QStringLiteral("127.0.0.1"));
    portSpin->setValue(gateway.port());
    listButton->click();

    QTRY_COMPARE_WITH_TIMEOUT(statusLabel->text().contains(QStringLiteral("no engines")), true, 5000);
    QCOMPARE(combo->count(), 0);
    QVERIFY(!buttonBox->button(QDialogButtonBox::Ok)->isEnabled());
}

void RemoteEngineDialogTest::testOkDisabledWithoutSelection() {
    RemoteEngineDialog dialog(QString(), 0);
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("engineCombo"));
    auto *buttonBox = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(combo != nullptr);
    QVERIFY(buttonBox != nullptr);

    QVERIFY(!buttonBox->button(QDialogButtonBox::Ok)->isEnabled());
}

QTEST_MAIN(RemoteEngineDialogTest)
#include "remoteenginedialog_test.moc"
