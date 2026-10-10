// SPDX-License-Identifier: GPL-3.0-or-later
// Access functions → SQLite SQL for the expression compiler
// (base_expression.cpp): Year/Month/Day, text functions, Nz, IIf, Round,
// Int, Date()/Now(), DateSerial and the aggregates; M86: DatePart and the
// date patterns of Format (what the Crosstab Query Wizard groups by).
#include "base_expression_p.h"

#include <QHash>

namespace QindaOffice::Base::Expr::detail {

namespace {

// A quoted SQL text literal's content ("'q'" → q), or nullopt.
std::optional<QString> literalText(const QString &sql)
{
    if (sql.size() >= 2 && sql.startsWith(QLatin1Char('\'')) && sql.endsWith(QLatin1Char('\''))) {
        return sql.mid(1, sql.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    return std::nullopt;
}

QString part(const char *code, const QString &date)
{
    return QStringLiteral("CAST(strftime('%1', %2) AS INTEGER)").arg(QLatin1String(code), date);
}

QString monthName(const QString &date, bool full)
{
    static const char *const names[] = {"January", "February", "March",     "April",   "May",      "June",
                                        "July",    "August",   "September", "October", "November", "December"};
    QString sql = QStringLiteral("(CASE %1").arg(part("%m", date));
    for (int m = 0; m < 12; ++m) {
        const QString name = QString::fromLatin1(names[m]);
        sql += QStringLiteral(" WHEN %1 THEN '%2'").arg(m + 1).arg(full ? name : name.left(3));
    }
    return sql + QStringLiteral(" END)");
}

QString dayName(const QString &date, bool full)
{
    static const char *const names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    QString sql = QStringLiteral("(CASE %1").arg(part("%w", date));
    for (int d = 0; d < 7; ++d) {
        const QString name = QString::fromLatin1(names[d]);
        sql += QStringLiteral(" WHEN %1 THEN '%2'").arg(d).arg(full ? name : name.left(3));
    }
    return sql + QStringLiteral(" END)");
}

// DatePart's intervals (Access's, the week starting on Sunday).
std::optional<QString> datePart(const QString &interval, const QString &date)
{
    const QString i = interval.toLower();
    if (i == QLatin1String("yyyy")) {
        return part("%Y", date);
    }
    if (i == QLatin1String("q")) {
        return QStringLiteral("((%1 + 2) / 3)").arg(part("%m", date));
    }
    if (i == QLatin1String("m")) {
        return part("%m", date);
    }
    if (i == QLatin1String("d")) {
        return part("%d", date);
    }
    if (i == QLatin1String("y")) {
        return part("%j", date);
    }
    if (i == QLatin1String("w")) {
        return QStringLiteral("(%1 + 1)").arg(part("%w", date));
    }
    if (i == QLatin1String("ww")) {
        return QStringLiteral("((%1 + %2 - 1) / 7 + 1)").arg(part("%j", date), part("%w", QStringLiteral("date(%1, 'start of year')").arg(date)));
    }
    if (i == QLatin1String("h")) {
        return part("%H", date);
    }
    if (i == QLatin1String("n")) {
        return part("%M", date);
    }
    if (i == QLatin1String("s")) {
        return part("%S", date);
    }
    return std::nullopt;
}

// Format's date patterns: named (Short Date, Medium Date, Long Date) and
// built of yyyy yy q mmmm mmm mm m dddd ddd dd d hh h nn ss with literal
// text between ("mmm yyyy", "yyyy-mm"); "" for any other pattern.
QString formatDate(const QString &pattern, const QString &date)
{
    const QString named = pattern.toLower();
    if (named == QLatin1String("short date")) {
        return QStringLiteral("date(%1)").arg(date);
    }
    if (named == QLatin1String("medium date")) {
        return formatDate(QStringLiteral("dd-mmm-yy"), date);
    }
    if (named == QLatin1String("long date")) {
        return formatDate(QStringLiteral("dddd, mmmm d, yyyy"), date);
    }
    static const char *const tokens[] = {"yyyy", "yy", "mmmm", "mmm", "mm", "m", "dddd", "ddd", "dd", "d",
                                         "hh",   "h",  "nn",   "ss",  "q"};
    QStringList parts;
    QString literal;
    const auto flush = [&] {
        if (!literal.isEmpty()) {
            parts.append(QStringLiteral("'%1'").arg(QString(literal).replace(QLatin1Char('\''), QStringLiteral("''"))));
            literal.clear();
        }
    };
    for (int i = 0; i < pattern.size();) {
        const QString rest = pattern.mid(i).toLower();
        const auto token = std::find_if(std::begin(tokens), std::end(tokens),
                                        [&](const char *t) { return rest.startsWith(QLatin1String(t)); });
        if (token == std::end(tokens)) {
            if (pattern.at(i).isLetterOrNumber() && pattern.at(i) != QLatin1Char('T')) {
                return {}; // a number pattern or a code QindaBase does not know
            }
            literal += pattern.at(i++);
            continue;
        }
        flush();
        const QString t = QString::fromLatin1(*token);
        i += int(t.size());
        if (t == QLatin1String("yyyy")) {
            parts.append(QStringLiteral("strftime('%Y', %1)").arg(date));
        } else if (t == QLatin1String("yy")) {
            parts.append(QStringLiteral("substr(strftime('%Y', %1), 3)").arg(date));
        } else if (t == QLatin1String("q")) {
            parts.append(QStringLiteral("CAST((%1 + 2) / 3 AS TEXT)").arg(part("%m", date)));
        } else if (t == QLatin1String("mmmm") || t == QLatin1String("mmm")) {
            parts.append(monthName(date, t.size() == 4));
        } else if (t == QLatin1String("dddd") || t == QLatin1String("ddd")) {
            parts.append(dayName(date, t.size() == 4));
        } else {
            static const QHash<QString, QString> codes{{QStringLiteral("mm"), QStringLiteral("%m")}, {QStringLiteral("dd"), QStringLiteral("%d")},
                                                       {QStringLiteral("hh"), QStringLiteral("%H")}, {QStringLiteral("nn"), QStringLiteral("%M")},
                                                       {QStringLiteral("ss"), QStringLiteral("%S")}, {QStringLiteral("m"), QStringLiteral("%m")},
                                                       {QStringLiteral("d"), QStringLiteral("%d")}, {QStringLiteral("h"), QStringLiteral("%H")}};
            const QString code = QStringLiteral("strftime('%1', %2)").arg(codes.value(t), date);
            parts.append(t.size() == 2 ? code : QStringLiteral("CAST(CAST(%1 AS INTEGER) AS TEXT)").arg(code));
        }
    }
    flush();
    return parts.isEmpty() ? QString() : QLatin1Char('(') + parts.join(QStringLiteral(" || ")) + QLatin1Char(')');
}

} // namespace

CallResult callSql(const QString &name, const QList<CallArg> &args)
{
    QString error;
    const auto arg = [&](int i) { return i < args.size() ? args.at(i).sql : QStringLiteral("NULL"); };
    const auto need = [&](int count) {
        if (args.size() < count && error.isEmpty()) {
            error = QStringLiteral("%1() needs %2 argument(s).").arg(name).arg(count);
        }
    };
    static const QHash<QString, QString> parts{{QStringLiteral("year"), QStringLiteral("%Y")},
                                                {QStringLiteral("month"), QStringLiteral("%m")},
                                                {QStringLiteral("day"), QStringLiteral("%d")}};
    if (parts.contains(name)) {
        need(1);
        return CallResult{QStringLiteral("CAST(strftime('%1', %2) AS INTEGER)").arg(parts.value(name), arg(0)), FieldType::Integer,
                args.value(0).aggregate};
    }
    static const QHash<QString, QString> aggregates{{QStringLiteral("sum"), QStringLiteral("sum")},
                                                     {QStringLiteral("avg"), QStringLiteral("avg")},
                                                     {QStringLiteral("count"), QStringLiteral("count")},
                                                     {QStringLiteral("min"), QStringLiteral("min")},
                                                     {QStringLiteral("max"), QStringLiteral("max")}};
    if (aggregates.contains(name)) {
        need(1);
        const std::optional<FieldType> type = name == QLatin1String("count") ? std::optional(FieldType::Integer)
                                              : name == QLatin1String("avg") && args.value(0).type != FieldType::Currency
                                                  ? std::optional(FieldType::Number)
                                                  : args.value(0).type;
        return CallResult{QStringLiteral("%1(%2)").arg(aggregates.value(name), arg(0)), type, true};
    }
    static const QHash<QString, QString> text{{QStringLiteral("upper"), QStringLiteral("upper")},
                                               {QStringLiteral("ucase"), QStringLiteral("upper")},
                                               {QStringLiteral("lower"), QStringLiteral("lower")},
                                               {QStringLiteral("lcase"), QStringLiteral("lower")},
                                               {QStringLiteral("trim"), QStringLiteral("trim")},
                                               {QStringLiteral("ltrim"), QStringLiteral("ltrim")},
                                               {QStringLiteral("rtrim"), QStringLiteral("rtrim")}};
    if (text.contains(name)) {
        need(1);
        return CallResult{QStringLiteral("%1(%2)").arg(text.value(name), arg(0)), FieldType::Text, args.value(0).aggregate};
    }
    bool agg = false;
    for (const CallArg &v : args) {
        agg = agg || v.aggregate;
    }
    if (name == QLatin1String("len")) {
        need(1);
        return CallResult{QStringLiteral("length(%1)").arg(arg(0)), FieldType::Integer, agg};
    }
    if (name == QLatin1String("left")) {
        need(2);
        return CallResult{QStringLiteral("substr(%1, 1, %2)").arg(arg(0), arg(1)), FieldType::Text, agg};
    }
    if (name == QLatin1String("right")) {
        need(2);
        return CallResult{QStringLiteral("substr(%1, -(%2))").arg(arg(0), arg(1)), FieldType::Text, agg};
    }
    if (name == QLatin1String("mid")) {
        need(2);
        return CallResult{args.size() > 2 ? QStringLiteral("substr(%1, %2, %3)").arg(arg(0), arg(1), arg(2))
                                : QStringLiteral("substr(%1, %2)").arg(arg(0), arg(1)),
                FieldType::Text, agg};
    }
    if (name == QLatin1String("instr")) {
        need(2);
        return CallResult{QStringLiteral("instr(%1, %2)").arg(arg(0), arg(1)), FieldType::Integer, agg};
    }
    if (name == QLatin1String("replace")) {
        need(3);
        return CallResult{QStringLiteral("replace(%1, %2, %3)").arg(arg(0), arg(1), arg(2)), FieldType::Text, agg};
    }
    if (name == QLatin1String("round")) {
        need(1);
        return CallResult{QStringLiteral("round(%1, %2)").arg(arg(0), args.size() > 1 ? arg(1) : QStringLiteral("0")),
                args.value(0).type, agg};
    }
    if (name == QLatin1String("abs")) {
        need(1);
        return CallResult{QStringLiteral("abs(%1)").arg(arg(0)), args.value(0).type, agg};
    }
    if (name == QLatin1String("int")) {
        need(1);
        return CallResult{QStringLiteral("CAST(CASE WHEN %1 < 0 AND %1 <> CAST(%1 AS INTEGER) THEN CAST(%1 AS INTEGER) - 1 "
                               "ELSE CAST(%1 AS INTEGER) END AS INTEGER)")
                    .arg(arg(0)),
                FieldType::Integer, agg};
    }
    if (name == QLatin1String("nz")) {
        need(1);
        const QString fallback = args.size() > 1 ? arg(1)
                                 : args.value(0).type && isNumeric(*args.value(0).type) ? QStringLiteral("0")
                                                                                         : QStringLiteral("''");
        return CallResult{QStringLiteral("coalesce(%1, %2)").arg(arg(0), fallback), args.value(0).type, agg};
    }
    if (name == QLatin1String("iif")) {
        need(3);
        return CallResult{QStringLiteral("(CASE WHEN %1 THEN %2 ELSE %3 END)").arg(arg(0), arg(1), arg(2)), args.value(1).type, agg};
    }
    if (name == QLatin1String("date")) {
        return CallResult{QStringLiteral("date('now','localtime')"), FieldType::Date};
    }
    if (name == QLatin1String("now")) {
        return CallResult{QStringLiteral("datetime('now','localtime')"), FieldType::DateTime};
    }
    if (name == QLatin1String("datepart")) {
        need(2);
        const std::optional<QString> interval = literalText(arg(0));
        const std::optional<QString> sql = interval ? datePart(*interval, arg(1)) : std::nullopt;
        if (!sql) {
            return CallResult{{}, {}, false, QStringLiteral("DatePart() takes an interval in quotes: \"yyyy\", \"q\", \"m\", \"d\", \"ww\", \"w\", \"y\", \"h\", \"n\" or \"s\".")};
        }
        return CallResult{*sql, FieldType::Integer, agg};
    }
    if (name == QLatin1String("format")) {
        need(1);
        if (args.size() == 1) {
            return CallResult{QStringLiteral("CAST(%1 AS TEXT)").arg(arg(0)), FieldType::Text, agg};
        }
        const std::optional<QString> pattern = literalText(arg(1));
        const QString sql = pattern ? formatDate(*pattern, arg(0)) : QString();
        if (sql.isEmpty()) {
            return CallResult{{}, {}, false, QStringLiteral("Format() takes a date pattern in quotes here: \"yyyy\", \"q\", \"mmm\", \"mmm yyyy\", \"Short Date\"…")};
        }
        return CallResult{sql, FieldType::Text, agg};
    }
    if (name == QLatin1String("dateserial")) {
        need(3);
        return CallResult{QStringLiteral("printf('%04d-%02d-%02d', %1, %2, %3)").arg(arg(0), arg(1), arg(2)), FieldType::Date, agg};
    }
    if (error.isEmpty()) {
        error = QStringLiteral("Unknown function %1().").arg(name);
    }
    return CallResult{{}, {}, false, error};
}

} // namespace QindaOffice::Base::Expr::detail
