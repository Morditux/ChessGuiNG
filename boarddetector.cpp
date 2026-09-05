#include "boarddetector.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace
{
constexpr double NegativeInfinity =
    -std::numeric_limits<double>::infinity();

bool sameExtent(const std::pair<int, int>& a,
                const std::pair<int, int>& b,
                int tolerance = 2)
{
    return std::abs(a.first - b.first) <= tolerance &&
           std::abs(a.second - b.second) <= tolerance;
}
}

BoardDetector::BoardDetector(int maxDetectDimension)
    : maxDetectDimension_(maxDetectDimension)
{
}

std::vector<BoardDetector::Candidate>
BoardDetector::detect(const QImage& image) const
{
    if (image.isNull() || image.width() < 2 || image.height() < 2)
        return {};

    QImage detectionImage = image;

    if (maxDetectDimension_ > 0) {
        const int maxDimension = std::max(image.width(), image.height());

        if (maxDimension > maxDetectDimension_) {
            const double scale =
                static_cast<double>(maxDetectDimension_) /
                static_cast<double>(maxDimension);

            const int targetWidth =
                std::max(2, static_cast<int>(
                    std::lround(image.width() * scale)));

            const int targetHeight =
                std::max(2, static_cast<int>(
                    std::lround(image.height() * scale)));

            detectionImage = image.scaled(targetWidth,
                                          targetHeight,
                                          Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
        }
    }

    const GrayImage gray = toGray(detectionImage);

    const std::optional<Box> rawBox = findChessboardCorners(gray);
    if (!rawBox.has_value())
        return {};

    const Box snappedBox = snapCorners(gray, *rawBox);

    std::vector<Candidate> result;
    result.reserve(2);

    const QRect rawRect =
        mapBoxToOriginal(*rawBox,
                         gray.width,
                         gray.height,
                         image.width(),
                         image.height());

    result.push_back({
        rawRect,
        checkerboardScore(gray, *rawBox),
        false
    });

    if (snappedBox != *rawBox) {
        const QRect snappedRect =
            mapBoxToOriginal(snappedBox,
                             gray.width,
                             gray.height,
                             image.width(),
                             image.height());

        // Scaling back to the original image can occasionally collapse
        // two very close candidates to the same integer QRect.
        if (snappedRect != rawRect) {
            result.push_back({
                snappedRect,
                checkerboardScore(gray, snappedBox),
                true
            });
        }
    }

    return result;
}

BoardDetector::GrayImage
BoardDetector::toGray(const QImage& image) const
{
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);

    GrayImage out;
    out.width = argb.width();
    out.height = argb.height();
    out.data.resize(static_cast<std::size_t>(out.width) *
                    static_cast<std::size_t>(out.height));

    for (int y = 0; y < out.height; ++y) {
        const auto* row =
            reinterpret_cast<const QRgb*>(argb.constScanLine(y));

        for (int x = 0; x < out.width; ++x) {
            const QRgb p = row[x];

            const float r = static_cast<float>(qRed(p));
            const float g = static_cast<float>(qGreen(p));
            const float b = static_cast<float>(qBlue(p));

            out.data[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(out.width) +
                     static_cast<std::size_t>(x)] =
                0.299f * r +
                0.587f * g +
                0.114f * b;
        }
    }

    return out;
}

std::vector<float>
BoardDetector::gradientRows(const GrayImage& image) const
{
    const int width = image.width;
    const int height = image.height;

    std::vector<float> out(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height),
        0.0f);

    if (width <= 0 || height < 2)
        return out;

    for (int x = 0; x < width; ++x) {
        out[static_cast<std::size_t>(x)] =
            image.data[static_cast<std::size_t>(width + x)] -
            image.data[static_cast<std::size_t>(x)];

        const int last = (height - 1) * width;

        out[static_cast<std::size_t>(last + x)] =
            image.data[static_cast<std::size_t>(last + x)] -
            image.data[static_cast<std::size_t>(last - width + x)];
    }

    for (int y = 1; y < height - 1; ++y) {
        const int row = y * width;
        const int previous = (y - 1) * width;
        const int next = (y + 1) * width;

        for (int x = 0; x < width; ++x) {
            out[static_cast<std::size_t>(row + x)] =
                (image.data[static_cast<std::size_t>(next + x)] -
                 image.data[static_cast<std::size_t>(previous + x)]) *
                0.5f;
        }
    }

    return out;
}

