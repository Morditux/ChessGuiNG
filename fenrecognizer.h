#pragma once

#include "boarddetector.h"

#include <QImage>
#include <QString>

#include <memory>
#include <vector>

#include <onnxruntime_cxx_api.h>

/**
 * Runs the Fenshot tile classifier and turns its 64 predictions into FEN.
 *
 * The model contract is:
 *   input  : float32 [64, 1024] (64 grayscale 32x32 tiles, values [0, 1])
 *   output : float32 [64, 13]   (class probabilities)
 *
 * Tiles are emitted in A1..H8 order, which is the order expected by
 * chess-tiles-v2.onnx. The returned FEN uses the usual ranks 8..1 order.
 */
class FenRecognizer
{
public:
    /**
     * Board orientation chosen by the pawn-rank heuristic.
     */
    enum class Orientation {
        /** Input placement kept as read from the image. */
        Auto,
        /** Placement rotated 180 degrees. */
        Flipped180
    };

    /**
     * The result of recognizing one board candidate.
     *
     * Confidence values are probabilities selected by the classifier for
     * each tile, and therefore normally lie in the range [0, 1]. They are
     * useful for comparing candidates and spotting uncertain tiles; they are
     * not a calibrated probability that the complete FEN is correct.
     */
    struct Result
    {
        /** Board placement in FEN notation, without the remaining FEN fields. */
        QString placement;

        /** Complete FEN assembled from the recognized placement. */
        QString fen;

        /** Mean of the 64 winning tile-class probabilities. */
        float meanConfidence = 0.0f;

        /** Lowest winning tile-class probability among the 64 tiles. */
        float minConfidence = 0.0f;

        /**
         * Orientation selected by the pawn-rank heuristic. It is Auto when
         * the input placement is kept and Flipped180 when it is rotated 180
         * degrees. If the position does not contain pawns for both sides,
         * the value is Auto because the orientation cannot be inferred.
         */
        Orientation orientation = Orientation::Auto;
    };

    /**
     * Loads an ONNX tile-classification model.
     *
     * The model is expected to expose exactly one input and one output. The
     * session is created once and reused by subsequent calls to recognize().
     * Construction does not throw for a missing or invalid model; callers can
     * use isReady() and errorString() to inspect that failure.
     *
     * @param modelPath Path to the ONNX model file.
     */
    explicit FenRecognizer(const QString& modelPath);

    /**
     * Reports whether the ONNX Runtime session was created successfully.
     *
     * @return true when recognize() can run; false otherwise.
     */
    [[nodiscard]] bool isReady() const;

    /**
     * Returns the model-loading error recorded during construction.
     *
     * The string is empty when the recognizer is ready. It is intended for
     * diagnostics and user-facing status messages.
     *
     * @return The last initialization error, or an empty string.
     */
    [[nodiscard]] QString errorString() const;

    /**
     * Classifies the board inside an image and returns a FEN representation.
     *
     * The candidate rectangle must use coordinates from the original image,
     * as returned by BoardDetector::detect(). The rectangle is converted to a
     * 256x256 grayscale board, split into 64 32x32 tiles, and passed to the
     * ONNX model. The placement is then optionally rotated by 180 degrees
     * using the pawn-rank orientation heuristic.
     *
     * The generated FEN keeps the recognized placement and infers castling
     * availability only from kings and rooks on their home squares. Because a
     * screenshot does not provide game-state metadata, the side to move is
     * set to white, en-passant is set to '-', and the move counters are set to
     * 0 and 1.
     *
     * @param image Source screenshot containing the board.
     * @param board Board candidate whose rect is expressed in image
     *              coordinates.
     * @return Placement, complete FEN, confidence statistics, and the chosen
     *         orientation.
     * @throws std::runtime_error if the model is not ready, the image or
     *         rectangle is invalid, or the ONNX output does not match the
     *         expected shape.
     */
    [[nodiscard]] Result recognize(const QImage& image,
                                   const BoardDetector::Candidate& board) const;

private:
    /** Intermediate classifier output before orientation resolution. */
    struct Classification
    {
        /** Compressed placement in the detector's current orientation. */
        QString placement;

