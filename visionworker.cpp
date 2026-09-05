//
// Background worker for screenshot board detection and FEN recognition.
//

#include "visionworker.h"

#include <stdexcept>
#include <vector>

VisionWorker::VisionWorker(const QString &modelPath, QObject *parent)
    : QObject(parent)
    , boardDetector_(1600)
    , recognizer_(modelPath) {
    qRegisterMetaType<VisionResult>("VisionResult");
}

bool VisionWorker::isReady() const {
    return recognizer_.isReady();
}

QString VisionWorker::errorString() const {
    return recognizer_.errorString();
}

void VisionWorker::processImage(const QImage &sourceImage, quint64 requestId) {
    VisionResult result;
    result.requestId = requestId;

    if (sourceImage.isNull()) {
        result.errorMessage = tr("The image is empty.");
        emit finished(result);
        return;
    }

    if (!recognizer_.isReady()) {
        result.errorMessage = recognizer_.errorString();
        emit finished(result);
        return;
    }

    try {
        const QImage image = sourceImage.convertToFormat(QImage::Format_ARGB32);
        const std::vector<BoardDetector::Candidate> candidates =
            boardDetector_.detect(image);

        if (candidates.empty()) {
            result.errorMessage =
                tr("No chessboard detected in the image.");
            emit finished(result);
            return;
        }

        QString lastError;
        for (const BoardDetector::Candidate &candidate : candidates) {
            try {
                const FenRecognizer::Result recognized =
                    recognizer_.recognize(image, candidate);
                if (!result.result.has_value() ||
                    recognized.meanConfidence > result.result->meanConfidence) {
                    result.result = recognized;
                    result.snapped = candidate.snapped;
                }
            } catch (const std::exception &exception) {
                lastError = QString::fromUtf8(exception.what());
            }
        }

        if (!result.result.has_value()) {
            result.errorMessage =
                lastError.isEmpty()
                    ? tr("Chessboard recognition failed.")
                    : lastError;
        }
    } catch (const std::exception &exception) {
        result.errorMessage = QString::fromUtf8(exception.what());
    }

    emit finished(result);
}
