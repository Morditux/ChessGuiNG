//
// Developer probe for the classical evaluator.
//
//   evalprobe breakdown < positions.txt   per-term breakdown of every position
//   evalprobe bench                       node throughput and evaluation cost
//   evalprobe score < positions.txt       static score of every position
//   evalprobe kpk < fens.txt              win/draw of king-pawn-king positions
//   evalprobe signature DEPTH < fens.txt  cold single-thread search signature
//                                         (score, move, nodes, mate distance),
//                                         the values the reference tests pin
//
// Position files hold one "label | FEN" per line; blank lines and lines
// starting with '#' are skipped. Build it with -DCHESSGUI_BUILD_TOOLS=ON.
//

#include "heuristiceval.h"
#include "kpkbitbase.h"
#include "rules.h"

#include <QCoreApplication>
#include <QString>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

struct BenchPosition {
    const char *label;
    const char *fen;
};

constexpr BenchPosition BenchPositions[] = {
    {"middlegame",
     "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N2N2/PP2BPPP/R2QKB1R w KQ - 0 9"},
    {"rook ending", "8/5pk1/6p1/3R4/1r5P/6P1/5PK1/8 w - - 0 40"},
    {"opening", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"tactical",
     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
};

std::string trim(const std::string &text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

int breakdownCommand() {
    std::string line;
    int failures = 0;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto bar = line.find('|');
        const std::string label =
            bar == std::string::npos ? std::string() : trim(line.substr(0, bar));
        const std::string fen =
            trim(bar == std::string::npos ? line : line.substr(bar + 1));

        Rules rules;
        if (!rules.loadFen(QString::fromStdString(fen))) {
            std::printf("%s | illegal FEN: %s\n", label.c_str(), fen.c_str());
            ++failures;
            continue;
        }
        const auto b = HeuristicEval::evaluateBreakdown(rules);
        std::printf("%s | %s\n", label.c_str(), fen.c_str());
        std::printf("  total %+d  material %+d  placement %+d  pawns %+d  "
                    "space %+d  mobility %+d  kingSafety %+d\n",
                    b.total, b.material, b.placement, b.pawns, b.space,
                    b.mobility, b.kingSafety);
        std::printf("  threats %+d  outposts %+d  badBishops %+d  "
                    "connectedRooks %+d  mopUp %+d  tempo %+d  inCheck %+d\n",
                    b.threats, b.outposts, b.badBishops, b.connectedRooks,
                    b.mopUp, b.tempo, b.inCheck);
    }
    return failures == 0 ? 0 : 1;
}

// "label | FEN" or a bare FEN.
bool nextPosition(std::string *label, std::string *fen) {
    std::string line;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto bar = line.find('|');
        *label = bar == std::string::npos ? std::string()
                                          : trim(line.substr(0, bar));
        *fen = trim(bar == std::string::npos ? line : line.substr(bar + 1));
        return true;
    }
    return false;
}

int scoreCommand() {
    std::string label;
    std::string fen;
    while (nextPosition(&label, &fen)) {
        Rules rules;
        if (!rules.loadFen(QString::fromStdString(fen))) {
            std::printf("illegal | %s\n", fen.c_str());
            continue;
        }
        std::printf("%+d | %s\n", HeuristicEval::evaluateCentipawns(rules),
                    fen.c_str());
    }
    return 0;
}

// King and pawn against king positions: "win" or "draw" for the side with the
// pawn, straight from the bitbase.
int kpkCommand() {
    std::string label;
    std::string fen;
    while (nextPosition(&label, &fen)) {
        Rules rules;
        if (!rules.loadFen(QString::fromStdString(fen))) {
            std::printf("illegal | %s\n", fen.c_str());
            continue;
        }
        Rules::Position pawn;
        Rules::Position kings[2];
        Rules::Color pawnColor = Rules::Color::White;
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                const auto piece = rules.pieceAt({row, column});
                if (!piece) {
                    continue;
                }
                if (piece->type == Rules::PieceType::Pawn) {
                    pawn = {row, column};
                    pawnColor = piece->color;
                } else if (piece->type == Rules::PieceType::King) {
                    kings[piece->color == Rules::Color::White ? 0 : 1] = {row, column};
                }
            }
        }
        const int own = pawnColor == Rules::Color::White ? 0 : 1;
        std::printf("%s | %s\n",
                    KpkBitbase::isWin(pawnColor, rules.currentPlayer(), pawn,
                                      kings[own], kings[1 - own])
                        ? "win" : "draw",
                    fen.c_str());
    }
    return 0;
}

int signatureCommand(int depth) {
    HeuristicEval::resetParams();
    HeuristicEval::setSearchThreads(1);
    std::string label;
    std::string fen;
    while (nextPosition(&label, &fen)) {
        Rules rules;
        if (!rules.loadFen(QString::fromStdString(fen))) {
            std::printf("illegal | %s\n", fen.c_str());
            continue;
        }
        HeuristicEval::clearSearchCache();
        const auto result = HeuristicEval::search(rules, depth);
        const QString move =
            result.bestMove ? Rules::toUci(*result.bestMove) : QStringLiteral("-");
        std::printf("%d %s %d %d | %s\n", result.centipawns,
                    move.toLatin1().constData(), result.nodes,
                    result.mateIn.value_or(0), fen.c_str());
    }
    return 0;
}

int benchCommand() {
    using Clock = std::chrono::steady_clock;
    HeuristicEval::setSearchThreads(1);

    std::printf("%-12s %5s %10s %8s %9s %7s  %s\n", "position", "depth",
                "nodes", "ms", "knodes/s", "score", "move");
    for (const BenchPosition &position : BenchPositions) {
        Rules rules;
        if (!rules.loadFen(QString::fromLatin1(position.fen))) {
            std::printf("%-12s illegal FEN\n", position.label);
            continue;
        }
        for (int depth = 4; depth <= 6; ++depth) {
            HeuristicEval::clearSearchCache();
            const auto start = Clock::now();
            const auto result = HeuristicEval::search(rules, depth);
            const double ms = std::chrono::duration<double, std::milli>(
                                  Clock::now() - start).count();
            const QString move =
                result.bestMove ? Rules::toUci(*result.bestMove) : QString();
            std::printf("%-12s %5d %10d %8.0f %9.0f %+7d  %s\n", position.label,
                        depth, result.nodes, ms,
                        ms > 0 ? result.nodes / ms : 0.0, result.centipawns,
                        move.toLatin1().constData());
        }
    }

    Rules rules;
    rules.loadFen(QString::fromLatin1(BenchPositions[0].fen));
    constexpr int Calls = 200000;
    long long sink = 0;
    const auto start = Clock::now();
    for (int i = 0; i < Calls; ++i) {
        sink += HeuristicEval::evaluateCentipawns(rules);
    }
    const double us = std::chrono::duration<double, std::micro>(
                          Clock::now() - start).count();
    std::printf("evaluateCentipawns: %.2f us per call (%d calls, checksum %lld)\n",
                us / Calls, Calls, sink);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "breakdown") {
        return breakdownCommand();
    }
    if (command == "bench") {
        return benchCommand();
    }
    if (command == "score") {
        return scoreCommand();
    }
    if (command == "kpk") {
        return kpkCommand();
    }
    if (command == "signature" && argc > 2) {
        return signatureCommand(std::atoi(argv[2]));
    }
    std::fprintf(stderr,
                 "usage: evalprobe breakdown < positions.txt\n"
                 "       evalprobe bench\n"
                 "       evalprobe score < positions.txt\n"
                 "       evalprobe signature DEPTH < fens.txt\n");
    return 2;
}
