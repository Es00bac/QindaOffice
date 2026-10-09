// SPDX-License-Identifier: GPL-3.0-or-later
// Open purchase orders and the time reports (M68, D-077; reports.h).
// Purchase-order balances come from PurchaseOrders (what live bills
// received), durations from TimeTracking — the numbers the editors show.
#include "reports.h"

#include "company_file.h"
#include "lists.h"
#include "purchase_orders.h"
#include "reports_util.h"
#include "time_tracking.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <functional>

namespace QindaOffice::Books {

using namespace ReportUtil;

namespace {

ReportRow row(RowKind kind, const QString &key, int depth, const QList<ReportCell> &cells)
{
    ReportRow r;
    r.kind = kind;
    r.key = key;
    r.depth = depth;
    r.cells = cells;
    return r;
}

// `label` then blanks up to `columns`, with `values` placed by index.
QList<ReportCell> cellsOf(const QString &label, int columns, const QList<QPair<int, ReportCell>> &values = {})
{
    QList<ReportCell> cells{ReportCell::ofText(label)};
    for (int i = 1; i < columns; ++i) {
        cells << ReportCell::blank();
    }
    for (const auto &[index, cell] : values) {
        cells[index] = cell;
    }
    return cells;
}

QString partyName(Lists &lists, qint64 id, const QString &none)
{
    const std::optional<Party> p = lists.party(id);
    return p ? p->displayName : none;
}

QString itemName(Lists &lists, qint64 id)
{
    const std::optional<Item> i = lists.item(id);
    return i ? i->name : QStringLiteral("No service item");
}

// Sorted by name, the "none" group (id 0) last.
template<class Map>
QList<qint64> byName(const Map &groups, const std::function<QString(qint64)> &name)
{
    QList<qint64> ids = groups.keys();
    std::sort(ids.begin(), ids.end(), [&name](qint64 a, qint64 b) {
        if ((a == 0) != (b == 0)) {
            return b == 0;
        }
        return name(a).compare(name(b), Qt::CaseInsensitive) < 0;
    });
    return ids;
}

// One group or row of a duration summary.
struct Node {
    QString key;       // "party:3", "employee:2", "vendor:7", "item:5"
    QString name;
    bool none = false; // "No customer:job" / "No service item": sorts last
    qint64 minutes = 0;
};

struct Group {
    Node node;
    QHash<QString, Node> inner;
};

void sortNodes(QList<Node> &nodes)
{
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node &a, const Node &b) {
        if (a.none != b.none) {
            return b.none;
        }
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

// Two-level duration summary: each group a section, its rows, a subtotal.
ReportTable durationSummary(ReportTable t, const QHash<QString, Group> &groups, const QString &labelTitle)
{
    t.columns = {{QStringLiteral("label"), labelTitle, ColumnType::Text},
                 {QStringLiteral("duration"), QStringLiteral("Duration"), ColumnType::Duration}};
    QList<Node> outer;
    for (const Group &g : groups) {
        outer << g.node;
    }
    sortNodes(outer);
    qint64 grand = 0;
    for (const Node &o : std::as_const(outer)) {
        t.rows << row(RowKind::Section, o.key, 0, cellsOf(o.name, 2));
        QList<Node> inner = groups.value(o.key).inner.values();
        sortNodes(inner);
        // No drill-down: time is not a document (the reports list it).
        for (const Node &i : std::as_const(inner)) {
            t.rows << row(RowKind::Account, o.key + QLatin1Char(':') + i.key, 1,
                          cellsOf(i.name, 2, {{1, ReportCell::ofDuration(i.minutes)}}));
        }
        t.rows << row(RowKind::Subtotal, QStringLiteral("subtotal:") + o.key, 0,
                      cellsOf(QStringLiteral("Total ") + o.name, 2, {{1, ReportCell::ofDuration(o.minutes)}}));
        grand += o.minutes;
    }
    t.rows << row(RowKind::Total, QStringLiteral("total"), 0,
                  cellsOf(QStringLiteral("Total"), 2, {{1, ReportCell::ofDuration(grand)}}));
    return t;
}

void add(QHash<QString, Group> &groups, const Node &outer, const Node &inner, qint64 minutes)
{
    Group &g = groups[outer.key];
    if (g.node.key.isEmpty()) {
        g.node = outer;
    }
    g.node.minutes += minutes;
    Node &n = g.inner[inner.key];
    if (n.key.isEmpty()) {
        n = inner;
    }
    n.minutes += minutes;
}

Node customerNode(Lists &lists, qint64 id)
{
    return Node{QStringLiteral("party:%1").arg(id), partyName(lists, id, QStringLiteral("No customer:job")), id == 0, 0};
}

} // namespace

ReportTable Reports::openPurchaseOrders(const QDate &asOf) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("open_purchase_orders"), QStringLiteral("Open Purchase Orders"),
                              periodText({}, asOf));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Vendor / order"), ColumnType::Text},
                 {QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("expected"), QStringLiteral("Expected"), ColumnType::Date},
                 {QStringLiteral("amount"), QStringLiteral("Amount"), ColumnType::Money},
                 {QStringLiteral("open"), QStringLiteral("Open Balance"), ColumnType::Money}};
    PurchaseOrders orders(m_file);
    Documents documents(m_file);
    Lists lists(m_file);
    DocumentFilter filter;
    filter.kinds = {DocKind::PurchaseOrder};
    filter.to = asOf;
    QMap<qint64, QList<QPair<BusinessDocument, qint64>>> byVendor;
    for (const BusinessDocument &po : documents.list(filter)) {
        const DocumentStatus status = orders.status(po, asOf);
        if (status == DocumentStatus::Closed || status == DocumentStatus::Void) {
            continue;
        }
        byVendor[po.partyId] << qMakePair(po, orders.remainingAmount(po.id, asOf).minor());
    }
    const auto vendorName = [&lists](qint64 id) { return partyName(lists, id, QString()); };
    qint64 grandAmount = 0;
    qint64 grandOpen = 0;
    for (qint64 vendor : byName(byVendor, vendorName)) {
        const QString key = QStringLiteral("party:%1").arg(vendor);
        t.rows << row(RowKind::Section, key, 0, cellsOf(vendorName(vendor), 5));
        qint64 amount = 0;
        qint64 open = 0;
        for (const auto &[po, remaining] : byVendor.value(vendor)) {
            ReportRow r = row(RowKind::Detail, QStringLiteral("doc:%1").arg(po.id), 1,
                              {ReportCell::ofText(po.number), ReportCell::ofDate(po.date), ReportCell::ofDate(po.dueDate),
                               ReportCell::ofMoney(po.total.minor()), ReportCell::ofMoney(remaining)});
            r.drill.documentIds = {po.id};
            t.rows << r;
            amount += po.total.minor();
            open += remaining;
        }
        ReportRow sub = row(RowKind::Subtotal, QStringLiteral("subtotal:") + key, 0,
                            cellsOf(QStringLiteral("Total ") + vendorName(vendor), 5,
                                    {{3, ReportCell::ofMoney(amount)}, {4, ReportCell::ofMoney(open)}}));
        sub.drill.partyId = vendor;
        t.rows << sub;
        grandAmount += amount;
        grandOpen += open;
    }
    t.rows << row(RowKind::Total, QStringLiteral("total"), 0,
                  cellsOf(QStringLiteral("Total"), 5,
                          {{3, ReportCell::ofMoney(grandAmount)}, {4, ReportCell::ofMoney(grandOpen)}}));
    return t;
}

