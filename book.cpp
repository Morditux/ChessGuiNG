// Opening-book access for the Crafty book.bin format.

#include "book.h"

#include "craftyhashkeys.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cstring>

namespace {

constexpr std::uint64_t kTopHashMask = (UINT64_C(0xffff) << 48);
constexpr std::uint32_t kPlayedMask = UINT32_C(0x00ffffff);
constexpr std::uint32_t kRejectedFlags = UINT32_C(0x03);

std::uint32_t byteAt(const QByteArray &data, qsizetype offset) {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(data.at(offset)));
}

} // namespace

Book::Book(const QString &path) {
    load(path);
}

bool Book::isLoaded() const {
    return !data_.isEmpty() && index_.size() == IndexEntries;
}

QString Book::errorString() const {
    return errorString_;
}

bool Book::load(const QString &path) {
    data_.clear();
    index_.clear();
    errorString_.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorString_ = QStringLiteral("Opening book could not be loaded: %1")
                           .arg(file.errorString());
        return false;
    }

    data_ = file.readAll();
    if (data_.size() < IndexBytes + 4) {
        data_.clear();
        errorString_ = QStringLiteral("Opening book is truncated.");
        return false;
    }

    index_.resize(IndexEntries);
    for (int i = 0; i < IndexEntries; ++i) {
        index_[i] = readUInt32(data_, static_cast<qsizetype>(i) * 4);
        if (index_[i] == 0) {
            continue;
        }

        const qsizetype clusterOffset = index_[i];
        if (clusterOffset < IndexBytes ||
            clusterOffset + 4 > data_.size()) {
            data_.clear();
            index_.clear();
            errorString_ = QStringLiteral("Opening book contains an invalid index.");
            return false;
        }

        const std::uint32_t count = readUInt32(data_, clusterOffset);
        const qsizetype remaining = data_.size() - clusterOffset - 4;
        if (static_cast<qsizetype>(count) > remaining / RecordBytes) {
            data_.clear();
            index_.clear();
            errorString_ = QStringLiteral("Opening book contains an invalid cluster.");
            return false;
        }
    }

    return true;
}

std::uint32_t Book::readUInt32(const QByteArray &data, qsizetype offset) {
    return byteAt(data, offset) |
           (byteAt(data, offset + 1) << 8) |
           (byteAt(data, offset + 2) << 16) |
           (byteAt(data, offset + 3) << 24);
}

std::uint64_t Book::readUInt64(const QByteArray &data, qsizetype offset) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(byteAt(data, offset + i)) << (i * 8);
    }
    return value;
}

