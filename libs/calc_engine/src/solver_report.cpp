// SPDX-License-Identifier: GPL-3.0-or-later
// Solver's Answer Report (solver.h, M90, D-098), in the layout of Excel's:
// a header block (the workbook and sheet, when, the result, the engine with
// its time, iterations and subproblems, the options), then three tables —
// Objective Cell (Max / Min / Value Of) with its original and final value,
// Variable Cells with theirs and whether each is continuous, integer or
// binary, and Constraints with each cell's value, its formula, Binding or
// Not Binding and the slack. A cell's Name is Excel's: the nearest text to
// its left in the row and the nearest text above it in the column. Values
// keep the number format of the cell they report. The sheet is "Answer
// Report N" and goes before the model's sheet, as Excel puts it.

#include "solver.h"

#include "sheet.h"
#include "structure.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>

#include <cmath>
#include <iterator>

namespace QindaOffice::Calc {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Solver", text);
}

constexpr int kLabelReach = 1000; // how far a Name is looked for

QString textAt(const Workbook &book, qint32 sheet, qint32 row, qint32 col)
{
    const Value value = book.cellValue(sheet, row, col);
    return isText(value) ? std::get<QString>(value).trimmed() : QString();
}

// Excel's Name for a reported cell: the label beside it and the one above it.
QString cellName(const Workbook &book, qint32 sheet, const CellAddr &cell)
{
    QString left;
    for (qint32 c = cell.col - 1; c >= 0 && c >= cell.col - kLabelReach && left.isEmpty(); --c) {
        left = textAt(book, sheet, cell.row, c);
    }
    QString above;
    for (qint32 r = cell.row - 1; r >= 0 && r >= cell.row - kLabelReach && above.isEmpty(); --r) {
        above = textAt(book, sheet, r, cell.col);
    }
    return (left + QLatin1Char(' ') + above).trimmed();
}

QString limitText(double value)
{
    return value > 0 ? QLocale::c().toString(value, 'g', 12) : tr("Unlimited");
}

class ReportWriter {
public:
    ReportWriter(Workbook &book, qint32 report, qint32 source)
        : m_book(book)
        , m_report(report)
        , m_source(source)
    {
    }

    void put(qint32 row, qint32 col, const Value &value, qint32 style = 0)
    {
        m_book.setCellValue(m_report, {row, col}, value);
        if (style > 0) {
            m_book.sheetAt(m_report).setStyleIdAt({row, col}, style);
        }
    }
    void text(qint32 row, qint32 col, const QString &text, bool bold = false)
    {
        put(row, col, Value(text), bold ? styleFor(true, {}) : 0);
    }
    // A value as the reported cell formats it.
    void value(qint32 row, qint32 col, const Value &value, const CellAddr &from)
    {
        const CellStyle *style = m_book.style(m_book.sheetAt(m_source).styleIdAt(from));
        put(row, col, value, styleFor(false, style != nullptr ? style->numberFormat : QString()));
    }
    void header(qint32 row, const QStringList &titles)
    {
        for (qsizetype i = 0; i < titles.size(); ++i) {
            text(row, qint32(i) + 1, titles.at(i), true);
        }
    }

private:
    qint32 styleFor(bool bold, const QString &numberFormat)
    {
        const QString key = QString::number(int(bold)) + QLatin1Char('|') + numberFormat;
        if (const auto found = m_styles.constFind(key); found != m_styles.constEnd()) {
            return found.value();
        }
        CellStyle style;
        style.bold = bold;
        style.numberFormat = numberFormat.isEmpty() ? QStringLiteral("General") : numberFormat;
        const qint32 id = m_book.addStyle(style);
        m_styles.insert(key, id);
        return id;
    }

    Workbook &m_book;
    qint32 m_report;
    qint32 m_source;
    QHash<QString, qint32> m_styles;
};

QString goalText(const SolverModel &model)
{
    switch (model.goal) {
    case SolverGoal::Min: return tr("Min");
    case SolverGoal::ValueOf: return tr("Value Of");
    case SolverGoal::Max: break;
    }
    return tr("Max");
}

} // namespace

