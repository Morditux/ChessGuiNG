//
// Interactive dialog for editing a chess position (board setup, flags and FEN).
//

#include "positioneditordialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace {

QChar pieceFenChar(const Rules::Piece &piece) {
    QChar letter;
    switch (piece.type) {
    case Rules::PieceType::Pawn:   letter = QLatin1Char('P'); break;
    case Rules::PieceType::Knight: letter = QLatin1Char('N'); break;
    case Rules::PieceType::Bishop: letter = QLatin1Char('B'); break;
    case Rules::PieceType::Rook:   letter = QLatin1Char('R'); break;
    case Rules::PieceType::Queen:  letter = QLatin1Char('Q'); break;
    case Rules::PieceType::King:   letter = QLatin1Char('K'); break;
    case Rules::PieceType::None:   return {};
    }
    return piece.color == Rules::Color::White ? letter : letter.toLower();
}

QString pieceResourceKey(const Rules::Piece &piece) {
    const QString color = piece.color == Rules::Color::White
                              ? QStringLiteral("w")
                              : QStringLiteral("b");
    switch (piece.type) {
    case Rules::PieceType::Pawn:   return color + QStringLiteral("P");
    case Rules::PieceType::Knight: return color + QStringLiteral("N");
    case Rules::PieceType::Bishop: return color + QStringLiteral("B");
    case Rules::PieceType::Rook:   return color + QStringLiteral("R");
    case Rules::PieceType::Queen:  return color + QStringLiteral("Q");
    case Rules::PieceType::King:   return color + QStringLiteral("K");
    case Rules::PieceType::None:   return {};
    }
    return {};
}

bool piecesEqual(const std::optional<Rules::Piece> &lhs,
                 const std::optional<Rules::Piece> &rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }
    return lhs->type == rhs->type && lhs->color == rhs->color;
}

} // namespace

namespace detail {

class PositionEditorBoard : public QWidget {
public:
    enum class ToolMode {
        Move,
        Erase,
        PlacePiece
    };

    explicit PositionEditorBoard(ChessBoard::BoardTheme theme, bool flipped,
                                 QWidget *parent = nullptr)
        : QWidget(parent)
        , theme_(theme)
        , flipped_(flipped) {
        setMinimumSize(320, 320);
        setMouseTracking(true);

        const QStringList names = {
            QStringLiteral("wK"), QStringLiteral("wQ"), QStringLiteral("wR"),
            QStringLiteral("wB"), QStringLiteral("wN"), QStringLiteral("wP"),
            QStringLiteral("bK"), QStringLiteral("bQ"), QStringLiteral("bR"),
            QStringLiteral("bB"), QStringLiteral("bN"), QStringLiteral("bP")
        };
        for (const QString &name : names) {
            auto *renderer = new QSvgRenderer(
                QStringLiteral(":/pieces/%1.svg").arg(name), this);
            if (renderer->isValid()) {
                renderers_.insert(name, renderer);
            }
        }
    }

    void setBoardChangedCallback(std::function<void()> callback) {
        onBoardChanged_ = std::move(callback);
    }

    void setFlipped(bool flipped) {
        if (flipped_ == flipped) {
            return;
        }
        flipped_ = flipped;
        update();
    }

    [[nodiscard]] bool isFlipped() const {
        return flipped_;
    }

    void setToolMove() {
        toolMode_ = ToolMode::Move;
        setCursor(Qt::OpenHandCursor);
    }

    void setToolErase() {
        toolMode_ = ToolMode::Erase;
        setCursor(Qt::CrossCursor);
    }

    void setToolPiece(const Rules::Piece &piece) {
        toolMode_ = ToolMode::PlacePiece;
        activePiece_ = piece;
        setCursor(Qt::PointingHandCursor);
    }

    void setBoard(const Rules::Board &board) {
        board_ = board;
        update();
    }

    [[nodiscard]] const Rules::Board &board() const {
        return board_;
    }

    void setPieceAt(Rules::Position pos, const std::optional<Rules::Piece> &piece) {
        if (!Rules::isInside(pos)) {
            return;
        }
        if (piecesEqual(board_[pos.row][pos.column], piece)) {
            return;
        }
        board_[pos.row][pos.column] = piece;
        update();
        if (onBoardChanged_) {
            onBoardChanged_();
        }
    }

    [[nodiscard]] std::optional<Rules::Piece> pieceAt(Rules::Position pos) const {
        if (!Rules::isInside(pos)) {
            return std::nullopt;
        }
        return board_[pos.row][pos.column];
    }