std::vector<float>
BoardDetector::gradientCols(const GrayImage& image) {
    const int width = image.width;
    const int height = image.height;

    std::vector<float> out(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height),
        0.0f);

    if (width < 2 || height <= 0)
        return out;

    for (int y = 0; y < height; ++y) {
        const int row = y * width;

        out[static_cast<std::size_t>(row)] =
            image.data[static_cast<std::size_t>(row + 1)] -
            image.data[static_cast<std::size_t>(row)];

        out[static_cast<std::size_t>(row + width - 1)] =
            image.data[static_cast<std::size_t>(row + width - 1)] -
            image.data[static_cast<std::size_t>(row + width - 2)];

        for (int x = 1; x < width - 1; ++x) {
            out[static_cast<std::size_t>(row + x)] =
                (image.data[static_cast<std::size_t>(row + x + 1)] -
                 image.data[static_cast<std::size_t>(row + x - 1)]) *
                0.5f;
        }
    }

    return out;
}

std::vector<double>
BoardDetector::houghResponse(const std::vector<float>& gradient,
                             int width,
                             int height,
                             Axis axis) {
    const int count = axis == Axis::Rows ? height : width;

    std::vector<double> positive(
        static_cast<std::size_t>(count), 0.0);

    std::vector<double> negative(
        static_cast<std::size_t>(count), 0.0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float g =
                gradient[static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)];

            const int index = axis == Axis::Rows ? y : x;

            if (g > 0.0f)
                positive[static_cast<std::size_t>(index)] += g;
            else
                negative[static_cast<std::size_t>(index)] -= g;
        }
    }

    std::vector<double> out(
        static_cast<std::size_t>(count), 0.0);

    for (int i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)] =
            positive[static_cast<std::size_t>(i)] *
            negative[static_cast<std::size_t>(i)];
    }

    return out;
}

double BoardDetector::maxRange(const std::vector<double>& values,
                               int fromInclusive,
                               int toExclusive)
{
    if (values.empty())
        return NegativeInfinity;

    fromInclusive = std::max(0, fromInclusive);
    toExclusive =
        std::min(static_cast<int>(values.size()), toExclusive);

    if (fromInclusive >= toExclusive)
        return NegativeInfinity;

    double maximum = NegativeInfinity;

    for (int i = fromInclusive; i < toExclusive; ++i) {
        maximum =
            std::max(maximum, values[static_cast<std::size_t>(i)]);
    }

    return maximum;
}

std::vector<double>
BoardDetector::nonMaxSuppress(const std::vector<double>& values,
                              int windowSize) {
    std::vector<double> out = values;
    const int n = static_cast<int>(values.size());

    if (n == 0)
        return out;

    for (int i = 0; i < n; ++i) {
        const double left =
            i == 0
                ? 0.0
                : maxRange(values,
                           std::max(0, i - windowSize),
                           i);

        // Intentionally asymmetric to mirror the reference detector.
        const double right =
            i >= n - 2
                ? 0.0
                : maxRange(values,
                           i + 1,
                           std::min(n - 1, i + windowSize));

        if (values[static_cast<std::size_t>(i)] < left ||
            values[static_cast<std::size_t>(i)] <= right) {
            out[static_cast<std::size_t>(i)] = 0.0;
        }
    }

    return out;
}