ReportTable Reports::timeByJob(const QDate &from, const QDate &to) const
{
    TimeFilter filter;
    filter.from = from;
    filter.to = to;
    Lists lists(m_file);
    QHash<QString, Group> groups;
    for (const TimeEntry &e : TimeTracking(m_file).entries(filter)) {
        add(groups, customerNode(lists, e.customerId),
            Node{QStringLiteral("item:%1").arg(e.itemId), itemName(lists, e.itemId), e.itemId == 0, 0}, e.minutes);
    }
    return durationSummary(makeTable(m_file, QStringLiteral("time_by_job"), QStringLiteral("Time by Job"),
                                     periodText(from, to)),
                           groups, QStringLiteral("Customer:job / service"));
}

ReportTable Reports::timeByName(const QDate &from, const QDate &to) const
{
    TimeFilter filter;
    filter.from = from;
    filter.to = to;
    Lists lists(m_file);
    TimeTracking time(m_file);
    QHash<QString, Group> groups;
    for (const TimeEntry &e : time.entries(filter)) {
        add(groups, Node{e.worker.key(), time.workerName(e.worker), false, 0}, customerNode(lists, e.customerId),
            e.minutes);
    }
    return durationSummary(makeTable(m_file, QStringLiteral("time_by_name"), QStringLiteral("Time by Name"),
                                     periodText(from, to)),
                           groups, QStringLiteral("Name / customer:job"));
}