    QSize sizeHint() const override {
        return {384, 384};
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));

        const Geometry geo = geometry();
        if (geo.squareSize <= 0) {
            return;
        }

        const ChessBoard::ThemeColors colors = ChessBoard::colorsForTheme(theme_);
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const Rules::Position pos{row, col};
                const Rules::Position disp = displayed(pos);
                const QRect sq = squareRect(pos, geo);
                painter.fillRect(sq, (disp.row + disp.column) % 2 == 0
                                         ? colors.lightSquare
                                         : colors.darkSquare);
                if (hoveredPos_.has_value() && *hoveredPos_ == pos) {
                    QColor hover(232, 198, 90, 95);
                    painter.fillRect(sq, hover);
                }
            }
        }

        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const Rules::Position pos{row, col};
                if (movingPiece_.has_value() && moveFrom_.has_value() &&
                    *moveFrom_ == pos) {
                    continue;
                }
                const auto &piece = board_[row][col];
                if (!piece.has_value()) {
                    continue;
                }
                if (auto *renderer = renderers_.value(pieceResourceKey(*piece), nullptr)) {
                    const QRectF sq(squareRect(pos, geo));
                    const qreal pad = geo.squareSize * 0.06;
                    renderer->render(&painter, sq.adjusted(pad, pad, -pad, -pad));
                }
            }
        }

        if (movingPiece_.has_value()) {
            if (auto *renderer = renderers_.value(pieceResourceKey(*movingPiece_), nullptr)) {
                const QRectF dragged(
                    dragPoint_.x() - geo.squareSize / 2.0,
                    dragPoint_.y() - geo.squareSize / 2.0,
                    geo.squareSize,
                    geo.squareSize);
                const qreal pad = geo.squareSize * 0.06;
                renderer->render(&painter, dragged.adjusted(pad, pad, -pad, -pad));
            }
        }

        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(colors.border, 2));
        painter.drawRect(geo.boardX, geo.boardY,
                         geo.boardSide - 1, geo.boardSide - 1);

        QFont coordFont = font();
        coordFont.setBold(true);
        painter.setFont(coordFont);
        QColor coordColor = palette().color(QPalette::WindowText);
        coordColor.setAlpha(200);
        painter.setPen(coordColor);

        for (int col = 0; col < 8; ++col) {
            const int fileCol = flipped_ ? 7 - col : col;
            const QRect fileRect(
                geo.boardX + col * geo.squareSize,
                geo.boardY + geo.boardSide + 2,
                geo.squareSize,
                kBottomMargin - 2);
            painter.drawText(fileRect, Qt::AlignHCenter | Qt::AlignTop,
                             QString(QChar('a' + fileCol)));
        }

        for (int row = 0; row < 8; ++row) {
            const int rank = flipped_ ? row + 1 : 8 - row;
            const QRect rankRect(
                geo.boardX - kLeftMargin + 2,
                geo.boardY + row * geo.squareSize,
                kLeftMargin - 5,
                geo.squareSize);
            painter.drawText(rankRect, Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(rank));
        }
    }

    void mousePressEvent(QMouseEvent *event) override {
        const auto pos = positionAt(event->position().toPoint());
        if (!pos.has_value()) {
            QWidget::mousePressEvent(event);
            return;
        }

        if (event->button() == Qt::RightButton) {
            rightErasing_ = true;
            setPieceAt(*pos, std::nullopt);
            event->accept();
            return;
        }

        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        if (toolMode_ == ToolMode::Move) {
            const auto existing = board_[pos->row][pos->column];
            if (existing.has_value()) {
                moveFrom_ = pos;
                movingPiece_ = existing;
                dragPoint_ = event->position().toPoint();
                setCursor(Qt::ClosedHandCursor);
                update();
            }
        } else if (toolMode_ == ToolMode::Erase) {
            leftPainting_ = true;
            setPieceAt(*pos, std::nullopt);
        } else {
            leftPainting_ = true;
            setPieceAt(*pos, activePiece_);
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        const QPoint pt = event->position().toPoint();
        const auto pos = positionAt(pt);
        if (hoveredPos_ != pos) {
            hoveredPos_ = pos;
            update();
        }

        if (rightErasing_ && (event->buttons() & Qt::RightButton) && pos.has_value()) {
            setPieceAt(*pos, std::nullopt);
            event->accept();
            return;
        }

        if (movingPiece_.has_value() && (event->buttons() & Qt::LeftButton)) {
            dragPoint_ = pt;
            update();
            event->accept();
            return;
        }

        if (leftPainting_ && (event->buttons() & Qt::LeftButton) && pos.has_value()) {
            if (toolMode_ == ToolMode::Erase) {
                setPieceAt(*pos, std::nullopt);
            } else if (toolMode_ == ToolMode::PlacePiece) {
                setPieceAt(*pos, activePiece_);
            }
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::RightButton) {
            rightErasing_ = false;
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton) {
            if (movingPiece_.has_value() && moveFrom_.has_value()) {
                const auto target = positionAt(event->position().toPoint());
                const Rules::Position source = *moveFrom_;
                const Rules::Piece moved = *movingPiece_;
                moveFrom_.reset();
                movingPiece_.reset();
                setCursor(Qt::OpenHandCursor);

                if (target.has_value() && *target != source) {
                    board_[source.row][source.column].reset();
                    board_[target->row][target->column] = moved;
                    update();
                    if (onBoardChanged_) {
                        onBoardChanged_();
                    }
                } else {
                    update();
                }
            }
            leftPainting_ = false;
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        hoveredPos_.reset();
        update();
        QWidget::leaveEvent(event);
    }

private:
    static constexpr int kLeftMargin = 24;
    static constexpr int kRightMargin = 6;
    static constexpr int kTopMargin = 6;
    static constexpr int kBottomMargin = 24;

    struct Geometry {
        int boardX = 0;
        int boardY = 0;
        int squareSize = 0;
        int boardSide = 0;
    };

    [[nodiscard]] Geometry geometry() const {
        const int availW = width() - kLeftMargin - kRightMargin;
        const int availH = height() - kTopMargin - kBottomMargin;
        const int side = std::min(availW, availH);
        const int sq = side / 8;
        if (sq <= 0) {
            return {};
        }
        const int boardSide = sq * 8;
        return {
            kLeftMargin + (availW - boardSide) / 2,
            kTopMargin + (availH - boardSide) / 2,
            sq,
            boardSide
        };
    }

    [[nodiscard]] Rules::Position displayed(Rules::Position pos) const {
        if (!flipped_) {
            return pos;
        }
        return {7 - pos.row, 7 - pos.column};
    }

    [[nodiscard]] QRect squareRect(Rules::Position pos, const Geometry &geo) const {
        const Rules::Position disp = displayed(pos);
        return {
            geo.boardX + disp.column * geo.squareSize,
            geo.boardY + disp.row * geo.squareSize,
            geo.squareSize,
            geo.squareSize
        };
    }

    [[nodiscard]] std::optional<Rules::Position> positionAt(const QPoint &pt) const {
        const Geometry geo = geometry();
        if (geo.squareSize <= 0 ||
            pt.x() < geo.boardX || pt.x() >= geo.boardX + geo.boardSide ||
            pt.y() < geo.boardY || pt.y() >= geo.boardY + geo.boardSide) {
            return std::nullopt;
        }
        const Rules::Position disp{
            (pt.y() - geo.boardY) / geo.squareSize,
            (pt.x() - geo.boardX) / geo.squareSize
        };
        return displayed(disp);
    }

    ChessBoard::BoardTheme theme_;
    bool flipped_ = false;
    QHash<QString, QSvgRenderer *> renderers_;
    Rules::Board board_{};
    ToolMode toolMode_ = ToolMode::PlacePiece;
    Rules::Piece activePiece_{Rules::PieceType::Queen, Rules::Color::White, true};
    std::optional<Rules::Position> hoveredPos_;
    std::optional<Rules::Position> moveFrom_;
    std::optional<Rules::Piece> movingPiece_;
    QPoint dragPoint_;
    bool leftPainting_ = false;
    bool rightErasing_ = false;
    std::function<void()> onBoardChanged_;
};

} // namespace detail

