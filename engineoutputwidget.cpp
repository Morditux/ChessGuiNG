//
// Widget for displaying chess engine (UCI) analysis output.
//

#include "engineoutputwidget.h"

#include <QFontDatabase>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

EngineOutputWidget::EngineOutputWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
    updateHeaderSummary();
}

QString EngineOutputWidget::engineName() const {
    return engineName_;
}

QString EngineOutputWidget::engineStatus() const {
    return engineStatus_;
}

int EngineOutputWidget::depth() const {
    return depth_;
}

int EngineOutputWidget::seldepth() const {
    return seldepth_;
}

QString EngineOutputWidget::scoreText() const {
    return scoreText_;
}

qint64 EngineOutputWidget::nodes() const {
    return nodes_;
}

qint64 EngineOutputWidget::nodesPerSecond() const {
    return nps_;
}

qint64 EngineOutputWidget::time() const {
    return timeMs_;
}

QString EngineOutputWidget::bestMove() const {
    return bestMove_;
}

QString EngineOutputWidget::uciLog() const {
    return logEdit_ ? logEdit_->toPlainText() : QString();
}

QList<EngineAnalysisLine> EngineOutputWidget::analysisLines() const {
    return analysisLines_;
}

QPushButton *EngineOutputWidget::startButton() const {
    return startButton_;
}

QPushButton *EngineOutputWidget::pauseButton() const {
    return pauseButton_;
}

QPushButton *EngineOutputWidget::stopButton() const {
    return stopButton_;
}

bool EngineOutputWidget::analysisSectionVisible() const {
    return analysisSectionVisible_;
}

bool EngineOutputWidget::logSectionVisible() const {
    return logSectionVisible_;
}

bool EngineOutputWidget::detailsVisible() const {
    return detailsVisible_;
}

EngineOutputWidget::DetailsPage EngineOutputWidget::detailsPage() const {
    return detailsPage_;
}

QToolButton *EngineOutputWidget::detailsToggleButton() const {
    return detailsToggleButton_;
}

QToolButton *EngineOutputWidget::analysisToggleButton() const {
    return analysisToggleButton_;
}

QToolButton *EngineOutputWidget::logToggleButton() const {
    return logToggleButton_;
}

QTableWidget *EngineOutputWidget::pvTable() const {
    return pvTable_;
}

QPlainTextEdit *EngineOutputWidget::logEdit() const {
    return logEdit_;
}

QSize EngineOutputWidget::sizeHint() const {
    return {800, desiredHeight()};
}

QSize EngineOutputWidget::minimumSizeHint() const {
    return {300, desiredHeight()};
}

int EngineOutputWidget::desiredHeight() const {
    int height = 0;
    if (headerFrame_) {
        height += headerFrame_->sizeHint().height();
    }
    if (detailsToggleButton_) {
        height += detailsToggleButton_->sizeHint().height();
    }
    if (detailsVisible_ && detailsStack_) {
        height += qMax(140, detailsStack_->minimumSizeHint().height());
    }
    return height + 12;
}

void EngineOutputWidget::setEngineName(const QString &name) {
    if (engineName_ == name) {
        return;
    }
    engineName_ = name;
    updateHeaderSummary();
}

void EngineOutputWidget::setEngineStatus(const QString &status) {
    if (engineStatus_ == status) {
        return;
    }
    engineStatus_ = status;
    updateHeaderSummary();
}

void EngineOutputWidget::setDepth(int depth, int seldepth) {
    depth_ = depth;
    seldepth_ = seldepth;
    updateHeaderSummary();
}

void EngineOutputWidget::setScore(double scoreCp, std::optional<int> mateIn) {
    scoreText_ = formatScore(scoreCp, mateIn);
    updateHeaderSummary();
}

void EngineOutputWidget::setScoreText(const QString &scoreText) {
    scoreText_ = scoreText;
    updateHeaderSummary();
}

void EngineOutputWidget::setNodes(qint64 nodes) {
    nodes_ = nodes;
    updateHeaderSummary();
}

void EngineOutputWidget::setNodesPerSecond(qint64 nps) {
    nps_ = nps;
    updateHeaderSummary();
}

void EngineOutputWidget::setTime(qint64 timeMs) {
    timeMs_ = timeMs;
    updateHeaderSummary();
}

void EngineOutputWidget::setBestMove(const QString &bestMove) {
    bestMove_ = bestMove;
    updateHeaderSummary();
}

