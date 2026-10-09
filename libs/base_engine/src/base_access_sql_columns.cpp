// SPDX-License-Identifier: GPL-3.0-or-later
// The SQL view's parser, part two: statements as grid columns — select
// items, GROUP BY, WHERE/HAVING split into per-column criteria rows, ORDER
// BY, a crosstab's TRANSFORM/PIVOT, UPDATE's SET, INSERT INTO's targets.
// Subjects are matched to columns by key (SqlParser::keyIn); what matches
// no column gets a hidden one, as Access's designer does.
#include "base_access_sql_parser.h"

namespace QindaOffice::Base::AccessSql {

namespace {

TotalKind totalOf(const QString &word)
{
    const QString w = word.toLower();
    return w == QLatin1String("sum")     ? TotalKind::Sum
           : w == QLatin1String("avg")   ? TotalKind::Average
           : w == QLatin1String("count") ? TotalKind::Count
           : w == QLatin1String("min")   ? TotalKind::Min
                                         : TotalKind::Max;
}

QString functionOf(TotalKind total)
{
    switch (total) {
    case TotalKind::Sum: return QStringLiteral("sum");
    case TotalKind::Average: return QStringLiteral("avg");
    case TotalKind::Count: return QStringLiteral("count");
    case TotalKind::Min: return QStringLiteral("min");
    case TotalKind::Max: return QStringLiteral("max");
    default: return {};
    }
}

QString prefixOf(TotalKind total)
{
    const QString f = functionOf(total);
    return f.isEmpty() ? QString() : accessAggregate(f) + QStringLiteral("Of");
}

BoolNode unwrap(BoolNode node)
{
    while (node.kind == BoolNode::Group) {
        node = node.children.first();
    }
    return node;
}

} // namespace

bool SqlParser::aggregateCallIn(const QList<Token> &t, int first, int last)
{
    return last - first >= 3 && t.at(first).kind == Token::Word && !accessAggregate(t.at(first).text).isEmpty()
           && t.at(first + 1).op("(") && closing(t, first + 1) == last - 1;
}

bool SqlParser::containsAggregate(const QList<Token> &t, int first, int last)
{
    for (int i = first; i + 1 < last; ++i) {
        if (t.at(i).kind == Token::Word && !accessAggregate(t.at(i).text).isEmpty() && t.at(i + 1).op("(")) {
            return true;
        }
    }
    return false;
}

std::optional<QPair<QString, QString>> SqlParser::fieldRefIn(const QList<Token> &t, int first, int last) const
{
    if (last - first == 1 && nameToken(t.at(first))) {
        for (const QueryTable &table : d.tables) {
            const TableDef def = m_defs.value(table.ref().toLower());
            if (const FieldDef *f = def.field(t.at(first).text)) {
                return QPair<QString, QString>{table.ref(), f->name};
            }
        }
        return std::nullopt;
    }
    if (last - first == 3 && nameToken(t.at(first)) && t.at(first + 1).op(".") && nameToken(t.at(first + 2))) {
        for (const QueryTable &table : d.tables) {
            if (table.ref().compare(t.at(first).text, Qt::CaseInsensitive) != 0) {
                continue;
            }
            const TableDef def = m_defs.value(table.ref().toLower());
            if (const FieldDef *f = def.field(t.at(first + 2).text)) {
                return QPair<QString, QString>{table.ref(), f->name};
            }
        }
    }
    return std::nullopt;
}

QString SqlParser::keyIn(const QList<Token> &t, int first, int last) const
{
    if (const auto ref = fieldRefIn(t, first, last)) {
        return QStringLiteral("f:%1.%2").arg(ref->first.toLower(), ref->second.toLower());
    }
    if (aggregateCallIn(t, first, last)) {
        return QStringLiteral("a:%1:%2").arg(t.at(first).text.toLower(), keyIn(t, first + 2, last - 1));
    }
    QStringList parts;
    for (int i = first; i < last; ++i) {
        const Token &x = t.at(i);
        switch (x.kind) {
        case Token::Word: parts.append(x.text.toLower()); break;
        case Token::Bracket: parts.append(QLatin1Char('[') + x.text.toLower() + QLatin1Char(']')); break;
        case Token::String: parts.append(QLatin1Char('"') + x.text + QLatin1Char('"')); break;
        case Token::Date: parts.append(QLatin1Char('#') + x.text + QLatin1Char('#')); break;
        default: parts.append(x.text); break;
        }
    }
    return QStringLiteral("e:") + parts.join(QLatin1Char(' '));
}

QString SqlParser::columnKey(const QueryColumn &c) const
{
    QString base;
    if (!c.expression.trimmed().isEmpty()) {
        QString error;
        const QList<Token> t = tokenize(c.expression, &error);
        base = error.isEmpty() ? keyIn(t, 0, int(t.size()) - 1) : QStringLiteral("e:") + c.expression;
    } else if (c.field == QLatin1String("*")) {
        return QStringLiteral("*");
    } else {
        base = QStringLiteral("f:%1.%2").arg(c.table.toLower(), c.field.toLower());
    }
    const QString fn = functionOf(c.total);
    return totals() && !fn.isEmpty() ? QStringLiteral("a:%1:%2").arg(fn, base) : base;
}

bool SqlParser::columnAggregates(const QueryColumn &c) const
{
    if (!totals()) {
        return false;
    }
    if (!functionOf(c.total).isEmpty()) {
        return true;
    }
    QString error;
    const QList<Token> t = tokenize(c.expression, &error);
    return !c.expression.trimmed().isEmpty() && containsAggregate(t, 0, int(t.size()) - 1);
}

bool SqlParser::toHaving(const QueryColumn &c) const
{
    return totals() && c.total != TotalKind::Where && (columnAggregates(c) || c.total == TotalKind::GroupBy)
           && !(d.kind == QueryKind::Crosstab && c.crosstab == QLatin1String("column"));
}

QueryColumn SqlParser::columnFor(Range r, bool totalsOn) const
{
    QueryColumn c;
    if (r.last - r.first >= 3 && at(r.last - 2).is("as") && nameToken(at(r.last - 1))) {
        c.alias = at(r.last - 1).text;
        r.last -= 2;
    }
    if (r.last - r.first == 1 && at(r.first).op("*")) {
        c.field = QStringLiteral("*");
        return c;
    }
    if (r.last - r.first == 3 && nameToken(at(r.first)) && at(r.first + 1).op(".") && at(r.first + 2).op("*")) {
        for (const QueryTable &t : d.tables) {
            if (t.ref().compare(at(r.first).text, Qt::CaseInsensitive) == 0) {
                c.table = t.ref();
            }
        }
        c.field = QStringLiteral("*");
        return c;
    }
    if (totalsOn && aggregateCallIn(m_t, r.first, r.last)) {
        c.total = totalOf(at(r.first).text);
        const Range inner{r.first + 2, r.last - 1};
        if (inner.last - inner.first == 1 && at(inner.first).op("*")) {
            c.expression = text(r); // Count(*)
            c.total = TotalKind::Expression;
        } else if (const auto ref = fieldRefIn(m_t, inner.first, inner.last)) {
            c.table = ref->first;
            c.field = ref->second;
            if (c.alias.compare(prefixOf(c.total) + c.field, Qt::CaseInsensitive) == 0) {
                c.alias.clear();
            }
        } else {
            c.expression = text(inner);
        }
        return c;
    }
    if (const auto ref = fieldRefIn(m_t, r.first, r.last)) {
        c.table = ref->first;
        c.field = ref->second;
        if (c.alias.compare(c.field, Qt::CaseInsensitive) == 0) {
            c.alias.clear();
        }
        return c;
    }
    c.expression = text(r);
    if (totalsOn && containsAggregate(m_t, r.first, r.last)) {
        c.total = TotalKind::Expression;
    }
    return c;
}

bool SqlParser::selectList(Range r, bool totalsOn, const QString &role)
{
    for (const Range &item : splitCommas(r)) {
        if (item.empty()) {
            return fail(QStringLiteral("A selected column is missing."));
        }
        QueryColumn c = columnFor(item, totalsOn);
        if (!c.expression.isEmpty() && c.alias == QStringLiteral("Expr%1").arg(d.columns.size() + 1)) {
            c.alias.clear();
        }
        c.crosstab = role;
        addColumn(c);
    }
    return true;
}

bool SqlParser::groupBy(Range r)
{
    for (const Range &item : splitCommas(r)) {
        const QString key = keyOf(item);
        const bool known = std::any_of(d.columns.cbegin(), d.columns.cend(), [&](const QueryColumn &c) {
            return c.total == TotalKind::GroupBy && !columnAggregates(c) && columnKey(c) == key;
        });
        if (known) {
            continue;
        }
        if (d.kind == QueryKind::Crosstab) {
            return fail(QStringLiteral("A crosstab groups by its row headings only."));
        }
        QueryColumn c = columnFor(item, false);
        c.show = false;
        addColumn(c);
    }
    return true;
}

bool SqlParser::rebuild(const BoolNode &node, Item *item, QString *criteria)
{
    const BoolNode n = unwrap(node);
    if (n.kind == BoolNode::Term) {
        SplitTerm split;
        if (!splitTerm(m_src, m_t, n.first, n.last, &split)) {
            return fail(QStringLiteral("\"%1\" does not compare a field with a value, so it cannot go in the grid; keep "
                                       "this as SQL.")
                            .arg(textOf(m_src, m_t, n.first, n.last)));
        }
        const QString key = keyOf({split.first, split.opAt});
        if (item->key.isEmpty()) {
            item->key = key;
            item->subject = {split.first, split.opAt};
        } else if (item->key != key) {
            return fail(QStringLiteral("One group of conditions names several fields; keep this as SQL."));
        }
        *criteria = split.criteria;
        return true;
    }
    if (n.kind == BoolNode::Not) {
        const BoolNode inner = unwrap(n.children.first());
        QString c;
        if ((inner.kind != BoolNode::Term && inner.kind != BoolNode::Not) || !rebuild(inner, item, &c)) {
            return fail(QStringLiteral("NOT over several conditions cannot go in the grid; keep this as SQL."));
        }
        *criteria = QStringLiteral("Not ") + c;
        return true;
    }
    QStringList parts;
    for (const BoolNode &child : n.children) {
        // Criteria text has no parentheses for grouping: an OR inside an AND
        // cannot be written in one cell.
        if (n.kind == BoolNode::And && unwrap(child).kind == BoolNode::Or) {
            return fail(QStringLiteral("(… Or …) And … cannot go in one criteria cell; keep this as SQL."));
        }
        QString c;
        if (!rebuild(child, item, &c)) {
            return false;
        }
        parts.append(c);
    }
    *criteria = parts.join(n.kind == BoolNode::And ? QStringLiteral(" And ") : QStringLiteral(" Or "));
    return true;
}

bool SqlParser::itemsOf(const BoolNode &node, QList<Item> *out)
{
    BoolNode row = node.kind == BoolNode::Group ? node.children.first() : node;
    const QList<BoolNode> parts = row.kind == BoolNode::And ? row.children : QList<BoolNode>{row};
    for (const BoolNode &part : parts) {
        Item item;
        if (!rebuild(part, &item, &item.criteria)) {
            return false;
        }
        out->append(item);
    }
    return true;
}

int SqlParser::targetFor(const Item &item, bool having)
{
    const bool crosstab = d.kind == QueryKind::Crosstab;
    for (int i = 0; i < d.columns.size(); ++i) {
        const QueryColumn &c = d.columns.at(i);
        if (c.field == QLatin1String("*") || columnKey(c) != item.key) {
            continue;
        }
        const bool fits = having ? toHaving(c)
                          : crosstab && c.crosstab == QLatin1String("column") ? true
                          : totals() ? c.total == TotalKind::Where
                                     : true;
        if (fits) {
            return i;
        }
    }
    if (having && crosstab) {
        fail(QStringLiteral("A crosstab's HAVING tests its row headings only."));
        return -1;
    }
    QueryColumn c = columnFor(item.subject, having);
    c.show = false;
    if (!having && totals()) {
        c.total = TotalKind::Where;
    }
    return addColumn(c);
}

bool SqlParser::setCriteria(int column, int row, const QString &text)
{
    QueryColumn &c = d.columns[column];
    QString *cell = &c.criteria;
    if (row > 0) {
        while (c.orCriteria.size() < row) {
            c.orCriteria.append(QString());
        }
        cell = &c.orCriteria[row - 1];
    }
    if (cell->isEmpty()) {
        *cell = text;
        return true;
    }
    QString error;
    const auto hasOr = [&](const QString &value) {
        const QList<Token> t = tokenize(value, &error);
        return std::any_of(t.cbegin(), t.cend(), [](const Token &x) { return x.is("or"); });
    };
    if (hasOr(*cell) || hasOr(text)) {
        return fail(QStringLiteral("Two conditions on %1 in one row, one of them with Or, cannot share a cell; keep "
                                   "this as SQL.")
                        .arg(c.field.isEmpty() ? c.expression : c.field));
    }
    *cell += QStringLiteral(" And ") + text;
    return true;
}

bool SqlParser::criteria(Range r, bool having)
{
    BoolNode root;
    QString error;
    if (!parseBool(m_t, r.first, r.last, &root, &error)) {
        return fail(error);
    }
    const QList<BoolNode> rows = root.kind == BoolNode::Or ? root.children : QList<BoolNode>{root};
    for (int row = 0; row < rows.size(); ++row) {
        QList<Item> items;
        if (!itemsOf(rows.at(row), &items)) {
            return false;
        }
        for (const Item &item : std::as_const(items)) {
            const int column = targetFor(item, having);
            if (column < 0 || !setCriteria(column, row, item.criteria)) {
                return false;
            }
        }
    }
    return true;
}

bool SqlParser::orderBy(Range r)
{
    QList<QPair<Range, int>> keys;
    for (Range item : splitCommas(r)) {
        int sort = 1;
        if (item.last - item.first > 1 && (at(item.last - 1).is("asc") || at(item.last - 1).is("desc"))) {
            sort = at(item.last - 1).is("desc") ? -1 : 1;
            --item.last;
        }
        keys.append({item, sort});
    }
    // Sorts land on their columns when those come in the same order;
    // otherwise hidden sort columns carry the order (Access does the same).
    QList<int> targets;
    for (const auto &[range, sort] : keys) {
        const QString key = keyOf(range);
        int found = -1;
        for (int i = 0; i < d.columns.size() && found < 0; ++i) {
            const QueryColumn &c = d.columns.at(i);
            const bool sortable = !(totals() && c.total == TotalKind::Where)
                                  && (d.kind != QueryKind::Crosstab || c.crosstab == QLatin1String("row"));
            found = sortable && columnKey(c) == key ? i : -1;
        }
        targets.append(found);
    }
    const bool inOrder = !targets.contains(-1) && std::is_sorted(targets.cbegin(), targets.cend())
                         && std::adjacent_find(targets.cbegin(), targets.cend()) == targets.cend();
    for (int k = 0; k < keys.size(); ++k) {
        if (inOrder) {
            d.columns[targets.at(k)].sort = keys.at(k).second;
            continue;
        }
        if (d.kind == QueryKind::Crosstab) {
            return fail(QStringLiteral("A crosstab sorts by its row headings, in their order."));
        }
        QueryColumn c = columnFor(keys.at(k).first, totals());
        c.show = false;
        c.sort = keys.at(k).second;
        addColumn(c);
    }
    return true;
}

bool SqlParser::inList(Range r, QStringList *out)
{
    if (r.empty() || !at(r.first).op("(") || closing(m_t, r.first) != r.last - 1) {
        return fail(QStringLiteral("PIVOT … IN takes a list in parentheses."));
    }
    for (const Range &item : splitCommas({r.first + 1, r.last - 1})) {
        if (item.last - item.first == 2 && at(item.first).op("-") && at(item.first + 1).kind == Token::Number) {
            out->append(QLatin1Char('-') + at(item.first + 1).text);
        } else if (item.last - item.first == 1 && at(item.first).kind != Token::Op) {
            out->append(at(item.first).text);
        } else {
            return fail(QStringLiteral("PIVOT … IN lists values only."));
        }
    }
    return true;
}

} // namespace QindaOffice::Base::AccessSql