PositionEditorDialog::PositionEditorDialog(const QString &initialFen,
                                           ChessBoard::BoardTheme theme,
                                           bool flipped,
                                           QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Edit Position"));
    setModal(true);
    resize(780, 560);

    setupUi(theme, flipped);

    const QString fallbackFen = Rules().toFen();
    Rules probe;
    if (!initialFen.trimmed().isEmpty() && probe.loadFen(initialFen)) {
        loadFenIntoEditor(probe.toFen());
    } else {
        loadFenIntoEditor(fallbackFen);
    }
}

void PositionEditorDialog::setupUi(ChessBoard::BoardTheme theme, bool flipped) {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(12);

    editorBoard_ = new detail::PositionEditorBoard(theme, flipped, this);
    editorBoard_->setObjectName(QStringLiteral("positionEditorBoard"));
    editorBoard_->setBoardChangedCallback([this] { syncFromControls(true); });
    topLayout->addWidget(editorBoard_, 1);

    auto *controlsColumn = new QVBoxLayout();
    controlsColumn->setSpacing(10);

    // Piece palette
    auto *paletteGroup = new QGroupBox(tr("Pieces"), this);
    auto *paletteLayout = new QVBoxLayout(paletteGroup);
    auto *grid = new QGridLayout();
    grid->setSpacing(4);

    auto *toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);

    struct PieceChoice {
        Rules::Piece piece;
        QString key;
        QString label;
    };
    const QVector<PieceChoice> choices = {
        {{Rules::PieceType::King,   Rules::Color::White, true}, QStringLiteral("wK"), tr("White King")},
        {{Rules::PieceType::Queen,  Rules::Color::White, true}, QStringLiteral("wQ"), tr("White Queen")},
        {{Rules::PieceType::Rook,   Rules::Color::White, true}, QStringLiteral("wR"), tr("White Rook")},
        {{Rules::PieceType::Bishop, Rules::Color::White, true}, QStringLiteral("wB"), tr("White Bishop")},
        {{Rules::PieceType::Knight, Rules::Color::White, true}, QStringLiteral("wN"), tr("White Knight")},
        {{Rules::PieceType::Pawn,   Rules::Color::White, true}, QStringLiteral("wP"), tr("White Pawn")},
        {{Rules::PieceType::King,   Rules::Color::Black, true}, QStringLiteral("bK"), tr("Black King")},
        {{Rules::PieceType::Queen,  Rules::Color::Black, true}, QStringLiteral("bQ"), tr("Black Queen")},
        {{Rules::PieceType::Rook,   Rules::Color::Black, true}, QStringLiteral("bR"), tr("Black Rook")},
        {{Rules::PieceType::Bishop, Rules::Color::Black, true}, QStringLiteral("bB"), tr("Black Bishop")},
        {{Rules::PieceType::Knight, Rules::Color::Black, true}, QStringLiteral("bN"), tr("Black Knight")},
        {{Rules::PieceType::Pawn,   Rules::Color::Black, true}, QStringLiteral("bP"), tr("Black Pawn")},
    };

    QToolButton *defaultToolButton = nullptr;
    for (int i = 0; i < choices.size(); ++i) {
        const auto &choice = choices.at(i);
        auto *button = new QToolButton(paletteGroup);
        button->setObjectName(QStringLiteral("pieceTool_%1").arg(choice.key));
        button->setIcon(QIcon(QStringLiteral(":/pieces/%1.svg").arg(choice.key)));
        button->setIconSize(QSize(28, 28));
        button->setFixedSize(38, 38);
        button->setCheckable(true);
        button->setToolTip(choice.label);
        button->setAccessibleName(choice.label);
        toolGroup->addButton(button);
        grid->addWidget(button, i / 6, i % 6);
        connect(button, &QToolButton::clicked, this, [this, piece = choice.piece] {
            editorBoard_->setToolPiece(piece);
        });
        if (choice.key == QStringLiteral("wQ")) {
            defaultToolButton = button;
        }
    }
    paletteLayout->addLayout(grid);

    auto *toolsRow = new QHBoxLayout();
    auto *moveButton = new QToolButton(paletteGroup);
    moveButton->setObjectName(QStringLiteral("movePieceToolButton"));
    moveButton->setText(tr("Move piece"));
    moveButton->setCheckable(true);
    moveButton->setToolTip(tr("Drag an existing piece to another square"));
    toolGroup->addButton(moveButton);
    connect(moveButton, &QToolButton::clicked, this, [this] {
        editorBoard_->setToolMove();
    });

    auto *eraseButton = new QToolButton(paletteGroup);
    eraseButton->setObjectName(QStringLiteral("erasePieceToolButton"));
    eraseButton->setText(tr("Erase"));
    eraseButton->setCheckable(true);
    eraseButton->setToolTip(tr("Clear clicked squares (right-click also erases)"));
    toolGroup->addButton(eraseButton);
    connect(eraseButton, &QToolButton::clicked, this, [this] {
        editorBoard_->setToolErase();
    });

    toolsRow->addWidget(moveButton);
    toolsRow->addWidget(eraseButton);
    toolsRow->addStretch();
    paletteLayout->addLayout(toolsRow);

    if (defaultToolButton != nullptr) {
        defaultToolButton->setChecked(true);
        editorBoard_->setToolPiece({Rules::PieceType::Queen, Rules::Color::White, true});
    }

    auto *hintLabel = new QLabel(
        tr("Left-click or drag to place; right-click to clear a square."),
        paletteGroup);
    hintLabel->setWordWrap(true);
    paletteLayout->addWidget(hintLabel);

    auto *presetsRow = new QHBoxLayout();
    auto *startPosButton = new QPushButton(tr("Starting position"), paletteGroup);
    startPosButton->setObjectName(QStringLiteral("startPositionButton"));
    startPosButton->setAutoDefault(false);
    connect(startPosButton, &QPushButton::clicked, this, [this] {
        loadFenIntoEditor(Rules().toFen());
    });

    auto *clearBoardButton = new QPushButton(tr("Clear board"), paletteGroup);
    clearBoardButton->setObjectName(QStringLiteral("clearBoardButton"));
    clearBoardButton->setAutoDefault(false);
    connect(clearBoardButton, &QPushButton::clicked, this, [this] {
        Rules::Board empty{};
        editorBoard_->setBoard(empty);
        syncFromControls(true);
    });

    auto *flipButton = new QPushButton(tr("Flip"), paletteGroup);
    flipButton->setObjectName(QStringLiteral("editorFlipButton"));
    flipButton->setAutoDefault(false);
    connect(flipButton, &QPushButton::clicked, this, [this] {
        editorBoard_->setFlipped(!editorBoard_->isFlipped());
    });

    presetsRow->addWidget(startPosButton);
    presetsRow->addWidget(clearBoardButton);
    presetsRow->addWidget(flipButton);
    paletteLayout->addLayout(presetsRow);

    controlsColumn->addWidget(paletteGroup);

    // State group
    auto *stateGroup = new QGroupBox(tr("Position state"), this);
    auto *stateForm = new QFormLayout(stateGroup);

    auto *sideRow = new QHBoxLayout();
    whiteToMoveRadio_ = new QRadioButton(tr("White"), stateGroup);
    whiteToMoveRadio_->setObjectName(QStringLiteral("whiteToMoveRadio"));
    blackToMoveRadio_ = new QRadioButton(tr("Black"), stateGroup);
    blackToMoveRadio_->setObjectName(QStringLiteral("blackToMoveRadio"));
    whiteToMoveRadio_->setChecked(true);
    sideRow->addWidget(whiteToMoveRadio_);
    sideRow->addWidget(blackToMoveRadio_);
    sideRow->addStretch();
    stateForm->addRow(tr("Side to move:"), sideRow);

    connect(whiteToMoveRadio_, &QRadioButton::toggled, this, [this](bool) {
        if (!updating_) {
            syncFromControls(true);
        }
    });

    auto *castlingGrid = new QGridLayout();
    whiteKingSideCheck_ = new QCheckBox(tr("White O-O"), stateGroup);
    whiteKingSideCheck_->setObjectName(QStringLiteral("whiteKingSideCheck"));
    whiteQueenSideCheck_ = new QCheckBox(tr("White O-O-O"), stateGroup);
    whiteQueenSideCheck_->setObjectName(QStringLiteral("whiteQueenSideCheck"));
    blackKingSideCheck_ = new QCheckBox(tr("Black O-O"), stateGroup);
    blackKingSideCheck_->setObjectName(QStringLiteral("blackKingSideCheck"));
    blackQueenSideCheck_ = new QCheckBox(tr("Black O-O-O"), stateGroup);
    blackQueenSideCheck_->setObjectName(QStringLiteral("blackQueenSideCheck"));
    castlingGrid->addWidget(whiteKingSideCheck_, 0, 0);
    castlingGrid->addWidget(whiteQueenSideCheck_, 0, 1);
    castlingGrid->addWidget(blackKingSideCheck_, 1, 0);
    castlingGrid->addWidget(blackQueenSideCheck_, 1, 1);
    stateForm->addRow(tr("Castling:"), castlingGrid);

    for (QCheckBox *check : {whiteKingSideCheck_, whiteQueenSideCheck_,
                            blackKingSideCheck_, blackQueenSideCheck_}) {
        connect(check, &QCheckBox::toggled, this, [this](bool) {
            if (!updating_) {
                syncFromControls(true);
            }
        });
    }

    enPassantCombo_ = new QComboBox(stateGroup);
    enPassantCombo_->setObjectName(QStringLiteral("enPassantCombo"));
    enPassantCombo_->addItem(QStringLiteral("-"));
    stateForm->addRow(tr("En passant:"), enPassantCombo_);
    connect(enPassantCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!updating_) {
            syncFromControls(true);
        }
    });

    auto *clocksRow = new QHBoxLayout();
    halfmoveSpin_ = new QSpinBox(stateGroup);
    halfmoveSpin_->setObjectName(QStringLiteral("halfmoveSpin"));
    halfmoveSpin_->setRange(0, 150);
    halfmoveSpin_->setValue(0);
    fullmoveSpin_ = new QSpinBox(stateGroup);
    fullmoveSpin_->setObjectName(QStringLiteral("fullmoveSpin"));
    fullmoveSpin_->setRange(1, 9999);
    fullmoveSpin_->setValue(1);
    clocksRow->addWidget(new QLabel(tr("Halfmove:"), stateGroup));
    clocksRow->addWidget(halfmoveSpin_);
    clocksRow->addWidget(new QLabel(tr("Fullmove:"), stateGroup));
    clocksRow->addWidget(fullmoveSpin_);
    stateForm->addRow(tr("Counters:"), clocksRow);

    connect(halfmoveSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!updating_) {
            syncFromControls(true);
        }
    });
    connect(fullmoveSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!updating_) {
            syncFromControls(true);
        }
    });

    controlsColumn->addWidget(stateGroup);
    controlsColumn->addStretch();
    topLayout->addLayout(controlsColumn);
    mainLayout->addLayout(topLayout, 1);

    // FEN bar
    auto *fenRow = new QHBoxLayout();
    fenRow->addWidget(new QLabel(tr("FEN:"), this));
    fenEdit_ = new QLineEdit(this);
    fenEdit_->setObjectName(QStringLiteral("fenEdit"));
    fenEdit_->setClearButtonEnabled(true);
    fenEdit_->setAccessibleName(tr("FEN notation"));
    fenRow->addWidget(fenEdit_, 1);

    auto *copyFenButton = new QPushButton(tr("Copy"), this);
    copyFenButton->setObjectName(QStringLiteral("copyFenButton"));
    copyFenButton->setAutoDefault(false);
    connect(copyFenButton, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(fenEdit_->text().trimmed());
    });
    fenRow->addWidget(copyFenButton);
    mainLayout->addLayout(fenRow);

    connect(fenEdit_, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (updating_) {
            return;
        }
        Rules probe;
        if (probe.loadFen(text)) {
            applyBoardFromFen(probe.toFen());
            validateAndUpdateStatus(text);
        } else {
            valid_ = false;
            validationLabel_->setText(tr("Invalid FEN position."));
            validationLabel_->setStyleSheet(QStringLiteral("color: #c8453a;"));
            if (QPushButton *ok = okButton()) {
                ok->setEnabled(false);
            }
        }
    });

    validationLabel_ = new QLabel(this);
    validationLabel_->setObjectName(QStringLiteral("validationLabel"));
    validationLabel_->setWordWrap(true);
    mainLayout->addWidget(validationLabel_);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox_->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox_);
}

