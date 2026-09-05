#pragma once

#include <QImage>
#include <QRect>

#include <optional>
#include <utility>
#include <vector>

/*
 * BoardDetector
 *
 * Detects an axis-aligned 8x8 chessboard in a screenshot.
 *
 * Detection strategy inspired by Fenshot / tensorflow_chessbot:
 *   - grayscale conversion
 *   - horizontal/vertical gradients
 *   - 1D peak responses
 *   - regularly-spaced line sequence search
 *   - checkerboard correlation
 *   - parity repair
 *   - optional grid-snap refinement
 *
 * The detector does NOT recognize pieces and does NOT generate FEN.
 *
 * If you redistribute code substantially derived from Fenshot, keep the
 * corresponding upstream MIT license/attribution with your project.
 */
class BoardDetector
{
public:
    struct Candidate
    {
        // Rectangle in coordinates of the ORIGINAL QImage.
        // x + width and y + height are the exclusive x1/y1 bounds.
        QRect rect;

        // Checkerboard correlation score measured on the detection image.
        // Useful for diagnostics only; do not compare it to ONNX confidence.
        double score = 0.0;

        // false = raw geometric detection
        // true  = checkerboard grid-snap refinement
        bool snapped = false;
    };

    // Fenshot downsizes very large screenshots for detection.
    // Set maxDetectDimension <= 0 to disable downscaling.
    explicit BoardDetector(int maxDetectDimension = 1600);

    // Returns:
    //   {}                       if no board is found
    //   {raw}                    if snap gives the same rectangle
    //   {raw, snappedCandidate}  otherwise
    //
    // The caller should classify every returned candidate and keep the one
    // with the highest mean tile-classification confidence.
    [[nodiscard]]
    std::vector<Candidate> detect(const QImage& image) const;

private:
    static constexpr double PeakKeepRatio = 0.20;
    static constexpr int MinSequenceLength = 7;
    static constexpr int SequenceErrorPixels = 5;
    static constexpr int MaxCandidateSequences = 5;
    static constexpr int NonMaxWindow = 5;

    struct GrayImage
    {
        std::vector<float> data;
        int width = 0;
        int height = 0;
    };

    // x1/y1 are EXCLUSIVE bounds.
    struct Box
    {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;

        [[nodiscard]] int width() const  { return x1 - x0; }
        [[nodiscard]] int height() const { return y1 - y0; }
        [[nodiscard]] bool valid() const { return x1 > x0 && y1 > y0; }

        friend bool operator==(const Box& a, const Box& b)
        {
            return a.x0 == b.x0 && a.y0 == b.y0 &&
                   a.x1 == b.x1 && a.y1 == b.y1;
        }

        friend bool operator!=(const Box& a, const Box& b)
        {
            return !(a == b);
        }
    };

    struct CandidateSequence
    {
        // 7..9 lines used by the normal two-axis path.
        std::vector<int> trimmed;

        // Full arithmetic sequence used by one-axis reconstruction.
        std::vector<int> full;
    };

    struct Reconstruction
    {
        Box box;
        double score = 0.0;
    };

    enum class Axis
    {
        Rows,
        Cols
    };

    int maxDetectDimension_ = 1600;

    [[nodiscard]] GrayImage toGray(const QImage& image) const;

    [[nodiscard]]
    std::vector<float> gradientRows(const GrayImage& image) const;

    [[nodiscard]]
    static std::vector<float> gradientCols(const GrayImage& image);

    [[nodiscard]]
    static std::vector<double> houghResponse(const std::vector<float>& gradient,
                                             int width,
                                             int height,
                                             Axis axis);

    [[nodiscard]]
    static std::vector<double> nonMaxSuppress(const std::vector<double>& values,
                                              int windowSize = NonMaxWindow);

    [[nodiscard]]
    static std::vector<std::vector<int>>
    getAllSequences(const std::vector<int>& peakPositions);

    [[nodiscard]]
    static std::pair<std::vector<int>, std::vector<double>>
    trimSequence(const std::vector<int>& sequence,
                 const std::vector<double>& values);

    [[nodiscard]]
    static std::vector<CandidateSequence>
    rankedPeakSequences(const std::vector<double>& hough);

    [[nodiscard]]
    static double checkerboardScore(const GrayImage& image, const Box& box);

    [[nodiscard]]
    static std::optional<Reconstruction>
    reconstructSquareBoard(const GrayImage& image,
                           const std::vector<int>& goodAxisLines,
                           Axis goodAxis);

    [[nodiscard]]
    static std::optional<Box>
    reconstructFromCandidates(const GrayImage& image,
                              const std::vector<CandidateSequence>& candidates,
                              Axis goodAxis);

    [[nodiscard]]
    static Box repairParity(const GrayImage& image, const Box& box);

    [[nodiscard]]
    static Box snapCorners(const GrayImage& image, const Box& box);

    [[nodiscard]]
    std::optional<Box> findChessboardCorners(const GrayImage& image) const;

    [[nodiscard]]
    static double median(std::vector<double> values);

    [[nodiscard]]
    static double maxRange(const std::vector<double>& values,
                           int fromInclusive,
                           int toExclusive);

    [[nodiscard]]
    static QRect mapBoxToOriginal(const Box& box,
                                  int detectionWidth,
                                  int detectionHeight,
                                  int originalWidth,
                                  int originalHeight);
};
