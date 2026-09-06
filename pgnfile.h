//
// Helpers for reading PGN files: game splitting and tag pair extraction.
//

#ifndef CHESSGUI_PGNFILE_H
#define CHESSGUI_PGNFILE_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace PgnFile {

struct GameSegment {
    // Text without the separator whitespace around the game.
    QString text;
    // Half-open range of text in the original PGN document. Separator
    // whitespace is deliberately outside this range so it survives a
    // replacement unchanged.
    int start = 0;
    int end = 0;
};

// Splits a PGN document into games while retaining each game's range in the
// original document. The returned text keeps the original internal
// formatting; only whitespace surrounding a game is excluded.
QVector<GameSegment> splitGameSegments(const QString &content);

// Splits PGN text into individual games. A new game starts when a block of
// tag pairs (lines starting with '[') appears while the current game already
// has content; consecutive movetext blocks separated by blank lines belong to
// the same game.
QStringList splitGames(const QString &content);

// Replaces one game in the original document and preserves every character
// outside that game's range.
QString replaceGame(const QString &content,
                    const QVector<GameSegment> &segments,
                    int index,
                    const QString &replacement);

// Reads the value of a tag pair from the header section of a single game,
// e.g. tagValue(game, "Result") returns "1-0" for [Result "1-0"]. Returns an
// empty string when the tag is absent.
QString tagValue(const QString &gameText, const QString &tag);

// Returns the header lines ([Tag "value"] pairs) of a single game.
QStringList parseHeaders(const QString &gameText);

// Counts the number of SAN moves in the movetext of a single game.
int moveCount(const QString &gameText);

} // namespace PgnFile

#endif // CHESSGUI_PGNFILE_H