void EngineOutputWidget::setControlButtonsEnabled(bool startEnabled, bool pauseEnabled, bool stopEnabled) {
    if (startButton_) {
        startButton_->setEnabled(startEnabled);
    }
    if (pauseButton_) {
        pauseButton_->setEnabled(pauseEnabled);
    }
    if (stopButton_) {
        stopButton_->setEnabled(stopEnabled);
    }
}

void EngineOutputWidget::updateAnalysisLine(const EngineAnalysisLine &line) {
    if (line.multipv <= 1) {
        if (line.depth.has_value()) {
            depth_ = *line.depth;
        }
        if (line.seldepth.has_value()) {
            seldepth_ = *line.seldepth;
        }
        if (line.scoreCp.has_value() || line.mateIn.has_value()) {
            scoreText_ = formatScore(line.scoreCp.value_or(0.0), line.mateIn);
        }
        if (line.nodes.has_value()) {
            nodes_ = *line.nodes;
        }
        if (line.nps.has_value()) {
            nps_ = *line.nps;
        }
        if (line.timeMs.has_value()) {
            timeMs_ = *line.timeMs;
        }

        const QString firstMove = line.pv.section(QChar(' '), 0, 0);
        if (!firstMove.isEmpty()) {
            bestMove_ = firstMove;
        }
        updateHeaderSummary();
    }

    bool found = false;
    for (auto &existing : analysisLines_) {
        if (existing.multipv == line.multipv) {
            if (line.depth.has_value()) existing.depth = line.depth;
            if (line.seldepth.has_value()) existing.seldepth = line.seldepth;
            if (line.scoreCp.has_value()) existing.scoreCp = line.scoreCp;
            if (line.mateIn.has_value()) existing.mateIn = line.mateIn;
            if (line.nodes.has_value()) existing.nodes = line.nodes;
            if (line.nps.has_value()) existing.nps = line.nps;
            if (line.timeMs.has_value()) existing.timeMs = line.timeMs;
            if (!line.pv.isEmpty()) existing.pv = line.pv;
            if (!line.currmove.isEmpty()) existing.currmove = line.currmove;
            if (line.currmovenumber.has_value()) existing.currmovenumber = line.currmovenumber;
            found = true;
            break;
        }
    }

    if (!found) {
        analysisLines_.append(line);
        std::sort(analysisLines_.begin(), analysisLines_.end(),
                  [](const EngineAnalysisLine &a, const EngineAnalysisLine &b) {
                      return a.multipv < b.multipv;
                  });
    }

    updateTableDisplay();
    updateHeaderSummary();
}

void EngineOutputWidget::setAnalysisLines(const QList<EngineAnalysisLine> &lines) {
    analysisLines_ = lines;
    std::sort(analysisLines_.begin(), analysisLines_.end(),
              [](const EngineAnalysisLine &a, const EngineAnalysisLine &b) {
                  return a.multipv < b.multipv;
              });

    if (!analysisLines_.isEmpty()) {
        const auto &best = analysisLines_.first();
        if (best.depth.has_value()) depth_ = *best.depth;
        if (best.seldepth.has_value()) seldepth_ = *best.seldepth;
        if (best.scoreCp.has_value() || best.mateIn.has_value()) {
            scoreText_ = formatScore(best.scoreCp.value_or(0.0), best.mateIn);
        }
        if (best.nodes.has_value()) nodes_ = *best.nodes;
        if (best.nps.has_value()) nps_ = *best.nps;
        if (best.timeMs.has_value()) timeMs_ = *best.timeMs;
        const QString firstMove = best.pv.section(QChar(' '), 0, 0);
        if (!firstMove.isEmpty()) {
            bestMove_ = firstMove;
        }
        updateHeaderSummary();
    }

    updateTableDisplay();
    updateHeaderSummary();
}

void EngineOutputWidget::appendUciLog(const QString &line) {
    if (logEdit_) {
        logEdit_->appendPlainText(line);
    }
}

void EngineOutputWidget::clearLog() {
    if (logEdit_) {
        logEdit_->clear();
    }
}

void EngineOutputWidget::clearAnalysis() {
    depth_ = 0;
    seldepth_ = 0;
    scoreText_ = QStringLiteral("-");
    nodes_ = 0;
    nps_ = 0;
    timeMs_ = 0;
    bestMove_ = QStringLiteral("-");
    analysisLines_.clear();

    updateHeaderSummary();
    updateTableDisplay();
}

