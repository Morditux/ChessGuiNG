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
    // Fischer increment granted to a player after each move played.
    qint64 incrementMilliseconds = 0;

    // PGN TimeControl tag derived from the numeric fields, e.g. "300+3".
    [[nodiscard]] QString pgnTimeControl() const;

    [[nodiscard]] QStringList pgnHeaders(const QString &result = QStringLiteral("*")) const;
};

#endif // CHESSGUI_COMPUTERGAMESETTINGS_H
