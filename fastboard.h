#ifndef CHESSGUI_FASTBOARD_H
#define CHESSGUI_FASTBOARD_H

//
// Internal bitboard board representation for HeuristicEval and HeuristicSearch.
// Coordinates follow Rules: sq = row * 8 + column (0 = a8, 7 = h8, 56 = a1, 63 = h1).
// White moves toward row 0 (sq - 8), Black moves toward row 7 (sq + 8).
//

#include "rules.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace HeuristicSearch {

using Bitboard = quint64;

enum FastColor : uint8_t {
    White = 0,
    Black = 1
};

enum FastPiece : uint8_t {
    NoPiece = 0,
    Pawn = 1,
    Knight = 2,
    Bishop = 3,
    Rook = 4,
    Queen = 5,
    King = 6
};

enum CastlingRights : uint8_t {
    NoCastling = 0,
    WhiteKingSide = 1,
    WhiteQueenSide = 2,
    BlackKingSide = 4,
    BlackQueenSide = 8
};

// Packed 16-bit move: bits 0..5 = from, bits 6..11 = to, bits 12..14 = promo.
using FastMove = uint16_t;
constexpr FastMove NoFastMove = 0;

[[nodiscard]] constexpr FastMove makeFastMove(int from, int to,
                                              FastPiece promo = NoPiece) {
    return static_cast<FastMove>((from & 63) | ((to & 63) << 6) |
                                 ((static_cast<int>(promo) & 7) << 12));
}

[[nodiscard]] constexpr int moveFrom(FastMove move) {
    return move & 63;
}

[[nodiscard]] constexpr int moveTo(FastMove move) {
    return (move >> 6) & 63;
}

[[nodiscard]] constexpr FastPiece movePromo(FastMove move) {
    return static_cast<FastPiece>((move >> 12) & 7);
}

[[nodiscard]] constexpr FastMove toFastMove(const Rules::Move &move) {
    if (move.from.row < 0) {
        return NoFastMove;
    }
    return makeFastMove(move.from.row * 8 + move.from.column,
                        move.to.row * 8 + move.to.column,
                        static_cast<FastPiece>(move.promotion));
}

[[nodiscard]] constexpr Rules::Move toRulesMove(FastMove move) {
    if (move == NoFastMove) {
        return {};
    }
    const int from = moveFrom(move);
    const int to = moveTo(move);
    return Rules::Move{
        {from >> 3, from & 7},
        {to >> 3, to & 7},
        static_cast<Rules::PieceType>(movePromo(move))};
}

[[nodiscard]] constexpr FastColor oppositeColor(FastColor color) {
    return color == White ? Black : White;
}

[[nodiscard]] constexpr uint8_t makePieceByte(FastColor color, FastPiece piece) {
    return static_cast<uint8_t>((static_cast<uint8_t>(color) << 3) |
                                static_cast<uint8_t>(piece));
}

[[nodiscard]] constexpr FastPiece pieceByteType(uint8_t byte) {
    return static_cast<FastPiece>(byte & 7);
}

[[nodiscard]] constexpr FastColor pieceByteColor(uint8_t byte) {
    return static_cast<FastColor>(byte >> 3);
}

[[nodiscard]] constexpr int squareRow(int sq) {
    return sq >> 3;
}

[[nodiscard]] constexpr int squareCol(int sq) {
    return sq & 7;
}

[[nodiscard]] constexpr int relativeSquare(FastColor color, int sq) {
    return color == White ? (sq ^ 56) : sq;
}

[[nodiscard]] constexpr int relativeRank(FastColor color, int sq) {
    return color == White ? (8 - (sq >> 3)) : ((sq >> 3) + 1);
}

[[nodiscard]] constexpr Bitboard squareBB(int sq) {
    return Bitboard{1} << sq;
}

[[nodiscard]] inline int popLsb(Bitboard &bb) {
    const int sq = std::countr_zero(bb);
    bb &= bb - 1;
    return sq;
}

constexpr Bitboard FileA = 0x0101010101010101ULL;
constexpr Bitboard FileB = FileA << 1;
constexpr Bitboard FileC = FileA << 2;
constexpr Bitboard FileD = FileA << 3;
constexpr Bitboard FileE = FileA << 4;
constexpr Bitboard FileF = FileA << 5;
constexpr Bitboard FileG = FileA << 6;
constexpr Bitboard FileH = FileA << 7;

constexpr std::array<Bitboard, 8> FileMask = {
    FileA, FileB, FileC, FileD, FileE, FileF, FileG, FileH};

constexpr Bitboard Rank8 = 0x00000000000000FFULL; // row 0
constexpr Bitboard Rank7 = 0x000000000000FF00ULL; // row 1
constexpr Bitboard Rank6 = 0x0000000000FF0000ULL; // row 2
constexpr Bitboard Rank5 = 0x00000000FF000000ULL; // row 3
constexpr Bitboard Rank4 = 0x000000FF00000000ULL; // row 4
constexpr Bitboard Rank3 = 0x0000FF0000000000ULL; // row 5
constexpr Bitboard Rank2 = 0x00FF000000000000ULL; // row 6
constexpr Bitboard Rank1 = 0xFF00000000000000ULL; // row 7

constexpr std::array<Bitboard, 8> RankMask = {
    Rank8, Rank7, Rank6, Rank5, Rank4, Rank3, Rank2, Rank1};

