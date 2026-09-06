//
// PGN visual annotations: [%csl ...] (colored squares) and [%cal ...] (colored arrows).
//

#include "pgnannotations.h"

#include <QRegularExpression>
#include <QStringList>

namespace PgnAnnotations {

QColor greenColor() {
    return {"#15781b"};
}

QColor redColor() {
    return {"#d32f2f"};
}

QColor blueColor() {
    return {"#1976d2"};
}

QColor orangeColor() {
    return {"#e67e22"};
}

QColor codeToColor(char code) {
    switch (code) {
    case 'R':
    case 'r':
        return redColor();
    case 'B':
    case 'b':
        return blueColor();
    case 'Y':
    case 'y':
    case 'O':
    case 'o':
        return orangeColor();
    case 'G':
    case 'g':
    default:
        return greenColor();
    }
}

char colorToCode(const QColor &color) {
    if (color == redColor() || color.name().compare(QLatin1String("#d32f2f"), Qt::CaseInsensitive) == 0) {
        return 'R';
    }
    if (color == blueColor() || color.name().compare(QLatin1String("#1976d2"), Qt::CaseInsensitive) == 0) {
        return 'B';
    }
    if (color == orangeColor() || color.name().compare(QLatin1String("#e67e22"), Qt::CaseInsensitive) == 0) {
        return 'Y';
    }
    return 'G';
}

QString positionToSquare(Rules::Position pos) {
    if (!Rules::isInside(pos)) {
        return {};
    }
    return QString(QChar('a' + pos.column)) + QString::number(8 - pos.row);
}

std::optional<Rules::Position> squareToPosition(const QString &sq) {
    if (sq.size() != 2) {
        return std::nullopt;
    }
    const char file = sq[0].toLatin1();
    const char rank = sq[1].toLatin1();
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return std::nullopt;
    }
    const int col = file - 'a';
    const int row = 8 - (rank - '0');
    return Rules::Position{row, col};
}

QString encode(const std::vector<UserArrow> &arrows,
               const std::vector<SquareAnnotation> &squares) {
    QString result;
    if (!squares.empty()) {
        QStringList tokens;
        tokens.reserve(static_cast<qsizetype>(squares.size()));
        for (const auto &sq : squares) {
            const QString posStr = positionToSquare(sq.position);
            if (!posStr.isEmpty()) {
                tokens.append(QChar(colorToCode(sq.color)) + posStr);
            }
        }
        if (!tokens.isEmpty()) {
            result += QStringLiteral("[%csl ") + tokens.join(QLatin1Char(',')) + QStringLiteral("]");
        }
    }

    if (!arrows.empty()) {
        QStringList tokens;
        tokens.reserve(static_cast<qsizetype>(arrows.size()));
        for (const auto &arrow : arrows) {
            const QString fromStr = positionToSquare(arrow.from);
            const QString toStr = positionToSquare(arrow.to);
            if (!fromStr.isEmpty() && !toStr.isEmpty()) {
                tokens.append(QChar(colorToCode(arrow.color)) + fromStr + toStr);
            }
        }
        if (!tokens.isEmpty()) {
            if (!result.isEmpty()) {
                result += QLatin1Char(' ');
            }
            result += QStringLiteral("[%cal ") + tokens.join(QLatin1Char(',')) + QStringLiteral("]");
        }
    }

    return result;
}

bool decode(const QString &comment,
            std::vector<UserArrow> &outArrows,
            std::vector<SquareAnnotation> &outSquares) {
    bool matched = false;

    static const QRegularExpression cslRegex(QStringLiteral(R"(\[%csl\s+([^\]]+)\])"));
    const auto cslMatch = cslRegex.match(comment);
    if (cslMatch.hasMatch()) {
        const QStringList entries = cslMatch.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &rawEntry : entries) {
            const QString entry = rawEntry.trimmed();
            if (entry.size() >= 3) {
                const char code = entry[0].toLatin1();
                const auto pos = squareToPosition(entry.sliced(1, 2));
                if (pos.has_value()) {
                    outSquares.push_back(SquareAnnotation{*pos, codeToColor(code)});
                    matched = true;
                }
            }
        }
    }

    static const QRegularExpression calRegex(QStringLiteral(R"(\[%cal\s+([^\]]+)\])"));
    const auto calMatch = calRegex.match(comment);
    if (calMatch.hasMatch()) {
        const QStringList entries = calMatch.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &rawEntry : entries) {
            const QString entry = rawEntry.trimmed();
            if (entry.size() >= 5) {
                const char code = entry[0].toLatin1();
                const auto from = squareToPosition(entry.sliced(1, 2));
                const auto to = squareToPosition(entry.sliced(3, 2));
                if (from.has_value() && to.has_value()) {
                    outArrows.push_back(UserArrow{*from, *to, codeToColor(code)});
                    matched = true;
                }
            }
        }
    }

    return matched;
}

QString stripAnnotationTags(const QString &comment) {
    static const QRegularExpression cslRegex(QStringLiteral(R"(\[%csl\s+[^\]]+\])"));
    static const QRegularExpression calRegex(QStringLiteral(R"(\[%cal\s+[^\]]+\])"));

    QString cleaned = comment;
    cleaned.remove(cslRegex);
    cleaned.remove(calRegex);

    cleaned = cleaned.trimmed();
    if (cleaned.startsWith(QLatin1Char('{')) && cleaned.endsWith(QLatin1Char('}'))) {
        cleaned = cleaned.mid(1, cleaned.size() - 2).trimmed();
    }

    static const QRegularExpression spaceRegex(QStringLiteral(R"(\s+)"));
    cleaned.replace(spaceRegex, QStringLiteral(" "));
    return cleaned.trimmed();
}

QString formatComment(const std::vector<UserArrow> &arrows,
                     const std::vector<SquareAnnotation> &squares,
                     const QString &commentText) {
    const QString tags = encode(arrows, squares);
    const QString text = stripAnnotationTags(commentText);

    if (tags.isEmpty()) {
        return text;
    }
    if (text.isEmpty()) {
        return tags;
    }
    return tags + QLatin1Char(' ') + text;
}

} // namespace PgnAnnotations
