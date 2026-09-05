// Opening-book access for the Crafty book.bin format.

#ifndef CHESSGUI_BOOK_H
#define CHESSGUI_BOOK_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>
#include <vector>

#include "rules.h"

class Book {
public:
    struct Entry {
        Rules::Move move;
        std::uint32_t played = 0;
        std::uint32_t flags = 0;
        float learn = 0.0F;
    };

    explicit Book(const QString &path = QStringLiteral(":/ChessGui/books/book.bin"));

    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] QList<Entry> moves(const Rules &position) const;
    [[nodiscard]] std::optional<Rules::Move> pickMove(const Rules &position) const;

private:
    static constexpr int IndexEntries = 32768;
    static constexpr int IndexBytes = IndexEntries * 4;
    static constexpr int RecordBytes = 16;

    bool load(const QString &path);

    [[nodiscard]] static std::uint32_t readUInt32(const QByteArray &data,
                                                   qsizetype offset);
    [[nodiscard]] static std::uint64_t readUInt64(const QByteArray &data,
                                                   qsizetype offset);
    [[nodiscard]] static float readFloat(const QByteArray &data,
                                         qsizetype offset);
    [[nodiscard]] static std::uint64_t positionHash(const Rules &position);
    [[nodiscard]] static bool hasEnPassantCapture(const Rules &position,
                                                  const QString &target);
    [[nodiscard]] static int craftyPieceIndex(Rules::PieceType type);
    [[nodiscard]] static std::vector<Rules::Move> legalMoves(const Rules &position);

    QByteArray data_;
    QVector<std::uint32_t> index_;
    QString errorString_;
};

#endif // CHESSGUI_BOOK_H
