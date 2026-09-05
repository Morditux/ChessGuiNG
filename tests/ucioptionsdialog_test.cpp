//
// Unit tests for UciOptionsDialog.
//

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTest>

#include "ucioptionsdialog.h"

class UciOptionsDialogTest : public QObject {
    Q_OBJECT

private slots:
    void testEmptyOptions();
    void testOptionValuesByType();
    void testDefaultsUsed();
    void testButtonOptionExcluded();
};

namespace {

QList<UciOption> sampleOptions() {
    UciOption spin;
    spin.name = QStringLiteral("Threads");
    spin.type = UciOption::Type::Spin;
    spin.defaultValue = QStringLiteral("1");
    spin.minValue = QStringLiteral("1");
    spin.maxValue = QStringLiteral("16");

    UciOption check;
    check.name = QStringLiteral("Ponder");
    check.type = UciOption::Type::Check;
    check.defaultValue = QStringLiteral("false");

    UciOption combo;
    combo.name = QStringLiteral("Style");
    combo.type = UciOption::Type::Combo;
    combo.defaultValue = QStringLiteral("Normal");
    combo.vars = {QStringLiteral("Normal"), QStringLiteral("Solid"),
                  QStringLiteral("Risky")};

    UciOption stringOption;
    stringOption.name = QStringLiteral("EvalFile");
    stringOption.type = UciOption::Type::String;

    UciOption button;
    button.name = QStringLiteral("Clear Hash");
    button.type = UciOption::Type::Button;

    return {spin, check, combo, stringOption, button};
}

QWidget *widgetFor(UciOptionsDialog &dialog, const QString &name) {
    for (QWidget *widget : dialog.findChildren<QWidget *>()) {
        if (widget->accessibleName() == name) {
            return widget;
        }
    }
    return nullptr;
}

} // namespace

void UciOptionsDialogTest::testEmptyOptions() {
    const QMap<QString, QString> values = {
        {QStringLiteral("Threads"), QStringLiteral("4")}
    };
    UciOptionsDialog dialog(QString(), QList<UciOption>(), values);

    const QMap<QString, QString> result = dialog.optionValues();
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value(QStringLiteral("Threads")), QStringLiteral("4"));
}

void UciOptionsDialogTest::testOptionValuesByType() {
    const QMap<QString, QString> values = {
        {QStringLiteral("Threads"), QStringLiteral("4")},
        {QStringLiteral("Ponder"), QStringLiteral("true")}
    };
    UciOptionsDialog dialog(QStringLiteral("Stockfish"), sampleOptions(), values);

    auto *spinBox = qobject_cast<QSpinBox *>(widgetFor(dialog, QStringLiteral("Threads")));
    auto *checkBox = qobject_cast<QCheckBox *>(widgetFor(dialog, QStringLiteral("Ponder")));
    auto *comboBox = qobject_cast<QComboBox *>(widgetFor(dialog, QStringLiteral("Style")));
    auto *lineEdit = qobject_cast<QLineEdit *>(widgetFor(dialog, QStringLiteral("EvalFile")));
    QVERIFY(spinBox != nullptr);
    QVERIFY(checkBox != nullptr);
    QVERIFY(comboBox != nullptr);
    QVERIFY(lineEdit != nullptr);

    QCOMPARE(spinBox->value(), 4);
    QVERIFY(checkBox->isChecked());

    spinBox->setValue(6);
    checkBox->setChecked(false);
    comboBox->setCurrentText(QStringLiteral("Risky"));
    lineEdit->setText(QStringLiteral("nn-1a298aa575a0.nnue"));

    const QMap<QString, QString> result = dialog.optionValues();
    QCOMPARE(result.size(), 4);
    QCOMPARE(result.value(QStringLiteral("Threads")), QStringLiteral("6"));
    QCOMPARE(result.value(QStringLiteral("Ponder")), QStringLiteral("false"));
    QCOMPARE(result.value(QStringLiteral("Style")), QStringLiteral("Risky"));
    QCOMPARE(result.value(QStringLiteral("EvalFile")),
             QStringLiteral("nn-1a298aa575a0.nnue"));
    QVERIFY(!result.contains(QStringLiteral("Clear Hash")));
}

void UciOptionsDialogTest::testDefaultsUsed() {
    UciOptionsDialog dialog(QString(), sampleOptions(), {});

    auto *spinBox = qobject_cast<QSpinBox *>(widgetFor(dialog, QStringLiteral("Threads")));
    auto *checkBox = qobject_cast<QCheckBox *>(widgetFor(dialog, QStringLiteral("Ponder")));
    auto *comboBox = qobject_cast<QComboBox *>(widgetFor(dialog, QStringLiteral("Style")));
    auto *lineEdit = qobject_cast<QLineEdit *>(widgetFor(dialog, QStringLiteral("EvalFile")));
    QVERIFY(spinBox != nullptr);
    QVERIFY(checkBox != nullptr);
    QVERIFY(comboBox != nullptr);
    QVERIFY(lineEdit != nullptr);

    QCOMPARE(spinBox->value(), 1);
    QVERIFY(!checkBox->isChecked());
    QCOMPARE(comboBox->currentText(), QStringLiteral("Normal"));
    QVERIFY(lineEdit->text().isEmpty());

    const QMap<QString, QString> result = dialog.optionValues();
    QCOMPARE(result.value(QStringLiteral("Threads")), QStringLiteral("1"));
    QCOMPARE(result.value(QStringLiteral("Ponder")), QStringLiteral("false"));
    QCOMPARE(result.value(QStringLiteral("Style")), QStringLiteral("Normal"));
}

void UciOptionsDialogTest::testButtonOptionExcluded() {
    UciOptionsDialog dialog(QString(), sampleOptions(), {});
    QVERIFY(widgetFor(dialog, QStringLiteral("Clear Hash")) == nullptr);
    QVERIFY(!dialog.optionValues().contains(QStringLiteral("Clear Hash")));
}

QTEST_MAIN(UciOptionsDialogTest)
#include "ucioptionsdialog_test.moc"
