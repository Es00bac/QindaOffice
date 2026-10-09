// SPDX-License-Identifier: GPL-3.0-or-later
// mdb-queries' SQL → a QindaBase query design (base_access_query.h).
#include "base_access_query.h"

#include <QRegularExpression>

namespace QindaOffice::Base {

namespace {

// The text with every literal ("…", '…', […], #…#) masked by '_', so
// keyword, comma and parenthesis searches never look inside one; `depth`
// is the parenthesis depth before each character.
struct Masked {
    QString text;
    QString masked;
    QList<int> depth;

    explicit Masked(const QString &source)
        : text(source)
        , masked(source)
    {
        int level = 0;
        depth.reserve(source.size());
        QChar close;
        for (qsizetype i = 0; i < source.size(); ++i) {
            const QChar ch = source.at(i);
            depth.append(level);
            if (!close.isNull()) {
                masked[i] = QLatin1Char('_');
                if (ch == close) {
                    const bool doubled = (close == QLatin1Char('"') || close == QLatin1Char('\''))
                                         && i + 1 < source.size() && source.at(i + 1) == close;
                    if (doubled) {
                        depth.append(level);
                        masked[++i] = QLatin1Char('_');
                    } else {
                        close = QChar();
                    }
                }
                continue;
            }
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\'') || ch == QLatin1Char('#')) {
                close = ch;
                masked[i] = QLatin1Char('_');
            } else if (ch == QLatin1Char('[')) {
                close = QLatin1Char(']');
                masked[i] = QLatin1Char('_');
            } else if (ch == QLatin1Char('(')) {
                ++level;
            } else if (ch == QLatin1Char(')')) {
                --level;
                depth[i] = level;
            }
        }
    }

    // The first top-level match of `re` at or after `from`, or -1.
    [[nodiscard]] qsizetype find(const QRegularExpression &re, qsizetype from, qsizetype *length = nullptr) const
    {
        QRegularExpressionMatchIterator it = re.globalMatch(masked, from);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (depth.value(m.capturedStart()) == 0) {
                if (length != nullptr) {
                    *length = m.capturedLength();
                }
                return m.capturedStart();
            }
        }
        return -1;
    }

    // Splits at top-level occurrences of `re`.
    [[nodiscard]] QStringList split(const QRegularExpression &re) const
    {
        QStringList parts;
        qsizetype start = 0;
        qsizetype length = 0;
        for (qsizetype at = find(re, 0, &length); at >= 0; at = find(re, start, &length)) {
            parts.append(text.mid(start, at - start).trimmed());
            start = at + length;
        }
        parts.append(text.mid(start).trimmed());
        return parts;
    }
};

QRegularExpression word(const char *keyword)
{
    return QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(keyword)),
                              QRegularExpression::CaseInsensitiveOption);
}

// "((x))" → "x" while the outer parentheses enclose everything.
QString stripOuter(QString text)
{
    for (text = text.trimmed(); text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'));) {
        const Masked m(text);
        bool encloses = true;
        for (qsizetype i = 1; i + 1 < text.size(); ++i) {
            if (m.depth.at(i) == 0) { // the first parenthesis closed before the end
                encloses = false;
                break;
            }
        }
        if (!encloses) {
            break;
        }
        text = text.mid(1, text.size() - 2).trimmed();
    }
    return text;
}

QString unbracket(const QString &name)
{
    return name.startsWith(QLatin1Char('[')) && name.endsWith(QLatin1Char(']')) ? name.mid(1, name.size() - 2) : name;
}

// "[Members].[Name]", "Members.Name", "Name" (captured: table, field).
const QString kRef = QStringLiteral("(?:(\\[[^\\]]+\\]|[A-Za-z_][A-Za-z0-9_]*)\\.)?(\\[[^\\]]+\\]|[A-Za-z_][A-Za-z0-9_]*)");

struct FieldRef {
    QString table; // Access spelling, "" when unqualified
    QString field;
};

std::optional<FieldRef> plainRef(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^") + kRef + QStringLiteral("$"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    return FieldRef{unbracket(m.captured(1)), unbracket(m.captured(2))};
}

// A WHERE conjunct "(Members.City)=\"Berlin\"" → (field, criteria).
std::optional<QPair<FieldRef, QString>> criteriaOf(const QString &conjunct)
{
    static const QRegularExpression re(QStringLiteral("^\\(?\\s*") + kRef
                                           + QStringLiteral("\\s*\\)?\\s*((?:<>|<=|>=|=|<|>)|(?:Not\\s+)?Like\\b|"
                                                            "Between\\b|(?:Not\\s+)?In\\b|Is\\b)(.*)$"),
                                       QRegularExpression::CaseInsensitiveOption
                                           | QRegularExpression::DotMatchesEverythingOption);
    const QString text = stripOuter(conjunct);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch() || Masked(text).find(word("or"), 0) >= 0) {
        return std::nullopt;
    }
    const QString op = m.captured(3);
    const QString rest = m.captured(4).trimmed();
    const bool symbol = !op.isEmpty() && !op.at(0).isLetter();
    return QPair<FieldRef, QString>{FieldRef{unbracket(m.captured(1)), unbracket(m.captured(2))},
                                    symbol ? op + rest : op + QLatin1Char(' ') + rest};
}

