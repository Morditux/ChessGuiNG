#include "fenrecognizer.h"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
    constexpr int BoardPixels = 256;
    constexpr int TilePixels = 32;
    constexpr int TileCount = 64;
    constexpr int TileInputSize = TilePixels * TilePixels;
    constexpr int ClassCount = 13;

    // Index 0 is empty. The remaining classes match the model exported by Fenshot.
    constexpr char Labels[] = "1KQRBNPkqrbnp";

    /**
     * Temporarily redirects the POSIX stderr descriptor while ONNX Runtime is
     * initialized.
     *
     * The Ubuntu ONNX package enables static schema registration, while its
     * ONNX Runtime package registers the same schemas again in Ort::Env. The
     * duplicate messages are harmless but noisy. The guard suppresses those
     * diagnostics during construction; exceptions are still reported through
     * FenRecognizer::error_. The official Windows package does not need this
     * workaround, so it deliberately does not replace the Windows CRT stream.
     */
#ifndef _WIN32
    class ScopedStderrSilencer {
    public:
        ScopedStderrSilencer() {
            std::fflush(stderr);
            savedFd_ = ::dup(STDERR_FILENO);
            nullFd_ = ::open("/dev/null", O_WRONLY);
            if (savedFd_ >= 0 && nullFd_ >= 0)
                ::dup2(nullFd_, STDERR_FILENO);
        }

        ~ScopedStderrSilencer() {
            if (savedFd_ >= 0)
                ::dup2(savedFd_, STDERR_FILENO);
            if (savedFd_ >= 0)
                ::close(savedFd_);
            if (nullFd_ >= 0)
                ::close(nullFd_);
        }

    private:
        int savedFd_ = -1;
        int nullFd_ = -1;
    };
#endif

    /**
     * Reads one grayscale pixel, clamping coordinates to the image edges.
     *
     * Clamping reproduces the edge behavior required by the bilinear resize
     * when a sample falls just outside the source image.
     */
    float sampleClamped(const std::vector<float> &gray,
                        int width,
                        int height,
                        int x,
                        int y) {
        const int cx = std::clamp(x, 0, width - 1);
        const int cy = std::clamp(y, 0, height - 1);
        return gray[static_cast<std::size_t>(cy) *
                    static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(cx)];
    }

    /**
     * Expands a compressed placement into one character per board square.
     *
     * Empty squares are represented by '1' in the returned 64-character
     * string. The ranks remain ordered from rank 8 to rank 1, matching FEN.
     *
     * @throws std::runtime_error if the placement does not describe eight
     *         ranks of eight squares each.
     */
    QString expandedPlacement(const QString &placement) {
        const QStringList ranks = placement.split(QLatin1Char('/'));
        if (ranks.size() != 8)
            throw std::runtime_error("Invalid placement returned by classifier");

        QString expanded;
        expanded.reserve(64);

        for (const QString &rank: ranks) {
            QString row;

            for (const QChar ch: rank) {
                if (ch.isDigit()) {
                    const int count = ch.digitValue();
                    if (count < 1 || count > 8)
                        throw std::runtime_error("Invalid empty-square run in placement");
                    row += QString(count, QLatin1Char('1'));
                } else {
                    row += ch;
                }
            }

            if (row.size() != 8)
                throw std::runtime_error("Invalid rank width returned by classifier");

            expanded += row;
        }

        return expanded;
    }
}

