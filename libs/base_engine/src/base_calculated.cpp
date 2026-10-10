// SPDX-License-Identifier: GPL-3.0-or-later
// Calculated fields: Access expressions compiled into generated columns,
// and the generated-column SQL of foreign files. See base_calculated.h.
#include "base_calculated.h"

#include "base_expression.h"

namespace QindaOffice::Base {

namespace {

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

QString scale(int decimals)
{
    QString text = QStringLiteral("1");
    for (int i = 0; i < decimals; ++i) {
        text += QLatin1Char('0');
    }
    return text;
}

} // namespace

QString convertValueSql(const QString &sql, std::optional<FieldType> from, FieldType to, const CurrencyFormat &currency)
{
    const bool money = from == FieldType::Currency;
    const QString unit = scale(currency.decimals);
    switch (to) {
    case FieldType::Currency:
        return money ? QStringLiteral("CAST(round(%1) AS INTEGER)").arg(sql)
                     : QStringLiteral("CAST(round((%1) * %2) AS INTEGER)").arg(sql, unit);
    case FieldType::Integer:
    case FieldType::LargeNumber:
        if (money) {
            return QStringLiteral("CAST(round((%1) * 1.0 / %2) AS INTEGER)").arg(sql, unit);
        }
        return from && isWholeNumber(*from) ? sql : QStringLiteral("CAST(round(%1) AS INTEGER)").arg(sql);
    case FieldType::Number:
        return money ? QStringLiteral("((%1) * 1.0 / %2)").arg(sql, unit) : sql;
    case FieldType::YesNo:
        return QStringLiteral("(CASE WHEN (%1) THEN 1 ELSE 0 END)").arg(sql);
    case FieldType::Date:
        return QStringLiteral("date(%1)").arg(sql);
    case FieldType::DateTime:
        return QStringLiteral("datetime(%1)").arg(sql);
    default:
        if (money) {
            return QStringLiteral("printf('%.%1f', (%2) * 1.0 / %3)").arg(currency.decimals).arg(sql, unit);
        }
        return from == FieldType::Text || from == FieldType::LongText || from == FieldType::Date
                       || from == FieldType::DateTime
                   ? sql
                   : QStringLiteral("CAST(%1 AS TEXT)").arg(sql);
    }
}

namespace {

FieldType naturalType(std::optional<FieldType> type)
{
    if (!type) {
        return FieldType::Text;
    }
    switch (*type) {
    case FieldType::AutoNumber:
        return FieldType::Integer;
    case FieldType::File:
    case FieldType::Other:
        return FieldType::Text;
    default:
        return *type;
    }
}

} // namespace

CompiledCalculation compileCalculatedField(const TableDef &table, const FieldDef &field, const CurrencyFormat &currency)
{
    CompiledCalculation out;
    out.type = field.type;
    const QString text = field.expression.trimmed();
    if (text.isEmpty()) {
        if (!field.generatedSql.trimmed().isEmpty()) {
            out.sql = field.generatedSql.trimmed(); // a foreign file's own, kept as it is
            if (out.type == FieldType::Other) {
                out.type = FieldType::Number;
            }
            return out;
        }
        out.error = QStringLiteral("Give the calculated field %1 an expression, such as [quantity] * [price].")
                        .arg(field.name);
        return out;
    }
    QString self;
    Expr::Context ctx;
    ctx.currency = currency;
    ctx.parameters = nullptr; // nothing is asked for: an unknown [name] is an error
    ctx.resolve = [&table, &field, &self](const QString &owner, const QString &name) -> std::optional<Expr::FieldRef> {
        if (!owner.isEmpty() && owner.compare(table.name, Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
        const FieldDef *f = table.field(name);
        if (f == nullptr) {
            return std::nullopt;
        }
        if (f->name.compare(field.name, Qt::CaseInsensitive) == 0) {
            self = f->name;
        }
        return Expr::FieldRef{q(f->name), f->type};
    };
    const Expr::Compiled c = Expr::expression(text, ctx);
    if (!c.ok()) {
        out.error = QStringLiteral("The expression of %1: %2").arg(field.name, c.error);
        return out;
    }
    if (!self.isEmpty()) {
        out.error = QStringLiteral("The expression of %1 uses %1 itself.").arg(field.name);
        return out;
    }
    if (c.aggregate) {
        out.error = QStringLiteral("A calculated field works on its own record; %1 cannot total records (use a query).")
                        .arg(field.name);
        return out;
    }
    // AGENT-GUARD: SQLite refuses non-deterministic functions in generated
    // columns; say why before the plan fails on it.
    if (c.sql.contains(QLatin1String("'now'"))) {
        out.error = QStringLiteral("%1 cannot use Date() or Now(): a calculated field gives the same value every time it "
                                   "is read. Use a query for today's date.")
                        .arg(field.name);
        return out;
    }
    if (out.type == FieldType::Other) {
        out.type = naturalType(c.type);
    }
    if (out.type == FieldType::File || out.type == FieldType::AutoNumber) {
        out.error = QStringLiteral("A calculated field's result is text, a number, money, Yes/No or a date.");
        return out;
    }
    out.sql = convertValueSql(c.sql, c.type, out.type, currency);
    return out;
}

QString generatedClause(const QString &sql)
{
    return QStringLiteral(" GENERATED ALWAYS AS (%1) VIRTUAL").arg(sql);
}

QString resolveCalculatedTypes(TableDef *table, const CurrencyFormat &currency)
{
    for (FieldDef &f : table->fields) {
        if (!f.calculated) {
            continue;
        }
        const CompiledCalculation c = compileCalculatedField(*table, f, currency);
        if (!c.ok()) {
            return c.error;
        }
        f.type = c.type;
    }
    return {};
}

namespace {

// The top-level parts of the parenthesized list opening at `open`, quotes
// and nesting kept; `close` receives its closing parenthesis (-1: none).
QStringList topLevelParts(const QString &sql, int open, int *close)
{
    QStringList parts;
    int depth = 0;
    int start = open + 1;
    QChar quote;
    for (int i = open; i < sql.size(); ++i) {
        const QChar c = sql.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
            quote = c;
        } else if (c == QLatin1Char('[')) {
            quote = QLatin1Char(']');
        } else if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            if (--depth == 0) {
                parts.append(sql.mid(start, i - start));
                *close = i;
                return parts;
            }
        } else if (c == QLatin1Char(',') && depth == 1) {
            parts.append(sql.mid(start, i - start));
            start = i + 1;
        }
    }
    *close = -1;
    return parts;
}

// The first name of a column definition, unquoted.
QString leadingName(const QString &part)
{
    const QString t = part.trimmed();
    if (t.isEmpty()) {
        return {};
    }
    const QChar first = t.at(0);
    if (first == QLatin1Char('"') || first == QLatin1Char('`') || first == QLatin1Char('[')) {
        const QChar end = first == QLatin1Char('[') ? QLatin1Char(']') : first;
        const int stop = int(t.indexOf(end, 1));
        return stop < 0 ? QString() : t.mid(1, stop - 1);
    }
    int i = 0;
    while (i < t.size() && !t.at(i).isSpace()) {
        ++i;
    }
    return t.left(i);
}

} // namespace

QHash<QString, QString> generatedColumnSql(const QString &createTableSql)
{
    QHash<QString, QString> out;
    // The column list is the first parenthesis after the table's name.
    int open = -1;
    QChar quote;
    for (int i = 0; i < createTableSql.size() && open < 0; ++i) {
        const QChar c = createTableSql.at(i);
        if (!quote.isNull()) {
            quote = c == quote ? QChar() : quote;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('[')) {
            quote = QLatin1Char(']');
        } else if (c == QLatin1Char('(')) {
            open = i;
        }
    }
    int close = -1;
    if (open < 0) {
        return out;
    }
    for (const QString &part : topLevelParts(createTableSql, open, &close)) {
        const QString name = leadingName(part);
        static const QStringList constraints{QStringLiteral("constraint"), QStringLiteral("primary"),
                                             QStringLiteral("unique"), QStringLiteral("check"),
                                             QStringLiteral("foreign")};
        if (name.isEmpty() || constraints.contains(name.toLower())) {
            continue;
        }
        // "… [GENERATED ALWAYS] AS ( expr ) [VIRTUAL|STORED]": the AS that
        // a parenthesis follows (a type such as DECIMAL(8,2) has none before).
        const QString upper = part.toUpper();
        for (int at = int(upper.indexOf(QLatin1String("AS"))); at >= 0; at = int(upper.indexOf(QLatin1String("AS"), at + 2))) {
            const bool word = (at == 0 || !upper.at(at - 1).isLetterOrNumber())
                              && (at + 2 >= upper.size() || !upper.at(at + 2).isLetterOrNumber());
            int p = at + 2;
            while (p < part.size() && part.at(p).isSpace()) {
                ++p;
            }
            if (!word || p >= part.size() || part.at(p) != QLatin1Char('(')) {
                continue;
            }
            int end = -1;
            topLevelParts(part, p, &end); // only the matching parenthesis matters here
            if (end > p) {
                out.insert(name.toLower(), part.mid(p + 1, end - p - 1).trimmed());
            }
            break;
        }
    }
    return out;
}

} // namespace QindaOffice::Base
