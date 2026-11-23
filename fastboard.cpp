#include "fastboard.h"
#include "heuristiceval.h"

#include <algorithm>
#include <limits>

namespace HeuristicSearch {

namespace {

constexpr quint64 ZobristPawnBase = 0x9E3779B97F4A7C15ULL;

struct FastZobristTable {
    std::array<std::array<std::array<quint64, 64>, 7>, 2> pieces{};
    quint64 sideToMove = 0;
    std::array<quint64, 16> castling{};
    std::array<quint64, 8> enPassantFile{};

    constexpr FastZobristTable() {
        quint64 state = 0x123456789ABCDEF0ULL;
        const auto next = [&state]() -> quint64 {
            state += 0x9E3779B97F4A7C15ULL;
            quint64 z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        };
        for (int c = 0; c < 2; ++c) {
            for (int pt = 1; pt <= 6; ++pt) {
                for (int sq = 0; sq < 64; ++sq) {
                    pieces[c][pt][sq] = next();
                }
            }
        }
        sideToMove = next();
        for (int i = 0; i < 16; ++i) {
            castling[i] = i == 0 ? 0 : next();
        }
        for (int f = 0; f < 8; ++f) {
            enPassantFile[f] = next();
        }
    }
};

constexpr FastZobristTable FastZobrist{};

constexpr std::array<uint8_t, 64> buildCastlingMask() {
    std::array<uint8_t, 64> mask{};
    mask.fill(0xFF);
    mask[0] = static_cast<uint8_t>(~BlackQueenSide);  // a8
    mask[4] = static_cast<uint8_t>(~(BlackKingSide | BlackQueenSide)); // e8
    mask[7] = static_cast<uint8_t>(~BlackKingSide);   // h8
    mask[56] = static_cast<uint8_t>(~WhiteQueenSide); // a1
    mask[60] = static_cast<uint8_t>(~(WhiteKingSide | WhiteQueenSide)); // e1
    mask[63] = static_cast<uint8_t>(~WhiteKingSide);  // h1
    return mask;
}

constexpr std::array<uint8_t, 64> CastlingMask = buildCastlingMask();

constexpr AttackTables buildAttackTables() {
    AttackTables t;
    constexpr int knightDr[8] = {-2, -2, -1, -1, 1, 1, 2, 2};
    constexpr int knightDc[8] = {-1, 1, -2, 2, -2, 2, -1, 1};
    constexpr int kingDr[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    constexpr int kingDc[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

    for (int sq = 0; sq < 64; ++sq) {
        const int r = sq >> 3;
        const int c = sq & 7;

        for (int i = 0; i < 8; ++i) {
            const int nr = r + knightDr[i];
            const int nc = c + knightDc[i];
            if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
                t.knight[sq] |= squareBB(nr * 8 + nc);
            }
            const int kr = r + kingDr[i];
            const int kc = c + kingDc[i];
            if (kr >= 0 && kr < 8 && kc >= 0 && kc < 8) {
                t.king[sq] |= squareBB(kr * 8 + kc);
            }
        }

        if (r > 0) {
            if (c > 0) {
                t.pawn[White][sq] |= squareBB((r - 1) * 8 + (c - 1));
            }
            if (c < 7) {
                t.pawn[White][sq] |= squareBB((r - 1) * 8 + (c + 1));
            }
        }
        if (r < 7) {
            if (c > 0) {
                t.pawn[Black][sq] |= squareBB((r + 1) * 8 + (c - 1));
            }
            if (c < 7) {
                t.pawn[Black][sq] |= squareBB((r + 1) * 8 + (c + 1));
            }
        }

        t.fileLine[sq] = FileMask[c];

        Bitboard diag = 0;
        Bitboard anti = 0;
        for (int rr = 0; rr < 8; ++rr) {
            for (int cc = 0; cc < 8; ++cc) {
                if (rr - cc == r - c) {
                    diag |= squareBB(rr * 8 + cc);
                }
                if (rr + cc == r + c) {
                    anti |= squareBB(rr * 8 + cc);
                }
            }
        }
        t.diagLine[sq] = diag;
        t.antiDiagLine[sq] = anti;
    }

    for (int col = 0; col < 8; ++col) {
        for (int occ6 = 0; occ6 < 64; ++occ6) {
            const int occ8 = occ6 << 1;
            uint8_t att = 0;
            for (int cc = col + 1; cc < 8; ++cc) {
                att |= static_cast<uint8_t>(1 << cc);
                if (occ8 & (1 << cc)) {
                    break;
                }
            }
            for (int cc = col - 1; cc >= 0; --cc) {
                att |= static_cast<uint8_t>(1 << cc);
                if (occ8 & (1 << cc)) {
                    break;
                }
            }
            t.rankAttacksByte[col][occ6] = att;
        }
    }

    return t;
}

constexpr AttackTables PrecomputedAttackTables = buildAttackTables();

inline bool isCountedPiece(FastPiece pt) {
    return pt >= Knight && pt <= Queen;
}

inline int pieceTypeValue(FastPiece pt) {
    switch (pt) {
    case Pawn:   return HeuristicEval::pieceValue(Rules::PieceType::Pawn);
    case Knight: return HeuristicEval::pieceValue(Rules::PieceType::Knight);
    case Bishop: return HeuristicEval::pieceValue(Rules::PieceType::Bishop);
    case Rook:   return HeuristicEval::pieceValue(Rules::PieceType::Rook);
    case Queen:  return HeuristicEval::pieceValue(Rules::PieceType::Queen);
    default:     return 0;
    }
}

void emitPawnMove(FastMove *&out, int from, int to, bool promotion) {
    if (promotion) {
        *out++ = makeFastMove(from, to, Queen);
        *out++ = makeFastMove(from, to, Rook);
        *out++ = makeFastMove(from, to, Bishop);
        *out++ = makeFastMove(from, to, Knight);
    } else {
        *out++ = makeFastMove(from, to);
    }
}

} // namespace

const AttackTables &attackTables() {
    return PrecomputedAttackTables;
}

FastBoard FastBoard::fromRules(const Rules &rules) {
    FastBoard pos;
    const Rules::Board &src = rules.board();
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const auto &sqPiece = src[row][col];
            if (!sqPiece.has_value() ||
                sqPiece->type == Rules::PieceType::None) {
                continue;
            }
            const int sq = row * 8 + col;
            const FastColor c =
                sqPiece->color == Rules::Color::White ? White : Black;
            const FastPiece pt = static_cast<FastPiece>(sqPiece->type);
            const Bitboard bit = squareBB(sq);
            pos.byColor[c] |= bit;
            pos.byType[pt] |= bit;
            pos.board[sq] = makePieceByte(c, pt);
            if (pt == King) {
                pos.kingSq[c] = static_cast<uint8_t>(sq);
            } else if (isCountedPiece(pt)) {
                ++pos.nonPawnCount[c];
            }
        }
    }