QString PositionEditorDialog::fen() const {
    return fenEdit_ != nullptr ? fenEdit_->text().trimmed() : QString();
}

bool PositionEditorDialog::isPositionValid() const {
    return valid_;
}

void PositionEditorDialog::setBoardPiece(Rules::Position pos,
                                         const std::optional<Rules::Piece> &piece) {
    if (editorBoard_ != nullptr) {
        editorBoard_->setPieceAt(pos, piece);
    }
}

std::optional<Rules::Piece> PositionEditorDialog::boardPiece(Rules::Position pos) const {
    return editorBoard_ != nullptr ? editorBoard_->pieceAt(pos) : std::nullopt;
}

void PositionEditorDialog::loadFenIntoEditor(const QString &fenText) {
    applyBoardFromFen(fenText);
    if (fenEdit_ != nullptr) {
        const QSignalBlocker blocker(fenEdit_);
        fenEdit_->setText(buildFenFromControls());
    }
    validateAndUpdateStatus(fen());
}

QLineEdit *PositionEditorDialog::fenEdit() const { return fenEdit_; }
QLabel *PositionEditorDialog::validationLabel() const { return validationLabel_; }
QPushButton *PositionEditorDialog::okButton() const {
    return buttonBox_ != nullptr ? buttonBox_->button(QDialogButtonBox::Ok) : nullptr;
}
QComboBox *PositionEditorDialog::enPassantCombo() const { return enPassantCombo_; }
QCheckBox *PositionEditorDialog::whiteKingSideCheck() const { return whiteKingSideCheck_; }
QCheckBox *PositionEditorDialog::whiteQueenSideCheck() const { return whiteQueenSideCheck_; }
QCheckBox *PositionEditorDialog::blackKingSideCheck() const { return blackKingSideCheck_; }
QCheckBox *PositionEditorDialog::blackQueenSideCheck() const { return blackQueenSideCheck_; }
QRadioButton *PositionEditorDialog::whiteToMoveRadio() const { return whiteToMoveRadio_; }
QRadioButton *PositionEditorDialog::blackToMoveRadio() const { return blackToMoveRadio_; }

