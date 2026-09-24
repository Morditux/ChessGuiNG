//
// Classical heuristic evaluation for chess positions.
//

#ifndef CHESSGUI_HEURISTIC_EVAL_H
#define CHESSGUI_HEURISTIC_EVAL_H

#include <functional>
#include <optional>
#include <vector>

#include "rules.h"

// Stateless, UI-independent classical evaluator.
class HeuristicEval {
public:
    static constexpr int MateScore = 10000;

    // Depth the evaluator is normally searched at, and the deepest one callers
    // are expected to configure for the live evaluation (deeper searches see
    // more tactics but make a whole-game curve much slower).
    static constexpr int DefaultSearchDepth = 2;
    static constexpr int MaxSearchDepth = 8;

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

        // Space: safe central squares a side's pawns control in the enemy
        // half, per square.
        int spaceBonusMG = 2;
        int spaceBonusEG = 1;

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
        // Per attacked square near the king, weighted by the strongest enemy
        // attacker on that square (`kingAttack*Percent` are percentages of this
        // penalty, so 100 keeps a knight or bishop attack at its old value).
        int kingAttackPenalty = 12;
        int kingAttackPawnPercent = 50;
        int kingAttackKnightPercent = 100;
        int kingAttackBishopPercent = 100;
        int kingAttackRookPercent = 150;
        int kingAttackQueenPercent = 250;
        int kingShelterBonus = 8;
        // The pawn directly in front of the king shields it best; the pawns on
        // the adjacent files are worth this percentage of the same bonus.
        int kingShelterAdjacentFilePercent = 60;
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

    // Limits a search has to respect. Zero disables the matching limit, and
    // `shouldStop` lets a caller cancel a search from another thread; it is
    // polled while the search runs.
    struct SearchLimits {
        int maxNodes = 0;
        int maxMilliseconds = 0;
        std::function<bool()> shouldStop;
    };

    // Per-term contributions of the classical evaluation, from White's
    // perspective and already tapered with the phase. It is what the UI can
    // show to explain a score, and what a test can use to pin one term without
    // the others. The terms do not sum to `total` when a draw scaling applies.
    struct EvalBreakdown {
        int material = 0;
        int placement = 0;
        int pawns = 0;
        int space = 0;
        int mobility = 0;
        int kingSafety = 0;
        int threats = 0;
        int outposts = 0;
        int badBishops = 0;
        int connectedRooks = 0;
        int mopUp = 0;
        int tempo = 0;
        int inCheck = 0;
        // Final score, i.e. what evaluateCentipawns() returns for the same
        // position: `±MateScore` for a checkmate and zero for a draw.
        int total = 0;
    };

    // Outcome of a shallow search around the classical evaluation. The score
    // is always from White's perspective; mateIn is in full moves, positive
    // when White delivers mate.
    struct SearchResult {
        int centipawns = 0;
        std::optional<int> mateIn;
        // Best root move, when at least one legal move was searched.
        std::optional<Rules::Move> bestMove;
        // Best line found, starting with `bestMove`. After the first move it is
        // read back from the transposition table, so it is a best-effort line
        // and holds only the root move when the search ran without a table
        // (depth below 3).
        std::vector<Rules::Move> principalVariation;
        // Deepest iteration the search completed.
        int depth = 0;
        // Nodes visited by this call, main search and quiescence.
        int nodes = 0;
        // True when a limit stopped the search before it reached `depth`.
        bool aborted = false;
    };

    // Evaluates the position from White's perspective in centipawns
    // (+ for White, - for Black). A drawn position — checkmate excluded — is
    // worth exactly zero: stalemate, material the rules engine calls
    // insufficient, the fifty-move rule and threefold repetition.
    [[nodiscard]] static int evaluateCentipawns(const Rules &rules);

    // Same evaluation, with the contribution of every term. A terminal or
    // drawn position reports only `total`.
    [[nodiscard]] static EvalBreakdown evaluateBreakdown(const Rules &rules);

    // Classical evaluation refined by a short alpha-beta search with a
    // quiescence extension. `depth` counts the plies searched before the
    // quiescence search, so depth 2 looks one move ahead for each side and
    // then resolves captures. Searches of depth 3 or more split the root moves
    // across a persistent worker pool; from depth 4 they deepen iteratively
    // with aspiration windows around the previous score. Detects mates inside
    // the search horizon and reports them through `mateIn`, and scores the
    // fifty-move and repetition draws it meets inside the horizon as draws
    // without ever letting them hide a checkmate.
    [[nodiscard]] static SearchResult search(
        const Rules &rules, int depth = DefaultSearchDepth,
        int quiescenceDepth = 2);

    // The same search under explicit limits, which stop it early; the result
    // then keeps the last iteration that completed and reports `aborted`. A
    // depth above the search horizon is clamped to it rather than refused.
    [[nodiscard]] static SearchResult search(const Rules &rules, int depth,
                                             int quiescenceDepth,
                                             const SearchLimits &limits);

    // Converts the centipawn score into a display percentage for White.
    // This is a display proxy, not a proven win probability.
    [[nodiscard]] static double evaluateDisplayPercentage(const Rules &rules);

    // Winning chance of the side the score belongs to, in [0.0, 100.0], from
    // the one model the gauge, the curve, the engine score display and the
    // accuracy of the game report share (see winprobability.h), so two panels
    // cannot disagree about the same position.
    [[nodiscard]] static double centipawnsToPercentage(double cp);

    // Classical piece value in centipawns, shared with the UI so that the
    // material balance it displays cannot drift from the evaluation. It follows
    // the active EvalParams, so tuning the weights moves the displayed balance
    // with them. Kings and empty squares are worth nothing.
    [[nodiscard]] static int pieceValue(Rules::PieceType type);
};

#endif // CHESSGUI_HEURISTIC_EVAL_H
