//
// Created by mordicus on 27/08/2026.
//

#include "movelistwidget.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QStyledItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>

namespace {
constexpr int kColumnNumber = 0;
constexpr int kColumnWhite = 1;
constexpr int kColumnBlack = 2;
// The played move is model state; the view's current index is keyboard focus.
class CurrentMoveDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->state.setFlag(QStyle::State_Selected,
                              index.data(MoveListWidget::CurrentMoveRole).toBool());
    }
};
} // namespace

MoveListWidget::MoveListWidget(QWidget *parent)
    : QTableView(parent) {
    model_ = new QStandardItemModel(this);
    setModel(model_);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setSelectionMode(QAbstractItemView::NoSelection);
    setItemDelegate(new CurrentMoveDelegate(this));
    setAccessibleName(tr("Moves"));
    setWordWrap(false);

    verticalHeader()->hide();
    connect(this, &QTableView::clicked, this, &MoveListWidget::activateMove);

    rebuildModel();
}

void MoveListWidget::setPgn(const QStringList &pgnMovePairs, int currentPly) {
    pgnMovePairs_ = pgnMovePairs;
    currentPly_ = qMax(0, currentPly);
    rebuildModel();
}

void MoveListWidget::setCurrentMove(int currentPly) {
    const int ply = qMax(0, currentPly);
    if (ply == currentPly_) {
        return;
    }
    currentPly_ = ply;
    selectPly(ply);
}

void MoveListWidget::setAuditAnnotations(const QVector<AuditAnnotation> &annotations) {
    if (auditAnnotations_ == annotations) return;
    auditAnnotations_ = annotations;
    rebuildModel();
}

int MoveListWidget::currentMove() const {
    return currentPly_;
}