void EngineOutputWidget::clear() {
    clearAnalysis();
    clearLog();
}

void EngineOutputWidget::setAnalysisSectionVisible(bool visible) {
    if (visible) {
        setDetailsPage(DetailsPage::Variations);
        setDetailsVisible(true);
    } else if (detailsPage_ == DetailsPage::Variations) {
        setDetailsVisible(false);
    }
    if (analysisSectionVisible_ != visible) {
        analysisSectionVisible_ = visible;
        emit analysisSectionToggled(visible);
    }
}

void EngineOutputWidget::setLogSectionVisible(bool visible) {
    if (visible) {
        setDetailsPage(DetailsPage::UciLog);
        setDetailsVisible(true);
    } else if (detailsPage_ == DetailsPage::UciLog) {
        setDetailsVisible(false);
    }
    if (logSectionVisible_ != visible) {
        logSectionVisible_ = visible;
        emit logSectionToggled(visible);
    }
}

void EngineOutputWidget::setDetailsVisible(bool visible) {
    if (detailsVisible_ == visible) {
        return;
    }
    const bool oldAnalysisVisible = analysisSectionVisible_;
    const bool oldLogVisible = logSectionVisible_;
    detailsVisible_ = visible;
    analysisSectionVisible_ = visible && detailsPage_ == DetailsPage::Variations;
    logSectionVisible_ = visible && detailsPage_ == DetailsPage::UciLog;
    if (detailsStack_) {
        detailsStack_->setVisible(visible);
    }
    if (detailsToggleButton_) {
        const QSignalBlocker blocker(detailsToggleButton_);
        detailsToggleButton_->setChecked(visible);
        detailsToggleButton_->setArrowType(visible ? Qt::DownArrow : Qt::RightArrow);
    }
    if (analysisToggleButton_) {
        analysisToggleButton_->setVisible(visible);
    }
    if (logToggleButton_) {
        logToggleButton_->setVisible(visible);
    }
    if (oldAnalysisVisible != analysisSectionVisible_) {
        emit analysisSectionToggled(analysisSectionVisible_);
    }
    if (oldLogVisible != logSectionVisible_) {
        emit logSectionToggled(logSectionVisible_);
    }
    emit detailsToggled(visible);
}

void EngineOutputWidget::setDetailsPage(DetailsPage page) {
    if (page != DetailsPage::Variations && page != DetailsPage::UciLog) {
        page = DetailsPage::Variations;
    }
    if (detailsPage_ == page) {
        return;
    }
    const bool oldAnalysisVisible = analysisSectionVisible_;
    const bool oldLogVisible = logSectionVisible_;
    detailsPage_ = page;
    analysisSectionVisible_ = detailsVisible_ && page == DetailsPage::Variations;
    logSectionVisible_ = detailsVisible_ && page == DetailsPage::UciLog;
    if (detailsStack_) {
        detailsStack_->setCurrentIndex(static_cast<int>(page));
    }
    if (analysisToggleButton_ && logToggleButton_) {
        const QSignalBlocker analysisBlocker(analysisToggleButton_);
        const QSignalBlocker logBlocker(logToggleButton_);
        analysisToggleButton_->setChecked(page == DetailsPage::Variations);
        logToggleButton_->setChecked(page == DetailsPage::UciLog);
    }
    if (oldAnalysisVisible != analysisSectionVisible_) {
        emit analysisSectionToggled(analysisSectionVisible_);
    }
    if (oldLogVisible != logSectionVisible_) {
        emit logSectionToggled(logSectionVisible_);
    }
    emit detailsPageChanged(page);
}

void EngineOutputWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->setSpacing(4);

    // --- Compact summary (always visible) ---
    headerFrame_ = new QFrame(this);
    headerFrame_->setFrameShape(QFrame::StyledPanel);
    headerFrame_->setFrameShadow(QFrame::Raised);

    auto *headerLayout = new QGridLayout(headerFrame_);
    headerLayout->setContentsMargins(6, 4, 6, 4);
    headerLayout->setHorizontalSpacing(10);
    headerLayout->setVerticalSpacing(2);

    nameLabel_ = new QLabel(headerFrame_);
    statusLabel_ = new QLabel(headerFrame_);
    scoreLabel_ = new QLabel(headerFrame_);
    depthLabel_ = new QLabel(headerFrame_);
    bestMoveLabel_ = new QLabel(headerFrame_);
    primaryPvLabel_ = new QLabel(headerFrame_);
    primaryPvLabel_->setObjectName(QStringLiteral("primaryPvLabel"));
    primaryPvLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                             Qt::TextSelectableByKeyboard);
    primaryPvLabel_->setFocusPolicy(Qt::StrongFocus);
    primaryPvLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    for (QLabel *label : {nameLabel_, statusLabel_, depthLabel_, bestMoveLabel_}) {
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    QFont scoreFont = scoreLabel_->font();
    scoreFont.setPointSizeF(scoreFont.pointSizeF() * 1.25);
    scoreFont.setWeight(QFont::DemiBold);
    scoreLabel_->setFont(scoreFont);

    // The compact row keeps the high-value information visible at 800x600.
    startButton_ = new QPushButton(tr("Start"), headerFrame_);
    startButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    startButton_->setEnabled(false);

    pauseButton_ = new QPushButton(tr("Pause"), headerFrame_);
    pauseButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    pauseButton_->setEnabled(false);

    stopButton_ = new QPushButton(tr("Stop"), headerFrame_);
    stopButton_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stopButton_->setEnabled(false);

    connect(startButton_, &QPushButton::clicked, this, &EngineOutputWidget::startClicked);
    connect(pauseButton_, &QPushButton::clicked, this, &EngineOutputWidget::pauseClicked);
    connect(stopButton_, &QPushButton::clicked, this, &EngineOutputWidget::stopClicked);

    auto *controlLayout = new QHBoxLayout();
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(4);
    controlLayout->addWidget(startButton_);
    controlLayout->addWidget(pauseButton_);
    controlLayout->addWidget(stopButton_);

    headerLayout->addWidget(nameLabel_, 0, 0);
    headerLayout->addWidget(statusLabel_, 0, 1);
    headerLayout->addWidget(scoreLabel_, 0, 2);
    headerLayout->addWidget(depthLabel_, 0, 3);
    headerLayout->addWidget(bestMoveLabel_, 0, 4);
    headerLayout->addLayout(controlLayout, 0, 5);
    headerLayout->addWidget(primaryPvLabel_, 1, 0, 1, 5);
    headerLayout->setColumnStretch(0, 1);
    headerLayout->setColumnStretch(1, 1);
    headerLayout->setColumnStretch(4, 1);

    mainLayout->addWidget(headerFrame_);

    // --- Details navigation and stacked expert views ---
    auto *detailsHeader = new QHBoxLayout();
    detailsHeader->setContentsMargins(0, 0, 0, 0);
    detailsToggleButton_ = new QToolButton(this);
    detailsToggleButton_->setCheckable(true);
    detailsToggleButton_->setObjectName(QStringLiteral("detailsToggleButton"));
    detailsToggleButton_->setChecked(false);
    detailsToggleButton_->setArrowType(Qt::RightArrow);
    detailsToggleButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailsToggleButton_->setText(tr("Details"));
    detailsToggleButton_->setToolTip(tr("Show detailed engine analysis and the UCI log"));
    detailsToggleButton_->setAccessibleName(tr("Engine details"));
    detailsToggleButton_->setAccessibleDescription(
        tr("Show or hide detailed engine analysis and the UCI log"));

    analysisToggleButton_ = new QToolButton(this);
    analysisToggleButton_->setCheckable(true);
    analysisToggleButton_->setChecked(true);
    analysisToggleButton_->setAutoRaise(true);
    analysisToggleButton_->setText(tr("Variations"));
    analysisToggleButton_->setToolTip(tr("Show engine principal variations"));
    analysisToggleButton_->setAccessibleDescription(
        tr("Show the engine principal variations table"));

    logToggleButton_ = new QToolButton(this);
    logToggleButton_->setCheckable(true);
    logToggleButton_->setChecked(false);
    logToggleButton_->setAutoRaise(true);
    logToggleButton_->setText(tr("UCI Log"));
    logToggleButton_->setToolTip(tr("Show the UCI protocol log"));
    logToggleButton_->setAccessibleDescription(tr("Show the UCI protocol log"));

    detailsHeader->addWidget(detailsToggleButton_);
    detailsHeader->addStretch();
    detailsHeader->addWidget(analysisToggleButton_);
    detailsHeader->addWidget(logToggleButton_);
    mainLayout->addLayout(detailsHeader);

    analysisSection_ = new QWidget(this);
    nodesLabel_ = new QLabel(analysisSection_);
    npsLabel_ = new QLabel(analysisSection_);
    timeLabel_ = new QLabel(analysisSection_);
    auto *analysisLayout = new QVBoxLayout(analysisSection_);
    analysisLayout->setContentsMargins(0, 0, 0, 0);

    auto *metricsLayout = new QHBoxLayout();
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(12);
    metricsLayout->addWidget(nodesLabel_);
    metricsLayout->addWidget(npsLabel_);
    metricsLayout->addWidget(timeLabel_);
    metricsLayout->addStretch();
    analysisLayout->addLayout(metricsLayout);

    pvTable_ = new QTableWidget(0, 5, analysisSection_);
    pvTable_->setHorizontalHeaderLabels({
        tr("#"),
        tr("Eval"),
        tr("Depth"),
        tr("Nodes"),
        tr("Principal Variation (PV)")
    });
    pvTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    pvTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pvTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pvTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    pvTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    pvTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pvTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pvTable_->setAlternatingRowColors(true);
    pvTable_->verticalHeader()->setVisible(false);
    analysisLayout->addWidget(pvTable_, 1);

    connect(pvTable_, &QTableWidget::activated, this, [this](const QModelIndex &index) {
        const int row = index.row();
        if (row >= 0 && row < analysisLines_.size()) {
            emit lineSelected(analysisLines_.at(row).multipv,
                              analysisLines_.at(row).pv);
        }
    });

    logSection_ = new QWidget(this);
    auto *logLayout = new QVBoxLayout(logSection_);
    logLayout->setContentsMargins(0, 0, 0, 0);

    logEdit_ = new QPlainTextEdit(logSection_);
    logEdit_->setReadOnly(true);
    logEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto *logButtonLayout = new QHBoxLayout();
    clearLogButton_ = new QPushButton(tr("Clear Log"), logSection_);
    connect(clearLogButton_, &QPushButton::clicked, this, &EngineOutputWidget::clearLog);
    logButtonLayout->addStretch();
    logButtonLayout->addWidget(clearLogButton_);

    logLayout->addWidget(logEdit_);
    logLayout->addLayout(logButtonLayout);

    detailsStack_ = new QStackedWidget(this);
    detailsStack_->addWidget(analysisSection_);
    detailsStack_->addWidget(logSection_);
    detailsStack_->setCurrentIndex(static_cast<int>(detailsPage_));
    detailsStack_->setVisible(false);
    analysisToggleButton_->setVisible(false);
    logToggleButton_->setVisible(false);
    mainLayout->addWidget(detailsStack_, 1);

    connect(detailsToggleButton_, &QToolButton::toggled,
            this, &EngineOutputWidget::setDetailsVisible);
    connect(analysisToggleButton_, &QToolButton::clicked,
            this, [this] { setDetailsPage(DetailsPage::Variations); });
    connect(logToggleButton_, &QToolButton::clicked,
            this, [this] { setDetailsPage(DetailsPage::UciLog); });

    analysisSectionVisible_ = false;
    logSectionVisible_ = false;
}

