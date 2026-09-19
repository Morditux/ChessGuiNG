#ifndef CHESSGUI_WINPROBABILITY_H
#define CHESSGUI_WINPROBABILITY_H

//
// The one place a chess score becomes a winning chance. The evaluation gauge,
// the whole-game curve, the engine score display and the accuracy of the game
// report all read it, so two panels can never disagree about the same position.
//

#include <algorithm>
#include <cmath>
#include <optional>

namespace WinProbability {

// A score at or beyond this many centipawns is a decided game: it is what a
// mate score looks like once the mate itself is off the board.
constexpr double DecidedScore = 10000.0;

// The logistic every model below shares. The 0.003682 slope is the calibrated
// centipawn-to-winning-chance curve the report's accuracy was built on.
constexpr double Slope = 0.003682;

// Winning chance of the side the score belongs to, in [0, 100].
[[nodiscard]] inline double fromCentipawns(double centipawns) {
    if (centipawns >= DecidedScore) {
        return 100.0;
    }
    if (centipawns <= -DecidedScore) {
        return 0.0;
    }
    return std::clamp(100.0 / (1.0 + std::exp(-Slope * centipawns)), 0.0,
                      100.0);
}

// Same, for a score that may come with a mate distance: a forced mate is a
// decided game whatever the centipawns say, and a mate already on the board
// (distance zero) is only a drawn game, since the side to move has been mated
// or has mated and the score is read from the other side's point of view.
[[nodiscard]] inline double fromScore(double centipawns,
                                      std::optional<int> mateIn = std::nullopt) {
    if (mateIn.has_value()) {
        if (*mateIn > 0) {
            return 100.0;
        }
        if (*mateIn < 0) {
            return 0.0;
        }
        return 50.0;
    }
    return fromCentipawns(centipawns);
}

} // namespace WinProbability

#endif // CHESSGUI_WINPROBABILITY_H
