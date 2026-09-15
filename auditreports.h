//
// Data produced by the whole-game audit and rendered as an analysis report.
// It is UI independent so that the controller builds it and a dialog shows it.
//

#ifndef CHESSGUI_AUDITREPORTS_H
#define CHESSGUI_AUDITREPORTS_H

#include <QString>
#include <QVector>

#include "pgnannotations.h"

// One move flagged by the audit.
struct AuditFinding {
    AuditSeverity severity = AuditSeverity::None;
    // 1-based ply of the flagged move.
    int ply = 0;
    // Centipawns lost with the played move, 0 when the loss is a forced mate.
    int centipawnLoss = 0;
    // Best alternative in UCI notation, the form stored in the PGN annotation.
    QString bestMove;
    bool forcedMate = false;
    // Display form of bestMove, filled for the report only.
    QString bestMoveSan;
};

// Per-player aggregation over the analysed main line.
struct AuditPlayerStats {
    int inaccuracies = 0;
    int mistakes = 0;
    int blunders = 0;
    // Moves the engine could score, and moves whose loss is a centipawn value.
    int scoredMoves = 0;
    int centipawnMoves = 0;
    qint64 totalCentipawnLoss = 0;
    double averageCentipawnLoss = 0.0;
    // Mean accuracy of the player's moves, in percent.
    double accuracy = 0.0;
};

// Enriched result of a whole-game audit.
struct AuditReport {
    // False until an audit produced a usable report for the current game.
    bool valid = false;
    // Search depth the audit was run with.
    int depth = 0;
    // Number of main-line plies the report covers.
    int plies = 0;
    // Positions the engine actually had to score (plies + 1).
    int analysedPositions = 0;
    AuditPlayerStats white;
    AuditPlayerStats black;
    // Standard algebraic notation of each ply, indexed by ply.
    QVector<QString> sanByPly;
    // Centipawn loss of each ply, -1 when a centipawn value cannot express it.
    QVector<int> centipawnLossByPly;
    QVector<AuditFinding> findings;

    [[nodiscard]] int findingCount() const { return findings.size(); }
};

namespace AuditReports {

// Display accuracy in percent for one move, from the winning percentage its
// player lost between the best continuation and the move played. The curve is
// the published accuracy fit for average centipawn loss data: it is a display
// proxy, not a proven probability.
[[nodiscard]] double moveAccuracy(double winningPercentageLoss);

} // namespace AuditReports

#endif // CHESSGUI_AUDITREPORTS_H