void EngineOutputWidget::updateHeaderSummary() {
    if (!nameLabel_) {
        return;
    }

    nameLabel_->setText(tr("<b>Engine:</b> %1").arg(engineName_));
    statusLabel_->setText(tr("<b>Status:</b> %1").arg(engineStatus_));
    scoreLabel_->setText(tr("<b>Eval:</b> %1").arg(scoreText_));
    bestMoveLabel_->setText(tr("<b>Best:</b> %1").arg(bestMove_));

    if (depth_ > 0) {
        if (seldepth_ > 0) {
            depthLabel_->setText(tr("<b>Depth:</b> %1/%2").arg(depth_).arg(seldepth_));
        } else {
            depthLabel_->setText(tr("<b>Depth:</b> %1").arg(depth_));
        }
    } else {
        depthLabel_->setText(tr("<b>Depth:</b> -"));
    }

    nodesLabel_->setText(tr("<b>Nodes:</b> %1").arg(formatNodes(nodes_)));
    npsLabel_->setText(tr("<b>Speed:</b> %1").arg(formatNps(nps_)));
    timeLabel_->setText(tr("<b>Time:</b> %1").arg(formatTime(timeMs_)));

    QString primaryPv = QStringLiteral("-");
    if (!analysisLines_.isEmpty() && !analysisLines_.first().pv.isEmpty()) {
        primaryPv = analysisLines_.first().pv;
    }
    if (primaryPvLabel_) {
        primaryPvLabel_->setText(
            tr("<b>PV:</b> %1").arg(primaryPv.toHtmlEscaped()));
        primaryPvLabel_->setToolTip(primaryPv);
        primaryPvLabel_->setAccessibleDescription(
            tr("Principal variation: %1").arg(primaryPv));
    }
}