void PositionEditorDialog::syncFromControls(bool updateFenField) {
    if (updating_) {
        return;
    }
    updating_ = true;
    updateCastlingAvailability();
    updateEnPassantOptions(enPassantCombo_->currentText());
    const QString generatedFen = buildFenFromControls();
    if (updateFenField && fenEdit_ != nullptr) {
        const QSignalBlocker blocker(fenEdit_);
        fenEdit_->setText(generatedFen);
    }
    updating_ = false;
    validateAndUpdateStatus(generatedFen);
}

void PositionEditorDialog::updateCastlingAvailability() {
    const Rules::Board &board = editorBoard_->board();
    const auto isPiece = [&](int row, int col, Rules::PieceType type, Rules::Color color) {
        const auto &p = board[row][col];
        return p.has_value() && p->type == type && p->color == color;
    };

    const bool wKing = isPiece(7, 4, Rules::PieceType::King, Rules::Color::White);
    const bool bKing = isPiece(0, 4, Rules::PieceType::King, Rules::Color::Black);

    const auto syncCheck = [](QCheckBox *box, bool available) {
        box->setEnabled(available);
        if (!available) {
            box->setChecked(false);
        }
    };
    syncCheck(whiteKingSideCheck_,
              wKing && isPiece(7, 7, Rules::PieceType::Rook, Rules::Color::White));
    syncCheck(whiteQueenSideCheck_,
              wKing && isPiece(7, 0, Rules::PieceType::Rook, Rules::Color::White));
    syncCheck(blackKingSideCheck_,
              bKing && isPiece(0, 7, Rules::PieceType::Rook, Rules::Color::Black));
    syncCheck(blackQueenSideCheck_,
              bKing && isPiece(0, 0, Rules::PieceType::Rook, Rules::Color::Black));
}

