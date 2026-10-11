// SPDX-License-Identifier: GPL-3.0-or-later
// Data ▸ Solver in QindaCalc (calc_solver.h, M90, D-098).

#include "calc_solver.h"

#include "calc_host.h"
#include "spreadsheet_document.h"
#include "standard_actions.h"

#include "parser.h"
#include "sheet.h"
#include "workbook.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
namespace ActionId = Quick::ActionId;

struct CalcSolver::Job {
    std::atomic_bool stop{false};
    std::atomic_int trials{0};
    std::atomic<double> objective{0};
    SolverOutcome outcome; // the worker's; read once the thread has finished
};

namespace {

// References qualified with the model's sheet (stored) or without it (shown).
NodePtr withSheet(const NodePtr &node, const QString &sheet, bool qualify)
{
    if (!node) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::Ref: {
        RangeRef ref = node->ref;
        if (qualify && ref.sheet.isEmpty()) {
            ref.sheet = quotedSheetName(sheet);
        } else if (!qualify && !ref.sheet.isEmpty()
                   && unquotedSheetName(ref.sheet).compare(sheet, Qt::CaseInsensitive) == 0) {
            ref.sheet.clear();
        } else {
            return node;
        }
        return Node::makeRef(ref);
    }
    case Node::Kind::Unary:
        return Node::makeUnary(node->op, withSheet(node->left, sheet, qualify));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op, withSheet(node->left, sheet, qualify), withSheet(node->right, sheet, qualify));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        for (const NodePtr &arg : node->args) {
            args.append(withSheet(arg, sheet, qualify));
        }
        return Node::makeCall(node->text, args);
    }
    default:
        return node;
    }
}

// A formula or reference list with the sheet's references qualified (or
// not); text that does not parse is kept as typed (the checks say why).
QString converted(const QString &text, const QString &sheet, bool qualify)
{
    QString body = text.trimmed();
    if (body.startsWith(QLatin1Char('='))) {
        body = body.mid(1).trimmed();
    }
    if (body.isEmpty()) {
        return {};
    }
    const ParseResult parsed = parseFormula(body);
    if (!parsed.error.isEmpty() || !parsed.root) {
        return body;
    }
    return nameBodyText(withSheet(parsed.root, sheet, qualify));
}

QString limitText(double value)
{
    return value > 0 ? solverNumberText(value) : QString();
}

double numberOf(const QVariantMap &map, const char *key, double fallback)
{
    bool ok = false;
    const QString text = map.value(QLatin1String(key)).toString().trimmed();
    const double value = QLocale::c().toDouble(text, &ok);
    return ok ? value : (text.isEmpty() && map.contains(QLatin1String(key)) ? 0.0 : fallback);
}

} // namespace

CalcSolver::CalcSolver(CalcHost *host)
    : QObject(host)
    , m_host(host)
{
    connect(host->spreadsheetDocument(), &SpreadsheetDocument::contentChanged, this, [this] {
        if (m_before) { m_stale = true; stop(); }
    });
    m_poll.setInterval(100);
    connect(&m_poll, &QTimer::timeout, this, &CalcSolver::pollProgress);
}

CalcSolver::~CalcSolver()
{
    if (m_job) {
        m_job->stop.store(true);
    }
    if (m_thread) {
        m_thread->wait();
        delete m_thread.data();
    }
}

void CalcSolver::createActions(Quick::ActionModel *model)
{
    Quick::ActionEntry *entry = model->add(QString::fromLatin1(ActionId::kCalcSolver), tr("Sol&ver…"), {},
                                           QStringLiteral("calculator"));
    connect(entry, &Quick::ActionEntry::triggered, this, [this] {
        if (!m_host->m_syncing && !running()) {
            emit parametersRequested();
        }
    });
}

void CalcSolver::insertMenus(Quick::MenuTree *tree)
{
    tree->appendTo(QStringLiteral("calcDataMenu"), Quick::MenuNode::action(QString::fromLatin1(ActionId::kCalcSolver)));
}

void CalcSolver::sync(Quick::ActionModel *model, bool worksheet)
{
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSolver), worksheet && !running());
}

QString CalcSolver::sheetName() const
{
    const SpreadsheetDocument *document = m_host->spreadsheetDocument();
    return document->workbook()->sheetName(document->activeSheet());
}

