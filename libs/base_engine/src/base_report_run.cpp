// SPDX-License-Identifier: GPL-3.0-or-later
// Reports evaluated by the engine, part two: the run — filter, Group, Sort
// and Total, the bands and every total computed exactly (currency in
// integer minor units). See base_report.h for the contract.
#include "base_report.h"

#include <cmath>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

bool isNull(const QVariant &v)
{
    return !v.isValid() || v.isNull();
}

bool integral(FieldType t)
{
    return t == FieldType::Integer || t == FieldType::AutoNumber || t == FieldType::Currency || t == FieldType::YesNo;
}

QString valueKey(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double && std::floor(v.toDouble()) == v.toDouble()
               ? QString::number(qint64(v.toDouble()))
               : v.toString();
}

// A whole number bare, any other to two places (the toolkit's default).
QString plainNumber(double value)
{
    if (std::isfinite(value) && std::floor(value) == value && std::fabs(value) < 1e15) {
        return QString::number(qint64(value));
    }
    return QString::number(value, 'f', 2);
}

// round(sum / count), half away from zero, in integers.
qint64 roundedQuotient(qint64 sum, qint64 count)
{
    const qint64 magnitude = (std::llabs(sum) * 2 + count) / (2 * count);
    return sum < 0 ? -magnitude : magnitude;
}

struct Rows {
    const ReportSource &source;
    const CurrencyFormat &currency;

    // -1, 0, 1 for two non-null values of `column` (lookups by name, ties
    // by the stored key so one key's rows stay together).
    [[nodiscard]] int compare(int column, const QVariant &a, const QVariant &b) const
    {
        const ReportColumn &c = source.columns.at(column);
        if (!c.lookup.isEmpty()) {
            const int byName = source.display(column, a, currency).compare(source.display(column, b, currency),
                                                                           Qt::CaseInsensitive);
            if (byName != 0) {
                return byName < 0 ? -1 : 1;
            }
        }
        const bool numbers = isNumeric(c.type) || c.type == FieldType::YesNo;
        if (numbers && integral(c.type) && a.metaType().id() != QMetaType::Double
            && b.metaType().id() != QMetaType::Double) {
            const qint64 x = a.toLongLong();
            const qint64 y = b.toLongLong();
            return x < y ? -1 : x > y ? 1 : 0;
        }
        if (numbers) {
            const double x = a.toDouble();
            const double y = b.toDouble();
            return x < y ? -1 : x > y ? 1 : 0;
        }
        int cmp = a.toString().compare(b.toString(), Qt::CaseInsensitive);
        if (cmp == 0) {
            cmp = a.toString().compare(b.toString());
        }
        return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
    }
};

struct Measure {
    int column = -1; // -1: countRows
    QString field;
    QString function;
    QString label;
};

struct Accumulator {
    qint64 rows = 0;
    QList<qint64> values;   // non-null values per measure
    QList<qint64> exact;    // integral sums
    QList<double> real;     // decimal sums
    QList<QVariant> lowest; // per measure
    QList<QVariant> highest;
    void reset(int measures)
    {
        rows = 0;
        values = QList<qint64>(measures, 0);
        exact = QList<qint64>(measures, 0);
        real = QList<double>(measures, 0.0);
        lowest = QList<QVariant>(measures);
        highest = QList<QVariant>(measures);
    }
};

class Runner {
public:
    Runner(const ReportSource &source, const ReportSpec &spec, const CurrencyFormat &currency)
        : m_source(source), m_spec(spec), m_currency(currency), m_rows{source, currency}
    {
    }
    ReportRun run();

private:
    QString fail(const QString &error)
    {
        if (m_error.isEmpty()) {
            m_error = error;
        }
        return m_error;
    }
    QString label(int column) const { return m_source.columns.at(column).label; }
    bool measuresFrom(const QList<ReportTotal> &totals, QList<Measure> *out);
    void add(Accumulator &acc, const QList<Measure> &measures, const QVariantList &row) const;
    // The total as printed, recorded in run.totals.
    QString total(const Accumulator &acc, const QList<Measure> &measures, int index, int level, ReportRun &run) const;
    QString totalsText(const QString &lead, const Accumulator &acc, const QList<Measure> &measures, int level,
                       ReportRun &run) const;

