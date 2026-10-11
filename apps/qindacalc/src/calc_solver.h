// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ Solver in QindaCalc (M90, D-098): the Solver Parameters dialog's
// model (SolverDialog.qml and its Add Constraint and Options dialogs), Solve
// on a worker thread with progress and Stop (SolverProgressDialog.qml), and
// Solver Results (SolverResultsDialog.qml): Keep Solver Solution or Restore
// Original Values, with the Answer Report, as ONE undo step "Solver".
// calc_engine does the solving (solver.h) and keeps the model in Excel's
// hidden solver_* names (solver_model.h).
//
// AGENT-NOTE (threading, the engine's rule: one thread per Workbook): Solve
// takes a snapshot of the document on the GUI thread; the worker rebuilds
// its own Workbook from it (calc_undo.h's restoreSnapshot) and solves that
// copy, reporting through atomics a GUI timer reads and stopping on an
// atomic flag. The answer — the variable cells' values — is written into
// the real document on the GUI thread, where it shows behind Solver
// Results until Keep or Restore makes the step. While a solve runs, the
// progress dialog is modal; whatever else changes in the window meanwhile
// is put back by the step (it restarts from the snapshot).

#include "calc_undo.h"
#include "solver.h"

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

namespace QindaOffice::Quick {
class ActionModel;
class MenuTree;
}

namespace QindaOffice::Apps::QindaCalc {

class CalcHost;

class CalcSolver final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by CalcHost.")
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // "Trial Solution 12: 34.5" while solving.
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
public:
    explicit CalcSolver(CalcHost *host);
    ~CalcSolver() override;

    // Data ▸ Analyze ▸ Solver (calcSolverAction) and its Data menu entry.
    void createActions(Quick::ActionModel *model);
    void insertMenus(Quick::MenuTree *tree);
    void sync(Quick::ActionModel *model, bool worksheet);

    [[nodiscard]] bool running() const { return m_job != nullptr; }
    [[nodiscard]] QString progressText() const { return m_progress; }

    // The active sheet's model for the dialog (references without the
    // sheet's own name): {objective, goal ("max" | "min" | "value"), valueOf,
    // variables, constraints: [{left, relation, right, text}], nonNegative,
    // method (1 GRG Nonlinear, 2 Simplex LP, 3 Evolutionary), options: {…}}.
    // A sheet with no model gets Excel's defaults with the cursor as objective.
    Q_INVOKABLE QVariantMap parameters() const;
    // Reset All: Excel's defaults, nothing filled in.
    Q_INVOKABLE QVariantMap defaultParameters() const;
    // Add / Change Constraint: "" or Excel's message.
    Q_INVOKABLE QString checkConstraint(const QVariantMap &parameters, const QVariantMap &constraint) const;
    // Close: the model kept in the workbook's names, one undo step
    // "Solver Parameters" when it changed. "" or what is wrong.
    Q_INVOKABLE QString saveParameters(const QVariantMap &parameters);
    // Solve: "" — started; finished() follows — or why it cannot start.
    Q_INVOKABLE QString solve(const QVariantMap &parameters);
    Q_INVOKABLE void stop();
    // Solver Results' OK: Keep Solver Solution (or Restore Original Values)
    // with or without the Answer Report — one undo step.
    Q_INVOKABLE void finish(bool keep, bool answerReport);

signals:
    void parametersRequested();
    void runningChanged();
    void progressChanged();
    // {status (Excel's code), message, feasible, changed}.
    void finished(const QVariantMap &result);

private:
    struct Job;
    [[nodiscard]] CalcEng::SolverModel modelFrom(const QVariantMap &parameters) const;
    [[nodiscard]] QVariantMap mapOf(const CalcEng::SolverModel &model) const;
    [[nodiscard]] QString sheetName() const;
    void workerDone();
    void pollProgress();

    CalcHost *m_host;
    std::shared_ptr<Job> m_job;
    QPointer<QThread> m_thread;
    QTimer m_poll;
    QString m_progress;
    // The solve waiting for Solver Results: the workbook before it, the
    // model it solved, on which sheet, and its outcome.
    std::shared_ptr<WorkbookSnapshot> m_before;
    CalcEng::SolverModel m_model;
    qint32 m_sheet = 0;
    bool m_stale = false; // a document edit after the worker snapshot
    CalcEng::SolverOutcome m_outcome;
};

} // namespace QindaOffice::Apps::QindaCalc
