//
// Background worker for the heuristic evaluation: the whole-game curve and the
// refinement of the live evaluation.
//

#include "curveworker.h"

#include <QMetaType>

#include "uciparser.h"

CurveWorker::CurveWorker(QObject *parent)
    : QObject(parent) {
    // The results cross a thread boundary, so their types have to be known to
    // the meta-object system.
    qRegisterMetaType<Rules::Move>("Rules::Move");
    qRegisterMetaType<std::optional<Rules::Move>>("std::optional<Rules::Move>");
    qRegisterMetaType<QVector<double>>("QVector<double>");
}

void CurveWorker::beginRequest(quint64 requestId) {
    // A request that was cancelled while it was still queued must not run.
    const quint64 cancelled =
        pendingCancel_.exchange(0, std::memory_order_relaxed);
    stopRequested_.store(cancelled == requestId, std::memory_order_relaxed);
    activeRequest_.store(requestId, std::memory_order_relaxed);
}

void CurveWorker::endRequest(quint64 requestId) {
    quint64 current = requestId;
    activeRequest_.compare_exchange_strong(current, 0,
                                           std::memory_order_relaxed);
    stopRequested_.store(false, std::memory_order_relaxed);
}

HeuristicEval::SearchLimits CurveWorker::limitsFor(quint64 requestId) const {
    HeuristicEval::SearchLimits limits;
    limits.shouldStop = [this, requestId] {
        return stopRequested_.load(std::memory_order_relaxed) ||
               activeRequest_.load(std::memory_order_relaxed) != requestId;
    };
    return limits;
}

void CurveWorker::computeCurve(quint64 requestId, const QString &initialFen,
                               const QStringList &uciMoves, int depth,
                               int quiescenceDepth) {
    beginRequest(requestId);

    Rules replay;
    if (initialFen.isEmpty() || !replay.loadFen(initialFen)) {
        replay.reset();
    }

    const int total = uciMoves.size() + 1;
    const HeuristicEval::SearchLimits limits = limitsFor(requestId);
    QVector<double> curve;
    curve.reserve(total);

    const auto point = [this, depth, quiescenceDepth,
                        &limits](const Rules &position) {
        const HeuristicEval::SearchResult result =
            HeuristicEval::search(position, depth, quiescenceDepth, limits);
        return HeuristicEval::centipawnsToPercentage(result.centipawns);
    };

    curve.append(point(replay));
    emit progress(requestId, 1, total);

    for (const QString &uci : uciMoves) {
        // The position the point belongs to changed, so the request is over:
        // whoever superseded it is responsible for the curve now.
        if (limits.shouldStop()) {
            endRequest(requestId);
            emit cancelled(requestId);
            return;
        }

        const auto move = UciParser::parseMove(uci);
        if (!move.has_value() || !replay.tryMove(*move)) {
            break;
        }
        curve.append(point(replay));
        emit progress(requestId, curve.size(), total);
    }

    // The search reports a cancellation through its result as well, which is
    // only visible after the call; asking the flag again covers it.
    const bool wasCancelled = limits.shouldStop();
    endRequest(requestId);
    if (wasCancelled) {
        emit cancelled(requestId);
        return;
    }
    emit curveReady(requestId, curve);
}

void CurveWorker::evaluatePosition(quint64 requestId, const QString &fen,
                                   int depth, int quiescenceDepth) {
    beginRequest(requestId);

    Rules rules;
    if (!rules.loadFen(fen)) {
        endRequest(requestId);
        emit cancelled(requestId);
        return;
    }

    const HeuristicEval::SearchResult result =
        HeuristicEval::search(rules, depth, quiescenceDepth,
                              limitsFor(requestId));
    const bool wasCancelled = stopRequested_.load(std::memory_order_relaxed);
    endRequest(requestId);
    if (wasCancelled) {
        emit cancelled(requestId);
        return;
    }

    emit evaluationReady(requestId, result.centipawns, result.mateIn.has_value(),
                         result.mateIn.value_or(0), result.bestMove);
}

void CurveWorker::cancelRequest(quint64 requestId) {
    // Only the request that is actually running may be stopped: a cancel that
    // arrives after its job finished must not kill the next one.
    if (activeRequest_.load(std::memory_order_relaxed) == requestId) {
        stopRequested_.store(true, std::memory_order_relaxed);
        return;
    }
    // The job may still be queued; remember the request so that it gives up as
    // soon as it starts.
    pendingCancel_.store(requestId, std::memory_order_relaxed);
}

void CurveWorker::stop() {
    stopRequested_.store(true, std::memory_order_relaxed);
}

bool CurveWorker::isBusy() const {
    return activeRequest_.load(std::memory_order_relaxed) != 0;
}