    const ReportSource &m_source;
    const ReportSpec &m_spec;
    const CurrencyFormat &m_currency;
    Rows m_rows;
    QString m_error;
    QStringList m_key; // the current group values as printed
};

bool Runner::measuresFrom(const QList<ReportTotal> &totals, QList<Measure> *out)
{
    static const QStringList functions{s("countRows"), s("countValues"), s("sum"), s("average"), s("minimum"), s("maximum")};
    static const QHash<QString, QString> words{{s("countValues"), s("Number of %1")}, {s("sum"), s("Sum of %1")},
                                               {s("average"), s("Average of %1")}, {s("minimum"), s("Lowest %1")},
                                               {s("maximum"), s("Highest %1")}};
    for (const ReportTotal &t : totals) {
        if (!functions.contains(t.function)) {
            fail(s("The report has an unknown total %1.").arg(t.function));
            return false;
        }
        if (t.function == QLatin1String("countRows")) {
            continue; // the count leads every footer
        }
        const int column = m_source.columnIndex(t.field);
        if (column < 0) {
            fail(s("The report totals %1, which is not in %2.").arg(t.field, m_source.name));
            return false;
        }
        const ReportColumn &c = m_source.columns.at(column);
        if ((t.function == QLatin1String("sum") || t.function == QLatin1String("average"))
            && (!isNumeric(c.type) || !c.lookup.isEmpty())) {
            fail(s("%1 cannot be summed or averaged: it is not a number field.").arg(c.label));
            return false;
        }
        out->append({column, c.name, t.function,
                     t.label.isEmpty() ? words.value(t.function).arg(c.label) : t.label});
    }
    return true;
}

void Runner::add(Accumulator &acc, const QList<Measure> &measures, const QVariantList &row) const
{
    ++acc.rows;
    for (int m = 0; m < measures.size(); ++m) {
        const QVariant v = row.value(measures.at(m).column);
        if (isNull(v)) {
            continue;
        }
        ++acc.values[m];
        const FieldType type = m_source.columns.at(measures.at(m).column).type;
        if (integral(type) && v.metaType().id() != QMetaType::Double) {
            acc.exact[m] += v.toLongLong();
        } else if (type == FieldType::Currency) {
            // A REAL in a currency column (a foreign file) holds units.
            acc.real[m] += v.toDouble() * std::pow(10.0, m_currency.decimals);
        } else {
            acc.real[m] += v.toDouble();
        }
        const int column = measures.at(m).column;
        if (isNull(acc.lowest.at(m)) || m_rows.compare(column, v, acc.lowest.at(m)) < 0) {
            acc.lowest[m] = v;
        }
        if (isNull(acc.highest.at(m)) || m_rows.compare(column, v, acc.highest.at(m)) > 0) {
            acc.highest[m] = v;
        }
    }
}

QString Runner::total(const Accumulator &acc, const QList<Measure> &measures, int index, int level, ReportRun &run) const
{
    const Measure &m = measures.at(index);
    const FieldType type = m_source.columns.at(m.column).type;
    QVariant value;
    QString text;
    const qint64 n = acc.values.at(index);
    if (m.function == QLatin1String("countValues")) {
        value = n;
        text = QString::number(n);
    } else if (n == 0) {
        text = s("—");
    } else if (m.function == QLatin1String("sum")) {
        if (integral(type)) {
            const qint64 sum = acc.exact.at(index) + qint64(std::llround(acc.real.at(index)));
            value = sum;
            text = type == FieldType::Currency ? formatMoney(sum, m_currency.decimals, m_currency.symbol)
                                               : QString::number(sum);
        } else {
            value = acc.real.at(index) + double(acc.exact.at(index));
            text = plainNumber(value.toDouble());
        }
    } else if (m.function == QLatin1String("average")) {
        if (type == FieldType::Currency) {
            const qint64 average = roundedQuotient(acc.exact.at(index) + qint64(std::llround(acc.real.at(index))), n);
            value = average;
            text = formatMoney(average, m_currency.decimals, m_currency.symbol);
        } else {
            value = (double(acc.exact.at(index)) + acc.real.at(index)) / double(n);
            text = plainNumber(value.toDouble());
        }
    } else {
        value = m.function == QLatin1String("minimum") ? acc.lowest.at(index) : acc.highest.at(index);
        text = m_source.display(m.column, value, m_currency);
    }
    run.totals.append({level, level < 0 ? QStringList() : m_key.mid(0, level + 1), m.field, m.function, value, text});
    return m.label + QLatin1Char(' ') + text;
}

