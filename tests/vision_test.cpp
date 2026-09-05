#include <QImage>
#include <QSignalSpy>
#include <QTest>

#include <optional>
#include <stdexcept>

#include "boarddetector.h"
#include "fenrecognizer.h"
#include "visionworker.h"

class VisionTest : public QObject {
    Q_OBJECT

private slots:
    void testDetectorRejectsInvalidImage();
    void testWorkerReportsEmptyImage();
    void testRecognizerLoadsModel();
    void testRecognizesScreenshotWhenProvided();
};

void VisionTest::testDetectorRejectsInvalidImage() {
    BoardDetector detector;
    QVERIFY(detector.detect(QImage()).empty());
}

void VisionTest::testWorkerReportsEmptyImage() {
    VisionWorker worker(QString{});
    QSignalSpy finishedSpy(&worker, &VisionWorker::finished);

    // Same-thread invocation: the worker emits its result synchronously.
    QMetaObject::invokeMethod(&worker, [&worker] {
        worker.processImage(QImage(), 42);
    });

    QCOMPARE(finishedSpy.count(), 1);
    const auto result = finishedSpy.first().at(0).value<VisionResult>();
    QCOMPARE(result.requestId, quint64(42));
    QVERIFY(!result.result.has_value());
    QVERIFY(result.errorMessage.contains(QStringLiteral("image is empty")));
}

void VisionTest::testRecognizerLoadsModel() {
#ifdef CHESSGUI_SOURCE_MODEL_PATH
    FenRecognizer recognizer(QString::fromLocal8Bit(CHESSGUI_SOURCE_MODEL_PATH));
    QVERIFY2(recognizer.isReady(), qPrintable(recognizer.errorString()));

    BoardDetector::Candidate candidate;
    candidate.rect = QRect(0, 0, 256, 256);

    bool threw = false;
    try {
        (void)recognizer.recognize(QImage(), candidate);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    QVERIFY(threw);
#else
    QSKIP("No model path was configured");
#endif
}

void VisionTest::testRecognizesScreenshotWhenProvided() {
    const QString path = qEnvironmentVariable("CHESSGUI_SAMPLE_IMAGE");
    if (path.isEmpty()) {
        QSKIP("Set CHESSGUI_SAMPLE_IMAGE to run the end-to-end screenshot check");
    }

    const QImage image(path);
    QVERIFY2(!image.isNull(), qPrintable(path));

    FenRecognizer recognizer(QString::fromLocal8Bit(CHESSGUI_SOURCE_MODEL_PATH));
    QVERIFY2(recognizer.isReady(), qPrintable(recognizer.errorString()));

    BoardDetector detector;
    const auto candidates = detector.detect(image);
    QVERIFY(!candidates.empty());

    std::optional<FenRecognizer::Result> best;
    for (const auto &candidate : candidates) {
        const auto result = recognizer.recognize(image, candidate);
        if (!best.has_value() || result.meanConfidence > best->meanConfidence) {
            best = result;
        }
    }

    QVERIFY(best.has_value());
    QVERIFY(best->fen.split(QChar(' '), Qt::SkipEmptyParts).size() == 6);
    QVERIFY(best->fen.contains(QChar('K')));
    QVERIFY(best->fen.contains(QChar('k')));
    qInfo().noquote() << "Detected FEN:" << best->fen
                      << "confidence:" << best->meanConfidence;
}

QTEST_MAIN(VisionTest)
#include "vision_test.moc"