std::vector<std::vector<int>>
BoardDetector::getAllSequences(
    const std::vector<int>& peakPositions) {
    if (peakPositions.size() <
        static_cast<std::size_t>(MinSequenceLength)) {
        return {};
    }

    std::vector<std::vector<int>> sequences;

    for (std::size_t i = 0;
         i + 1 < peakPositions.size();
         ++i) {
        for (std::size_t j = i + 1;
             j < peakPositions.size();
             ++j) {

            bool duplicate = false;

            for (const auto& previous : sequences) {
                for (std::size_t k = 0;
                     k + 1 < previous.size();
                     ++k) {
                    if (peakPositions[i] == previous[k] &&
                        peakPositions[j] == previous[k + 1]) {
                        duplicate = true;
                        break;
                    }
                }

                if (duplicate)
                    break;
            }

            if (duplicate)
                continue;

            const int spacing =
                peakPositions[j] - peakPositions[i];

            if (spacing < SequenceErrorPixels)
                continue;

            std::vector<int> sequence {
                peakPositions[i],
                peakPositions[j]
            };

            int expected = sequence.back() + spacing;

            for (;;) {
                int bestIndex = -1;
                int bestDistance =
                    std::numeric_limits<int>::max();

                for (std::size_t k = 0;
                     k < peakPositions.size();
                     ++k) {
                    const int distance =
                        std::abs(peakPositions[k] - expected);

                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestIndex = static_cast<int>(k);
                    }
                }

                if (bestIndex < 0 ||
                    bestDistance >= SequenceErrorPixels) {
                    break;
                }

                sequence.push_back(
                    peakPositions[static_cast<std::size_t>(bestIndex)]);

                expected = sequence.back() + spacing;
            }

            if (sequence.size() >=
                static_cast<std::size_t>(MinSequenceLength)) {
                sequences.push_back(std::move(sequence));
            }
        }
    }

    return sequences;
}

std::pair<std::vector<int>, std::vector<double>>
BoardDetector::trimSequence(
    const std::vector<int>& sequence,
    const std::vector<double>& values) {
    std::vector<int> positions = sequence;
    std::vector<double> strengths = values;

    // 8/9-line sequences remain intact. Longer sequences are reduced to 7.
    if (positions.size() > 9) {
        while (positions.size() > 7) {
            if (strengths.front() > strengths.back()) {
                positions.pop_back();
                strengths.pop_back();
            } else {
                positions.erase(positions.begin());
                strengths.erase(strengths.begin());
            }
        }
    }

    return {std::move(positions), std::move(strengths)};
}

std::vector<BoardDetector::CandidateSequence>
BoardDetector::rankedPeakSequences(
    const std::vector<double>& hough) {
    const std::vector<double> suppressed =
        nonMaxSuppress(hough);

    const double peak =
        maxRange(suppressed,
                 0,
                 static_cast<int>(suppressed.size()));

    if (!(peak > 0.0))
        return {};

    std::vector<int> positions;
    std::vector<double> normalizedValues;

    positions.reserve(suppressed.size());
    normalizedValues.reserve(suppressed.size());

    for (int i = 0;
         i < static_cast<int>(suppressed.size());
         ++i) {
        const double normalized =
            suppressed[static_cast<std::size_t>(i)] / peak;

        if (normalized >= PeakKeepRatio) {
            positions.push_back(i);
            normalizedValues.push_back(normalized);
        }
    }

    const auto sequences = getAllSequences(positions);

    if (sequences.empty())
        return {};

    std::unordered_map<int, double> strengthAt;
    strengthAt.reserve(positions.size());

    for (std::size_t i = 0; i < positions.size(); ++i) {
        strengthAt.emplace(positions[i], normalizedValues[i]);
    }

    struct ScoredSequence
    {
        CandidateSequence candidate;
        double score = 0.0;
    };

    std::vector<ScoredSequence> scored;
    scored.reserve(sequences.size());

    for (const auto& sequence : sequences) {
        std::vector<double> strengths;
        strengths.reserve(sequence.size());

        for (const int position : sequence) {
            const auto it = strengthAt.find(position);
            strengths.push_back(
                it != strengthAt.end() ? it->second : 0.0);
        }

        auto [trimmedPositions, trimmedStrengths] =
            trimSequence(sequence, strengths);

        const double sum =
            std::accumulate(trimmedStrengths.begin(),
                            trimmedStrengths.end(),
                            0.0);

        const double average =
            trimmedStrengths.empty()
                ? 0.0
                : sum / static_cast<double>(trimmedStrengths.size());

        scored.push_back({
            {std::move(trimmedPositions), sequence},
            average
        });
    }

    std::sort(
        scored.begin(),
        scored.end(),
        [](const ScoredSequence& a, const ScoredSequence& b) {
            return a.score > b.score;
        });

    std::vector<CandidateSequence> unique;
    unique.reserve(MaxCandidateSequences);

    for (const auto& item : scored) {
        bool exists = false;

        for (const auto& previous : unique) {
            if (previous.full == item.candidate.full) {
                exists = true;
                break;
            }
        }

        if (exists)
            continue;

        unique.push_back(item.candidate);

        if (unique.size() >=
            static_cast<std::size_t>(MaxCandidateSequences)) {
            break;
        }
    }

    return unique;
}