QString Runner::totalsText(const QString &lead, const Accumulator &acc, const QList<Measure> &measures, int level,
                           ReportRun &run) const
{
    QStringList parts{lead, s("Count %1").arg(acc.rows)};
    run.totals.append({level, level < 0 ? QStringList() : m_key.mid(0, level + 1), QString(), s("countRows"),
                       acc.rows, QString::number(acc.rows)});
    for (int i = 0; i < measures.size(); ++i) {
        parts.append(total(acc, measures, i, level, run));
    }
    return parts.join(s(" · "));
}

ReportRun Runner::run()
{
    ReportRun out;
    QList<int> columns;
    if (m_spec.columns.isEmpty()) {
        for (int i = 0; i < m_source.columns.size(); ++i) {
            columns.append(i);
            out.columnLabels.append(label(i));
        }
    }
    for (const auto &[field, given] : m_spec.columns) {
        const int i = m_source.columnIndex(field);
        if (i < 0) {
            out.error = s("The report shows %1, which is not in %2.").arg(field, m_source.name);
            return out;
        }
        columns.append(i);
        out.columnLabels.append(given.isEmpty() ? label(i) : given);
    }
    QList<int> groupColumns;
    QList<QList<Measure>> groupMeasures;
    for (const ReportGroup &g : m_spec.groups) {
        const int i = m_source.columnIndex(g.field);
        if (i < 0) {
            out.error = s("The report groups on %1, which is not in %2.").arg(g.field, m_source.name);
            return out;
        }
        groupColumns.append(i);
        groupMeasures.append(QList<Measure>());
        if (!measuresFrom(g.totals, &groupMeasures.last())) {
            out.error = m_error;
            return out;
        }
    }
    QList<Measure> grandMeasures;
    if (!measuresFrom(m_spec.totals, &grandMeasures)) {
        out.error = m_error;
        return out;
    }
    // The designer's equality filter: by the stored value's edit text or
    // the printed text, case-blind ("Card", "25.00", a donor's name).
    QList<int> order;
    const int filterColumn = m_spec.filterField.isEmpty() ? -1 : m_source.columnIndex(m_spec.filterField);
    if (!m_spec.filterField.isEmpty() && filterColumn < 0) {
        out.error = s("The report filters on %1, which is not in %2.").arg(m_spec.filterField, m_source.name);
        return out;
    }
    for (int r = 0; r < m_source.rows.size(); ++r) {
        if (filterColumn >= 0) {
            const QVariant v = m_source.rows.at(r).value(filterColumn);
            const FieldType type = m_source.columns.at(filterColumn).type;
            const QString wanted = m_spec.filterValue.trimmed();
            const bool match = editText(type, v, m_currency).compare(wanted, Qt::CaseInsensitive) == 0
                               || m_source.display(filterColumn, v, m_currency).compare(wanted, Qt::CaseInsensitive) == 0;
            if (!match) {
                continue;
            }
        }
        order.append(r);
    }
    QList<ReportSortKey> keys;
    for (const ReportGroup &g : m_spec.groups) {
        keys.append({g.field, false});
    }
    for (const ReportSortKey &k : m_spec.sort) {
        if (m_source.columnIndex(k.field) < 0) {
            out.error = s("The report sorts on %1, which is not in %2.").arg(k.field, m_source.name);
            return out;
        }
        keys.append(k);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        for (const ReportSortKey &k : std::as_const(keys)) {
            const int c = m_source.columnIndex(k.field);
            const QVariant &x = m_source.rows.at(a).at(c);
            const QVariant &y = m_source.rows.at(b).at(c);
            if (isNull(x) != isNull(y)) {
                return isNull(y); // Nulls last, either direction
            }
            if (isNull(x)) {
                continue;
            }
            const int cmp = m_rows.compare(c, x, y);
            if (cmp != 0) {
                return k.descending ? cmp > 0 : cmp < 0;
            }
        }
        return false;
    });

    out.bands.append({ReportBand::Kind::ReportHeader, -1, m_spec.title, {}, false, false});
    QStringList info{s("Source: %1").arg(m_source.name), s("%1 record(s)").arg(order.size())};
    for (const QueryParameter &p : m_source.parameters) {
        info.append(s("%1 = %2").arg(p.prompt, m_source.parameterValues.value(p.name).toString()));
    }
    if (m_source.truncated) {
        info.append(s("the first %1 records of the source").arg(m_source.rows.size()));
    }
    out.bands.append({ReportBand::Kind::ReportInfo, -1, info.join(s(" · ")), {}, false, false});

    const int levels = int(groupColumns.size());
    QList<Accumulator> acc(levels);
    for (int level = 0; level < levels; ++level) {
        acc[level].reset(int(groupMeasures.at(level).size()));
    }
    Accumulator grand;
    grand.reset(int(grandMeasures.size()));
    QStringList previous; // stored keys
    m_key = QStringList(levels);
    bool first = true;
    const auto footer = [&](int level) {
        const ReportGroup &g = m_spec.groups.at(level);
        const QString groupLabel = g.label.isEmpty() ? label(groupColumns.at(level)) : g.label;
        const QString lead = s("%1 %2 total").arg(groupLabel, m_key.at(level));
        out.bands.append({ReportBand::Kind::GroupFooter, level,
                          totalsText(lead, acc.at(level), groupMeasures.at(level), level, out), {}, false, true});
        acc[level].reset(int(groupMeasures.at(level).size()));
    };
    for (const int r : std::as_const(order)) {
        const QVariantList &row = m_source.rows.at(r);
        QStringList key;
        for (const int c : std::as_const(groupColumns)) {
            key.append(isNull(row.value(c)) ? s("\x01") : valueKey(row.value(c)));
        }
        int changed = first ? 0 : levels;
        for (int level = 0; !first && level < levels; ++level) {
            if (key.at(level) != previous.at(level)) {
                changed = level;
                break;
            }
        }
        if (!first) {
            for (int level = levels - 1; level >= changed; --level) {
                footer(level);
            }
        }
        for (int level = changed; level < levels; ++level) {
            const ReportGroup &g = m_spec.groups.at(level);
            const QVariant v = row.value(groupColumns.at(level));
            m_key[level] = isNull(v) ? s("(blank)") : m_source.display(groupColumns.at(level), v, m_currency);
            // Only the outermost group that changes may start a page.
            const bool breakPage = g.pageBreakBefore && !first && level == changed;
            out.bands.append({ReportBand::Kind::GroupHeader, level,
                              s("%1: %2").arg(g.label.isEmpty() ? label(groupColumns.at(level)) : g.label, m_key.at(level)),
                              {}, breakPage, false});
            if (level == levels - 1) {
                ++out.groupCount;
            }
        }
        for (int level = 0; level < levels; ++level) {
            add(acc[level], groupMeasures.at(level), row);
        }
        add(grand, grandMeasures, row);
        if (m_spec.detail) {
            QStringList cells;
            for (const int c : std::as_const(columns)) {
                cells.append(m_source.display(c, row.value(c), m_currency));
            }
            out.bands.append({ReportBand::Kind::Detail, levels - 1, {}, cells, false, false});
        }
        previous = key;
        first = false;
    }
    if (!first) {
        for (int level = levels - 1; level >= 0; --level) {
            footer(level);
        }
    }
    out.detailRows = int(order.size());
    if (order.isEmpty()) {
        out.bands.append({ReportBand::Kind::Empty, -1, s("No records match this report."), {}, false, false});
    }
    if (levels > 0 || !m_spec.totals.isEmpty()) {
        out.bands.append({ReportBand::Kind::ReportFooter, -1, totalsText(s("Report total"), grand, grandMeasures, -1, out),
                          {}, false, true});
    }
    if (!m_spec.footer.isEmpty()) {
        out.bands.append({ReportBand::Kind::ReportFooter, -1, m_spec.footer, {}, false, false});
    }
    out.ok = true;
    return out;
}

} // namespace

ReportRun runReport(const ReportSource &source, const ReportSpec &spec, const CurrencyFormat &currency)
{
    Runner runner(source, spec, currency);
    return runner.run();
}

} // namespace QindaOffice::Base
