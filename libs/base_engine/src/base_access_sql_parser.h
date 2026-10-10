// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Access SQL parser's state (internal to base_access_sql_parse.cpp and
// base_access_sql_columns.cpp): the tokens, the design being built and the
// tables' definitions by reference.

#include "base_access_sql_p.h"
#include "base_query_p.h"

#include <QHash>

namespace QindaOffice::Base::AccessSql {

class SqlParser {
public:
    SqlParser(const BaseFile &file, const QString &src)
        : m_file(file), m_src(src)
    {
    }
    AccessSqlParse run();

private:
    struct Range {
        int first = 0;
        int last = 0;
        [[nodiscard]] bool empty() const { return first >= last; }
    };
    // A criteria item: the subject's tokens (in `src`'s token list) and the
    // criteria text the column gets.
    struct Item {
        QString key;
        Range subject;
        QString criteria;
    };

    bool fail(const QString &why)
    {
        if (m_error.isEmpty()) {
            m_error = why;
        }
        return false;
    }
    [[nodiscard]] const Token &at(int i) const { return m_t.at(qBound(0, i, int(m_t.size()) - 1)); }
    [[nodiscard]] QString text(Range r) const { return textOf(m_src, m_t, r.first, r.last); }
    [[nodiscard]] int find(int from, int to, const char *word) const;
    [[nodiscard]] QList<Range> splitCommas(Range r) const;

    // base_access_sql_parse.cpp: statements and FROM
    bool parameters();
    bool clauses(int first, QHash<QString, Range> *out);
    bool tableRef(int &pos, int last, QStringList *refs);
    bool fromPrimary(int &pos, int last, QStringList *refs);
    bool joinExpression(int &pos, int last, QStringList *refs);
    bool fromClause(Range r);

    // base_access_sql_columns.cpp: statements over columns
    bool selectStatement(int first);
    bool crosstabStatement();
    bool updateStatement();
    bool deleteStatement();
    bool appendStatement();
    // M86: SELECT … UNION [ALL] SELECT … [ORDER BY …] (base_access_sql_union.cpp);
    // `unions` holds the UNION tokens found at depth 0.
    bool unionStatement(const QList<int> &unions);
    [[nodiscard]] QList<int> topLevelUnions() const;
    // A [field] or table.field naming a field of the query's tables: (the
    // table's reference in the design, the field's own spelling).
    [[nodiscard]] std::optional<QPair<QString, QString>> fieldRefIn(const QList<Token> &t, int first, int last) const;
    // What a subject or column is, for matching criteria and sorts to
    // columns: "f:ref.field", "a:sum:<inner key>" or "e:<normalized text>".
    [[nodiscard]] QString keyIn(const QList<Token> &t, int first, int last) const;
    [[nodiscard]] QString keyOf(Range r) const { return keyIn(m_t, r.first, r.last); }
    [[nodiscard]] QString columnKey(const QueryColumn &c) const;
    [[nodiscard]] QueryColumn columnFor(Range r, bool totals) const;
    [[nodiscard]] static bool aggregateCallIn(const QList<Token> &t, int first, int last);
    [[nodiscard]] static bool containsAggregate(const QList<Token> &t, int first, int last);
    bool inList(Range r, QStringList *out);
    bool selectList(Range r, bool totals, const QString &role);
    bool groupBy(Range r);
    bool criteria(Range r, bool having);
    bool itemsOf(const BoolNode &node, QList<Item> *out);
    bool rebuild(const BoolNode &node, Item *item, QString *criteria);
    bool orderBy(Range r);
    int targetFor(const Item &item, bool having);
    bool setCriteria(int column, int row, const QString &text);
    int addColumn(const QueryColumn &c)
    {
        d.columns.append(c);
        return int(d.columns.size()) - 1;
    }
    [[nodiscard]] bool totals() const { return detail::totalsOn(d); }
    [[nodiscard]] bool columnAggregates(const QueryColumn &c) const;
    [[nodiscard]] bool toHaving(const QueryColumn &c) const;

    const BaseFile &m_file;
    const QString m_src;
    QList<Token> m_t;
    int m_first = 0;
    int m_last = 0;
    QString m_error;
    QHash<QString, TableDef> m_defs; // by table ref, lower case
    QueryDesign d;
};

} // namespace QindaOffice::Base::AccessSql