QVariantMap CalcSolver::mapOf(const SolverModel &model) const
{
    const QString sheet = sheetName();
    QVariantList constraints;
    for (const SolverConstraint &c : model.constraints) {
        const QString left = converted(c.left, sheet, false);
        const QString right = c.relation == SolverRelation::Integer ? QStringLiteral("integer")
            : c.relation == SolverRelation::Binary                  ? QStringLiteral("binary")
            : c.relation == SolverRelation::AllDifferent            ? QStringLiteral("AllDifferent")
                                                                    : converted(c.right, sheet, false);
        constraints.append(QVariantMap{{QStringLiteral("left"), left},
                                       {QStringLiteral("relation"), solverRelationText(c.relation)},
                                       {QStringLiteral("right"), right},
                                       {QStringLiteral("text"), left + QLatin1Char(' ') + solverRelationText(c.relation)
                                                                     + QLatin1Char(' ') + right}});
    }
    const SolverOptions &o = model.options;
    const QVariantMap options{
        {QStringLiteral("precision"), solverNumberText(o.precision)},
        {QStringLiteral("autoScale"), o.autoScale},
        {QStringLiteral("showIterations"), o.showIterations},
        {QStringLiteral("ignoreIntegers"), o.ignoreIntegers},
        {QStringLiteral("integerOptimality"), solverNumberText(o.integerOptimality * 100.0)},
        {QStringLiteral("maxTime"), limitText(o.maxTime)},
        {QStringLiteral("iterations"), limitText(double(o.iterations))},
        {QStringLiteral("maxSubproblems"), limitText(double(o.maxSubproblems))},
        {QStringLiteral("maxFeasible"), limitText(double(o.maxFeasible))},
        {QStringLiteral("convergence"), solverNumberText(o.convergence)},
        {QStringLiteral("centralDerivatives"), o.centralDerivatives},
        {QStringLiteral("multistart"), o.multistart},
        {QStringLiteral("populationSize"), QString::number(o.populationSize)},
        {QStringLiteral("randomSeed"), QString::number(o.randomSeed)},
        {QStringLiteral("requireBounds"), o.requireBounds},
        {QStringLiteral("mutationRate"), solverNumberText(o.mutationRate)},
        {QStringLiteral("maxTimeWithoutImprovement"), QString::number(o.maxTimeWithoutImprovement)},
        {QStringLiteral("estimates"), o.estimates},
        {QStringLiteral("search"), o.search},
    };
    return {{QStringLiteral("objective"), converted(model.objective, sheet, false)},
            {QStringLiteral("goal"), model.goal == SolverGoal::Min       ? QStringLiteral("min")
                                     : model.goal == SolverGoal::ValueOf ? QStringLiteral("value")
                                                                         : QStringLiteral("max")},
            {QStringLiteral("valueOf"), solverNumberText(model.valueOf)},
            {QStringLiteral("variables"), converted(model.variables, sheet, false)},
            {QStringLiteral("constraints"), constraints},
            {QStringLiteral("nonNegative"), model.nonNegative},
            {QStringLiteral("method"), int(model.method)},
            {QStringLiteral("options"), options}};
}