void PositionEditorDialog::updateEnPassantOptions(const QString &preferredTarget) {
    const Rules::Board &board = editorBoard_->board();
    const bool whiteToMove = whiteToMoveRadio_->isChecked();

    QStringList options{QStringLiteral("-")};
    const int epRow = whiteToMove ? 2 : 5;
    const int pawnRow = whiteToMove ? 3 : 4;
    const int originRow = whiteToMove ? 1 : 6;
    const Rules::Color pawnColor =
        whiteToMove ? Rules::Color::Black : Rules::Color::White;
    const char rankChar = whiteToMove ? '6' : '3';

    for (int col = 0; col < 8; ++col) {
        const auto &pawn = board[pawnRow][col];
        if (!board[epRow][col].has_value() &&
            !board[originRow][col].has_value() &&
            pawn.has_value() && pawn->type == Rules::PieceType::Pawn &&
            pawn->color == pawnColor) {
            options.append(QStringLiteral("%1%2")
                               .arg(QChar('a' + col))
                               .arg(QLatin1Char(rankChar)));
        }
    }

    const QSignalBlocker blocker(enPassantCombo_);
    enPassantCombo_->clear();
    enPassantCombo_->addItems(options);
    const int idx = enPassantCombo_->findText(preferredTarget);
    enPassantCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString PositionEditorDialog::buildFenFromControls() const {
    const Rules::Board &board = editorBoard_->board();
    QStringList ranks;
    ranks.reserve(8);
    for (int row = 0; row < 8; ++row) {
        QString rankStr;
        int emptyCount = 0;
        for (int col = 0; col < 8; ++col) {
            const auto &piece = board[row][col];
            if (!piece.has_value()) {
                ++emptyCount;
            } else {
                if (emptyCount > 0) {
                    rankStr += QString::number(emptyCount);
                    emptyCount = 0;
                }
                rankStr += pieceFenChar(*piece);
            }
        }
        if (emptyCount > 0) {
            rankStr += QString::number(emptyCount);
        }
        ranks.append(rankStr);
    }

    const QString side = whiteToMoveRadio_->isChecked()
                             ? QStringLiteral("w")
                             : QStringLiteral("b");

    QString castling;
    if (whiteKingSideCheck_->isChecked()) castling += QLatin1Char('K');
    if (whiteQueenSideCheck_->isChecked()) castling += QLatin1Char('Q');
    if (blackKingSideCheck_->isChecked()) castling += QLatin1Char('k');
    if (blackQueenSideCheck_->isChecked()) castling += QLatin1Char('q');
    if (castling.isEmpty()) {
        castling = QStringLiteral("-");
    }

    const QString ep = enPassantCombo_->currentText().isEmpty()
                           ? QStringLiteral("-")
                           : enPassantCombo_->currentText();

    return QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(ranks.join(QLatin1Char('/')),
             side,
             castling,
             ep)
        .arg(halfmoveSpin_->value())
        .arg(fullmoveSpin_->value());
}

QString PositionEditorDialog::diagnoseInvalidPosition() const {
    const Rules::Board &board = editorBoard_->board();
    int whiteKings = 0;
    int blackKings = 0;
    int whitePieces = 0;
    int blackPieces = 0;
    int whitePawns = 0;
    int blackPawns = 0;
    Rules::Position wKingPos{-1, -1};
    Rules::Position bKingPos{-1, -1};

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const auto &piece = board[row][col];
            if (!piece.has_value()) {
                continue;
            }
            if (piece->color == Rules::Color::White) {
                ++whitePieces;
                if (piece->type == Rules::PieceType::King) {
                    ++whiteKings;
                    wKingPos = {row, col};
                } else if (piece->type == Rules::PieceType::Pawn) {
                    ++whitePawns;
                    if (row == 0 || row == 7) {
                        return tr("Pawns cannot be placed on the 1st or 8th rank.");
                    }
                }
            } else {
                ++blackPieces;
                if (piece->type == Rules::PieceType::King) {
                    ++blackKings;
                    bKingPos = {row, col};
                } else if (piece->type == Rules::PieceType::Pawn) {
                    ++blackPawns;
                    if (row == 0 || row == 7) {
                        return tr("Pawns cannot be placed on the 1st or 8th rank.");
                    }
                }
            }
        }
    }

    if (whiteKings != 1) {
        return tr("White must have exactly one king.");
    }
    if (blackKings != 1) {
        return tr("Black must have exactly one king.");
    }
    if (std::abs(wKingPos.row - bKingPos.row) <= 1 &&
        std::abs(wKingPos.column - bKingPos.column) <= 1) {
        return tr("Kings cannot be placed on adjacent squares.");
    }
    if (whitePieces > 16 || blackPieces > 16) {
        return tr("Each side may have at most 16 pieces.");
    }
    if (whitePawns > 8 || blackPawns > 8) {
        return tr("Each side may have at most 8 pawns.");
    }
    return tr("The side not to move cannot already be in check.");
}

