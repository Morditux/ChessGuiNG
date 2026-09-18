//
// Win-probability curve of a whole game.
//

#ifndef CHESSGUI_EVALUATIONGRAPH_H
#define CHESSGUI_EVALUATIONGRAPH_H

#include <QColor>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include "pgnannotations.h"

class QMouseEvent;
class QPaintEvent;

// Read-only plot of White's display percentage after each ply, drawn as the
// boundary between White's territory (above) and Black's (below), like the
// evaluation bar. Clicking a point reports the ply so the caller can navigate.
class EvaluationGraph : public QWidget {
    Q_OBJECT

public:
    explicit EvaluationGraph(QWidget *parent = nullptr);

    // White's display percentage (0-100) after each ply; index 0 is the
    // starting position.
    [[nodiscard]] const QVector<double> &evaluations() const;
    void setEvaluations(const QVector<double> &percentages);

    [[nodiscard]] int currentPly() const;
    void setCurrentPly(int ply);

    // Mistakes and blunders reported by a game audit, marked on the curve.
    [[nodiscard]] const QVector<AuditAnnotation> &auditAnnotations() const;
    void setAuditAnnotations(const QVector<AuditAnnotation> &annotations);

    [[nodiscard]] QSize sizeHint() const override;

    // Hover text for the position plotted after `ply`: the move it follows and
    // White's display evaluation. Empty when `ply` is not plotted.
    [[nodiscard]] QString tooltipForPly(int ply) const;

signals:
    void plySelected(int ply);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] QRectF plotRect() const;
    [[nodiscard]] int xAtPly(int ply) const;
    [[nodiscard]] qreal yAtValue(double percentage) const;
    [[nodiscard]] int plyAtX(int x) const;

    QVector<double> evaluations_;
    QVector<AuditAnnotation> auditAnnotations_;
    int currentPly_ = 0;

    QColor whiteColor_ = QColor("#ffffff");
    QColor blackColor_ = QColor("#312e2b");
    QColor borderColor_ = QColor("#5c4033");
    // Matches the margin the board is drawn on.
    QColor plotColor_ = QColor("#f7f3eb");
};

#endif // CHESSGUI_EVALUATIONGRAPH_H