double BoardDetector::checkerboardScore(
    const GrayImage& image,
    const Box& box) {
    if (!box.valid())
        return 0.0;

    const int boxWidth = box.width();
    const int boxHeight = box.height();

    double score = 0.0;

    for (int targetY = 0; targetY < 64; ++targetY) {
        const int sourceY =
            box.y0 +
            static_cast<int>(std::floor(
                static_cast<double>(targetY) *
                static_cast<double>(boxHeight) / 64.0));

        for (int targetX = 0; targetX < 64; ++targetX) {
            const int sourceX =
                box.x0 +
                static_cast<int>(std::floor(
                    static_cast<double>(targetX) *
                    static_cast<double>(boxWidth) / 64.0));

            double pixel = 0.0;

            if (sourceX >= 0 && sourceX < image.width &&
                sourceY >= 0 && sourceY < image.height) {
                pixel =
                    image.data[
                        static_cast<std::size_t>(sourceY) *
                            static_cast<std::size_t>(image.width) +
                        static_cast<std::size_t>(sourceX)];
            }

            const bool evenSquare =
                ((targetX / 8) + (targetY / 8)) % 2 == 0;

            const double parity = evenSquare ? 1.0 : -1.0;

            score += parity * pixel / 64.0;
        }
    }

    return score;
}

double BoardDetector::median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());

    const std::size_t middle = values.size() / 2;

    if ((values.size() & 1U) != 0U)
        return values[middle];

    return (values[middle - 1] + values[middle]) * 0.5;
}