void PositionEditorDialog::validateAndUpdateStatus(const QString &fenText) {
    Rules probe;
    valid_ = probe.loadFen(fenText);
    if (valid_) {
        validationLabel_->setText(tr("Valid position."));
        validationLabel_->setStyleSheet(QStringLiteral("color: #3a9d4a;"));
    } else {
        validationLabel_->setText(diagnoseInvalidPosition());
        validationLabel_->setStyleSheet(QStringLiteral("color: #c8453a;"));
    }
    if (QPushButton *ok = okButton()) {
        ok->setEnabled(valid_);
    }
}

void PositionEditorDialog::applyBoardFromFen(const QString &fenText) {
    Rules parsed;
    if (!parsed.loadFen(fenText)) {
        return;
    }

    const QStringList parts = fenText.trimmed().split(
        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() != 6) {
        return;
    }

    updating_ = true;
    editorBoard_->setBoard(parsed.board());
    whiteToMoveRadio_->setChecked(parsed.currentPlayer() == Rules::Color::White);
    blackToMoveRadio_->setChecked(parsed.currentPlayer() == Rules::Color::Black);

    const QString &castling = parts.at(2);
    updateCastlingAvailability();
    if (whiteKingSideCheck_->isEnabled()) {
        whiteKingSideCheck_->setChecked(castling.contains(QLatin1Char('K')));
    }
    if (whiteQueenSideCheck_->isEnabled()) {
        whiteQueenSideCheck_->setChecked(castling.contains(QLatin1Char('Q')));
    }
    if (blackKingSideCheck_->isEnabled()) {
        blackKingSideCheck_->setChecked(castling.contains(QLatin1Char('k')));
    }
    if (blackQueenSideCheck_->isEnabled()) {
        blackQueenSideCheck_->setChecked(castling.contains(QLatin1Char('q')));
    }

    updateEnPassantOptions(parts.at(3));
    halfmoveSpin_->setValue(parts.at(4).toInt());
    fullmoveSpin_->setValue(parts.at(5).toInt());
    updating_ = false;
}
