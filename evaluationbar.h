//
// Evaluation bar widget for chess games.
//

#ifndef CHESSGUI_EVALUATIONBAR_H
#define CHESSGUI_EVALUATIONBAR_H

#include <QColor>
#include <QWidget>

class EvaluationBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(double evaluation READ evaluation WRITE setEvaluation NOTIFY evaluationChanged)
    Q_PROPERTY(QColor whiteColor READ whiteColor WRITE setWhiteColor)
    Q_PROPERTY(QColor blackColor READ blackColor WRITE setBlackColor)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
    Q_PROPERTY(bool showCenterLine READ showCenterLine WRITE setShowCenterLine)
    Q_PROPERTY(bool showEvaluationText READ showEvaluationText WRITE setShowEvaluationText)

public:
    explicit EvaluationBar(QWidget *parent = nullptr);
    ~EvaluationBar() override = default;

    [[nodiscard]] double value() const;
    [[nodiscard]] double evaluation() const;

    [[nodiscard]] QColor whiteColor() const;
    void setWhiteColor(const QColor &color);

    [[nodiscard]] QColor blackColor() const;
    void setBlackColor(const QColor &color);

    [[nodiscard]] QColor borderColor() const;
    void setBorderColor(const QColor &color);

    [[nodiscard]] bool showCenterLine() const;
    void setShowCenterLine(bool show);

    [[nodiscard]] bool showEvaluationText() const;
    void setShowEvaluationText(bool show);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

public slots:
    void setValue(double value);
    void setEvaluation(double eval);

signals:
    void valueChanged(double value);
    void evaluationChanged(double value);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double value_ = 50.0;
    QColor whiteColor_ = QColor("#ffffff");
    QColor blackColor_ = QColor("#312e2b");
    QColor borderColor_ = QColor("#5c4033");
    bool showCenterLine_ = true;
    bool showEvaluationText_ = false;

    void updateToolTip();
};

#endif // CHESSGUI_EVALUATIONBAR_H