// Top-level "And"s, keeping the one inside "Between x And y".
QStringList conjuncts(const QString &where)
{
    const Masked m(where);
    static const QRegularExpression keyword(QStringLiteral("\\b(?:AND|BETWEEN)\\b"), QRegularExpression::CaseInsensitiveOption);
    QStringList parts;
    qsizetype start = 0;
    bool between = false;
    QRegularExpressionMatchIterator it = keyword.globalMatch(m.masked);
    while (it.hasNext()) {
        const QRegularExpressionMatch k = it.next();
        if (m.depth.value(k.capturedStart()) != 0) {
            continue;
        }
        if (k.captured().compare(QLatin1String("between"), Qt::CaseInsensitive) == 0) {
            between = true;
        } else if (between) {
            between = false;
        } else {
            parts.append(where.mid(start, k.capturedStart() - start).trimmed());
            start = k.capturedEnd();
        }
    }
    parts.append(where.mid(start).trimmed());
    return parts;
}

struct Builder {
    QueryDesign design;
    QString accessTable;
    QString ref;

    [[nodiscard]] bool sameTable(const QString &table) const
    {
        return table.isEmpty() || table.compare(accessTable, Qt::CaseInsensitive) == 0;
    }
    QueryColumn *findField(const FieldRef &r)
    {
        for (QueryColumn &c : design.columns) {
            if (c.expression.isEmpty() && c.field.compare(r.field, Qt::CaseInsensitive) == 0) {
                return &c;
            }
        }
        return nullptr;
    }
    QueryColumn column(const QString &text)
    {
        QueryColumn c;
        const QString t = text.trimmed();
        if (t == QLatin1String("*") || t.endsWith(QLatin1String(".*"))) {
            c.table = ref;
            c.field = QStringLiteral("*");
        } else if (const std::optional<FieldRef> r = plainRef(t); r && sameTable(r->table)) {
            c.table = ref;
            c.field = r->field;
        } else {
            c.expression = t; // unnamed, as mdb-queries drops aliases: the compiler calls it ExprN
        }
        return c;
    }
    void addCriteria(const FieldRef &r, const QString &criteria)
    {
        QueryColumn *existing = findField(r);
        if (existing != nullptr && existing->criteria.isEmpty()) {
            existing->criteria = criteria;
            return;
        }
        QueryColumn hidden;
        hidden.table = ref;
        hidden.field = r.field;
        hidden.show = false;
        hidden.criteria = criteria;
        design.columns.append(hidden);
    }
};

} // namespace

QString accessQueryKind(int flags)
{
    switch (flags & 0xF0) {
    case 0: return QStringLiteral("select");
    case 16: return QStringLiteral("crosstab");
    case 32: return QStringLiteral("delete");
    case 48: return QStringLiteral("update");
    case 64: return QStringLiteral("append");
    case 80: return QStringLiteral("make-table");
    case 96: return QStringLiteral("data definition");
    case 112: return QStringLiteral("pass-through");
    case 128: return QStringLiteral("union");
    default: return QStringLiteral("unknown");
    }
}

