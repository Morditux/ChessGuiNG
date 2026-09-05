//
// Helpers for reading PGN files: game splitting and tag pair extraction.
//

#include "pgnfile.h"

#include <QRegularExpression>

namespace PgnFile {

namespace {

bool isTagBlock(const QString &block) {
    const QStringList lines = block.split(QChar('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return false;
    }
    static const QRegularExpression tagLine(
        QStringLiteral("^\\s*\\[[^\\]]*\\]\\s*$"));
    for (const QString &line : lines) {
        if (!tagLine.match(line).hasMatch()) {
            return false;
        }
    }
    return true;
}

QString cleanBody(const QString &gameText) {
    QString body = gameText;
    body.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
    body.remove(QRegularExpression(QStringLiteral("\\{[^\\}]*\\}")));
    body.remove(QRegularExpression(QStringLiteral(";[^\\n]*")));
    body.remove(QRegularExpression(QStringLiteral("\\b\\d+\\.+")));
    body.remove(QRegularExpression(QStringLiteral("\\$\\d+")));
    body.remove(QRegularExpression(QStringLiteral("\\b(1-0|0-1|1/2-1/2|\\*)\\b")));
    return body;
}

} // namespace

QStringList splitGames(const QString &content) {
    const QStringList blocks =
        content.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")),
                      Qt::SkipEmptyParts);

    QStringList games;
    QString current;
    for (const QString &rawBlock : blocks) {
        const QString block = rawBlock.trimmed();
        if (block.isEmpty()) {
            continue;
        }
        if (isTagBlock(block)) {
            if (!current.isEmpty()) {
                games.append(current);
                current.clear();
            }
            current += block;
        } else {
            if (!current.isEmpty() && !current.endsWith(QChar('\n'))) {
                current += QChar(' ');
            }
            current += block;
        }
    }
    if (!current.isEmpty()) {
        games.append(current);
    }
    return games;
}

QStringList parseHeaders(const QString &gameText) {
    QStringList headers;
    static const QRegularExpression tagPair(
        QStringLiteral("\\[([^\\]]+)\\]"));
    auto it = tagPair.globalMatch(gameText);
    while (it.hasNext()) {
        const QString inner = it.next().captured(1).trimmed();
        const int quote = inner.indexOf(QChar('"'));
        if (quote > 0 && inner.endsWith(QChar('"'))) {
            headers.append(QStringLiteral("[%1]").arg(inner));
        }
    }
    return headers;
}

QString tagValue(const QString &gameText, const QString &tag) {
    const QRegularExpression tagPair(
        QStringLiteral("\\[%1\\s+\"([^\"]*)\"\\]")
            .arg(QRegularExpression::escape(tag)),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = tagPair.match(gameText);
    return match.hasMatch() ? match.captured(1) : QString();
}

int moveCount(const QString &gameText) {
    const QString body = cleanBody(gameText);
    static const QRegularExpression sanMove(
        QStringLiteral("^(?:O-O-O|O-O|[KQRBN]?[a-h]?[1-8]?x?[a-h][1-8](?:=[QRBN])?)[+#]?$"));
    int count = 0;
    for (const QString &token :
         body.split(QRegularExpression(QStringLiteral("\\s+")),
                    Qt::SkipEmptyParts)) {
        if (sanMove.match(token).hasMatch()) {
            ++count;
        }
    }
    return count;
}

} // namespace PgnFile
