// SPDX-License-Identifier: GPL-3.0-or-later
// The Solver model in Excel's names (solver_model.h, M90, D-098).
//
// AGENT-NOTE: Excel's Solver add-in (Excel 2010 and later, solver_ver 3)
// writes one name per setting, scoped to the model's sheet and hidden;
// booleans are 1 (on) and 2 (off), "unlimited" limits 2147483647, an int /
// bin / dif constraint's right side the words integer / binary /
// AllDifferent. LibreOffice's SolverSettings (sc/source/core/data/
// SolverSettings.cxx) reads and writes the same names for the same reason.
// A sheet's names are read leniently (a missing one is Excel's default) and
// always written as Excel's whole set.

#include "solver_model.h"

#include "workbook.h"

#include <QHash>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Calc {

namespace {

constexpr qint64 kUnlimited = 2147483647;

bool isSolverName(const QString &name)
{
    return name.startsWith(QLatin1String("solver_"), Qt::CaseInsensitive);
}

} // namespace

QString solverNumberText(double value)
{
    if (!std::isfinite(value)) {
        return QStringLiteral("0");
    }
    QString text = QString::number(value, 'g', QLocale::FloatingPointShortest);
    if (!text.contains(QLatin1Char('e')) || std::abs(value) < 1e-15 || std::abs(value) >= 1e15) {
        return text;
    }
    // Exponent form: the same digits written out.
    for (int decimals = 0; decimals <= 20; ++decimals) {
        const QString fixed = QString::number(value, 'f', decimals);
        if (fixed.toDouble() == value) {
            return fixed;
        }
    }
    return text;
}

namespace {

QString numberText(double value)
{
    return solverNumberText(value);
}

QString limitText(qint64 value)
{
    return QString::number(value > 0 ? value : kUnlimited);
}

QString flagText(bool on)
{
    return on ? QStringLiteral("1") : QStringLiteral("2");
}

class Names {
public:
    Names(const Workbook &book, qint32 sheet)
    {
        for (const DefinedName &name : book.definedNames()) {
            if (name.sheet == sheet && isSolverName(name.name)) {
                m_values.insert(name.name.toLower(), name.formula.trimmed());
            }
        }
    }
    [[nodiscard]] bool has(const char *name) const { return m_values.contains(QLatin1String(name)); }
    [[nodiscard]] QString text(const QString &name) const { return m_values.value(name.toLower()); }
    [[nodiscard]] QString text(const char *name) const { return text(QLatin1String(name)); }
    [[nodiscard]] std::optional<double> number(const char *name) const
    {
        QString body = text(name);
        if (body.startsWith(QLatin1Char('=')))
            body = body.mid(1);
        bool ok = false;
        const double value = body.toDouble(&ok);
        return ok ? std::optional<double>(value) : std::nullopt;
    }
    [[nodiscard]] double number(const char *name, double fallback) const { return number(name).value_or(fallback); }
    // 1 on, 2 off (anything else: the default).
    [[nodiscard]] bool flag(const char *name, bool fallback) const
    {
        const std::optional<double> value = number(name);
        if (!value.has_value()) {
            return fallback;
        }
        return *value == 1 ? true : (*value == 2 ? false : fallback);
    }
    // A limit: 2147483647 (or more, or nothing) is unlimited.
    [[nodiscard]] qint64 limit(const char *name) const
    {
        const std::optional<double> value = number(name);
        if (!value.has_value() || *value <= 0 || *value >= double(kUnlimited)) {
            return 0;
        }
        return qint64(*value);
    }

private:
    QHash<QString, QString> m_values;
};

// A reference body without a leading '=' and without the brackets a union
// gets when the engine prints it ("(A1:B2,D4)").
QString refBody(const QString &body)
{
    QString text = body.trimmed();
    if (text.startsWith(QLatin1Char('='))) {
        text = text.mid(1);
    }
    return solverRefParts(text).join(QLatin1Char(','));
}

} // namespace

QStringList solverRefParts(const QString &text)
{
    QStringList parts;
    QString current;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('\'')) {
            quoted = !quoted;
        }
        if (!quoted && (c == QLatin1Char('(') || c == QLatin1Char(')'))) {
            continue; // a union the engine printed in brackets
        }
        if (c == QLatin1Char(',') && !quoted) {
            if (!current.trimmed().isEmpty()) {
                parts.append(current.trimmed());
            }
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty()) {
        parts.append(current.trimmed());
    }
    return parts;
}

