//
// Classical heuristic evaluation for chess positions.
//

#ifndef CHESSGUI_HEURISTIC_EVAL_H
#define CHESSGUI_HEURISTIC_EVAL_H

#include "rules.h"

// Stateless, UI-independent classical evaluator.
class HeuristicEval {
public:
    static constexpr int MateScore = 10000;

    // Evaluates the position from White's perspective in centipawns
    // (+ for White, - for Black).
    [[nodiscard]] static int evaluateCentipawns(const Rules &rules);

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
