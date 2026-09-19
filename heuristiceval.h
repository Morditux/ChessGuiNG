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

    // Every tunable weight of the classical evaluation, in centipawns unless
    // the name says otherwise. Percentages scale a term that is already
    // computed. `params()` returns the active set and `setParams()` replaces
    // it, so the evaluator can be calibrated without editing the terms.
    struct EvalParams {
        // Material.
        int pawnValue = 100;
        int knightValue = 320;
        int bishopValue = 330;
        int rookValue = 500;
        int queenValue = 900;

        // Side to move.
        int tempoBonus = 10;
        int inCheckPenalty = 50;

        // Pawn structure.
        int doubledPawnPenalty = 12;
        int isolatedPawnPenalty = 15;
        int supportedPawnBonus = 8;
        int backwardPawnPenalty = 10;
        int candidatePawnBonus = 8;
        int candidatePawnRankBonus = 2;
        int passedPawnBase = 12;
        int passedPawnRankBonus = 6;
        int passedPawnEndgameScalePercent = 150;
        int passedPawnBlockedPenalty = 8;
        int passedPawnControlledPenalty = 4;
        int passedPawnRookBehind = 20;

        // Mobility weights (middlegame, endgame).
        int mobilityPawnMG = 1;
        int mobilityPawnEG = 1;
        int mobilityKnightMG = 4;
        int mobilityKnightEG = 4;
        int mobilityBishopMG = 4;
        int mobilityBishopEG = 5;
        int mobilityRookMG = 2;
        int mobilityRookEG = 4;
        int mobilityQueenMG = 1;
        int mobilityQueenEG = 2;
        int mobilityKingMG = 2;
        int mobilityKingEG = 1;

        // King safety.
        int castledBonus = 15;
        int castlingRightsBonus = 10;
        int kingAttackPenalty = 12;
        int kingShelterBonus = 8;
        int kingShelterSecondRankPercent = 75;
        int kingShelterThirdRankPercent = 50;
        int openFileNearKingPenalty = 12;
        int kingPawnStormPenalty = 6;

        // Rooks.
        int rookOpenFileBonus = 20;
        int rookSemiOpenFileBonus = 10;
        int rookSeventhRankBonus = 20;

        // Threats.
        int hangingPieceDivisor = 8;
        int cheapAttackerMargin = 50;
        int cheapAttackerDivisor = 16;

        // Piece specific.
        int bishopPairMG = 30;
        int bishopPairEG = 50;
        int outpostKnight = 20;
        int outpostBishop = 10;
        int badBishopPenalty = 3;
        int badBishopCap = 20;
        int connectedRooksBonus = 15;
    };

    // Active weights. Configure them before starting a search: reading is not
    // synchronised against a concurrent setParams() call.
    [[nodiscard]] static const EvalParams &params();
    static void setParams(const EvalParams &params);
    static void resetParams();

    // Outcome of a shallow search around the classical evaluation. The score
    // is always from White's perspective; mateIn is in full moves, positive
    // when White delivers mate.
    struct SearchResult {
        int centipawns = 0;
        std::optional<int> mateIn;
    };

    // Evaluates the position from White's perspective in centipawns
    // (+ for White, - for Black). A drawn position — checkmate excluded — is
    // worth exactly zero: stalemate, material the rules engine calls
    // insufficient, the fifty-move rule and threefold repetition.
    [[nodiscard]] static int evaluateCentipawns(const Rules &rules);

    // Classical evaluation refined by a short alpha-beta search with a
    // quiescence extension. `depth` counts the plies searched before the
    // quiescence search, so depth 2 looks one move ahead for each side and
    // then resolves captures. Searches of depth 3 or more split the root moves
    // across a persistent worker pool; from depth 4 they deepen iteratively
    // with aspiration windows around the previous score. Detects mates inside
    // the search horizon and reports them through `mateIn`, and scores the
    // fifty-move and repetition draws it meets inside the horizon as draws
    // without ever letting them hide a checkmate.
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
    // material balance it displays cannot drift from the evaluation. It follows
    // the active EvalParams, so tuning the weights moves the displayed balance
    // with them. Kings and empty squares are worth nothing.
    [[nodiscard]] static int pieceValue(Rules::PieceType type);
};

#endif // CHESSGUI_HEURISTIC_EVAL_H