void EngineOutputWidget::updateTableDisplay() {
    if (!pvTable_) {
        return;
    }

    pvTable_->setRowCount(analysisLines_.size());
    for (int i = 0; i < analysisLines_.size(); ++i) {
        const auto &line = analysisLines_[i];

        auto *rankItem = new QTableWidgetItem(QString::number(line.multipv));
        rankItem->setTextAlignment(Qt::AlignCenter);

        QString scoreStr = QStringLiteral("-");
        if (line.scoreCp.has_value() || line.mateIn.has_value()) {
            scoreStr = formatScore(line.scoreCp.value_or(0.0), line.mateIn);
        }
        auto *scoreItem = new QTableWidgetItem(scoreStr);
        scoreItem->setTextAlignment(Qt::AlignCenter);

        QString depthStr = QStringLiteral("-");
        if (line.depth.has_value()) {
            depthStr = QString::number(*line.depth);
            if (line.seldepth.has_value() && *line.seldepth > 0) {
                depthStr += tr("/%1").arg(*line.seldepth);
            }
        }
        auto *depthItem = new QTableWidgetItem(depthStr);
        depthItem->setTextAlignment(Qt::AlignCenter);

        auto *nodesItem = new QTableWidgetItem(formatNodes(line.nodes.value_or(0)));
        nodesItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto *pvItem = new QTableWidgetItem(line.pv);

        pvTable_->setItem(i, 0, rankItem);
        pvTable_->setItem(i, 1, scoreItem);
        pvTable_->setItem(i, 2, depthItem);
        pvTable_->setItem(i, 3, nodesItem);
        pvTable_->setItem(i, 4, pvItem);
    }
}

QString EngineOutputWidget::formatScore(double scoreCp, std::optional<int> mateIn) {
    if (mateIn.has_value()) {
        const int mate = *mateIn;
        if (mate > 0) {
            return tr("+M%1").arg(mate);
        }
        if (mate < 0) {
            return tr("-M%1").arg(-mate);
        }
        return tr("Mate");
    }

    const double scorePawns = scoreCp / 100.0;
    const QString sign = scorePawns > 0.0 ? tr("+") : QString();
    return tr("%1%2").arg(sign).arg(scorePawns, 0, 'f', 2);
}

QString EngineOutputWidget::formatNodes(qint64 nodes) {
    if (nodes <= 0) {
        return QStringLiteral("-");
    }
    if (nodes >= 1'000'000) {
        return tr("%1 M").arg(static_cast<double>(nodes) / 1'000'000.0, 0, 'f', 2);
    }
    if (nodes >= 1'000) {
        return tr("%1 k").arg(static_cast<double>(nodes) / 1'000.0, 0, 'f', 1);
    }
    return QString::number(nodes);
}

QString EngineOutputWidget::formatNps(qint64 nps) {
    if (nps <= 0) {
        return QStringLiteral("-");
    }
    if (nps >= 1'000'000) {
        return tr("%1 MN/s").arg(static_cast<double>(nps) / 1'000'000.0, 0, 'f', 2);
    }
    if (nps >= 1'000) {
        return tr("%1 kN/s").arg(static_cast<double>(nps) / 1'000.0, 0, 'f', 1);
    }
    return tr("%1 N/s").arg(nps);
}

QString EngineOutputWidget::formatTime(qint64 timeMs) {
    if (timeMs <= 0) {
        return QStringLiteral("-");
    }
    if (timeMs >= 60'000) {
        const int mins = static_cast<int>(timeMs / 60'000);
        const int secs = static_cast<int>((timeMs % 60'000) / 1'000);
        return tr("%1m %2s").arg(mins).arg(secs, 2, 10, QChar('0'));
    }
    if (timeMs >= 1'000) {
        return tr("%1 s").arg(static_cast<double>(timeMs) / 1'000.0, 0, 'f', 1);
    }
    return tr("%1 ms").arg(timeMs);
}