        /** Winning probability for each tile, in A1..H8 tile order. */
        std::vector<float> confidences;

        /** Mean of confidences. */
        float meanConfidence = 0.0f;

        /** Minimum of confidences. */
        float minConfidence = 0.0f;
    };

    /**
     * Converts the board rectangle into the model's flattened tile tensor.
     *
     * Pixels are converted to BT.601-style grayscale, resized with
     * center-aligned bilinear interpolation, normalized to [0, 1], and
     * copied into 64 contiguous 32x32 tiles. Tile zero is A1 and tile 63 is
     * H8; the vertical copy is reversed because image coordinates start at
     * the top while chess ranks start at rank 1 at the bottom.
     *
     * @param image Source image.
     * @param board Board rectangle in source-image coordinates.
     * @return A flattened float tensor with shape [64, 1024].
     * @throws std::runtime_error if the image or rectangle is invalid.
     */
    [[nodiscard]] std::vector<float> extractTiles(const QImage& image,
                                                  const QRect& board) const;

    /**
     * Runs ONNX inference and converts the winning class of every tile into
     * a compressed board placement.
     *
     * The model output must have shape [64, 13]. Class index zero represents
     * an empty square; the other indices use the labels declared in the
     * implementation. The returned confidence vector preserves tile order.
     *
     * @param image Source image.
     * @param board Board rectangle in source-image coordinates.
     * @return Classification and per-tile confidence data.
     * @throws std::runtime_error if the recognizer is not ready, inference
     *         fails, or the model output has an unexpected shape.
     */
    [[nodiscard]] Classification classify(const QImage& image,
                                          const QRect& board) const;

    /**
     * Compresses one expanded eight-square rank into FEN notation.
     *
     * In the expanded representation, the character '1' denotes an empty
     * square. Consecutive '1' characters are replaced with one digit.
     *
     * @param expandedRank Eight characters representing one rank.
     * @return The compressed FEN rank.
     */
    [[nodiscard]] static QString compressRank(const QString& expandedRank);

    /**
     * Rotates a compressed placement by 180 degrees.
     *
     * @param placement Eight slash-separated FEN ranks.
     * @return The rotated placement in compressed FEN notation.
     * @throws std::runtime_error if placement is not an eight-by-eight board.
     */
    [[nodiscard]] static QString flipPlacement(const QString& placement);

    /**
     * Chooses the most plausible board orientation from pawn locations.
     *
     * The heuristic compares the average rank of white and black pawns in
     * the input placement and in its 180-degree rotation. It prefers the
     * arrangement where black pawns are farther up the board than white
     * pawns. If either side has no pawns, no reliable decision is possible
     * and the input placement is returned unchanged.
     *
     * @param placement Placement to evaluate.
     * @param orientation Optional output set to Orientation::Auto for the
     *                    input placement or Orientation::Flipped180 for the
     *                    rotated placement.
     * @return The selected placement.
     * @throws std::runtime_error if placement is not an eight-by-eight board.
     */
    [[nodiscard]] static QString resolveOrientation(const QString& placement,
                                                    Orientation* orientation);

    /**
     * Appends the non-placement fields to a board placement.
     *
     * Castling rights are inferred conservatively from king-and-rook home
     * squares. The other fields are fixed because they cannot be recovered
     * from an image: white to move, no en-passant target, halfmove clock 0,
     * and fullmove number 1.
     *
     * @param placement Eight slash-separated FEN ranks.
     * @return A complete six-field FEN string.
     * @throws std::runtime_error if placement is not an eight-by-eight board.
     */
    [[nodiscard]] static QString composeFen(const QString& placement);

    std::unique_ptr<Ort::Env> environment_;
    std::unique_ptr<Ort::Session> session_;
    QString error_;
};
