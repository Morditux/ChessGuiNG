//
// Dialog for selecting one game from a PGN file that contains several games.
//

#include "pgnselectdialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "pgnfile.h"

namespace {

constexpr int kColumnNumber = 0;
constexpr int kColumnEvent = 1;
constexpr int kColumnWhite = 2;
constexpr int kColumnBlack = 3;
constexpr int kColumnResult = 4;
constexpr int kColumnDate = 5;
constexpr int kColumnMoves = 6;

} // namespace

PgnSelectDialog::PgnSelectDialog(const QStringList &games, QWidget *parent)
    : QDialog(parent), games_(games) {
    setWindowTitle(tr("Select PGN Game"));
    setModal(true);
    resize(680, 420);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    gameTable_ = new QTableWidget(this);
    gameTable_->setObjectName(QStringLiteral("gameTable"));
    gameTable_->setColumnCount(7);
    gameTable_->setHorizontalHeaderLabels({
        tr("#"), tr("Event"), tr("White"), tr("Black"),
        tr("Result"), tr("Date"), tr("Moves")
    });
    gameTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    gameTable_->horizontalHeader()->setStretchLastSection(true);
    gameTable_->verticalHeader()->setVisible(false);
    gameTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    gameTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    gameTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    gameTable_->setAlternatingRowColors(true);
    gameTable_->setAccessibleName(tr("List of games in the PGN file"));
    gameTable_->setToolTip(tr("Double-click a game to load it"));

    for (int i = 0; i < games_.size(); ++i) {
        const QString &game = games_.at(i);
        const int row = gameTable_->rowCount();
        gameTable_->insertRow(row);
        const auto setItem = [this, row](int column, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            if (column == kColumnNumber || column == kColumnMoves) {
                item->setTextAlignment(Qt::AlignCenter);
            }
            gameTable_->setItem(row, column, item);
        };
        setItem(kColumnNumber, QString::number(i + 1));
        setItem(kColumnEvent, PgnFile::tagValue(game, QStringLiteral("Event")));
        setItem(kColumnWhite, PgnFile::tagValue(game, QStringLiteral("White")));
        setItem(kColumnBlack, PgnFile::tagValue(game, QStringLiteral("Black")));
        setItem(kColumnResult, PgnFile::tagValue(game, QStringLiteral("Result")));
        setItem(kColumnDate, PgnFile::tagValue(game, QStringLiteral("Date")));
        setItem(kColumnMoves, QString::number(PgnFile::moveCount(game)));
    }

    mainLayout->addWidget(gameTable_);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox_->setObjectName(QStringLiteral("buttonBox"));
    buttonBox_->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox_);

    connect(gameTable_, &QTableWidget::itemSelectionChanged,
            this, &PgnSelectDialog::onSelectionChanged);
    connect(gameTable_, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *) { accept(); });
}

QString PgnSelectDialog::selectedGame() const {
    const int row = gameTable_->currentRow();
    if (row < 0 || row >= games_.size()) {
        return QString();
    }
    return games_.at(row);
}

void PgnSelectDialog::onSelectionChanged() {
    buttonBox_->button(QDialogButtonBox::Ok)
        ->setEnabled(gameTable_->selectionModel()->hasSelection());
}
