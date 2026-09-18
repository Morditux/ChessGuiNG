//
// Classical heuristic evaluation for chess positions.
//

#ifndef CHESSGUI_HEURISTIC_EVAL_H
#define CHESSGUI_HEURISTIC_EVAL_H

#include <optional>

#include "rules.h"

// Stateless, UI-independent classical evaluator.
class HeuristicEval {
public:
    static constexpr int MateScore = 10000;

    // Outcome of a shallow search around the classical evaluation. The score
    // is always from White's perspective; mateIn is in full moves, positive
    // when White delivers mate.
    struct SearchResult {
        int centipawns = 0;
        std::optional<int> mateIn;
    };

    // Evaluates the position from White's perspective in centipawns
    // (+ for White, - for Black).
    [[nodiscard]] static int evaluateCentipawns(const Rules &rules);

    // Classical evaluation refined by a short alpha-beta search with a
    // quiescence extension. `depth` counts the plies searched before the
    // quiescence search, so depth 2 looks one move ahead for each side and
    // then resolves captures. Searches of depth 3 or more split the root moves
    // across a persistent worker pool; from depth 4 they deepen iteratively
    // with aspiration windows around the previous score. Detects mates inside
    // the search horizon and reports them through `mateIn`.
    [[nodiscard]] static SearchResult search(const Rules &rules, int depth = 2,
                                             int quiescenceDepth = 2);

    // Converts the centipawn score into a display percentage for White.
    // This is a display proxy, not a proven win probability.
    [[nodiscard]] static double evaluateDisplayPercentage(const Rules &rules);

    // Convenience evaluation for EvaluationBar.
    [[nodiscard]] static double evaluate(const Rules &rules);

    // Converts a centipawn score to a display percentage in [0.0, 100.0].
    [[nodiscard]] static double centipawnsToPercentage(double cp);

    // Classical piece value in centipawns, shared with the UI so that the
    // material balance it displays cannot drift from the evaluation. Kings
    // and empty squares are worth nothing.
    [[nodiscard]] static int pieceValue(Rules::PieceType type);
};

#endif // CHESSGUI_HEURISTIC_EVAL_H
