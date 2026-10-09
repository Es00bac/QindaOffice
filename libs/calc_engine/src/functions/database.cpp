// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): Excel's database functions — DSUM, DCOUNT, DCOUNTA,
// DAVERAGE, DMAX, DMIN, DGET, DPRODUCT, DSTDEV, DSTDEVP, DVAR, DVARP.
//
// AGENT-NOTE: the criteria range follows Excel's advanced-filter rules:
// its first row names database columns; a record matches when, for some
// criteria row, every non-empty criterion in it matches (AND across a row,
// OR down the rows). A criterion is a comparison (">5", "<>x", "=Apple")
// or a value: a number means equality, plain text means "begins with" (an
// implicit trailing *), wildcards allowed — criteriaMatches does the
// comparing. Formula criteria (a computed condition under a header that
// names no column) are not evaluated. The aggregate itself is the
// registered SUM/COUNT/… over the matching values, so the two cannot
// drift apart.

#include "functions.h"

#include "../eval_array.h"

namespace QindaOffice::Calc {

namespace {

std::optional<Value> gridOf(const EvalContext &ctx, const Arg &arg, ArrayValue *grid)
{
    if (const Value failed = arrayFromArg(ctx, arg, grid); isErr(failed)) {
        return failed;
    }
    return std::nullopt;
}

Value cellOf(const ArrayValue &grid, qint32 row, qint32 col)
{
    return grid.cells.at(row * grid.cols + col);
}

qint32 columnNamed(const ArrayValue &grid, const QString &label)
{
    for (qint32 c = 0; c < grid.cols; ++c) {
        if (toText(cellOf(grid, 0, c)).trimmed().compare(label.trimmed(), Qt::CaseInsensitive) == 0) {
            return c;
        }
    }
    return -1;
}

// One criterion against one database value.
bool matches(const Value &value, const Value &criterion)
{
    if (isBlank(criterion)) {
        return true;
    }
    if (isNumber(criterion) || isBool(criterion)) {
        return compareValues(value, criterion) == 0;
    }
    const QString text = toText(criterion);
    static const QStringList kOperators{QStringLiteral(">="), QStringLiteral("<="), QStringLiteral("<>"),
                                        QStringLiteral(">"),  QStringLiteral("<"),  QStringLiteral("=")};
    for (const QString &op : kOperators) {
        if (text.startsWith(op)) {
            return criteriaMatches(value, text);
        }
    }
    bool numeric = false;
    text.toDouble(&numeric);
    return criteriaMatches(value, numeric ? text : text + QLatin1Char('*'));
}

// The values of `field` in the records the criteria select; false with
// the error when the arguments are not a database and criteria.
bool selected(const EvalContext &ctx, const QVector<Arg> &args, QVector<Value> *values, Value *error)
{
    ArrayValue db;
    ArrayValue criteria;
    if (const auto err = gridOf(ctx, args.at(0), &db)) {
        *error = *err;
        return false;
    }
    if (const auto err = gridOf(ctx, args.at(2), &criteria)) {
        *error = *err;
        return false;
    }
    if (db.rows < 1 || criteria.rows < 2) {
        *error = Err::Value;
        return false;
    }
    // The field: a column label, or its 1-based position; omitted (DCOUNT,
    // DCOUNTA) means every record counts once.
    qint32 field = -1;
    const Value fieldValue = scalarOrValue(args.at(1));
    if (isNumber(fieldValue)) {
        field = qint32(std::get<double>(fieldValue)) - 1;
        if (field < 0 || field >= db.cols) {
            *error = Err::Value;
            return false;
        }
    } else if (!isBlank(fieldValue)) {
        field = columnNamed(db, toText(fieldValue));
        if (field < 0) {
            *error = Err::Value;
            return false;
        }
    }
    QVector<qint32> criteriaColumns;
    for (qint32 c = 0; c < criteria.cols; ++c) {
        criteriaColumns.append(columnNamed(db, toText(cellOf(criteria, 0, c))));
    }
    for (qint32 r = 1; r < db.rows; ++r) {
        bool any = false;
        for (qint32 k = 1; k < criteria.rows && !any; ++k) {
            bool all = true;
            for (qint32 c = 0; c < criteria.cols && all; ++c) {
                const Value criterion = cellOf(criteria, k, c);
                if (isBlank(criterion)) {
                    continue;
                }
                all = criteriaColumns.at(c) >= 0 && matches(cellOf(db, r, criteriaColumns.at(c)), criterion);
            }
            any = all;
        }
        if (any) {
            values->append(field >= 0 ? cellOf(db, r, field) : Value(1.0));
        }
    }
    return true;
}

// A database function: the selected values handed to the aggregate.
FunctionSpec databaseFunction(const char *aggregate, bool fieldOptional)
{
    const QString name = QString::fromLatin1(aggregate);
    return FunctionSpec{fieldOptional ? 2 : 3, 3, false, [name](EvalContext &ctx, const QVector<Arg> &given) -> Arg {
        QVector<Arg> args = given;
        if (args.size() == 2) {
            args = {given.at(0), Value(), given.at(1)}; // DCOUNT(database, criteria)
        }
        QVector<Value> values;
        Value error;
        if (!selected(ctx, args, &values, &error)) {
            return error;
        }
        if (name == QLatin1String("DGET")) {
            if (values.isEmpty()) {
                return Value(Err::Value);
            }
            return values.size() > 1 ? Value(Err::Num) : values.first();
        }
        ArrayValue picked{1, qint32(values.size()), values};
        if (values.isEmpty()) {
            picked = {1, 1, {Value()}};
        }
        return functionRegistry().value(name).fn(ctx, {Arg(picked)});
    }};
}

} // namespace

void registerDatabaseFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("DSUM"), databaseFunction("SUM", false));
    r.insert(QStringLiteral("DCOUNT"), databaseFunction("COUNT", true));
    r.insert(QStringLiteral("DCOUNTA"), databaseFunction("COUNTA", true));
    r.insert(QStringLiteral("DAVERAGE"), databaseFunction("AVERAGE", false));
    r.insert(QStringLiteral("DMAX"), databaseFunction("MAX", false));
    r.insert(QStringLiteral("DMIN"), databaseFunction("MIN", false));
    r.insert(QStringLiteral("DPRODUCT"), databaseFunction("PRODUCT", false));
    r.insert(QStringLiteral("DSTDEV"), databaseFunction("STDEV", false));
    r.insert(QStringLiteral("DSTDEVP"), databaseFunction("STDEVP", false));
    r.insert(QStringLiteral("DVAR"), databaseFunction("VAR", false));
    r.insert(QStringLiteral("DVARP"), databaseFunction("VARP", false));
    r.insert(QStringLiteral("DGET"), databaseFunction("DGET", false));
}

} // namespace QindaOffice::Calc
