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
    // the name says otherwise. Each term carries a middle-game and an
    // end-game value (`MG`/`EG`) and the evaluator interpolates between them
    // with the phase; a term that does not depend on the phase uses one value
    // for both. Percentages scale a term that is already computed. `params()`
    // returns the active set and `setParams()` replaces it, so the evaluator
    // can be calibrated without editing the terms.
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
        int doubledPawnPenaltyMG = 12;
        int doubledPawnPenaltyEG = 18;
        int isolatedPawnPenaltyMG = 15;
        int isolatedPawnPenaltyEG = 18;
        int supportedPawnBonusMG = 8;
        int supportedPawnBonusEG = 5;
        int backwardPawnPenaltyMG = 10;
        int backwardPawnPenaltyEG = 12;
        int candidatePawnBonusMG = 8;
        int candidatePawnBonusEG = 12;
        int candidatePawnRankBonusMG = 2;
        int candidatePawnRankBonusEG = 3;
        int passedPawnBaseMG = 12;
        int passedPawnBaseEG = 25;
        int passedPawnRankBonusMG = 6;
        int passedPawnRankBonusEG = 15;
        int passedPawnBlockedPenaltyMG = 8;
        int passedPawnBlockedPenaltyEG = 15;
        int passedPawnControlledPenaltyMG = 4;
        int passedPawnControlledPenaltyEG = 10;
        int passedPawnRookBehindMG = 20;
        int passedPawnRookBehindEG = 30;
        int passedPawnEnemyRookBehindMG = 15;
        int passedPawnEnemyRookBehindEG = 25;

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

        // King safety (middlegame terms; the shelter keeps a smaller endgame
        // value so that a king still prefers pawns in front of it).
        int castledBonus = 15;
        int castlingRightsBonus = 10;
        int kingAttackPenalty = 12;
        int kingShelterBonus = 8;
        int kingShelterSecondRankPercent = 75;
        int kingShelterThirdRankPercent = 50;
        int kingShelterEndgamePercent = 50;
        int openFileNearKingPenalty = 12;
        int kingPawnStormPenalty = 6;

        // Rooks.
        int rookOpenFileMG = 20;
        int rookOpenFileEG = 10;
        int rookSemiOpenFileMG = 10;
        int rookSemiOpenFileEG = 8;
        int rookSeventhRankMG = 20;
        int rookSeventhRankEG = 30;

        // Endgame mop-up: with an advantage of at least `mopUpMaterialThreshold`
        // centipawns and few pieces left, the stronger side gains by driving
        // the enemy king to the edge and by walking its own king in.
        int mopUpMaterialThreshold = 400;
        int mopUpEdgeBonus = 8;
        int mopUpKingProximityBonus = 3;

        // Threats.
        int hangingPieceDivisor = 8;
        int cheapAttackerMargin = 50;
        int cheapAttackerDivisor = 16;

        // Piece specific.
        int bishopPairMG = 30;
        int bishopPairEG = 50;
        int outpostKnightMG = 20;
        int outpostKnightEG = 10;
        int outpostBishopMG = 10;
        int outpostBishopEG = 5;
        int badBishopPenalty = 3;
        int badBishopCap = 20;
        int connectedRooksMG = 30;
        int connectedRooksEG = 20;
    };

    // Active weights. Configure them before starting a search: reading is not
    // synchronised against a concurrent setParams() call. Replacing the weights
    // also drops the cached search results, which were computed with them.
    [[nodiscard]] static const EvalParams &params();
    static void setParams(const EvalParams &params);
    static void resetParams();

    // Upper bound on the root workers a search may use. Zero (the default)
    // derives the number from the hardware and the CHESSGUI_EVAL_THREADS
    // environment variable, one disables the parallel search so its result no
    // longer depends on thread scheduling, and any larger value caps the
    // parallelism without creating more threads than the pool has.
    static void setSearchThreads(int threads);
    [[nodiscard]] static int searchThreads();

    // Drops the persistent transposition table that `search()` reuses between
    // calls. setParams() and resetParams() already do this; it is exposed so a
    // benchmark can compare a cold search with a warm one. Like them, it must
    // not run while a search is in flight on another thread.
    static void clearSearchCache();

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