// Squares where (row + col) % 2 == 0 (includes a8=0 and h1=63).
constexpr Bitboard EvenSquaresMask = 0xAA55AA55AA55AA55ULL;
constexpr Bitboard OddSquaresMask = ~EvenSquaresMask;

constexpr Bitboard QueensideMask = FileA | FileB | FileC | FileD;
constexpr Bitboard KingsideMask = FileE | FileF | FileG | FileH;

[[nodiscard]] inline Bitboard bswap64(Bitboard x) {
#ifdef _MSC_VER
    return _byteswap_uint64(x);
#else
    return __builtin_bswap64(x);
#endif
}

// Precomputed attack and line tables (defined in fastboard.cpp).
struct AttackTables {
    std::array<Bitboard, 64> knight{};
    std::array<Bitboard, 64> king{};
    std::array<std::array<Bitboard, 64>, 2> pawn{};
    std::array<Bitboard, 64> fileLine{};
    std::array<Bitboard, 64> diagLine{};
    std::array<Bitboard, 64> antiDiagLine{};
    std::array<std::array<uint8_t, 64>, 8> rankAttacksByte{};
};

const AttackTables &attackTables();

[[nodiscard]] inline Bitboard lineAttack(Bitboard occ, Bitboard sqBit,
                                         Bitboard mask) {
    Bitboard forward = occ & mask;
    Bitboard reverse = bswap64(forward);
    forward -= 2 * sqBit;
    reverse -= 2 * bswap64(sqBit);
    forward ^= bswap64(reverse);
    return forward & mask;
}

[[nodiscard]] inline Bitboard bishopAttacks(int sq, Bitboard occ) {
    const auto &t = attackTables();
    const Bitboard sqBit = squareBB(sq);
    return lineAttack(occ, sqBit, t.diagLine[sq]) |
           lineAttack(occ, sqBit, t.antiDiagLine[sq]);
}

[[nodiscard]] inline Bitboard rookAttacks(int sq, Bitboard occ) {
    const auto &t = attackTables();
    const Bitboard sqBit = squareBB(sq);
    const int rankShift = sq & 56;
    const int occ6 = static_cast<int>((occ >> (rankShift + 1)) & 63);
    const Bitboard rankAtt =
        static_cast<Bitboard>(t.rankAttacksByte[sq & 7][occ6]) << rankShift;
    return lineAttack(occ, sqBit, t.fileLine[sq]) | rankAtt;
}

[[nodiscard]] inline Bitboard queenAttacks(int sq, Bitboard occ) {
    return bishopAttacks(sq, occ) | rookAttacks(sq, occ);
}

[[nodiscard]] constexpr Bitboard pawnAttacksBB(FastColor color, Bitboard pawns) {
    if (color == White) {
        return ((pawns & ~FileA) >> 9) | ((pawns & ~FileH) >> 7);
    }
    return ((pawns & ~FileA) << 7) | ((pawns & ~FileH) << 9);
}

struct FastUndo {
    uint8_t movedPiece = 0;
    uint8_t capturedPiece = 0;
    uint8_t previousCastling = 0;
    int8_t previousEpSquare = -1;
    int previousHalfmoveClock = 0;
    quint64 previousHash = 0;
    quint64 previousPawnHash = 0;
    std::array<int, 2> previousNonPawnCount{};
};

struct FastNullUndo {
    int8_t previousEpSquare = -1;
    quint64 previousHash = 0;
};

struct FastBoard {
    std::array<Bitboard, 2> byColor{};
    std::array<Bitboard, 7> byType{};
    std::array<uint8_t, 64> board{};
    std::array<uint8_t, 2> kingSq{64, 64};
    uint8_t sideToMove = White;
    uint8_t castling = NoCastling;
    int8_t epSquare = -1;
    int halfmoveClock = 0;
    std::array<int, 2> nonPawnCount{0, 0};
    quint64 hash = 0;
    quint64 pawnHash = 0;

    [[nodiscard]] Bitboard occupancy() const {
        return byColor[White] | byColor[Black];
    }

    [[nodiscard]] Bitboard pieces(FastColor color, FastPiece type) const {
        return byColor[color] & byType[type];
    }

    [[nodiscard]] bool hasNonPawnMaterial(FastColor color) const {
        return nonPawnCount[color] > 0;
    }

    [[nodiscard]] static FastBoard fromRules(const Rules &rules);

    void recomputeHashes();

    [[nodiscard]] bool isSquareAttacked(int sq, FastColor by) const;
    [[nodiscard]] bool isInCheck(FastColor color) const {
        return kingSq[color] < 64 &&
               isSquareAttacked(kingSq[color], oppositeColor(color));
    }

    int generatePseudoLegalMoves(FastMove *moves) const;
    int generateTacticalMoves(FastMove *moves) const;

    bool makeMove(FastMove move, FastUndo &undo);
    void unmakeMove(FastMove move, const FastUndo &undo);

    bool makeNullMove(FastNullUndo &undo);
    void unmakeNullMove(const FastNullUndo &undo);

    [[nodiscard]] bool hasAnyLegalMove() const;
};

[[nodiscard]] int seeValueFast(const FastBoard &pos, FastMove move);

} // namespace HeuristicSearch

#endif // CHESSGUI_FASTBOARD_H