    pos.sideToMove =
        rules.currentPlayer() == Rules::Color::White ? White : Black;
    pos.halfmoveClock = rules.halfmoveClock();

    const auto hasUnmoved = [&src](int row, int col, Rules::Color color,
                                    Rules::PieceType type) {
        const auto &p = src[row][col];
        return p.has_value() && p->color == color && p->type == type &&
               !p->hasMoved;
    };
    if (hasUnmoved(7, 4, Rules::Color::White, Rules::PieceType::King)) {
        if (hasUnmoved(7, 7, Rules::Color::White, Rules::PieceType::Rook)) {
            pos.castling |= WhiteKingSide;
        }
        if (hasUnmoved(7, 0, Rules::Color::White, Rules::PieceType::Rook)) {
            pos.castling |= WhiteQueenSide;
        }
    }
    if (hasUnmoved(0, 4, Rules::Color::Black, Rules::PieceType::King)) {
        if (hasUnmoved(0, 7, Rules::Color::Black, Rules::PieceType::Rook)) {
            pos.castling |= BlackKingSide;
        }
        if (hasUnmoved(0, 0, Rules::Color::Black, Rules::PieceType::Rook)) {
            pos.castling |= BlackQueenSide;
        }
    }

    if (const auto last = rules.lastMove(); last.has_value()) {
        const auto &p = src[last->to.row][last->to.column];
        if (p.has_value() && p->type == Rules::PieceType::Pawn &&
            std::abs(last->to.row - last->from.row) == 2) {
            const int epRow = (last->from.row + last->to.row) / 2;
            pos.epSquare = static_cast<int8_t>(epRow * 8 + last->to.column);
        }
    }

