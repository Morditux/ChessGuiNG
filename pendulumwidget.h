//
// Created by mordicus on 25/08/2026.
//

#ifndef CHESSGUI_PENDULUMWIDGET_H
#define CHESSGUI_PENDULUMWIDGET_H

#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class PendulumWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 remainingMilliseconds READ remainingMilliseconds
                   WRITE setRemainingMilliseconds NOTIFY remainingMillisecondsChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(PieceColor pieceColor READ pieceColor WRITE setPieceColor
                   NOTIFY pieceColorChanged)

public:
    enum class PieceColor {
        White,
        Black
    };
    Q_ENUM(PieceColor)

    explicit PendulumWidget(QWidget *parent = nullptr);
    explicit PendulumWidget(PieceColor pieceColor, QWidget *parent = nullptr);

    [[nodiscard]] qint64 remainingMilliseconds() const;
    void setRemainingMilliseconds(qint64 milliseconds);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] PieceColor pieceColor() const;
    void setPieceColor(PieceColor pieceColor);

    [[nodiscard]] QString displayText() const;
    [[nodiscard]] QColor backgroundColor() const;
    [[nodiscard]] QColor displayColor() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

public slots:
    void set(int hours, int minutes, int seconds);
    void start();
    void stop();
    void reset();

signals:
    void remainingMillisecondsChanged(qint64 milliseconds);
    void runningChanged(bool running);
    void pieceColorChanged(PieceColor pieceColor);
    void displayTextChanged(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateCountdown();

private:
    [[nodiscard]] qint64 currentRemainingMilliseconds() const;
    [[nodiscard]] static QString formatTime(qint64 milliseconds);
    void refreshDisplay();
    void updateAccessibilityText();

    QTimer timer_;
    QElapsedTimer elapsedTimer_;
    qint64 initialMilliseconds_ = 0;
    qint64 remainingMilliseconds_ = 0;
    bool running_ = false;
    PieceColor pieceColor_ = PieceColor::White;
    QString displayText_ = QStringLiteral("00:00");
};

#endif // CHESSGUI_PENDULUMWIDGET_H