float Book::readFloat(const QByteArray &data, qsizetype offset) {
    const std::uint32_t bits = readUInt32(data, offset);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

int Book::craftyPieceIndex(Rules::PieceType type) {
    switch (type) {
    case Rules::PieceType::Pawn:   return 1;
    case Rules::PieceType::Knight: return 2;
    case Rules::PieceType::Bishop: return 3;
    case Rules::PieceType::Rook:   return 4;
    case Rules::PieceType::Queen:  return 5;
    case Rules::PieceType::King:   return 6;
    case Rules::PieceType::None:   return 0;
    }
    return 0;
}

bool Book::hasEnPassantCapture(const Rules &position, const QString &target) {
    if (target.size() != 2 || target == QStringLiteral("-")) {
        return false;
    }

    const int column = target.at(0).toLatin1() - 'a';
    const int rank = target.at(1).digitValue();
    if (column < 0 || column >= 8 || (rank != 3 && rank != 6)) {
        return false;
    }

    const bool whiteToMove = position.currentPlayer() == Rules::Color::White;
    if ((whiteToMove && rank != 6) || (!whiteToMove && rank != 3)) {
        return false;
    }

    const int targetRow = 8 - rank;
    const int pawnRow = whiteToMove ? targetRow + 1 : targetRow - 1;
    for (const int adjacentColumn : {column - 1, column + 1}) {
        if (adjacentColumn < 0 || adjacentColumn >= 8) {
            continue;
        }
        const auto pawn = position.pieceAt({pawnRow, adjacentColumn});
        if (pawn.has_value() && pawn->type == Rules::PieceType::Pawn &&
            pawn->color == position.currentPlayer()) {
            return true;
        }
    }
    return false;
}

std::uint64_t Book::positionHash(const Rules &position) {
    const QStringList fields = position.toFen().split(
        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (fields.size() < 4) {
        return 0;
    }

    std::uint64_t hash = 0;
    const QStringList ranks = fields.at(0).split(QChar('/'));
    if (ranks.size() != 8) {
        return 0;
    }

    for (int row = 0; row < 8; ++row) {
        int column = 0;
        for (const QChar character : ranks.at(row)) {
            if (character.isDigit()) {
                column += character.digitValue();
                continue;
            }

            if (column >= 8) {
                return 0;
            }
            Rules::PieceType type = Rules::PieceType::None;
            switch (character.toLower().toLatin1()) {
            case 'p': type = Rules::PieceType::Pawn; break;
            case 'n': type = Rules::PieceType::Knight; break;
            case 'b': type = Rules::PieceType::Bishop; break;
            case 'r': type = Rules::PieceType::Rook; break;
            case 'q': type = Rules::PieceType::Queen; break;
            case 'k': type = Rules::PieceType::King; break;
            default: return 0;
            }

            const bool white = character.isUpper();
            const int side = white ? 1 : 0;
            const int square = (7 - row) * 8 + column;
            hash ^= CraftyBookHash::piece[side][craftyPieceIndex(type)][square];
            ++column;
        }
        if (column != 8) {
            return 0;
        }
    }

    const QString &castling = fields.at(2);
    if (!castling.contains(QChar('K'))) {
        hash ^= CraftyBookHash::castle[0][1];
    }
    if (!castling.contains(QChar('Q'))) {
        hash ^= CraftyBookHash::castle[1][1];
    }
    if (!castling.contains(QChar('k'))) {
        hash ^= CraftyBookHash::castle[0][0];
    }
    if (!castling.contains(QChar('q'))) {
        hash ^= CraftyBookHash::castle[1][0];
    }

    if (hasEnPassantCapture(position, fields.at(3))) {
        const int file = fields.at(3).at(0).toLatin1() - 'a';
        const int rank = fields.at(3).at(1).digitValue();
        hash ^= CraftyBookHash::enPassant[(rank - 1) * 8 + file];
    }

    return hash;
}

std::vector<Rules::Move> Book::legalMoves(const Rules &position) {
    std::vector<Rules::Move> moves;
    const Rules::Color color = position.currentPlayer();

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const Rules::Position from{row, column};
            const auto piece = position.pieceAt(from);
            if (!piece.has_value() || piece->color != color) {
                continue;
            }

            for (const Rules::Position to : position.legalMoves(from)) {
                const bool promotion = piece->type == Rules::PieceType::Pawn &&
                                       (to.row == 0 || to.row == 7);
                if (promotion) {
                    for (const Rules::PieceType type : {
                             Rules::PieceType::Queen,
                             Rules::PieceType::Rook,
                             Rules::PieceType::Bishop,
                             Rules::PieceType::Knight}) {
                        moves.push_back({from, to, type});
                    }
                } else {
                    moves.push_back({from, to, Rules::PieceType::None});
                }
            }
        }
    }

    return moves;
}

QList<Book::Entry> Book::moves(const Rules &position) const {
    QList<Entry> result;
    if (!isLoaded()) {
        return result;
    }

    const std::uint64_t currentHash = positionHash(position);
    const int bucket = static_cast<int>(currentHash >> 49);
    const std::uint32_t clusterOffset = index_.at(bucket);
    if (clusterOffset == 0) {
        return result;
    }

    const std::uint32_t count = readUInt32(data_, clusterOffset);
    const std::uint64_t commonHash = currentHash & kTopHashMask;
    for (const Rules::Move &move : legalMoves(position)) {
        Rules nextPosition = position;
        if (!nextPosition.tryMove(move)) {
            continue;
        }

        const std::uint64_t nextHash = positionHash(nextPosition);
        const std::uint64_t orientedHash =
            position.currentPlayer() == Rules::Color::White
                ? nextHash
                : ~nextHash;
        const std::uint64_t bookKey =
            (orientedHash & ~kTopHashMask) | commonHash;

        for (std::uint32_t i = 0; i < count; ++i) {
            const qsizetype recordOffset =
                static_cast<qsizetype>(clusterOffset) + 4 +
                static_cast<qsizetype>(i) * RecordBytes;
            if (readUInt64(data_, recordOffset) != bookKey) {
                continue;
            }

            const std::uint32_t status = readUInt32(data_, recordOffset + 8);
            result.append({move,
                           status & kPlayedMask,
                           status >> 24,
                           readFloat(data_, recordOffset + 12)});
            break;
        }
    }

    return result;
}

std::optional<Rules::Move> Book::pickMove(const Rules &position) const {
    const QList<Entry> candidates = moves(position);
    if (candidates.isEmpty()) {
        return std::nullopt;
    }

    const Entry *best = nullptr;
    for (const Entry &candidate : candidates) {
        if ((candidate.flags & kRejectedFlags) != 0) {
            continue;
        }
        if (best == nullptr || candidate.played > best->played) {
            best = &candidate;
        }
    }

    // Keep a usable fallback for a book position where every entry is marked
    // as rejected by Crafty's annotations.
    if (best == nullptr) {
        best = &*std::max_element(
            candidates.cbegin(), candidates.cend(),
            [](const Entry &left, const Entry &right) {
                return left.played < right.played;
            });
    }
    return best->move;
}
