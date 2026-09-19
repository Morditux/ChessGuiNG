//
// Dialog for editing the weights and the search limits of the heuristic
// evaluator.
//

#ifndef CHESSGUI_EVALPARAMSDIALOG_H
#define CHESSGUI_EVALPARAMSDIALOG_H

#include <QDialog>
#include <QHash>
#include <QString>

#include "evalparamfields.h"
#include "heuristiceval.h"

class QFormLayout;
class QSpinBox;
class QWidget;

class EvalParamsDialog : public QDialog {
    Q_OBJECT

public:
    // `searchThreads` is the cap passed to HeuristicEval::setSearchThreads()
    // (zero means automatic) and `evaluationDepth` the depth the controller
    // searches at for the live evaluation and the whole-game curve. Restoring
    // the defaults puts the threads back to automatic and the depth back to
    // `defaultEvaluationDepth`.
    explicit EvalParamsDialog(const HeuristicEval::EvalParams &params,
                              int searchThreads, int evaluationDepth,
                              int defaultEvaluationDepth =
                                  HeuristicEval::DefaultSearchDepth,
                              QWidget *parent = nullptr);

    [[nodiscard]] HeuristicEval::EvalParams params() const;
    [[nodiscard]] int searchThreads() const;
    [[nodiscard]] int evaluationDepth() const;

private:
    QFormLayout *groupForm(const QString &group, QWidget *container);
    // Puts every weight back to the calibrated default, and the search limits
    // back to automatic threads and the default depth.
    void restoreDefaults();

    QHash<QString, QSpinBox *> fields_;
    QHash<QString, QFormLayout *> groups_;
    QSpinBox *threadsSpin_ = nullptr;
    QSpinBox *depthSpin_ = nullptr;
    int defaultDepth_ = HeuristicEval::DefaultSearchDepth;
};

#endif // CHESSGUI_EVALPARAMSDIALOG_H