SolverModel CalcSolver::modelFrom(const QVariantMap &map) const
{
    const QString sheet = sheetName();
    SolverModel model;
    model.objective = converted(map.value(QStringLiteral("objective")).toString(), sheet, true);
    const QString goal = map.value(QStringLiteral("goal")).toString();
    model.goal = goal == QLatin1String("min") ? SolverGoal::Min : (goal == QLatin1String("value") ? SolverGoal::ValueOf : SolverGoal::Max);
    model.valueOf = numberOf(map, "valueOf", 0);
    model.variables = converted(map.value(QStringLiteral("variables")).toString(), sheet, true);
    for (const QVariant &entry : map.value(QStringLiteral("constraints")).toList()) {
        const QVariantMap c = entry.toMap();
        SolverConstraint constraint;
        constraint.left = converted(c.value(QStringLiteral("left")).toString(), sheet, true);
        constraint.relation = solverRelationFrom(c.value(QStringLiteral("relation")).toString()).value_or(SolverRelation::LessEqual);
        if (constraint.relation <= SolverRelation::GreaterEqual) {
            constraint.right = converted(c.value(QStringLiteral("right")).toString(), sheet, true);
        }
        model.constraints.append(constraint);
    }
    model.nonNegative = map.value(QStringLiteral("nonNegative"), true).toBool();
    const int method = map.value(QStringLiteral("method"), 1).toInt();
    model.method = method >= 1 && method <= 3 ? SolverMethod(method) : SolverMethod::GrgNonlinear;
    const QVariantMap o = map.value(QStringLiteral("options")).toMap();
    SolverOptions &options = model.options;
    options.precision = numberOf(o, "precision", options.precision);
    options.autoScale = o.value(QStringLiteral("autoScale"), options.autoScale).toBool();
    options.showIterations = o.value(QStringLiteral("showIterations"), options.showIterations).toBool();
    options.ignoreIntegers = o.value(QStringLiteral("ignoreIntegers"), options.ignoreIntegers).toBool();
    options.integerOptimality = numberOf(o, "integerOptimality", options.integerOptimality * 100.0) / 100.0;
    options.maxTime = qMax(0.0, numberOf(o, "maxTime", options.maxTime));
    options.iterations = qMax(qint64(0), qint64(numberOf(o, "iterations", double(options.iterations))));
    options.maxSubproblems = qMax(qint64(0), qint64(numberOf(o, "maxSubproblems", double(options.maxSubproblems))));
    options.maxFeasible = qMax(qint64(0), qint64(numberOf(o, "maxFeasible", double(options.maxFeasible))));
    options.convergence = numberOf(o, "convergence", options.convergence);
    options.centralDerivatives = o.value(QStringLiteral("centralDerivatives"), options.centralDerivatives).toBool();
    options.multistart = o.value(QStringLiteral("multistart"), options.multistart).toBool();
    options.populationSize = int(numberOf(o, "populationSize", options.populationSize));
    options.randomSeed = int(numberOf(o, "randomSeed", options.randomSeed));
    options.requireBounds = o.value(QStringLiteral("requireBounds"), options.requireBounds).toBool();
    options.mutationRate = numberOf(o, "mutationRate", options.mutationRate);
    options.maxTimeWithoutImprovement = int(numberOf(o, "maxTimeWithoutImprovement", options.maxTimeWithoutImprovement));
    options.estimates = o.value(QStringLiteral("estimates"), options.estimates).toInt();
    options.search = o.value(QStringLiteral("search"), options.search).toInt();
    return model;
}

QVariantMap CalcSolver::parameters() const
{
    const SpreadsheetDocument *document = m_host->spreadsheetDocument();
    if (const std::optional<SolverModel> model = solverModelOf(*document->workbook(), document->activeSheet())) {
        return mapOf(*model);
    }
    // Excel starts Set Objective at the active cell.
    QVariantMap map = defaultParameters();
    map.insert(QStringLiteral("objective"), addrText(m_host->cursor(), true, true));
    return map;
}

QVariantMap CalcSolver::defaultParameters() const
{
    return mapOf(SolverModel{});
}

QString CalcSolver::checkConstraint(const QVariantMap &parameters, const QVariantMap &constraint) const
{
    const SpreadsheetDocument *document = m_host->spreadsheetDocument();
    QVariantMap single = parameters;
    single.insert(QStringLiteral("constraints"), QVariantList{constraint});
    const SolverModel model = modelFrom(single);
    if (model.constraints.isEmpty()) {
        return tr("Cell Reference is not valid.");
    }
    return solverConstraintProblem(*document->workbook(), document->activeSheet(), model, model.constraints.first());
}

QString CalcSolver::saveParameters(const QVariantMap &parameters)
{
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    const qint32 sheet = document->activeSheet();
    const SolverModel model = modelFrom(parameters);
    const std::optional<SolverModel> stored = solverModelOf(*document->workbook(), sheet);
    if ((stored.has_value() && *stored == model)
        || (!stored.has_value() && model.variables.isEmpty() && model.constraints.isEmpty())) {
        return {};
    }
    const std::shared_ptr<WorkbookSnapshot> before = document->snapshotNow();
    setSolverModel(*document->workbook(), sheet, model);
    document->commitSnapshotStep(*before, tr("Solver Parameters"));
    return {};
}