ReportTable Reports::unbilledTimeByJob(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("unbilled_time_by_job"), QStringLiteral("Unbilled Time by Job"),
                              periodText(from, to));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Customer:job / notes"), ColumnType::Text},
                 {QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("name"), QStringLiteral("Name"), ColumnType::Text},
                 {QStringLiteral("item"), QStringLiteral("Service"), ColumnType::Text},
                 {QStringLiteral("duration"), QStringLiteral("Duration"), ColumnType::Duration},
                 {QStringLiteral("rate"), QStringLiteral("Rate"), ColumnType::Money},
                 {QStringLiteral("amount"), QStringLiteral("Amount"), ColumnType::Money}};
    TimeFilter filter;
    filter.from = from;
    filter.to = to;
    filter.status = TimeStatus::Unbilled;
    Lists lists(m_file);
    TimeTracking time(m_file);
    QMap<qint64, QList<TimeEntry>> byCustomer;
    for (const TimeEntry &e : time.entries(filter)) {
        byCustomer[e.customerId] << e;
    }
    const auto customerName = [&lists](qint64 id) { return partyName(lists, id, QStringLiteral("No customer:job")); };
    qint64 grandMinutes = 0;
    qint64 grandAmount = 0;
    for (qint64 customer : byName(byCustomer, customerName)) {
        const QString key = QStringLiteral("party:%1").arg(customer);
        t.rows << row(RowKind::Section, key, 0, cellsOf(customerName(customer), 7));
        qint64 minutes = 0;
        qint64 amount = 0;
        for (const TimeEntry &e : byCustomer.value(customer)) {
            const std::optional<Item> item = lists.item(e.itemId);
            const Money rate = time.rateFor(e);
            const qint64 billed = rate.multiplied(TimeTracking::hours(e.minutes), RoundingMode::HalfUp).minor();
            t.rows << row(RowKind::Detail, QStringLiteral("time:%1").arg(e.id), 1,
                          {ReportCell::ofText(!e.notes.isEmpty() ? e.notes : item ? item->name : QString()),
                           ReportCell::ofDate(e.date),
                           ReportCell::ofText(time.workerName(e.worker)),
                           ReportCell::ofText(item ? item->name : QString()), ReportCell::ofDuration(e.minutes),
                           ReportCell::ofMoney(rate.minor()), ReportCell::ofMoney(billed)});
            minutes += e.minutes;
            amount += billed;
        }
        ReportRow sub = row(RowKind::Subtotal, QStringLiteral("subtotal:") + key, 0,
                            cellsOf(QStringLiteral("Total ") + customerName(customer), 7,
                                    {{4, ReportCell::ofDuration(minutes)}, {6, ReportCell::ofMoney(amount)}}));
        sub.drill.partyId = customer;
        t.rows << sub;
        grandMinutes += minutes;
        grandAmount += amount;
    }
    t.rows << row(RowKind::Total, QStringLiteral("total"), 0,
                  cellsOf(QStringLiteral("Total"), 7,
                          {{4, ReportCell::ofDuration(grandMinutes)}, {6, ReportCell::ofMoney(grandAmount)}}));
    return t;
}

} // namespace QindaOffice::Books
