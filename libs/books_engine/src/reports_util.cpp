// SPDX-License-Identifier: GPL-3.0-or-later
#include "reports_util.h"

#include "company_file.h"
#include "decimal.h"

#include <QSet>

#include <functional>

namespace QindaOffice::Books::ReportUtil {

QString periodText(const QDate &from, const QDate &to)
{
    if (!from.isValid()) {
        return QStringLiteral("As of %1").arg(to.toString(QStringLiteral("MMMM d, yyyy")));
    }
    return QStringLiteral("%1 – %2").arg(from.toString(QStringLiteral("MMMM d, yyyy")),
                                         to.toString(QStringLiteral("MMMM d, yyyy")));
}

QString basisText(Basis basis)
{
    return basis == Basis::Cash ? QStringLiteral("Cash basis") : QStringLiteral("Accrual basis");
}

ReportTable makeTable(CompanyFile &file, const QString &id, const QString &title, const QString &subtitle)
{
    ReportTable t;
    t.id = id;
    t.title = title;
    t.company = file.settings().displayName();
    t.subtitle = subtitle;
    t.currency = file.currency();
    return t;
}

void setColumns(ReportTable &table, const QString &labelTitle, const QList<ValueColumn> &columns)
{
    table.columns = {{QStringLiteral("label"), labelTitle, ColumnType::Text}};
    for (const ValueColumn &c : columns) {
        table.columns << ReportColumn{c.id, c.title, ColumnType::Money};
    }
}

ReportRow sectionRow(const QString &key, const QString &title, int columnCount, int depth)
{
    ReportRow r;
    r.kind = RowKind::Section;
    r.key = key;
    r.depth = depth;
    r.cells << ReportCell::ofText(title);
    for (int i = 1; i < columnCount; ++i) {
        r.cells << ReportCell::blank();
    }
    return r;
}

ReportRow moneyRow(RowKind kind, const QString &key, const QString &label, const QList<qint64> &values, int depth)
{
    ReportRow r;
    r.kind = kind;
    r.key = key;
    r.depth = depth;
    r.cells << ReportCell::ofText(label);
    for (qint64 v : values) {
        r.cells << ReportCell::ofMoney(v);
    }
    return r;
}

QList<qint64> add(const QList<qint64> &a, const QList<qint64> &b, int sign)
{
    QList<qint64> out = a;
    for (qsizetype i = 0; i < out.size() && i < b.size(); ++i) {
        out[i] += sign * b[i];
    }
    return out;
}

qint64 percentOf(qint64 part, qint64 whole, int scale)
{
    if (whole == 0) {
        return 0;
    }
    return scaleExact(part, 100 * pow10i(scale), whole, RoundingMode::HalfUp).value_or(0);
}

QList<qint64> appendSection(ReportTable &table, const QList<Account> &chart, const QList<AccountType> &types,
                            const QString &key, const QString &title, const QList<ValueColumn> &columns, int sign,
                            int depth)
{
    const int n = int(columns.size());
    QList<Account> members;
    QSet<qint64> memberIds;
    for (const Account &a : chart) {
        if (types.contains(a.type)) {
            members << a;
            memberIds.insert(a.id);
        }
    }
    QHash<qint64, QList<qsizetype>> children;
    QList<qsizetype> roots;
    for (qsizetype i = 0; i < members.size(); ++i) {
        if (members[i].parentId != 0 && memberIds.contains(members[i].parentId)) {
            children[members[i].parentId] << i;
        } else {
            roots << i;
        }
    }
    auto own = [&](const Account &a) {
        QList<qint64> v;
        for (const ValueColumn &c : columns) {
            v << sign * c.net.value(a.id);
        }
        return v;
    };
    // Subtree totals first, so empty subtrees can be skipped.
    QHash<qint64, QList<qint64>> subtree;
    std::function<QList<qint64>(qsizetype)> sum = [&](qsizetype i) {
        QList<qint64> v = own(members[i]);
        for (qsizetype k : children.value(members[i].id)) {
            v = add(v, sum(k));
        }
        subtree.insert(members[i].id, v);
        return v;
    };
    QList<qint64> total(n, 0);
    for (qsizetype r : roots) {
        total = add(total, sum(r));
    }
    auto allZero = [](const QList<qint64> &v) {
        for (qint64 x : v) {
            if (x != 0) {
                return false;
            }
        }
        return true;
    };
    const QDate from = columns.isEmpty() ? QDate() : columns.first().from;
    const QDate to = columns.isEmpty() ? QDate() : columns.last().to;
    table.rows << sectionRow(QStringLiteral("section:") + key, title, n + 1, depth);
    std::function<void(qsizetype, int)> emitRows = [&](qsizetype i, int level) {
        const Account &a = members[i];
        if (allZero(subtree.value(a.id))) {
            return;
        }
        const QString label = a.number.isEmpty() ? a.name : a.number + QLatin1Char(' ') + a.name;
        ReportRow row = moneyRow(RowKind::Account, QStringLiteral("account:%1").arg(a.id), label, own(a), level);
        row.drill.accountId = a.id;
        row.drill.from = from;
        row.drill.to = to;
        bool hasVisibleChildren = false;
        for (qsizetype k : children.value(a.id)) {
            hasVisibleChildren = hasVisibleChildren || !allZero(subtree.value(members[k].id));
        }
        if (hasVisibleChildren && allZero(own(a))) {
            for (int c = 1; c < row.cells.size(); ++c) {
                row.cells[c] = ReportCell::blank();
            }
        }
        table.rows << row;
        if (!hasVisibleChildren) {
            return;
        }
        for (qsizetype k : children.value(a.id)) {
            emitRows(k, level + 1);
        }
        ReportRow subtotal = moneyRow(RowKind::Subtotal, QStringLiteral("subtotal:account:%1").arg(a.id),
                                      QStringLiteral("Total ") + label, subtree.value(a.id), level);
        subtotal.drill.accountId = a.id;
        table.rows << subtotal;
    };
    for (qsizetype r : roots) {
        emitRows(r, depth + 1);
    }
    table.rows << moneyRow(RowKind::Subtotal, QStringLiteral("total:") + key, QStringLiteral("Total ") + title, total,
                           depth);
    return total;
}

} // namespace QindaOffice::Books::ReportUtil
