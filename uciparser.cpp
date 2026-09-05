//
// Parser for the Universal Chess Interface (UCI) protocol.
//

#include "uciparser.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

std::optional<EngineAnalysisLine> UciParser::parseInfoLine(const QString &line) {
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(QStringLiteral("info "))) {
        return std::nullopt;
    }

    const QStringList tokens = trimmed.split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts
    );

    if (tokens.size() < 2 || tokens[0] != QStringLiteral("info")) {
        return std::nullopt;
    }

    EngineAnalysisLine info;
    bool hasMeaningfulData = false;

    for (qsizetype i = 1; i < tokens.size(); ++i) {
        const QString &token = tokens[i];

        if (token == QStringLiteral("depth") && i + 1 < tokens.size()) {
            info.depth = tokens[++i].toInt();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("seldepth") && i + 1 < tokens.size()) {
            info.seldepth = tokens[++i].toInt();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("multipv") && i + 1 < tokens.size()) {
            info.multipv = tokens[++i].toInt();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("score") && i + 2 < tokens.size()) {
            const QString &scoreType = tokens[++i];
            if (scoreType == QStringLiteral("cp")) {
                info.scoreCp = tokens[++i].toDouble();
                hasMeaningfulData = true;
            } else if (scoreType == QStringLiteral("mate")) {
                info.mateIn = tokens[++i].toInt();
                hasMeaningfulData = true;
            }
        } else if (token == QStringLiteral("nodes") && i + 1 < tokens.size()) {
            info.nodes = tokens[++i].toLongLong();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("nps") && i + 1 < tokens.size()) {
            info.nps = tokens[++i].toLongLong();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("time") && i + 1 < tokens.size()) {
            info.timeMs = tokens[++i].toLongLong();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("currmove") && i + 1 < tokens.size()) {
            info.currmove = tokens[++i];
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("currmovenumber") && i + 1 < tokens.size()) {
            info.currmovenumber = tokens[++i].toInt();
            hasMeaningfulData = true;
        } else if (token == QStringLiteral("pv") && i + 1 < tokens.size()) {
            QStringList pvMoves;
            for (qsizetype j = i + 1; j < tokens.size(); ++j) {
                pvMoves.append(tokens[j]);
            }
            info.pv = pvMoves.join(QChar(' '));
            hasMeaningfulData = true;
            break;
        }
    }

    if (!hasMeaningfulData) {
        return std::nullopt;
    }

    return info;
}

std::optional<UciBestMove> UciParser::parseBestMoveLine(const QString &line) {
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(QStringLiteral("bestmove"))) {
        return std::nullopt;
    }

    const QStringList tokens = trimmed.split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts
    );

    if (tokens.size() < 2 || tokens[0] != QStringLiteral("bestmove")) {
        return std::nullopt;
    }

    UciBestMove bestMove;
    bestMove.move = tokens[1];

    if (tokens.size() >= 4 && tokens[2] == QStringLiteral("ponder")) {
        bestMove.ponder = tokens[3];
    }

    return bestMove;
}

std::optional<UciOption> UciParser::parseOptionLine(const QString &line) {
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(QStringLiteral("option "))) {
        return std::nullopt;
    }

    const QStringList tokens = trimmed.split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts
    );
    const int typeIndex = tokens.indexOf(QStringLiteral("type"));
    if (typeIndex < 2 || typeIndex + 1 >= tokens.size()) {
        return std::nullopt;
    }

    UciOption option;
    option.name = tokens.mid(2, typeIndex - 2).join(QChar(' '));
    const QString type = tokens.at(typeIndex + 1).toLower();
    if (type == QStringLiteral("check")) {
        option.type = UciOption::Type::Check;
    } else if (type == QStringLiteral("spin")) {
        option.type = UciOption::Type::Spin;
    } else if (type == QStringLiteral("combo")) {
        option.type = UciOption::Type::Combo;
    } else if (type == QStringLiteral("button")) {
        option.type = UciOption::Type::Button;
    } else if (type == QStringLiteral("string")) {
        option.type = UciOption::Type::String;
    }

    const auto readValue = [&tokens, typeIndex](const QString &keyword) {
        const int index = tokens.indexOf(keyword, typeIndex + 2);
        if (index < 0 || index + 1 >= tokens.size()) {
            return QString();
        }

        static const QStringList markers = {
            QStringLiteral("default"), QStringLiteral("min"),
            QStringLiteral("max"), QStringLiteral("var")
        };
        int end = index + 1;
        while (end < tokens.size() && !markers.contains(tokens.at(end))) {
            ++end;
        }
        return tokens.mid(index + 1, end - index - 1).join(QChar(' '));
    };

    option.defaultValue = readValue(QStringLiteral("default"));
    option.minValue = readValue(QStringLiteral("min"));
    option.maxValue = readValue(QStringLiteral("max"));

    for (int index = typeIndex + 2; index < tokens.size(); ++index) {
        if (tokens.at(index) != QStringLiteral("var") || index + 1 >= tokens.size()) {
            continue;
        }

        int end = index + 1;
        while (end < tokens.size() && tokens.at(end) != QStringLiteral("var")) {
            ++end;
        }
        option.vars.append(tokens.mid(index + 1, end - index - 1).join(QChar(' ')));
        index = end - 1;
    }

    return option.name.isEmpty() ? std::nullopt : std::optional<UciOption>(option);
}

std::optional<QString> UciParser::parseIdName(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(QStringLiteral("id name "))) {
        return trimmed.mid(8).trimmed();
    }
    return std::nullopt;
}

std::optional<QString> UciParser::parseIdAuthor(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(QStringLiteral("id author "))) {
        return trimmed.mid(10).trimmed();
    }
    return std::nullopt;
}

bool UciParser::isUciOk(const QString &line) {
    return line.trimmed() == QStringLiteral("uciok");
}

bool UciParser::isReadyOk(const QString &line) {
    return line.trimmed() == QStringLiteral("readyok");
}

QString UciParser::toUciMove(Rules::Position from, Rules::Position to,
                            Rules::PieceType promotion) {
    QString uci;
    uci += QChar('a' + from.column);
    uci += QString::number(8 - from.row);
    uci += QChar('a' + to.column);
    uci += QString::number(8 - to.row);

    switch (promotion) {
        case Rules::PieceType::Queen:
            uci += QChar('q');
            break;
        case Rules::PieceType::Rook:
            uci += QChar('r');
            break;
        case Rules::PieceType::Bishop:
            uci += QChar('b');
            break;
        case Rules::PieceType::Knight:
            uci += QChar('n');
            break;
        default:
            break;
    }

    return uci;
}

double UciParser::scoreToWinningPercentage(double scoreCp, std::optional<int> mateIn) {
    if (mateIn.has_value()) {
        const int mate = *mateIn;
        if (mate > 0) {
            return 100.0;
        }
        if (mate < 0) {
            return 0.0;
        }
        return 50.0;
    }

    // Standard winning percentage formula based on centipawns
    const double winPercentage = 100.0 / (1.0 + std::exp(-0.003682 * scoreCp));
    return std::clamp(winPercentage, 0.0, 100.0);
}