std::optional<BoardDetector::Reconstruction>
BoardDetector::reconstructSquareBoard(
    const GrayImage& image,
    const std::vector<int>& goodAxisLines,
    Axis goodAxis) {
    if (goodAxisLines.size() < 2)
        return std::nullopt;

    std::vector<double> spacings;
    spacings.reserve(goodAxisLines.size() - 1);

    for (std::size_t i = 1; i < goodAxisLines.size(); ++i) {
        spacings.push_back(
            static_cast<double>(goodAxisLines[i] - goodAxisLines[i - 1]));
    }

    const double tile = median(spacings);

    if (!(tile > 0.0))
        return std::nullopt;

    const int goodStart = goodAxisLines.front();
    const int goodEnd = goodAxisLines.back();

    if (goodEnd - goodStart <= 0)
        return std::nullopt;

    const int padding = static_cast<int>(std::lround(tile));

    std::vector<std::pair<int, int>> extents;
    extents.emplace_back(goodStart, goodEnd);

    for (std::size_t k = 0;
         k + 7 <= goodAxisLines.size();
         ++k) {
        const std::pair<int, int> extent {
            goodAxisLines[k] - padding,
            goodAxisLines[k + 6] + padding
        };

        bool duplicate = false;

        for (const auto& previous : extents) {
            if (sameExtent(previous, extent)) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            extents.push_back(extent);
    }

    const int weakAxisLimit =
        goodAxis == Axis::Cols ? image.height : image.width;

    std::optional<Box> bestBox;
    double bestScore = NegativeInfinity;

    for (const auto& [extentStart, extentEnd] : extents) {
        const int span = extentEnd - extentStart;

        if (span <= 0)
            continue;

        const int step =
            std::max(2,
                     static_cast<int>(std::lround(tile / 8.0)));

        for (int start = -span;
             start <= weakAxisLimit;
             start += step) {
            const int end = start + span;

            Box candidate;

            if (goodAxis == Axis::Cols) {
                candidate = {
                    extentStart,
                    start,
                    extentEnd,
                    end
                };
            } else {
                candidate = {
                    start,
                    extentStart,
                    end,
                    extentEnd
                };
            }

            const double score = checkerboardScore(image, candidate);

            if (score > bestScore) {
                bestScore = score;
                bestBox = candidate;
            }
        }
    }

    if (!bestBox.has_value())
        return std::nullopt;

    return Reconstruction {*bestBox, bestScore};
}

std::optional<BoardDetector::Box>
BoardDetector::reconstructFromCandidates(
    const GrayImage& image,
    const std::vector<CandidateSequence>& candidates,
    Axis goodAxis) {
    std::optional<Box> bestBox;
    double bestScore = NegativeInfinity;

    for (const auto& candidate : candidates) {
        const auto reconstruction =
            reconstructSquareBoard(image, candidate.full, goodAxis);

        if (reconstruction.has_value() &&
            reconstruction->score > bestScore) {
            bestScore = reconstruction->score;
            bestBox = reconstruction->box;
        }
    }

    return bestBox;
}

BoardDetector::Box
BoardDetector::repairParity(
    const GrayImage& image,
    const Box& box) {
    const double initialScore = checkerboardScore(image, box);

    if (initialScore >= 0.0)
        return box;

    const int tile =
        static_cast<int>(std::lround(
            static_cast<double>(box.width()) / 8.0));

    if (tile <= 0)
        return box;

    Box best = box;
    double bestScore = initialScore;

    const std::pair<int, int> shifts[] = {
        { tile,  0 },
        {-tile,  0 },
        { 0,  tile},
        { 0, -tile}
    };

    for (const auto& [dx, dy] : shifts) {
        const Box shifted {
            box.x0 + dx,
            box.y0 + dy,
            box.x1 + dx,
            box.y1 + dy
        };

        const double score = checkerboardScore(image, shifted);

        if (score > bestScore) {
            bestScore = score;
            best = shifted;
        }
    }

    return best;
}

BoardDetector::Box
BoardDetector::snapCorners(
    const GrayImage& image,
    const Box& box) {
    if (!box.valid())
        return box;

    const double tile = static_cast<double>(box.width()) / 8.0;

    const int radius =
        std::max(2,
                 static_cast<int>(std::lround(tile / 3.0)));

    int bestDx = 0;
    int bestDy = 0;
    double bestScore = NegativeInfinity;

    const auto evaluate = [&](int dx, int dy) {
        const Box shifted {
            box.x0 + dx,
            box.y0 + dy,
            box.x1 + dx,
            box.y1 + dy
        };

        const double score = checkerboardScore(image, shifted);

        if (score > bestScore) {
            bestScore = score;
            bestDx = dx;
            bestDy = dy;
        }
    };

    for (int dy = -radius; dy <= radius; dy += 2) {
        for (int dx = -radius; dx <= radius; dx += 2) {
            evaluate(dx, dy);
        }
    }

    const int centerX = bestDx;
    const int centerY = bestDy;

    for (int dy = centerY - 2; dy <= centerY + 2; ++dy) {
        for (int dx = centerX - 2; dx <= centerX + 2; ++dx) {
            evaluate(dx, dy);
        }
    }

    return {
        box.x0 + bestDx,
        box.y0 + bestDy,
        box.x1 + bestDx,
        box.y1 + bestDy
    };
}

std::optional<BoardDetector::Box>
BoardDetector::findChessboardCorners(
    const GrayImage& image) const
{
    if (image.width < 2 ||
        image.height < 2 ||
        image.data.empty()) {
        return std::nullopt;
    }

    const std::vector<float> gradY = gradientRows(image);
    const std::vector<float> gradX = gradientCols(image);

    const std::vector<double> houghRows =
        houghResponse(gradY,
                      image.width,
                      image.height,
                      Axis::Rows);

    const std::vector<double> houghCols =
        houghResponse(gradX,
                      image.width,
                      image.height,
                      Axis::Cols);

    const auto candidatesY = rankedPeakSequences(houghRows);
    const auto candidatesX = rankedPeakSequences(houghCols);

    const std::vector<int>* linesY =
        candidatesY.empty() ? nullptr : &candidatesY.front().trimmed;

    const std::vector<int>* linesX =
        candidatesX.empty() ? nullptr : &candidatesX.front().trimmed;

    if (linesX != nullptr && linesY == nullptr) {
        const auto box =
            reconstructFromCandidates(image,
                                      candidatesX,
                                      Axis::Cols);

        if (!box.has_value())
            return std::nullopt;

        return repairParity(image, *box);
    }

    if (linesY != nullptr && linesX == nullptr) {
        const auto box =
            reconstructFromCandidates(image,
                                      candidatesY,
                                      Axis::Rows);

        if (!box.has_value())
            return std::nullopt;

        return repairParity(image, *box);
    }

    if (linesX == nullptr || linesY == nullptr)
        return std::nullopt;

    if (linesX->size() < 7 || linesY->size() < 7)
        return std::nullopt;

    std::vector<double> spacingsX;
    std::vector<double> spacingsY;

    spacingsX.reserve(linesX->size() - 1);
    spacingsY.reserve(linesY->size() - 1);

    for (std::size_t i = 1; i < linesX->size(); ++i) {
        spacingsX.push_back(
            static_cast<double>((*linesX)[i] - (*linesX)[i - 1]));
    }

    for (std::size_t i = 1; i < linesY->size(); ++i) {
        spacingsY.push_back(
            static_cast<double>((*linesY)[i] - (*linesY)[i - 1]));
    }

    const double dx = median(spacingsX);
    const double dy = median(spacingsY);

    if (!(dx > 0.0) || !(dy > 0.0))
        return std::nullopt;

    std::vector<std::vector<int>> subgridsX;
    std::vector<std::vector<int>> subgridsY;

    for (std::size_t k = 0;
         k + 7 <= linesX->size();
         ++k) {
        subgridsX.emplace_back(
            linesX->begin() + static_cast<std::ptrdiff_t>(k),
            linesX->begin() + static_cast<std::ptrdiff_t>(k + 7));
    }

    for (std::size_t k = 0;
         k + 7 <= linesY->size();
         ++k) {
        subgridsY.emplace_back(
            linesY->begin() + static_cast<std::ptrdiff_t>(k),
            linesY->begin() + static_cast<std::ptrdiff_t>(k + 7));
    }

    std::optional<Box> bestBox;
    double bestScore = NegativeInfinity;

    for (const auto& subX : subgridsX) {
        for (const auto& subY : subgridsY) {
            const Box candidate {
                static_cast<int>(std::lround(
                    static_cast<double>(subX[0]) - dx)),

                static_cast<int>(std::lround(
                    static_cast<double>(subY[0]) - dy)),

                static_cast<int>(std::lround(
                    static_cast<double>(subX[6]) + dx)),

                static_cast<int>(std::lround(
                    static_cast<double>(subY[6]) + dy))
            };

            const double score = checkerboardScore(image, candidate);

            if (score > bestScore) {
                bestScore = score;
                bestBox = candidate;
            }
        }
    }

    if (!bestBox.has_value())
        return std::nullopt;

    return repairParity(image, *bestBox);
}

QRect BoardDetector::mapBoxToOriginal(
    const Box& box,
    int detectionWidth,
    int detectionHeight,
    int originalWidth,
    int originalHeight)
{
    if (detectionWidth <= 0 ||
        detectionHeight <= 0 ||
        originalWidth <= 0 ||
        originalHeight <= 0) {
        return {};
    }

    const double scaleX =
        static_cast<double>(originalWidth) /
        static_cast<double>(detectionWidth);

    const double scaleY =
        static_cast<double>(originalHeight) /
        static_cast<double>(detectionHeight);

    const int x0 =
        static_cast<int>(std::lround(box.x0 * scaleX));

    const int y0 =
        static_cast<int>(std::lround(box.y0 * scaleY));

    const int x1 =
        static_cast<int>(std::lround(box.x1 * scaleX));

    const int y1 =
        static_cast<int>(std::lround(box.y1 * scaleY));

    return QRect(x0, y0, x1 - x0, y1 - y0);
}