FenRecognizer::FenRecognizer(const QString &modelPath) {
    if (!QFileInfo::exists(modelPath)) {
        error_ = QStringLiteral("ONNX model not found: %1").arg(modelPath);
        return;
    }

    try {
#ifndef _WIN32
        ScopedStderrSilencer silence;
#endif
        // The Windows 1.23.2 C++ header uses a process-global API pointer.
        // Initialize it after the application has started rather than from a
        // static initializer in every executable that includes the header.
        const OrtApiBase *apiBase = OrtGetApiBase();
        if (apiBase == nullptr)
            throw std::runtime_error("ONNX Runtime API is unavailable");

        const OrtApi *api = apiBase->GetApi(ORT_API_VERSION);
        if (api == nullptr)
            throw std::runtime_error("ONNX Runtime API version is unsupported");

        Ort::InitApi(api);
        environment_ = std::make_unique<Ort::Env>(
                ORT_LOGGING_LEVEL_WARNING,
                "chessVision");

        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        options.SetIntraOpNumThreads(1);

#ifdef _WIN32
        const std::wstring nativePath = modelPath.toStdWString();
        session_ = std::make_unique<Ort::Session>(*environment_,
                                                   nativePath.c_str(),
                                                   options);
#else
        const QByteArray nativePath = QFileInfo(modelPath).absoluteFilePath().toUtf8();
        session_ = std::make_unique<Ort::Session>(*environment_,
                                                   nativePath.constData(),
                                                   options);
#endif

        if (session_->GetInputCount() != 1 || session_->GetOutputCount() != 1)
            throw std::runtime_error("The model must have one input and one output");
    } catch (const Ort::Exception &exception) {
        error_ = QStringLiteral("Could not load the model: %1")
                .arg(QString::fromUtf8(exception.what()));
        session_.reset();
    } catch (const std::exception &exception) {
        error_ = QStringLiteral("Could not load the model: %1")
                .arg(QString::fromUtf8(exception.what()));
        session_.reset();
    }
}

bool FenRecognizer::isReady() const {
    return session_ != nullptr;
}

QString FenRecognizer::errorString() const {
    return error_;
}

std::vector<float> FenRecognizer::extractTiles(const QImage &image,
                                               const QRect &board) const {
    if (image.isNull() || board.width() <= 0 || board.height() <= 0)
        throw std::runtime_error("Image ou plateau invalide");

    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    const int width = argb.width();
    const int height = argb.height();

    std::vector<float> gray(static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height));

    for (int y = 0; y < height; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));

        for (int x = 0; x < width; ++x) {
            const QRgb pixel = row[x];
            gray[static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)] =
                    0.299f * static_cast<float>(qRed(pixel)) +
                    0.587f * static_cast<float>(qGreen(pixel)) +
                    0.114f * static_cast<float>(qBlue(pixel));
        }
    }

    const double x0 = static_cast<double>(board.x());
    const double y0 = static_cast<double>(board.y());
    const double boardWidth = static_cast<double>(board.width());
    const double boardHeight = static_cast<double>(board.height());

    std::vector<float> resized(static_cast<std::size_t>(BoardPixels) *
                               static_cast<std::size_t>(BoardPixels));

    // This is the same center-aligned bilinear resize used by Fenshot.
    for (int targetY = 0; targetY < BoardPixels; ++targetY) {
        const double sourceY =
                y0 + ((static_cast<double>(targetY) + 0.5) * boardHeight) /
                static_cast<double>(BoardPixels) - 0.5;
        const int floorY = static_cast<int>(std::floor(sourceY));
        const double weightY = sourceY - static_cast<double>(floorY);

        for (int targetX = 0; targetX < BoardPixels; ++targetX) {
            const double sourceX =
                    x0 + ((static_cast<double>(targetX) + 0.5) * boardWidth) /
                    static_cast<double>(BoardPixels) - 0.5;
            const int floorX = static_cast<int>(std::floor(sourceX));
            const double weightX = sourceX - static_cast<double>(floorX);

            const double top =
                    static_cast<double>(sampleClamped(gray, width, height,
                                                      floorX, floorY)) *
                    (1.0 - weightX) +
                    static_cast<double>(sampleClamped(gray, width, height,
                                                      floorX + 1, floorY)) *
                    weightX;

            const double bottom =
                    static_cast<double>(sampleClamped(gray, width, height,
                                                      floorX, floorY + 1)) *
                    (1.0 - weightX) +
                    static_cast<double>(sampleClamped(gray, width, height,
                                                      floorX + 1, floorY + 1)) *
                    weightX;

            resized[static_cast<std::size_t>(targetY) * BoardPixels +
                    static_cast<std::size_t>(targetX)] =
                    static_cast<float>((top * (1.0 - weightY) +
                                        bottom * weightY) /
                                       255.0);
        }
    }

    std::vector<float> tiles(static_cast<std::size_t>(TileCount) *
                             static_cast<std::size_t>(TileInputSize));

    // rank 0 is rank 1, whose pixels are at the bottom of the image.
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const int tile = rank * 8 + file;
            const int sourceY0 = (7 - rank) * TilePixels;
            const int sourceX0 = file * TilePixels;

            for (int y = 0; y < TilePixels; ++y) {
                for (int x = 0; x < TilePixels; ++x) {
                    tiles[static_cast<std::size_t>(tile) * TileInputSize +
                          static_cast<std::size_t>(y) * TilePixels +
                          static_cast<std::size_t>(x)] =
                            resized[static_cast<std::size_t>(sourceY0 + y) *
                                    BoardPixels +
                                    static_cast<std::size_t>(sourceX0 + x)];
                }
            }
        }
    }

    return tiles;
}