qint32 addAnswerReport(Workbook &book, qint32 modelSheet, const SolverModel &model, const SolverOutcome &outcome,
                       const QString &workbookName, const QDateTime &created)
{
    if (modelSheet < 0 || modelSheet >= book.sheetCount()) {
        return -1;
    }
    QString name;
    for (int n = 1;; ++n) {
        name = tr("Answer Report %1").arg(n);
        if (book.sheetIndex(name) < 0) {
            break;
        }
    }
    const QString sheetName = book.sheetName(modelSheet);
    book.addSheet(name);
    static_cast<void>(moveSheet(book, book.sheetCount() - 1, modelSheet));
    const qint32 report = modelSheet;
    const qint32 source = modelSheet + 1;
    ReportWriter out(book, report, source);
    const SolverOptions &o = model.options;
    qint32 row = 0;
    out.text(row++, 0, tr("QindaCalc Answer Report"), true);
    out.text(row++, 0, tr("Worksheet: [%1]%2").arg(workbookName, sheetName));
    out.text(row++, 0, tr("Report Created: %1").arg(QLocale::c().toString(created, QStringLiteral("M/d/yyyy h:mm:ss AP"))));
    out.text(row++, 0, tr("Result: %1").arg(outcome.message));
    out.text(row++, 0, tr("Solver Engine"), true);
    out.text(row++, 1, tr("Engine: %1").arg(solverMethodName(model.method)));
    out.text(row++, 1, tr("Solution Time: %1 Seconds.").arg(QLocale::c().toString(outcome.seconds, 'f', 3)));
    out.text(row++, 1, tr("Iterations: %1 Subproblems: %2").arg(outcome.iterations).arg(outcome.subproblems));
    out.text(row++, 0, tr("Solver Options"), true);
    out.text(row++, 1, tr("Max Time %1,  Iterations %2, Precision %3%4")
                           .arg(limitText(o.maxTime), limitText(double(o.iterations)),
                                solverNumberText(o.precision),
                                o.autoScale ? tr(", Use Automatic Scaling") : QString()));
    if (model.method != SolverMethod::SimplexLp) {
        out.text(row++, 1, tr("Convergence %1, Population Size %2, Random Seed %3%4%5")
                               .arg(solverNumberText(o.convergence))
                               .arg(o.populationSize)
                               .arg(o.randomSeed)
                               .arg(model.method == SolverMethod::GrgNonlinear
                                        ? (o.centralDerivatives ? tr(", Derivatives Central") : tr(", Derivatives Forward"))
                                        : tr(", Mutation Rate %1").arg(solverNumberText(o.mutationRate)),
                                    o.requireBounds ? tr(", Require Bounds") : QString()));
    }
    out.text(row++, 1, tr("Max Subproblems %1, Max Integer Sols %2, Integer Tolerance %3%%4")
                           .arg(limitText(double(o.maxSubproblems)), limitText(double(o.maxFeasible)),
                                solverNumberText(o.integerOptimality * 100.0),
                                model.nonNegative ? tr(", Assume NonNegative") : QString()));
    row += 2;
    if (outcome.objective.has_value()) {
        out.text(row++, 0, tr("Objective Cell (%1)").arg(goalText(model)), true);
        out.header(row++, {tr("Cell"), tr("Name"), tr("Original Value"), tr("Final Value")});
        const CellAddr cell = *outcome.objective;
        out.text(row, 1, addrText(cell, true, true));
        out.text(row, 2, cellName(book, source, cell));
        out.value(row, 3, outcome.originalObjective, cell);
        out.value(row, 4, outcome.finalObjective, cell);
        row += 3;
    }
    out.text(row++, 0, tr("Variable Cells"), true);
    out.header(row++, {tr("Cell"), tr("Name"), tr("Original Value"), tr("Final Value"), tr("Integer")});
    for (qsizetype k = 0; k < outcome.variables.size(); ++k) {
        const CellAddr cell = outcome.variables.at(k);
        const int kind = outcome.variableKinds.value(k);
        out.text(row, 1, addrText(cell, true, true));
        out.text(row, 2, cellName(book, source, cell));
        out.value(row, 3, Value(outcome.originalValues.value(k)), cell);
        out.value(row, 4, book.cellValue(source, cell.row, cell.col), cell);
        out.text(row, 5, kind == 2 ? tr("Binary") : (kind == 1 ? tr("Integer") : tr("Contin")));
        ++row;
    }
    row += 2;
    out.text(row++, 0, tr("Constraints"), true);
    out.header(row++, {tr("Cell"), tr("Name"), tr("Cell Value"), tr("Formula"), tr("Status"), tr("Slack")});
    for (const SolverReportConstraint &line : outcome.constraints) {
        out.text(row, 1, addrText(line.cell, true, true));
        out.text(row, 2, cellName(book, source, line.cell));
        out.value(row, 3, line.value, line.cell);
        out.text(row, 4, line.formula);
        out.text(row, 5, line.binding ? tr("Binding") : tr("Not Binding"));
        out.value(row, 6, Value(line.slack), line.cell);
        ++row;
    }
    Sheet &sheet = book.sheetAt(report);
    const qreal widths[] = {16, 72, 180, 112, 112, 96, 80};
    for (qint32 c = 0; c < qint32(std::size(widths)); ++c) {
        sheet.setColumnWidth(c, widths[c]);
    }
    return report;
}

} // namespace QindaOffice::Calc
