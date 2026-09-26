// SPDX-License-Identifier: GPL-3.0-or-later
// Access functions → SQLite SQL for the expression compiler
// (base_expression.cpp): Year/Month/Day, text functions, Nz, IIf, Round,
// Int, Date()/Now(), DateSerial and the aggregates.
#include "base_expression_p.h"

#include <QHash>

namespace QindaOffice::Base::Expr::detail {

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