FenRecognizer::Classification FenRecognizer::classify(const QImage &image,
                                                      const QRect &board) const {
    if (!isReady())
        throw std::runtime_error(error_.toStdString());

    const std::vector<float> tiles = extractTiles(image, board);
    const std::array<int64_t, 2> inputShape{
        TileCount,
        TileInputSize
    };

    Ort::MemoryInfo memoryInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input = Ort::Value::CreateTensor<float>(
        memoryInfo,
        const_cast<float *>(tiles.data()),
        tiles.size(),
        inputShape.data(),
        inputShape.size());

    Ort::AllocatorWithDefaultOptions allocator;
    Ort::AllocatedStringPtr inputName =
            session_->GetInputNameAllocated(0, allocator);
    Ort::AllocatedStringPtr outputName =
            session_->GetOutputNameAllocated(0, allocator);

    const char *inputNames[] = {inputName.get()};
    const char *outputNames[] = {outputName.get()};

    auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                  inputNames,
                                  &input,
                                  1,
                                  outputNames,
                                  1);

    if (outputs.empty() || !outputs.front().IsTensor())
        throw std::runtime_error("La sortie ONNX n'est pas un tenseur");

    const auto shape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 2 || shape[0] != TileCount || shape[1] != ClassCount)
        throw std::runtime_error("Sortie ONNX inattendue : attendu [64, 13]");

    const float *probabilities = outputs.front().GetTensorData<float>();
    Classification result;
    result.confidences.reserve(TileCount);

    QStringList names;
    names.reserve(TileCount);

    for (int tile = 0; tile < TileCount; ++tile) {
        float bestProbability = -1.0f;
        int bestClass = 0;

        for (int classIndex = 0; classIndex < ClassCount; ++classIndex) {
            const float probability =
                    probabilities[static_cast<std::size_t>(tile) * ClassCount +
                                  static_cast<std::size_t>(classIndex)];

            if (probability > bestProbability) {
                bestProbability = probability;
                bestClass = classIndex;
            }
        }

        names.push_back(QString(QLatin1Char(Labels[bestClass])));
        result.confidences.push_back(bestProbability);
    }

    QStringList ranks;
    ranks.reserve(8);
    for (int rank = 7; rank >= 0; --rank)
        ranks.push_back(names.mid(rank * 8, 8).join(QString()));

    QStringList compressedRanks;
    compressedRanks.reserve(8);
    for (const QString &rank: ranks)
        compressedRanks.push_back(compressRank(rank));

    result.placement = compressedRanks.join(QLatin1Char('/'));
    result.minConfidence = *std::min_element(result.confidences.begin(),
                                             result.confidences.end());

    float sum = 0.0f;
    for (const float confidence: result.confidences)
        sum += confidence;
    result.meanConfidence = sum / static_cast<float>(result.confidences.size());

    return result;
}

FenRecognizer::Result FenRecognizer::recognize(
    const QImage &image,
    const BoardDetector::Candidate &board) const {
    const Classification classification = classify(image, board.rect);

    Result result;
    result.placement = resolveOrientation(classification.placement,
                                          &result.orientation);
    result.fen = composeFen(result.placement);
    result.meanConfidence = classification.meanConfidence;
    result.minConfidence = classification.minConfidence;
    return result;
}

