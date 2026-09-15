//
// Helpers for the enriched game-analysis report.
//

#include "auditreports.h"

#include <algorithm>
#include <cmath>

namespace {
    // Fit of the published accuracy data against the winning percentage lost
    // by a move: a loss of zero percent is a perfect move and the accuracy
    // falls off quickly as the loss grows.
    constexpr double AccuracyScale = 103.1668;
    constexpr double AccuracyDecay = 0.04354;
    constexpr double AccuracyOffset = 3.1669;
}

namespace AuditReports {

double moveAccuracy(double winningPercentageLoss) {
    const double loss = std::max(0.0, winningPercentageLoss);
    const double accuracy =
        AccuracyScale * std::exp(-AccuracyDecay * loss) - AccuracyOffset;
    return std::clamp(accuracy, 0.0, 100.0);
}

} // namespace AuditReports
