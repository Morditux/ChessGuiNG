// Settings for a game played against the UCI engine.

#ifndef CHESSGUI_COMPUTERGAMESETTINGS_H
#define CHESSGUI_COMPUTERGAMESETTINGS_H

#include <QString>
#include <QStringList>

#include <QtGlobal>

struct ComputerGameSettings {
    QString event = QStringLiteral("Casual Game");
    QString site = QStringLiteral("ChessGui");
    QString date;
    QString round = QStringLiteral("1");
    QString playerName = QStringLiteral("Player");
    QString engineName;
    bool enginePlaysWhite = false;
    qint64 timeLimitMilliseconds = 300000;
    QString timeControl = QStringLiteral("300");

    [[nodiscard]] QStringList pgnHeaders(const QString &result = QStringLiteral("*")) const;
};

#endif // CHESSGUI_COMPUTERGAMESETTINGS_H