    pos.recomputeHashes();
    return pos;
}

void FastBoard::recomputeHashes() {
    hash = 0;
    pawnHash = ZobristPawnBase;
    Bitboard occ = occupancy();
    while (occ != 0) {
        const int sq = popLsb(occ);
        const uint8_t b = board[sq];
        const FastColor c = pieceByteColor(b);
        const FastPiece pt = pieceByteType(b);
        const quint64 key = FastZobrist.pieces[c][pt][sq];
        hash ^= key;
        if (pt == Pawn) {
            pawnHash ^= key;
        }
    }
    if (sideToMove == Black) {
        hash ^= FastZobrist.sideToMove;
    }
    hash ^= FastZobrist.castling[castling];
    if (epSquare >= 0) {
        hash ^= FastZobrist.enPassantFile[epSquare & 7];
    }
}

bool FastBoard::isSquareAttacked(int sq, FastColor by) const {
    const auto &t = PrecomputedAttackTables;
    const Bitboard themBB = byColor[by];
    if ((t.pawn[oppositeColor(by)][sq] & themBB & byType[Pawn]) != 0) {
        return true;
    }
    if ((t.knight[sq] & themBB & byType[Knight]) != 0) {
        return true;
    }
    if ((t.king[sq] & themBB & byType[King]) != 0) {
        return true;
    }
    const Bitboard occ = occupancy();
    const Bitboard queens = byType[Queen];
    if ((bishopAttacks(sq, occ) & themBB & (byType[Bishop] | queens)) != 0) {
        return true;
    }
    if ((rookAttacks(sq, occ) & themBB & (byType[Rook] | queens)) != 0) {
        return true;
    }
    return false;
}

