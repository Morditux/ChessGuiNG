//
// Helpers for reading PGN files: game splitting and tag pair extraction.
//

#include "pgnfile.h"

#include <QRegularExpression>

namespace PgnFile {

namespace {

struct RawBlock {
    QString text;
    int start = 0;
    int end = 0;
};

QVector<RawBlock> splitBlocks(const QString &content) {
    QVector<RawBlock> blocks;
    static const QRegularExpression separator(QStringLiteral("\\n\\s*\\n"));

    int blockStart = 0;
    const auto matches = separator.globalMatch(content);
    auto appendBlock = [&blocks, &content](int start, int end) {
        while (start < end && content.at(start).isSpace()) {
            ++start;
        }
        while (end > start && content.at(end - 1).isSpace()) {
            --end;
        }
        if (start < end) {
            blocks.append({content.mid(start, end - start), start, end});
        }
    };

    auto it = matches;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        appendBlock(blockStart, match.capturedStart());
        blockStart = match.capturedEnd();
    }
    appendBlock(blockStart, content.size());
    return blocks;
}

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

QVector<GameSegment> splitGameSegments(const QString &content) {
    const QVector<RawBlock> blocks = splitBlocks(content);

    QVector<GameSegment> games;
    int currentStart = -1;
    int currentEnd = -1;
    const auto finishCurrent = [&games, &content, &currentStart, &currentEnd] {
        if (currentStart >= 0 && currentEnd >= currentStart) {
            games.append({content.mid(currentStart, currentEnd - currentStart),
                          currentStart, currentEnd});
        }
        currentStart = -1;
        currentEnd = -1;
    };

    for (const RawBlock &rawBlock : blocks) {
        const QString &block = rawBlock.text;
        if (isTagBlock(block)) {
            finishCurrent();
            currentStart = rawBlock.start;
            currentEnd = rawBlock.end;
        } else if (currentStart >= 0) {
            currentEnd = rawBlock.end;
        }
    }

    finishCurrent();

    // Keep the existing headerless-PGN behaviour useful while still giving a
    // source range that can be replaced if the document contains one game.
    if (games.isEmpty() && !content.trimmed().isEmpty()) {
        int start = 0;
        int end = content.size();
        while (start < end && content.at(start).isSpace()) {
            ++start;
        }
        while (end > start && content.at(end - 1).isSpace()) {
            --end;
        }
        games.append({content.mid(start, end - start), start, end});
    }
    return games;
}

QStringList splitGames(const QString &content) {
    QStringList games;
    QString current;
    for (const RawBlock &rawBlock : splitBlocks(content)) {
        const QString &block = rawBlock.text;
        if (isTagBlock(block)) {
            if (!current.isEmpty()) {
                games.append(current);
                current.clear();
            }
            current = block;
        } else if (!current.isEmpty()) {
            if (!current.endsWith(QChar('\n'))) {
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

QString replaceGame(const QString &content,
                    const QVector<GameSegment> &segments,
                    int index,
                    const QString &replacement) {
    if (index < 0 || index >= segments.size()) {
        return content;
    }

    const GameSegment &segment = segments.at(index);
    if (segment.start < 0 || segment.end < segment.start ||
        segment.end > content.size()) {
        return content;
    }

    return content.left(segment.start) + replacement + content.mid(segment.end);
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