std::optional<SolverModel> solverModelOf(const Workbook &book, qint32 sheet)
{
    if (sheet < 0 || sheet >= book.sheetCount()) {
        return std::nullopt;
    }
    const Names names(book, sheet);
    if (!names.has("solver_opt") && !names.has("solver_adj")) {
        return std::nullopt;
    }
    SolverModel model;
    model.objective = refBody(names.text("solver_opt"));
    const int type = int(names.number("solver_typ", 1));
    model.goal = type == 2 ? SolverGoal::Min : (type == 3 ? SolverGoal::ValueOf : SolverGoal::Max);
    model.valueOf = names.number("solver_val", 0);
    model.variables = solverRefParts(refBody(names.text("solver_adj"))).join(QLatin1Char(','));
    model.nonNegative = names.flag("solver_neg", true);
    const int engine = int(names.number("solver_eng", 0));
    if (engine >= 1 && engine <= 3) {
        model.method = SolverMethod(engine);
    } else {
        // Excel 2007 and earlier: "Assume Linear Model" (solver_lin 1).
        model.method = names.flag("solver_lin", false) ? SolverMethod::SimplexLp : SolverMethod::GrgNonlinear;
    }
    const int count = int(names.number("solver_num", 0));
    for (int i = 1; i <= qBound(0, count, 10000); ++i) {
        const QString n = QString::number(i);
        const QString left = refBody(names.text(QStringLiteral("solver_lhs") + n));
        const QString rel = names.text(QStringLiteral("solver_rel") + n);
        if (left.isEmpty()) {
            continue;
        }
        bool ok = false;
        const int code = rel.toInt(&ok);
        SolverConstraint constraint;
        constraint.left = left;
        constraint.relation = ok && code >= 1 && code <= 6 ? SolverRelation(code) : SolverRelation::LessEqual;
        if (constraint.relation <= SolverRelation::GreaterEqual) {
            QString right = names.text(QStringLiteral("solver_rhs") + n);
            if (right.startsWith(QLatin1Char('='))) {
                right = right.mid(1);
            }
            constraint.right = right.trimmed();
        }
        model.constraints.append(constraint);
    }
    SolverOptions &o = model.options;
    o.precision = names.number("solver_pre", o.precision);
    o.autoScale = names.flag("solver_scl", o.autoScale);
    o.showIterations = names.flag("solver_sho", o.showIterations);
    o.ignoreIntegers = names.flag("solver_rlx", o.ignoreIntegers);
    o.integerOptimality = names.number("solver_tol", o.integerOptimality);
    o.maxTime = double(names.limit("solver_tim"));
    o.iterations = names.limit("solver_itr");
    o.maxSubproblems = names.limit("solver_mip");
    o.maxFeasible = names.limit("solver_nod");
    o.convergence = names.number("solver_cvg", o.convergence);
    o.centralDerivatives = int(names.number("solver_drv", 1)) == 2;
    o.multistart = names.flag("solver_msl", o.multistart);
    o.populationSize = int(names.number("solver_ssz", o.populationSize));
    o.randomSeed = int(names.number("solver_rsd", o.randomSeed));
    o.requireBounds = names.flag("solver_rbv", o.requireBounds);
    o.mutationRate = names.number("solver_mrt", o.mutationRate);
    o.maxTimeWithoutImprovement = int(names.number("solver_mni", o.maxTimeWithoutImprovement));
    o.estimates = int(names.number("solver_est", o.estimates));
    o.search = int(names.number("solver_nwt", o.search));
    return model;
}

void clearSolverModel(Workbook &book, qint32 sheet)
{
    QList<DefinedName> kept;
    for (const DefinedName &name : book.definedNames()) {
        if (!(name.sheet == sheet && isSolverName(name.name))) {
            kept.append(name);
        }
    }
    if (kept.size() != book.definedNames().size()) {
        book.setDefinedNames(kept);
    }
}

