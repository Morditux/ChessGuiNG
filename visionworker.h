//
// Background worker for screenshot board detection and FEN recognition.
//

#ifndef CHESSGUI_VISIONWORKER_H
#define CHESSGUI_VISIONWORKER_H

#include <QImage>
#include <QObject>
#include <QString>

#include <optional>

#include "boarddetector.h"
#include "fenrecognizer.h"

struct VisionResult {
    quint64 requestId = 0;
    std::optional<FenRecognizer::Result> result;
    bool snapped = false;
    QString errorMessage;
};

class VisionWorker : public QObject {
    Q_OBJECT

public:
    explicit VisionWorker(const QString &modelPath, QObject *parent = nullptr);

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] QString errorString() const;

public slots:
    // Detects the board in the image and classifies its squares. The
    // recognition result is delivered through finished(); an empty result
    // means the image or model was invalid, or no board was detected.
    void processImage(const QImage &image, quint64 requestId);

signals:
    void finished(const VisionResult &result);

private:
    BoardDetector boardDetector_;
    FenRecognizer recognizer_;
};

#endif // CHESSGUI_VISIONWORKER_H
