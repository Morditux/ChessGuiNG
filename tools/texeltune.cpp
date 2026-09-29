//
// Texel-style calibration of the evaluation weights.
//
//   texeltune corpus.txt [options]
//
// The corpus holds one quiet position per line, `FEN | result`, the result being
// the outcome of the game it came from for White (1, 0.5 or 0); tools/
// genpositions.py makes one. Every position is scored by the quiescence search
// (`search(rules, 0, 2)`, never the bare static score) and turned into a winning
// chance by the calibrated slope of winprobability.h, which stays fixed: it is
// the evaluation that has to adapt to the model the gauge, the curve and the
// report share, not the other way round. The tuner minimises the mean squared
// error between that chance and the game result by local search, moving one
// weight at a time by a step inside the bounds of evalparamfields.h and keeping
// a move when the error falls; the step shrinks from pass to pass.
//
//   --passes N           passes over the weights (default 6)
//   --steps A,B,C        step of each pass, repeated (default 4,2,1)
//   --threads N          evaluation threads (default: the hardware's)
//   --max N              use at most N positions
//   --holdout PERCENT    keep that share of the positions out of the fit and
//                        report its error too (default 10)
//   --only A,B / --skip A,B   restrict or exclude weights by key
//   --write-header FILE  patch the defaults of the changed weights into FILE
//                        (heuristiceval.h) and print the new EvalParamsVersion
//                        to set
//
// Build it with -DCHESSGUI_BUILD_TOOLS=ON.
//

#include "evalparamfields.h"
#include "heuristiceval.h"
#include "rules.h"
#include "winprobability.h"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Sample {
    Rules rules;
    double result = 0.5;
};

std::vector<std::string> split(const std::string &text, char separator) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, separator)) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

// Weights that a calibration on quiet middlegame and endgame positions cannot
// judge: the anchor of the scale, the divisors and thresholds, and the endgame
// scaling and known-win values, which the corpus barely covers.
const std::set<std::string> DefaultSkip = {
    "pawnValue",           "hangingPieceDivisor", "cheapAttackerMargin",
    "cheapAttackerDivisor", "kingDangerDivisor",   "mopUpMaterialThreshold",
    "knownWinScore",       "knownWinRankBonus",   "unstoppablePasserBonus",
    "unstoppablePasserPerMove", "oppositeBishopsScale",
    "oppositeBishopsNoPawnsScale", "noPawnsMinorScale",
    "noPawnsRookVsMinorScale", "noPawnsRookScale", "generalScaleBase",
    "generalScalePerPawn", "rookEndgameOneFlankScale"};

class Corpus {
public:
    bool load(const std::string &path, std::size_t limit) {
        std::ifstream input(path);
        if (!input) {
            return false;
        }
        std::string line;
        while (std::getline(input, line) && samples_.size() < limit) {
            const auto bar = line.rfind('|');
            if (bar == std::string::npos) {
                continue;
            }
            Sample sample;
            if (!sample.rules.loadFen(QString::fromStdString(line.substr(0, bar)))) {
                continue;
            }
            sample.rules.setTrackRepetition(false);
            sample.result = std::atof(line.c_str() + bar + 1);
            samples_.push_back(std::move(sample));
        }
        return !samples_.empty();
    }

    void splitHoldout(int percent) {
        // The last positions come from different games than the first ones, so a
        // tail is a fair holdout.
        holdoutStart_ = samples_.size() -
                        samples_.size() * static_cast<std::size_t>(percent) / 100;
    }

    [[nodiscard]] std::size_t size() const { return samples_.size(); }
    [[nodiscard]] std::size_t trainSize() const { return holdoutStart_; }

    // Mean squared error over [first, last), the evaluation weights being the
    // ones currently set.
    [[nodiscard]] double error(std::size_t first, std::size_t last,
                               int threads) const {
        std::vector<double> sums(static_cast<std::size_t>(threads), 0.0);
        std::vector<std::thread> pool;
        const std::size_t span = (last - first + threads - 1) / threads;
        for (int t = 0; t < threads; ++t) {
            const std::size_t begin = first + span * t;
            const std::size_t end = std::min(last, begin + span);
            pool.emplace_back([this, begin, end, &sums, t] {
                double sum = 0.0;
                for (std::size_t i = begin; i < end; ++i) {
                    const auto result = HeuristicEval::search(samples_[i].rules, 0, 2);
                    const double chance =
                        WinProbability::fromCentipawns(result.centipawns) / 100.0;
                    const double difference = chance - samples_[i].result;
                    sum += difference * difference;
                }
                sums[static_cast<std::size_t>(t)] = sum;
            });
        }
        for (std::thread &thread : pool) {
            thread.join();
        }
        double total = 0.0;
        for (const double sum : sums) {
            total += sum;
        }
        return last > first ? total / static_cast<double>(last - first) : 0.0;
    }

private:
    std::vector<Sample> samples_;
    std::size_t holdoutStart_ = 0;
};