void setSolverModel(Workbook &book, qint32 sheet, const SolverModel &model)
{
    QList<std::pair<QString, QString>> entries;
    const auto add = [&entries](const QString &name, const QString &body) { entries.append({name, body}); };
    if (!model.objective.trimmed().isEmpty()) {
        add(QStringLiteral("solver_opt"), model.objective.trimmed());
    }
    add(QStringLiteral("solver_typ"), QString::number(int(model.goal)));
    add(QStringLiteral("solver_val"), numberText(model.valueOf));
    if (!model.variables.trimmed().isEmpty()) {
        add(QStringLiteral("solver_adj"), solverRefParts(model.variables).join(QLatin1Char(',')));
    }
    add(QStringLiteral("solver_num"), QString::number(model.constraints.size()));
    for (qsizetype i = 0; i < model.constraints.size(); ++i) {
        const SolverConstraint &c = model.constraints.at(i);
        const QString n = QString::number(i + 1);
        add(QStringLiteral("solver_lhs") + n, c.left);
        add(QStringLiteral("solver_rel") + n, QString::number(int(c.relation)));
        QString right = c.right;
        switch (c.relation) {
        case SolverRelation::Integer: right = QStringLiteral("integer"); break;
        case SolverRelation::Binary: right = QStringLiteral("binary"); break;
        case SolverRelation::AllDifferent: right = QStringLiteral("AllDifferent"); break;
        default: break;
        }
        add(QStringLiteral("solver_rhs") + n, right.isEmpty() ? QStringLiteral("0") : right);
    }
    const SolverOptions &o = model.options;
    add(QStringLiteral("solver_neg"), flagText(model.nonNegative));
    add(QStringLiteral("solver_eng"), QString::number(int(model.method)));
    add(QStringLiteral("solver_pre"), numberText(o.precision));
    add(QStringLiteral("solver_scl"), flagText(o.autoScale));
    add(QStringLiteral("solver_sho"), flagText(o.showIterations));
    add(QStringLiteral("solver_rlx"), flagText(o.ignoreIntegers));
    add(QStringLiteral("solver_tol"), numberText(o.integerOptimality));
    add(QStringLiteral("solver_tim"), limitText(qint64(std::llround(o.maxTime))));
    add(QStringLiteral("solver_itr"), limitText(o.iterations));
    add(QStringLiteral("solver_mip"), limitText(o.maxSubproblems));
    add(QStringLiteral("solver_nod"), limitText(o.maxFeasible));
    add(QStringLiteral("solver_cvg"), numberText(o.convergence));
    add(QStringLiteral("solver_drv"), o.centralDerivatives ? QStringLiteral("2") : QStringLiteral("1"));
    add(QStringLiteral("solver_msl"), flagText(o.multistart));
    add(QStringLiteral("solver_ssz"), QString::number(o.populationSize));
    add(QStringLiteral("solver_rsd"), QString::number(o.randomSeed));
    add(QStringLiteral("solver_rbv"), flagText(o.requireBounds));
    add(QStringLiteral("solver_mrt"), numberText(o.mutationRate));
    add(QStringLiteral("solver_mni"), QString::number(o.maxTimeWithoutImprovement));
    add(QStringLiteral("solver_est"), QString::number(o.estimates == 2 ? 2 : 1));
    add(QStringLiteral("solver_nwt"), QString::number(o.search == 2 ? 2 : 1));
    add(QStringLiteral("solver_ver"), QStringLiteral("3"));
    // Excel's order: by name (solver_lhs1 … before solver_lhs10 as text).
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    QList<DefinedName> names;
    for (const DefinedName &name : book.definedNames()) {
        if (!(name.sheet == sheet && isSolverName(name.name))) {
            names.append(name);
        }
    }
    for (const auto &[name, body] : std::as_const(entries)) {
        DefinedName entry;
        entry.name = name;
        entry.sheet = sheet;
        entry.formula = body;
        entry.hidden = true;
        names.append(entry);
    }
    book.setDefinedNames(names);
}

QString solverRelationText(SolverRelation relation)
{
    switch (relation) {
    case SolverRelation::LessEqual: return QStringLiteral("<=");
    case SolverRelation::Equal: return QStringLiteral("=");
    case SolverRelation::GreaterEqual: return QStringLiteral(">=");
    case SolverRelation::Integer: return QStringLiteral("int");
    case SolverRelation::Binary: return QStringLiteral("bin");
    case SolverRelation::AllDifferent: return QStringLiteral("dif");
    }
    return QStringLiteral("<=");
}

std::optional<SolverRelation> solverRelationFrom(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("<=")) return SolverRelation::LessEqual;
    if (t == QLatin1String("=")) return SolverRelation::Equal;
    if (t == QLatin1String(">=")) return SolverRelation::GreaterEqual;
    if (t == QLatin1String("int") || t == QLatin1String("integer")) return SolverRelation::Integer;
    if (t == QLatin1String("bin") || t == QLatin1String("binary")) return SolverRelation::Binary;
    if (t == QLatin1String("dif") || t == QLatin1String("alldifferent")) return SolverRelation::AllDifferent;
    return std::nullopt;
}

QString solverMethodName(SolverMethod method)
{
    switch (method) {
    case SolverMethod::SimplexLp: return QStringLiteral("Simplex LP");
    case SolverMethod::Evolutionary: return QStringLiteral("Evolutionary");
    case SolverMethod::GrgNonlinear: break;
    }
    return QStringLiteral("GRG Nonlinear");
}

} // namespace QindaOffice::Calc
