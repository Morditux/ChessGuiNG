// Settings for a game played against the UCI engine.

#include "computergamesettings.h"

namespace {

QString pgnValue(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        value = QStringLiteral("?");
    }
    value.replace(QChar('\\'), QStringLiteral("\\\\"));
    value.replace(QChar('"'), QStringLiteral("\\\""));
    return value;
}

} // namespace

QStringList ComputerGameSettings::pgnHeaders(const QString &result) const {
    return {
        QStringLiteral("[Event \"%1\"]").arg(pgnValue(event)),
        QStringLiteral("[Site \"%1\"]").arg(pgnValue(site)),
        QStringLiteral("[Date \"%1\"]").arg(pgnValue(date)),
        QStringLiteral("[Round \"%1\"]").arg(pgnValue(round)),
        QStringLiteral("[White \"%1\"]").arg(
            pgnValue(enginePlaysWhite ? engineName : playerName)),
        QStringLiteral("[Black \"%1\"]").arg(
            pgnValue(enginePlaysWhite ? playerName : engineName)),
        QStringLiteral("[TimeControl \"%1\"]").arg(pgnValue(timeControl)),
        QStringLiteral("[Result \"%1\"]").arg(pgnValue(result))
    };
}