int FastBoard::generatePseudoLegalMoves(FastMove *moves) const {
    FastMove *out = moves;
    const FastColor us = static_cast<FastColor>(sideToMove);
    const FastColor them = oppositeColor(us);
    const Bitboard occ = occupancy();
    const Bitboard empty = ~occ;
    const Bitboard enemyTargets = byColor[them] & ~byType[King];
    const Bitboard validTargets = empty | enemyTargets;

    // 1. Pawns.
    const Bitboard ourPawns = pieces(us, Pawn);
    if (ourPawns != 0) {
        const Bitboard promoRank = us == White ? Rank8 : Rank1;
        const Bitboard doubleSourceRank = us == White ? Rank2 : Rank7;
        const int forward = us == White ? -8 : 8;

        Bitboard singlePush =
            (us == White ? (ourPawns >> 8) : (ourPawns << 8)) & empty;
        Bitboard doublePush =
            (us == White
                 ? (((ourPawns & doubleSourceRank) >> 8) & empty) >> 8
                 : (((ourPawns & doubleSourceRank) << 8) & empty) << 8) &
            empty;

        Bitboard promoPushes = singlePush & promoRank;
        singlePush &= ~promoRank;

        while (singlePush != 0) {
            const int to = popLsb(singlePush);
            *out++ = makeFastMove(to - forward, to);
        }
        while (doublePush != 0) {
            const int to = popLsb(doublePush);
            *out++ = makeFastMove(to - 2 * forward, to);
        }
        while (promoPushes != 0) {
            const int to = popLsb(promoPushes);
            emitPawnMove(out, to - forward, to, true);
        }

        const int leftDelta = us == White ? -9 : 7;
        const int rightDelta = us == White ? -7 : 9;
        Bitboard capLeft =
            (us == White ? ((ourPawns & ~FileA) >> 9)
                         : ((ourPawns & ~FileA) << 7)) &
            enemyTargets;
        Bitboard capRight =
            (us == White ? ((ourPawns & ~FileH) >> 7)
                         : ((ourPawns & ~FileH) << 9)) &
            enemyTargets;

        while (capLeft != 0) {
            const int to = popLsb(capLeft);
            emitPawnMove(out, to - leftDelta, to,
                         (squareBB(to) & promoRank) != 0);
        }
        while (capRight != 0) {
            const int to = popLsb(capRight);
            emitPawnMove(out, to - rightDelta, to,
                         (squareBB(to) & promoRank) != 0);
        }

        if (epSquare >= 0) {
            Bitboard epAttackers =
                PrecomputedAttackTables.pawn[them][epSquare] & ourPawns;
            while (epAttackers != 0) {
                const int from = popLsb(epAttackers);
                *out++ = makeFastMove(from, epSquare);
            }
        }
    }

    // 2. Knights.
    Bitboard knights = pieces(us, Knight);
    while (knights != 0) {
        const int from = popLsb(knights);
        Bitboard att = PrecomputedAttackTables.knight[from] & validTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 3. Bishops.
    Bitboard bishops = pieces(us, Bishop);
    while (bishops != 0) {
        const int from = popLsb(bishops);
        Bitboard att = bishopAttacks(from, occ) & validTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 4. Rooks.
    Bitboard rooks = pieces(us, Rook);
    while (rooks != 0) {
        const int from = popLsb(rooks);
        Bitboard att = rookAttacks(from, occ) & validTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 5. Queens.
    Bitboard queens = pieces(us, Queen);
    while (queens != 0) {
        const int from = popLsb(queens);
        Bitboard att = queenAttacks(from, occ) & validTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 6. King & Castling.
    const int kSq = kingSq[us];
    if (kSq < 64) {
        Bitboard att = PrecomputedAttackTables.king[kSq] & validTargets;
        while (att != 0) {
            *out++ = makeFastMove(kSq, popLsb(att));
        }

        if (us == White) {
            if ((castling & (WhiteKingSide | WhiteQueenSide)) != 0 &&
                !isSquareAttacked(60, Black)) {
                if ((castling & WhiteKingSide) != 0 &&
                    (occ & (squareBB(61) | squareBB(62))) == 0 &&
                    !isSquareAttacked(61, Black)) {
                    *out++ = makeFastMove(60, 62);
                }
                if ((castling & WhiteQueenSide) != 0 &&
                    (occ & (squareBB(57) | squareBB(58) | squareBB(59))) == 0 &&
                    !isSquareAttacked(59, Black)) {
                    *out++ = makeFastMove(60, 58);
                }
            }
        } else {
            if ((castling & (BlackKingSide | BlackQueenSide)) != 0 &&
                !isSquareAttacked(4, White)) {
                if ((castling & BlackKingSide) != 0 &&
                    (occ & (squareBB(5) | squareBB(6))) == 0 &&
                    !isSquareAttacked(5, White)) {
                    *out++ = makeFastMove(4, 6);
                }
                if ((castling & BlackQueenSide) != 0 &&
                    (occ & (squareBB(1) | squareBB(2) | squareBB(3))) == 0 &&
                    !isSquareAttacked(3, White)) {
                    *out++ = makeFastMove(4, 2);
                }
            }
        }
    }

    return static_cast<int>(out - moves);
}

int FastBoard::generateTacticalMoves(FastMove *moves) const {
    FastMove *out = moves;
    const FastColor us = static_cast<FastColor>(sideToMove);
    const FastColor them = oppositeColor(us);
    const Bitboard occ = occupancy();
    const Bitboard empty = ~occ;
    const Bitboard enemyTargets = byColor[them] & ~byType[King];

    // 1. Pawns: captures, en passant, and quiet promotions.
    const Bitboard ourPawns = pieces(us, Pawn);
    if (ourPawns != 0) {
        const Bitboard promoRank = us == White ? Rank8 : Rank1;
        const int forward = us == White ? -8 : 8;
        Bitboard promoPushes =
            (us == White ? (ourPawns >> 8) : (ourPawns << 8)) & empty &
            promoRank;
        while (promoPushes != 0) {
            const int to = popLsb(promoPushes);
            emitPawnMove(out, to - forward, to, true);
        }

        const int leftDelta = us == White ? -9 : 7;
        const int rightDelta = us == White ? -7 : 9;
        Bitboard capLeft =
            (us == White ? ((ourPawns & ~FileA) >> 9)
                         : ((ourPawns & ~FileA) << 7)) &
            enemyTargets;
        Bitboard capRight =
            (us == White ? ((ourPawns & ~FileH) >> 7)
                         : ((ourPawns & ~FileH) << 9)) &
            enemyTargets;

        while (capLeft != 0) {
            const int to = popLsb(capLeft);
            emitPawnMove(out, to - leftDelta, to,
                         (squareBB(to) & promoRank) != 0);
        }
        while (capRight != 0) {
            const int to = popLsb(capRight);
            emitPawnMove(out, to - rightDelta, to,
                         (squareBB(to) & promoRank) != 0);
        }

        if (epSquare >= 0) {
            Bitboard epAttackers =
                PrecomputedAttackTables.pawn[them][epSquare] & ourPawns;
            while (epAttackers != 0) {
                const int from = popLsb(epAttackers);
                *out++ = makeFastMove(from, epSquare);
            }
        }
    }

    // 2. Knights.
    Bitboard knights = pieces(us, Knight);
    while (knights != 0) {
        const int from = popLsb(knights);
        Bitboard att = PrecomputedAttackTables.knight[from] & enemyTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 3. Bishops.
    Bitboard bishops = pieces(us, Bishop);
    while (bishops != 0) {
        const int from = popLsb(bishops);
        Bitboard att = bishopAttacks(from, occ) & enemyTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 4. Rooks.
    Bitboard rooks = pieces(us, Rook);
    while (rooks != 0) {
        const int from = popLsb(rooks);
        Bitboard att = rookAttacks(from, occ) & enemyTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 5. Queens.
    Bitboard queens = pieces(us, Queen);
    while (queens != 0) {
        const int from = popLsb(queens);
        Bitboard att = queenAttacks(from, occ) & enemyTargets;
        while (att != 0) {
            *out++ = makeFastMove(from, popLsb(att));
        }
    }

    // 6. King captures.
    const int kSq = kingSq[us];
    if (kSq < 64) {
        Bitboard att = PrecomputedAttackTables.king[kSq] & enemyTargets;
        while (att != 0) {
            *out++ = makeFastMove(kSq, popLsb(att));
        }
    }

    return static_cast<int>(out - moves);
}

bool FastBoard::makeMove(FastMove move, FastUndo &undo) {
    const int from = moveFrom(move);
    const int to = moveTo(move);
    const uint8_t moved = board[from];
    const FastColor us = static_cast<FastColor>(sideToMove);
    const FastColor them = oppositeColor(us);
    const FastPiece pt = pieceByteType(moved);

    undo.movedPiece = moved;
    undo.capturedPiece = 0;
    undo.previousCastling = castling;
    undo.previousEpSquare = epSquare;
    undo.previousHalfmoveClock = halfmoveClock;
    undo.previousHash = hash;
    undo.previousPawnHash = pawnHash;
    undo.previousNonPawnCount = nonPawnCount;

    if (epSquare >= 0) {
        hash ^= FastZobrist.enPassantFile[epSquare & 7];
    }
    hash ^= FastZobrist.castling[castling];

    const Bitboard fromBit = squareBB(from);
    const Bitboard toBit = squareBB(to);

    // Remove moving piece from `from`.
    byColor[us] ^= fromBit;
    byType[pt] ^= fromBit;
    board[from] = 0;
    const quint64 fromKey = FastZobrist.pieces[us][pt][from];
    hash ^= fromKey;
    if (pt == Pawn) {
        pawnHash ^= fromKey;
    }

    const bool isEp = pt == Pawn && to == epSquare;
    const bool isCastle = pt == King && std::abs((to & 7) - (from & 7)) == 2;
    bool hasCapture = false;

    if (isEp) {
        hasCapture = true;
        const int capSq = to + (us == White ? 8 : -8);
        const Bitboard capBit = squareBB(capSq);
        undo.capturedPiece = board[capSq];
        byColor[them] ^= capBit;
        byType[Pawn] ^= capBit;
        board[capSq] = 0;
        const quint64 capKey = FastZobrist.pieces[them][Pawn][capSq];
        hash ^= capKey;
        pawnHash ^= capKey;
    } else if (board[to] != 0) {
        hasCapture = true;
        const uint8_t cap = board[to];
        const FastPiece capPt = pieceByteType(cap);
        undo.capturedPiece = cap;
        byColor[them] ^= toBit;
        byType[capPt] ^= toBit;
        const quint64 capKey = FastZobrist.pieces[them][capPt][to];
        hash ^= capKey;
        if (capPt == Pawn) {
            pawnHash ^= capKey;
        } else if (isCountedPiece(capPt)) {
            --nonPawnCount[them];
        }
    }

    if (isCastle) {
        const int homeRowShift = from & 56;
        const int rookFrom = homeRowShift + ((to & 7) > (from & 7) ? 7 : 0);
        const int rookTo = homeRowShift + ((to & 7) > (from & 7) ? 5 : 3);
        const Bitboard rookMoveBits = squareBB(rookFrom) | squareBB(rookTo);
        byColor[us] ^= rookMoveBits;
        byType[Rook] ^= rookMoveBits;
        board[rookFrom] = 0;
        board[rookTo] = makePieceByte(us, Rook);
        hash ^= FastZobrist.pieces[us][Rook][rookFrom] ^
                FastZobrist.pieces[us][Rook][rookTo];
    }

    FastPiece landPt = pt;
    if (pt == Pawn && ((to >> 3) == 0 || (to >> 3) == 7)) {
        const FastPiece promo = movePromo(move);
        landPt = promo == NoPiece ? Queen : promo;
        if (isCountedPiece(landPt)) {
            ++nonPawnCount[us];
        }
    }

    byColor[us] |= toBit;
    byType[landPt] |= toBit;
    board[to] = makePieceByte(us, landPt);
    const quint64 toKey = FastZobrist.pieces[us][landPt][to];
    hash ^= toKey;
    if (landPt == Pawn) {
        pawnHash ^= toKey;
    }

    if (pt == King) {
        kingSq[us] = static_cast<uint8_t>(to);
    }

    castling &= CastlingMask[from] & CastlingMask[to];
    hash ^= FastZobrist.castling[castling];

    if (pt == Pawn && std::abs(to - from) == 16) {
        epSquare = static_cast<int8_t>((from + to) / 2);
        hash ^= FastZobrist.enPassantFile[epSquare & 7];
    } else {
        epSquare = -1;
    }

    halfmoveClock = (pt == Pawn || hasCapture) ? 0 : (halfmoveClock + 1);
    sideToMove = them;
    hash ^= FastZobrist.sideToMove;

    if (kingSq[us] < 64 && isSquareAttacked(kingSq[us], them)) {
        unmakeMove(move, undo);
        return false;
    }
    return true;
}

void FastBoard::unmakeMove(FastMove move, const FastUndo &undo) {
    const int from = moveFrom(move);
    const int to = moveTo(move);
    const FastPiece movedPt = pieceByteType(undo.movedPiece);
    const FastColor us = pieceByteColor(undo.movedPiece);
    const FastColor them = oppositeColor(us);
    const FastPiece landPt = pieceByteType(board[to]);

    const Bitboard fromBit = squareBB(from);
    const Bitboard toBit = squareBB(to);

    byColor[us] ^= toBit;
    byType[landPt] ^= toBit;
    board[to] = 0;

    byColor[us] |= fromBit;
    byType[movedPt] |= fromBit;
    board[from] = undo.movedPiece;

    if (movedPt == King) {
        kingSq[us] = static_cast<uint8_t>(from);
    }

    if (movedPt == Pawn && to == undo.previousEpSquare) {
        const int capSq = to + (us == White ? 8 : -8);
        const Bitboard capBit = squareBB(capSq);
        byColor[them] |= capBit;
        byType[Pawn] |= capBit;
        board[capSq] = undo.capturedPiece;
    } else if (undo.capturedPiece != 0) {
        const FastPiece capPt = pieceByteType(undo.capturedPiece);
        byColor[them] |= toBit;
        byType[capPt] |= toBit;
        board[to] = undo.capturedPiece;
    }

    if (movedPt == King && std::abs((to & 7) - (from & 7)) == 2) {
        const int homeRowShift = from & 56;
        const int rookFrom = homeRowShift + ((to & 7) > (from & 7) ? 7 : 0);
        const int rookTo = homeRowShift + ((to & 7) > (from & 7) ? 5 : 3);
        const Bitboard rookMoveBits = squareBB(rookFrom) | squareBB(rookTo);
        byColor[us] ^= rookMoveBits;
        byType[Rook] ^= rookMoveBits;
        board[rookTo] = 0;
        board[rookFrom] = makePieceByte(us, Rook);
    }

    sideToMove = us;
    castling = undo.previousCastling;
    epSquare = undo.previousEpSquare;
    halfmoveClock = undo.previousHalfmoveClock;
    hash = undo.previousHash;
    pawnHash = undo.previousPawnHash;
    nonPawnCount = undo.previousNonPawnCount;
}

bool FastBoard::makeNullMove(FastNullUndo &undo) {
    if (isInCheck(static_cast<FastColor>(sideToMove))) {
        return false;
    }
    undo.previousEpSquare = epSquare;
    undo.previousHash = hash;
    if (epSquare >= 0) {
        hash ^= FastZobrist.enPassantFile[epSquare & 7];
        epSquare = -1;
    }
    sideToMove = oppositeColor(static_cast<FastColor>(sideToMove));
    hash ^= FastZobrist.sideToMove;
    return true;
}

void FastBoard::unmakeNullMove(const FastNullUndo &undo) {
    sideToMove = oppositeColor(static_cast<FastColor>(sideToMove));
    epSquare = undo.previousEpSquare;
    hash = undo.previousHash;
}

bool FastBoard::hasAnyLegalMove() const {
    FastBoard probe = *this;
    FastMove moves[256];
    const int count = probe.generatePseudoLegalMoves(moves);
    for (int i = 0; i < count; ++i) {
        FastUndo undo;
        if (probe.makeMove(moves[i], undo)) {
            probe.unmakeMove(moves[i], undo);
            return true;
        }
    }
    return false;
}

int seeValueFast(const FastBoard &pos, FastMove move) {
    const int from = moveFrom(move);
    const int to = moveTo(move);
    const uint8_t moverByte = pos.board[from];
    if (moverByte == 0) {
        return 0;
    }
    const FastPiece moverPt = pieceByteType(moverByte);
    const FastColor us = pieceByteColor(moverByte);
    const uint8_t victimByte = pos.board[to];
    const bool enPassant = victimByte == 0 && moverPt == Pawn &&
                           (from & 7) != (to & 7);

    const int victimValue =
        victimByte != 0
            ? pieceTypeValue(pieceByteType(victimByte))
            : (enPassant ? pieceTypeValue(Pawn) : 0);

    FastPiece landingPt = moverPt;
    if (const FastPiece promo = movePromo(move); promo != NoPiece) {
        landingPt = promo;
    } else if (moverPt == Pawn && ((to >> 3) == 0 || (to >> 3) == 7)) {
        landingPt = Queen;
    }

    Bitboard occ = pos.occupancy() ^ squareBB(from);
    if (enPassant) {
        const int capSq = to + (us == White ? 8 : -8);
        occ ^= squareBB(capSq);
    }

    const auto &t = PrecomputedAttackTables;
    const Bitboard diagSliders = pos.byType[Bishop] | pos.byType[Queen];
    const Bitboard orthoSliders = pos.byType[Rook] | pos.byType[Queen];

    // Kings are excluded from SEE exchanges to match Rules-based SEE semantics.
    Bitboard attackers =
        ((t.pawn[Black][to] & pos.pieces(White, Pawn)) |
         (t.pawn[White][to] & pos.pieces(Black, Pawn)) |
         (t.knight[to] & pos.byType[Knight]) |
         (bishopAttacks(to, occ) & diagSliders) |
         (rookAttacks(to, occ) & orthoSliders)) &
        occ;

    constexpr int MaxSeePlies = 32;
    int gain[MaxSeePlies + 1] = {};
    gain[0] = victimValue;
    int onSquareValue = pieceTypeValue(landingPt);
    int depth = 0;
    FastColor side = oppositeColor(us);

    while (depth < MaxSeePlies) {
        const Bitboard sideAttackers = attackers & pos.byColor[side];
        if (sideAttackers == 0) {
            break;
        }

        FastPiece attPt = NoPiece;
        Bitboard subset = 0;
        for (const FastPiece pt : {Pawn, Knight, Bishop, Rook, Queen}) {
            subset = sideAttackers & pos.byType[pt];
            if (subset != 0) {
                attPt = pt;
                break;
            }
        }
        if (attPt == NoPiece) {
            break;
        }

        ++depth;
        gain[depth] = onSquareValue - gain[depth - 1];
        onSquareValue = pieceTypeValue(attPt);

        const int attSq = std::countr_zero(subset);
        occ ^= squareBB(attSq);
        if (attPt == Pawn || attPt == Bishop || attPt == Queen) {
            attackers |= bishopAttacks(to, occ) & diagSliders;
        }
        if (attPt == Rook || attPt == Queen) {
            attackers |= rookAttacks(to, occ) & orthoSliders;
        }
        attackers &= occ;
        side = oppositeColor(side);
    }

    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }
    return gain[0];
}

} // namespace HeuristicSearch
