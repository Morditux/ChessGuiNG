//
// Parser for the Universal Chess Interface (UCI) protocol.
//

#ifndef CHESSGUI_UCIPARSER_H
#define CHESSGUI_UCIPARSER_H

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "rules.h"

struct EngineAnalysisLine {
    int multipv = 1;
    std::optional<int> depth;
    std::optional<int> seldepth;
    std::optional<double> scoreCp;
    std::optional<int> mateIn;
    std::optional<qint64> nodes;
    std::optional<qint64> nps;
    std::optional<qint64> timeMs;
    QString pv;
    QString currmove;
    std::optional<int> currmovenumber;
};

struct UciBestMove {
    QString move;
    QString ponder;
};

struct UciOption {
    enum class Type {
        Check,
        Spin,
        Combo,
        Button,
        String,
        Unknown
    };

    QString name;
    Type type = Type::Unknown;
    QString defaultValue;
    QString minValue;
    QString maxValue;
    QStringList vars;
};

class UciParser {
public:
    static std::optional<EngineAnalysisLine> parseInfoLine(const QString &line);
    static std::optional<UciBestMove> parseBestMoveLine(const QString &line);
    static std::optional<UciOption> parseOptionLine(const QString &line);
    static std::optional<QString> parseIdName(const QString &line);
    static std::optional<QString> parseIdAuthor(const QString &line);
    static bool isUciOk(const QString &line);
    static bool isReadyOk(const QString &line);

    static QString toUciMove(Rules::Position from, Rules::Position to,
                             Rules::PieceType promotion = Rules::PieceType::None);
    static double scoreToWinningPercentage(double scoreCp, std::optional<int> mateIn = std::nullopt);
};

#endif // CHESSGUI_UCIPARSER_H
