//
// Widget dedicated to the move list of a chess game. It renders move pairs as
// rows of a table (move number, white move, black move) and highlights the
// current move independently of keyboard focus. It never displays PGN headers,
// descriptions or results: it is exclusively a move list.
//

#ifndef CHESSGUI_MOVELISTWIDGET_H
#define CHESSGUI_MOVELISTWIDGET_H

#include <QStringList>
#include <QTableView>
#include <QVector>

#include "pgnannotations.h"

class QStandardItemModel;

class MoveListWidget : public QTableView {
    Q_OBJECT

public:
    enum { CurrentMoveRole = Qt::UserRole + 1, AuditSeverityRole, AuditLossRole };

    explicit MoveListWidget(QWidget *parent = nullptr);

    // Displays the given PGN move pairs ("1. e4 e5") and marks the ply
    // at currentPly as the current move. A ply of 0 selects the starting
    // position and leaves every move unhighlighted.
    void setPgn(const QStringList &pgnMovePairs, int currentPly);

    // Re-renders the list with a different current ply without changing the
    // displayed moves.
    void setCurrentMove(int currentPly);
    void setAuditAnnotations(const QVector<AuditAnnotation> &annotations);

    [[nodiscard]] int currentMove() const;
    [[nodiscard]] QString currentMoveText() const;

signals:
    void moveSelected(int plyIndex);

protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuildModel();
    void selectPly(int currentPly);
    void activateMove(const QModelIndex &index);
    [[nodiscard]] QModelIndex indexForPly(int ply) const;

    QStandardItemModel *model_ = nullptr;
    QStringList pgnMovePairs_;
    QVector<int> whitePlys_;
    QVector<int> blackPlys_;
    int currentPly_ = 0;
    QVector<AuditAnnotation> auditAnnotations_;
};

#endif // CHESSGUI_MOVELISTWIDGET_H