QString CalcSolver::solve(const QVariantMap &parameters)
{
    if (running()) {
        return tr("Solver is already running.");
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    const qint32 sheet = document->activeSheet();
    const SolverModel model = modelFrom(parameters);
    if (const QString problem = solverModelProblem(*document->workbook(), sheet, model); !problem.isEmpty()) {
        return problem;
    }
    for (const QString &part : solverRefParts(model.variables)) {
        const ParseResult parsed = parseFormula(part);
        if (parsed.root && parsed.root->kind == Node::Kind::Ref && document->refuseCellEdit(parsed.root->ref.range)) {
            return tr("The variable cells cannot be changed (%1).").arg(converted(part, sheetName(), false));
        }
    }
    m_before = document->snapshotNow();
    m_stale = false;
    m_model = model;
    m_sheet = sheet;
    auto job = std::make_shared<Job>();
    m_job = job;
    const WorkbookSnapshot snapshot = *m_before;
    QThread *thread = QThread::create([job, snapshot, sheet, model] {
        // The worker's own copy: one thread per Workbook (solver.h).
        Workbook book;
        restoreSnapshot(&book, snapshot);
        SolverCallbacks callbacks;
        callbacks.progress = [job](const SolverProgress &progress) {
            job->trials.store(progress.trials);
            job->objective.store(progress.objective);
            return !job->stop.load();
        };
        job->outcome = solveModel(book, sheet, model, callbacks);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, this, [this, thread, job] {
        thread->deleteLater();
        if (m_job == job) {
            workerDone();
        }
    });
    m_progress = tr("Setting up problem…");
    m_poll.start();
    thread->start();
    sync(m_host->actions(), true);
    emit runningChanged();
    emit progressChanged();
    return {};
}

void CalcSolver::stop()
{
    if (m_job) {
        m_job->stop.store(true);
    }
}

void CalcSolver::pollProgress()
{
    if (!m_job) {
        return;
    }
    const int trials = m_job->trials.load();
    if (trials > 0) {
        m_progress = tr("Trial Solution %1: %2").arg(trials).arg(QLocale().toString(m_job->objective.load(), 'g', 10));
        emit progressChanged();
    }
}

void CalcSolver::workerDone()
{
    m_poll.stop();
    m_outcome = m_job->outcome;
    m_job.reset();
    m_thread.clear();
    if (m_stale) {
        // AGENT-GUARD: a worker snapshot must never overwrite later edits
        // (including native agent edits while the modal dialog is open).
        m_before.reset();
        m_outcome.changed = false;
        m_outcome.status = SolverStatus::Stopped;
        m_outcome.message = tr("The workbook changed while Solver was running. Run Solver again on the current values.");
    }
    // What Solver left shows in the cells behind Solver Results.
    Workbook &book = *m_host->spreadsheetDocument()->workbook();
    if (m_outcome.changed) {
        book.beginBatch();
        for (qsizetype k = 0; k < m_outcome.variables.size() && k < m_outcome.values.size(); ++k) {
            book.setCellValue(m_sheet, m_outcome.variables.at(k), Value(m_outcome.values.at(k)));
        }
        book.endBatch();
    }
    m_progress.clear();
    m_host->refreshGrid();
    m_host->syncFromCursor();
    emit runningChanged();
    emit progressChanged();
    emit finished({{QStringLiteral("status"), int(m_outcome.status)},
                   {QStringLiteral("message"), m_outcome.message},
                   {QStringLiteral("feasible"), solverStatusFeasible(m_outcome.status)},
                   {QStringLiteral("changed"), m_outcome.changed}});
}

void CalcSolver::finish(bool keep, bool answerReport)
{
    if (!m_before) {
        return;
    }
    if (m_stale) {
        m_before.reset();
        m_host->showStatusMessage(tr("The workbook changed after Solver finished; its pending result was not applied."), 8000);
        return;
    }
    SpreadsheetDocument *document = m_host->spreadsheetDocument();
    const std::shared_ptr<WorkbookSnapshot> before = std::move(m_before);
    m_before.reset();
    // The step starts from the workbook as it was: the model's names, then
    // the solution (Keep), then the report.
    document->restoreWithoutUndo(*before);
    Workbook &book = *document->workbook();
    const std::optional<SolverModel> stored = solverModelOf(book, m_sheet);
    bool changed = !stored.has_value() || !(*stored == m_model);
    setSolverModel(book, m_sheet, m_model);
    if (keep && m_outcome.changed) {
        book.beginBatch();
        for (qsizetype k = 0; k < m_outcome.variables.size() && k < m_outcome.values.size(); ++k) {
            book.setCellValue(m_sheet, m_outcome.variables.at(k), Value(m_outcome.values.at(k)));
        }
        book.endBatch();
        changed = true;
    }
    bool sheets = false;
    if (answerReport && m_outcome.started) {
        const QString path = document->canonicalPath();
        const QString name = path.isEmpty() ? document->displayTitle() : QFileInfo(path).fileName();
        const qint32 report = addAnswerReport(book, m_sheet, m_model, m_outcome, name, QDateTime::currentDateTime());
        if (report >= 0) {
            document->setActiveSheet(report);
            sheets = true;
            changed = true;
        }
    }
    if (changed) {
        document->commitSnapshotStep(*before, tr("Solver"), sheets);
    }
    m_host->refreshGrid();
    m_host->syncFromCursor();
    emit m_host->cursorChanged();
}

} // namespace QindaOffice::Apps::QindaCalc
