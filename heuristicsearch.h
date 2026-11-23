#ifndef CHESSGUI_HEURISTICSEARCH_H
#define CHESSGUI_HEURISTICSEARCH_H

//
// Internal interface between the classical evaluation (heuristiceval.cpp) and
// the search built around it (heuristicsearch.cpp). Nothing here is part of the
// evaluator's public API, and no other file should include it.
//

#include "fastboard.h"
#include "heuristiceval.h"
#include "rules.h"

#include <array>
#include <optional>

namespace HeuristicSearch {

// Moves a position can hold, which sizes every move buffer the evaluator and
// the search hand to FastBoard::generatePseudoLegalMoves.
constexpr int MaxMoves = 256;

// +1 for White, -1 for Black: turns a score from White's point of view into the
// side to move's, which is what a negamax search needs.
[[nodiscard]] constexpr int signFor(Rules::Color color) {
    return color == Rules::Color::White ? 1 : -1;
}

[[nodiscard]] constexpr int signFor(FastColor color) {
    return color == White ? 1 : -1;
}

// The static score of a position (`pos` holds its incremental Zobrist and pawn
// keys), in centipawns from White's point of view, and the same value in
// `breakdown` with its per-term contributions when the caller wants them.
int evaluateBoard(const FastBoard &pos, bool inCheck,
                  HeuristicEval::EvalBreakdown *breakdown);

// Drops the cache of pawn-structure scores the evaluation keeps by pawn key,
// which the search does together with its own caches.
void clearPawnCache();

// True when the side to move has at least one legal move, without the full scan
// `Rules::hasLegalMove` would do.
[[nodiscard]] bool hasAnyLegalMove(const Rules &rules);

// Static exchange evaluation of `move`: material the exchange is worth for the mover.
[[nodiscard]] int seeValue(const Rules &rules, const Rules::Move &move);

// Ordering score for a move, used by the search and testable in unit tests.
[[nodiscard]] int moveOrderingScore(const Rules &rules, const Rules::Move &move,
                                    const Rules::Move &tableMove = {},
                                    const Rules::Move &killer0 = {},
                                    const Rules::Move &killer1 = {});

// Test helpers to inspect transposition table behavior.
void storeTransposition(quint64 key, int depth, int score, int flag,
                        const Rules::Move &move);
// Writes the slot of `key` the way two workers interleaving on it would leave
// it: the key word of the first entry with the payload of the second. A probe
// has to reject it rather than hand the second payload to the first position.
void storeTornTransposition(quint64 key, int depth, int score, int flag,
                            const Rules::Move &move, int otherDepth, int otherScore, int otherFlag,
                            const Rules::Move &otherMove);
[[nodiscard]] bool probeTranspositionMove(quint64 key, Rules::Move &outMove);

// Taken by the evaluator: drops the transposition table and the evaluation
// cache, which it must do whenever the weights change because every cached
// score was computed with the previous ones.
void clearCaches();

} // namespace HeuristicSearch

#endif // CHESSGUI_HEURISTICSEARCH_H