AccessQueryConversion convertAccessQuery(const QString &sql, const QHash<QString, QString> &tables)
{
    AccessQueryConversion out;
    const QString text = sql.simplified();
    if (!text.startsWith(QLatin1String("SELECT"), Qt::CaseInsensitive)) {
        out.reason = QStringLiteral("mdb-queries gave no select statement for it.");
        return out;
    }
    Builder b;
    QString rest = text.mid(6).trimmed();
    static const QRegularExpression top(QStringLiteral("^TOP\\s+(\\d+)(\\s+PERCENT)?\\s+"), QRegularExpression::CaseInsensitiveOption);
    if (const QRegularExpressionMatch m = top.match(rest); m.hasMatch()) {
        if (!m.captured(2).isEmpty()) {
            out.reason = QStringLiteral("TOP … PERCENT has no QindaBase equivalent.");
            return out;
        }
        b.design.top = m.captured(1).toInt();
        rest = rest.mid(m.capturedLength());
    } else if (rest.startsWith(QLatin1String("DISTINCTROW "), Qt::CaseInsensitive)) {
        rest = rest.mid(12); // over one table, DISTINCTROW changes nothing
    } else if (rest.startsWith(QLatin1String("DISTINCT "), Qt::CaseInsensitive)) {
        b.design.distinct = true;
        rest = rest.mid(9);
    }
    const Masked masked(rest);
    if (masked.masked.contains(QLatin1Char('!'))) {
        out.reason = QStringLiteral("it reads values from a form (Forms!…), which QindaBase queries cannot.");
        return out;
    }
    static const QRegularExpression aggregate(
        QStringLiteral("\\b(?:Sum|Avg|Count|Min|Max|First|Last|StDev|StDevP|Var|VarP)\\s*\\("),
        QRegularExpression::CaseInsensitiveOption);
    if (aggregate.match(masked.masked).hasMatch()) {
        out.reason = QStringLiteral("it totals records, and mdb-queries does not give its Group By.");
        return out;
    }
    qsizetype length = 0;
    const qsizetype from = masked.find(word("FROM"), 0, &length);
    const QString columns = from < 0 ? QString() : rest.left(from).trimmed();
    if (columns.isEmpty()) {
        out.reason = QStringLiteral("mdb-queries could not rebuild it (a union, crosstab or SQL-only query).");
        return out;
    }
    QString tail = rest.mid(from + length).trimmed();
    QString orderBy;
    const Masked tailMask(tail);
    if (const qsizetype order = tailMask.find(word("ORDER\\s+BY"), 0, &length); order >= 0) {
        orderBy = tail.mid(order + length).trimmed();
        tail = tail.left(order).trimmed();
    }
    QString where;
    if (const qsizetype w = Masked(tail).find(word("WHERE"), 0, &length); w >= 0) {
        where = tail.mid(w + length).trimmed();
        tail = tail.left(w).trimmed();
    }
    const QStringList from_ = Masked(tail).split(QRegularExpression(QStringLiteral(",")));
    if (from_.first().isEmpty()) {
        out.reason = QStringLiteral("mdb-queries could not rebuild it (a union, crosstab or SQL-only query).");
        return out;
    }
    if (from_.size() != 1) {
        out.reason = QStringLiteral("it joins %1 tables, and mdb-queries does not give the join.").arg(from_.size());
        return out;
    }
    b.accessTable = unbracket(from_.first());
    const QString imported = tables.value(b.accessTable.toLower());
    if (imported.isEmpty()) {
        out.reason = QStringLiteral("it reads %1, which is not one of the imported tables.").arg(b.accessTable);
        return out;
    }
    QueryTable table{imported, imported.compare(b.accessTable, Qt::CaseInsensitive) == 0 ? QString() : b.accessTable};
    b.ref = table.ref();
    b.design.tables.append(table);
    for (const QString &c : Masked(columns).split(QRegularExpression(QStringLiteral(",")))) {
        if (!c.isEmpty()) {
            b.design.columns.append(b.column(c));
        }
    }
    if (!where.isEmpty()) {
        QList<QPair<FieldRef, QString>> criteria;
        for (const QString &part : conjuncts(stripOuter(where))) {
            const std::optional<QPair<FieldRef, QString>> c = criteriaOf(part);
            if (!c || !b.sameTable(c->first.table)) {
                criteria.clear();
                break;
            }
            criteria.append(*c);
        }
        if (!criteria.isEmpty()) {
            for (const auto &[field, text] : std::as_const(criteria)) {
                b.addCriteria(field, text);
            }
        } else {
            // Not a list of per-field conditions: the whole test as one hidden column.
            QueryColumn test;
            test.expression = stripOuter(where);
            test.show = false;
            test.criteria = QStringLiteral("<> False");
            b.design.columns.append(test);
        }
    }
    if (!orderBy.isEmpty()) {
        bool descending = false;
        static const QRegularExpression desc(QStringLiteral("\\s+DESC(?:ENDING)?$"), QRegularExpression::CaseInsensitiveOption);
        if (const QRegularExpressionMatch m = desc.match(orderBy); m.hasMatch()) {
            descending = true;
            orderBy.chop(m.capturedLength());
        }
        QueryColumn *target = nullptr;
        if (const std::optional<FieldRef> r = plainRef(orderBy); r && b.sameTable(r->table)) {
            target = b.findField(*r);
            if (target == nullptr) {
                QueryColumn hidden = b.column(orderBy);
                hidden.show = false;
                b.design.columns.append(hidden);
                target = &b.design.columns.last();
            }
        } else {
            QueryColumn hidden = b.column(orderBy);
            hidden.show = false;
            b.design.columns.append(hidden);
            target = &b.design.columns.last();
        }
        target->sort = descending ? -1 : 1;
    }
    out.design = b.design;
    return out;
}

} // namespace QindaOffice::Base