// Rewrites `int key = old;` to the new value in the header text.
bool patchDefault(std::string *text, const std::string &key, int value) {
    const std::string needle = "int " + key + " = ";
    const auto at = text->find(needle);
    if (at == std::string::npos) {
        return false;
    }
    const auto end = text->find(';', at);
    text->replace(at + needle.size(), end - at - needle.size(),
                  std::to_string(value));
    return true;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: texeltune corpus.txt [options]; see the source header\n");
        return 2;
    }
    std::string corpusPath = argv[1];
    int passes = 6;
    std::vector<int> steps = {4, 2, 1};
    int threads = std::max(1u, std::thread::hardware_concurrency());
    std::size_t limit = std::numeric_limits<std::size_t>::max();
    int holdoutPercent = 10;
    std::set<std::string> only;
    std::set<std::string> skip = DefaultSkip;
    std::string headerPath;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string option = argv[i];
        const std::string value = argv[i + 1];
        if (option == "--passes") {
            passes = std::atoi(value.c_str());
        } else if (option == "--steps") {
            steps.clear();
            for (const auto &part : split(value, ',')) {
                steps.push_back(std::max(1, std::atoi(part.c_str())));
            }
        } else if (option == "--threads") {
            threads = std::max(1, std::atoi(value.c_str()));
        } else if (option == "--max") {
            limit = static_cast<std::size_t>(std::atoll(value.c_str()));
        } else if (option == "--holdout") {
            holdoutPercent = std::clamp(std::atoi(value.c_str()), 0, 50);
        } else if (option == "--only") {
            for (const auto &part : split(value, ',')) {
                only.insert(part);
            }
        } else if (option == "--skip") {
            for (const auto &part : split(value, ',')) {
                skip.insert(part);
            }
        } else if (option == "--write-header") {
            headerPath = value;
        } else {
            std::fprintf(stderr, "unknown option %s\n", option.c_str());
            return 2;
        }
    }

    Corpus corpus;
    if (!corpus.load(corpusPath, limit)) {
        std::fprintf(stderr, "cannot read positions from %s\n", corpusPath.c_str());
        return 1;
    }
    corpus.splitHoldout(holdoutPercent);
    std::printf("%zu positions (%zu to fit), %d threads\n", corpus.size(),
                corpus.trainSize(), threads);

    HeuristicEval::resetParams();
    HeuristicEval::EvalParams params = HeuristicEval::params();
    const HeuristicEval::EvalParams defaults = params;
    std::vector<const EvalParamFields::Field *> fields;
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        if ((only.empty() || only.count(field.key)) && !skip.count(field.key)) {
            fields.push_back(&field);
        }
    }
    std::printf("tuning %zu weights\n", fields.size());

    const auto trainError = [&](const HeuristicEval::EvalParams &candidate) {
        HeuristicEval::setParams(candidate);
        return corpus.error(0, corpus.trainSize(), threads);
    };
    double best = trainError(params);
    const double initial = best;
    const double initialHoldout =
        corpus.trainSize() < corpus.size()
            ? corpus.error(corpus.trainSize(), corpus.size(), threads) : 0.0;
    std::printf("start: fit %.6f holdout %.6f\n", best, initialHoldout);

    const auto started = std::chrono::steady_clock::now();
    for (int pass = 0; pass < passes; ++pass) {
        const int step = steps[static_cast<std::size_t>(pass) % steps.size()];
        int improved = 0;
        for (const EvalParamFields::Field *field : fields) {
            for (const int direction : {1, -1}) {
                bool moved = false;
                while (true) {
                    HeuristicEval::EvalParams candidate = params;
                    const int value = candidate.*field->member + direction * step;
                    if (value < field->minimum || value > field->maximum) {
                        break;
                    }
                    candidate.*field->member = value;
                    const double error = trainError(candidate);
                    if (error < best - 1e-9) {
                        best = error;
                        params = candidate;
                        moved = true;
                        ++improved;
                    } else {
                        break;
                    }
                }
                if (moved) {
                    break;
                }
            }
        }
        const double seconds = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - started).count();
        std::printf("pass %d (step %d): fit %.6f, %d weights moved, %.0f s\n",
                    pass + 1, step, best, improved, seconds);
        std::fflush(stdout);
        if (improved == 0 && step == 1) {
            break;
        }
    }

    HeuristicEval::setParams(params);
    const double finalHoldout =
        corpus.trainSize() < corpus.size()
            ? corpus.error(corpus.trainSize(), corpus.size(), threads) : 0.0;
    std::printf("end: fit %.6f (from %.6f) holdout %.6f (from %.6f)\n", best,
                initial, finalHoldout, initialHoldout);

    std::string header;
    if (!headerPath.empty()) {
        std::ifstream in(headerPath);
        std::stringstream buffer;
        buffer << in.rdbuf();
        header = buffer.str();
    }
    int changed = 0;
    for (const EvalParamFields::Field &field : EvalParamFields::all()) {
        const int before = defaults.*field.member;
        const int after = params.*field.member;
        if (before == after) {
            continue;
        }
        ++changed;
        std::printf("%s %d -> %d\n", field.key, before, after);
        if (!headerPath.empty() && !patchDefault(&header, field.key, after)) {
            std::fprintf(stderr, "cannot find the default of %s in %s\n",
                         field.key, headerPath.c_str());
        }
    }
    if (!headerPath.empty() && changed > 0) {
        std::ofstream out(headerPath);
        out << header;
        std::printf("patched %d defaults in %s; increment EvalParamsVersion\n",
                    changed, headerPath.c_str());
    }
    return 0;
}