void MoveListWidget::rebuildModel() {
    clearSpans();
    model_->clear();
    model_->setHorizontalHeaderLabels({tr("#"), tr("White"), tr("Black")});

    auto *startItem = new QStandardItem(tr("Start"));
    startItem->setTextAlignment(Qt::AlignCenter);
    model_->appendRow({startItem, new QStandardItem(), new QStandardItem()});
    setSpan(0, 0, 1, 3);
    whitePlys_.assign(1, 0);
    blackPlys_.assign(1, 0);

    int ply = 0;
    int row = 1;
    for (const QString &pair : pgnMovePairs_) {
        QString number;
        QString white;
        QString black;
        int whitePly = -1;
        int blackPly = -1;
        bool blackStartsPair = false;
        const QStringList tokens = pair.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            if (token.startsWith(QLatin1Char('{')) || token.endsWith(QLatin1Char('}')) ||
                token.startsWith(QLatin1String("[%"))) {
                continue;
            }
            if (token.endsWith(QLatin1Char('.'))) {
                number = token.chopped(token.endsWith(QLatin1String("...")) ? 3 : 1);
                blackStartsPair = token.endsWith(QLatin1String("..."));
                continue;
            }
            ++ply;
            if (white.isEmpty() && !blackStartsPair) {
                white = token;
                whitePly = ply;
            } else {
                black = token;
                blackPly = ply;
            }
        }
        model_->setItem(row, kColumnNumber, new QStandardItem(number));
        if (!white.isEmpty()) {
            auto *item = new QStandardItem(white);
            if (whitePly >= 0 && whitePly < auditAnnotations_.size() && auditAnnotations_.at(whitePly).isValid()) {
                const AuditAnnotation &audit = auditAnnotations_.at(whitePly);
                item->setText(white + QLatin1Char(' ') + PgnAnnotations::auditSymbol(audit.severity));
                const QString description = tr("%1: %2 cp lost. Best move: %3.")
                    .arg(PgnAnnotations::auditSeverityText(audit.severity))
                    .arg(audit.centipawnLoss).arg(audit.bestMove);
                item->setToolTip(description);
                item->setData(description, Qt::AccessibleDescriptionRole);
                item->setData(item->text() + QStringLiteral(", ") + description, Qt::AccessibleTextRole);
                item->setData(static_cast<int>(audit.severity), AuditSeverityRole);
                item->setData(audit.centipawnLoss, AuditLossRole);
                item->setForeground(PgnAnnotations::auditColor(audit.severity));
            }
            model_->setItem(row, kColumnWhite, item);
        }
        if (!black.isEmpty()) {
            auto *item = new QStandardItem(black);
            if (blackPly >= 0 && blackPly < auditAnnotations_.size() && auditAnnotations_.at(blackPly).isValid()) {
                const AuditAnnotation &audit = auditAnnotations_.at(blackPly);
                item->setText(black + QLatin1Char(' ') + PgnAnnotations::auditSymbol(audit.severity));
                const QString description = tr("%1: %2 cp lost. Best move: %3.")
                    .arg(PgnAnnotations::auditSeverityText(audit.severity))
                    .arg(audit.centipawnLoss).arg(audit.bestMove);
                item->setToolTip(description);
                item->setData(description, Qt::AccessibleDescriptionRole);
                item->setData(item->text() + QStringLiteral(", ") + description, Qt::AccessibleTextRole);
                item->setData(static_cast<int>(audit.severity), AuditSeverityRole);
                item->setData(audit.centipawnLoss, AuditLossRole);
                item->setForeground(PgnAnnotations::auditColor(audit.severity));
            }
            model_->setItem(row, kColumnBlack, item);
        }
        whitePlys_.append(whitePly);
        blackPlys_.append(blackPly);
        ++row;
    }

    horizontalHeader()->setSectionResizeMode(kColumnNumber, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(kColumnWhite, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(kColumnBlack, QHeaderView::Stretch);
    selectPly(currentPly_);
}

QModelIndex MoveListWidget::indexForPly(int ply) const {
    if (ply == 0) {
        return model_->index(0, kColumnNumber);
    }
    for (int row = 1; row < whitePlys_.size(); ++row) {
        if (whitePlys_.at(row) == ply) {
            return model_->index(row, kColumnWhite);
        }
        if (blackPlys_.at(row) == ply) {
            return model_->index(row, kColumnBlack);
        }
    }
    return {};
}

QString MoveListWidget::currentMoveText() const {
    const QModelIndex index = indexForPly(currentPly_);
    if (!index.isValid() || currentPly_ == 0) {
        return tr("Start");
    }
    const QString number = model_->index(index.row(), kColumnNumber).data().toString();
    return (index.column() == kColumnBlack ? tr("%1… %2") : tr("%1. %2"))
        .arg(number, index.data().toString());
}

void MoveListWidget::selectPly(int currentPly) {
    const QModelIndex index = indexForPly(currentPly);
    for (int row = 0; row < model_->rowCount(); ++row) {
        for (int column = 0; column < model_->columnCount(); ++column) {
            const QModelIndex cell = model_->index(row, column);
            const bool current = cell == index;
            model_->setData(cell, current, CurrentMoveRole);
            QFont font;
            font.setBold(current);
            model_->setData(cell, font, Qt::FontRole);
        }
    }
    if (index.isValid()) {
        setCurrentIndex(index);
        scrollTo(index);
    }
}

void MoveListWidget::activateMove(const QModelIndex &index) {
    if (!index.isValid()) {
        return;
    }
    if (index.row() == 0) {
        emit moveSelected(0);
        return;
    }
    const int row = index.row();
    const int ply = index.column() == kColumnBlack ? blackPlys_.at(row)
                  : index.column() == kColumnWhite ? whitePlys_.at(row)
                  : whitePlys_.at(row) >= 0 ? whitePlys_.at(row) : blackPlys_.at(row);
    if (ply >= 0) {
        emit moveSelected(ply);
    }
}

void MoveListWidget::resizeEvent(QResizeEvent *event) {
    QTableView::resizeEvent(event);
    const auto index = indexForPly(currentPly_);
    if (index.isValid()) {
        scrollTo(index);
    }
}

bool MoveListWidget::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier &&
            (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
            event->ignore();
            return false; // Let the existing window navigation actions handle these.
        }
    }
    return QTableView::event(event);
}

void MoveListWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateMove(currentIndex());
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left) {
        if (currentPly_ > 0) {
            emit moveSelected(currentPly_ - 1);
            event->accept();
            return;
        }
    } else if (event->key() == Qt::Key_Right) {
        int maxPly = 0;
        for (const int p : whitePlys_) {
            maxPly = qMax(maxPly, p);
        }
        for (const int p : blackPlys_) {
            maxPly = qMax(maxPly, p);
        }
        if (currentPly_ < maxPly) {
            emit moveSelected(currentPly_ + 1);
            event->accept();
            return;
        }
    }
    QTableView::keyPressEvent(event);
}