QString FenRecognizer::compressRank(const QString &expandedRank) {
    QString compressed;
    int empty = 0;

    for (const QChar square: expandedRank) {
        if (square == QLatin1Char('1')) {
            ++empty;
            continue;
        }

        if (empty > 0) {
            compressed += QString::number(empty);
            empty = 0;
        }
        compressed += square;
    }

    if (empty > 0)
        compressed += QString::number(empty);

    return compressed;
}

QString FenRecognizer::flipPlacement(const QString &placement) {
    const QString expanded = expandedPlacement(placement);
    QString rotated;
    rotated.reserve(64);

    for (int row = 7; row >= 0; --row) {
        const QString source = expanded.mid(row * 8, 8);
        QString reversed = source;
        std::reverse(reversed.begin(), reversed.end());
        rotated += reversed;
    }

    QStringList ranks;
    ranks.reserve(8);
    for (int row = 0; row < 8; ++row)
        ranks.push_back(compressRank(rotated.mid(row * 8, 8)));

    return ranks.join(QLatin1Char('/'));
}

QString FenRecognizer::resolveOrientation(const QString &placement,
                                          FenRecognizer::Orientation *orientation) {
    const QString expanded = expandedPlacement(placement);
    double whiteSum = 0.0;
    double blackSum = 0.0;
    int whiteCount = 0;
    int blackCount = 0;

    for (int row = 0; row < 8; ++row) {
        const int rank = 8 - row;
        const QString current = expanded.mid(row * 8, 8);

        for (const QChar piece: current) {
            if (piece == QLatin1Char('P')) {
                whiteSum += rank;
                ++whiteCount;
            } else if (piece == QLatin1Char('p')) {
                blackSum += rank;
                ++blackCount;
            }
        }
    }

    if (orientation != nullptr)
        *orientation = FenRecognizer::Orientation::Auto;

    // With no pawns of either colour, orientation cannot be inferred.
    if (whiteCount == 0 || blackCount == 0)
        return placement;

    const double readNaturalness =
            blackSum / static_cast<double>(blackCount) -
            whiteSum / static_cast<double>(whiteCount);

    const QString rotated = flipPlacement(placement);
    const QString rotatedExpanded = expandedPlacement(rotated);
    double rotatedWhiteSum = 0.0;
    double rotatedBlackSum = 0.0;

    for (int row = 0; row < 8; ++row) {
        const int rank = 8 - row;
        const QString current = rotatedExpanded.mid(row * 8, 8);
        for (const QChar piece: current) {
            if (piece == QLatin1Char('P'))
                rotatedWhiteSum += rank;
            else if (piece == QLatin1Char('p'))
                rotatedBlackSum += rank;
        }
    }

    const double rotatedNaturalness =
            rotatedBlackSum / static_cast<double>(blackCount) -
            rotatedWhiteSum / static_cast<double>(whiteCount);

    if (rotatedNaturalness > readNaturalness) {
        if (orientation != nullptr)
            *orientation = FenRecognizer::Orientation::Flipped180;
        return rotated;
    }

    return placement;
}

QString FenRecognizer::composeFen(const QString &placement) {
    const QString expanded = expandedPlacement(placement);
    QString castling;

    const auto hasPiece = [&](int row, int file, QChar piece) {
        return expanded[row * 8 + file] == piece;
    };

    if (hasPiece(7, 4, QLatin1Char('K')) && hasPiece(7, 7, QLatin1Char('R')))
        castling += QLatin1Char('K');
    if (hasPiece(7, 4, QLatin1Char('K')) && hasPiece(7, 0, QLatin1Char('R')))
        castling += QLatin1Char('Q');
    if (hasPiece(0, 4, QLatin1Char('k')) && hasPiece(0, 7, QLatin1Char('r')))
        castling += QLatin1Char('k');
    if (hasPiece(0, 4, QLatin1Char('k')) && hasPiece(0, 0, QLatin1Char('r')))
        castling += QLatin1Char('q');
    if (castling.isEmpty())
        castling = QLatin1Char('-');

    // A screenshot contains no side-to-move or move-history information.
    return placement + QStringLiteral(" w ") + castling +
           QStringLiteral(" - 0 1");
}
