//
// Background worker for the heuristic evaluation: the whole-game curve and the
// refinement of the live evaluation.
//
// The heuristic search is cheap per position but the curve runs one search per
// ply, so doing it on the GUI thread froze the interface as soon as the
// evaluation depth grew. The worker replays the game and reports its progress,
// and every search it starts carries a `SearchLimits::shouldStop` callback that
// the caller can trip from the GUI thread.
//

#ifndef CHESSGUI_CURVEWORKER_H
#define CHESSGUI_CURVEWORKER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <optional>

#include "heuristiceval.h"
#include "rules.h"

class CurveWorker : public QObject {
    Q_OBJECT

public:
    explicit CurveWorker(QObject *parent = nullptr);

public slots:
    // White's display percentage after every ply of the game described by
    // `initialFen` and `uciMoves`, starting position included. Progress is
    // reported ply by ply; a request that is cancelled (or superseded) reports
    // `cancelled` instead of a curve.
    void computeCurve(quint64 requestId, const QString &initialFen,
                      const QStringList &uciMoves, int depth,
                      int quiescenceDepth);

    // Refines one position with a search at `depth`, for the live evaluation
    // and the move hint.
    void evaluatePosition(quint64 requestId, const QString &fen, int depth,
                          int quiescenceDepth);

    // Asks the running job to stop. Both of these only touch atomic flags, so
    // the GUI thread calls them directly (not through a queued connection)
    // even while a search is running, which is what makes cancelling - and
    // shutting the thread down - prompt.
    void cancelRequest(quint64 requestId);
    void stop();
    [[nodiscard]] bool isBusy() const;

signals:
    void progress(quint64 requestId, int completed, int total);
    void curveReady(quint64 requestId, const QVector<double> &curve);
    void evaluationReady(quint64 requestId, int centipawns, bool hasMate,
                         int mateIn, const std::optional<Rules::Move> &bestMove);
    void cancelled(quint64 requestId);

private:
    void beginRequest(quint64 requestId);
    void endRequest(quint64 requestId);
    // Limits handed to HeuristicEval::search(); the callback is what makes the
    // search give up quickly when the request is cancelled.
    [[nodiscard]] HeuristicEval::SearchLimits limitsFor(quint64 requestId) const;

    // Request the running job belongs to, and the flag it polls. Both are
    // atomic because the GUI thread cancels while this thread works.
    std::atomic<quint64> activeRequest_{0};
    std::atomic<bool> stopRequested_{false};
    // A cancel that arrives before the job started, so that cancelling a
    // request that is still queued is not lost.
    std::atomic<quint64> pendingCancel_{0};
};

#endif // CHESSGUI_CURVEWORKER_H
