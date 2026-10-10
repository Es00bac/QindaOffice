// SPDX-License-Identifier: GPL-3.0-or-later
// The report runner, part two: totals computed exactly (currency in integer
// minor units, averages rounded half away from zero) and M86's running
// sums — a detail column summing as the records go, and a group total's
// sum so far. See base_report.h.
#include "base_report_p.h"

#include <cmath>

namespace QindaOffice::Base::detail {

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
    return isWholeNumber(t) || t == FieldType::Currency || t == FieldType::YesNo;
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

} // namespace

Runner::Runner(const ReportSource &source, const ReportSpec &spec, const CurrencyFormat &currency)
    : m_source(source), m_spec(spec), m_currency(currency)
{
}

QString Runner::fail(const QString &error)
{
    if (m_error.isEmpty()) {
        m_error = error;
    }
    return m_error;
}

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
        const bool runs = t.running == QLatin1String("group") || t.running == QLatin1String("all");
        if (runs && t.function != QLatin1String("sum") && t.function != QLatin1String("countValues")) {
            fail(s("A running total adds sums or counts, not the %1 of %2.").arg(t.function, c.label));
            return false;
        }
        out->append({column, c.name, t.function, t.label.isEmpty() ? words.value(t.function).arg(c.label) : t.label,
                     runs ? t.running : s("none")});
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
        if (isNull(acc.lowest.at(m)) || compareValues(column, v, acc.lowest.at(m)) < 0) {
            acc.lowest[m] = v;
        }
        if (isNull(acc.highest.at(m)) || compareValues(column, v, acc.highest.at(m)) > 0) {
            acc.highest[m] = v;
        }
    }
}

TotalValue Runner::total(const Accumulator &acc, const Measure &m, int index) const
{
    TotalValue out;
    const FieldType type = m_source.columns.at(m.column).type;
    const qint64 n = acc.values.at(index);
    if (m.function == QLatin1String("countValues")) {
        out.value = n;
        out.exact = n;
        out.text = QString::number(n);
        out.empty = false;
        return out;
    }
    if (n == 0) {
        out.text = s("—");
        return out;
    }
    out.empty = false;
    if (m.function == QLatin1String("sum")) {
        if (integral(type)) {
            out.exact = acc.exact.at(index) + qint64(std::llround(acc.real.at(index)));
            out.value = out.exact;
            out.text = type == FieldType::Currency ? formatMoney(out.exact, m_currency.decimals, m_currency.symbol)
                                                   : QString::number(out.exact);
        } else {
            out.real = acc.real.at(index) + double(acc.exact.at(index));
            out.value = out.real;
            out.text = plainNumber(out.real);
        }
    } else if (m.function == QLatin1String("average")) {
        if (type == FieldType::Currency) {
            const qint64 average = roundedQuotient(acc.exact.at(index) + qint64(std::llround(acc.real.at(index))), n);
            out.value = average;
            out.text = formatMoney(average, m_currency.decimals, m_currency.symbol);
        } else {
            out.value = (double(acc.exact.at(index)) + acc.real.at(index)) / double(n);
            out.text = plainNumber(out.value.toDouble());
        }
    } else {
        out.value = m.function == QLatin1String("minimum") ? acc.lowest.at(index) : acc.highest.at(index);
        out.text = m_source.display(m.column, out.value, m_currency);
    }
    return out;
}

QString Runner::totalsText(const QString &lead, const Accumulator &acc, const QList<Measure> &measures, int level,
                           ReportRun &run)
{
    const QStringList key = level < 0 ? QStringList() : m_key.mid(0, level + 1);
    QStringList parts{lead, s("Count %1").arg(acc.rows)};
    run.totals.append({level, key, QString(), s("countRows"), acc.rows, QString::number(acc.rows)});
    for (int i = 0; i < measures.size(); ++i) {
        const Measure &m = measures.at(i);
        const TotalValue t = total(acc, m, i);
        parts.append(m.label + QLatin1Char(' ') + t.text);
        run.totals.append({level, key, m.field, m.function, t.value, t.text});
        if (level < 0 || m.running == QLatin1String("none")) {
            continue;
        }
        // M86: the sum so far of this total, over the enclosing group or
        // over the report (Access's Running Sum on a group footer).
        m_totalRunExact[level][i] += t.exact;
        m_totalRunReal[level][i] += t.real;
        const FieldType type = m_source.columns.at(m.column).type;
        const qint64 exact = m_totalRunExact.at(level).at(i) + qint64(std::llround(m_totalRunReal.at(level).at(i)));
        QString text;
        QVariant value;
        if (m.function == QLatin1String("countValues") || integral(type)) {
            value = exact;
            text = type == FieldType::Currency && m.function == QLatin1String("sum")
                       ? formatMoney(exact, m_currency.decimals, m_currency.symbol)
                       : QString::number(exact);
        } else {
            value = m_totalRunReal.at(level).at(i) + double(m_totalRunExact.at(level).at(i));
            text = plainNumber(value.toDouble());
        }
        parts.append(s("Running %1 %2").arg(m.label, text));
        run.totals.append({level, key, m.field, s("running:") + m.function, value, text});
    }
    return parts.join(s(" · "));
}

QString Runner::runningCell(int runIndex, const QVariantList &row)
{
    const int column = m_runColumns.at(runIndex);
    if (column < 0) {
        return QString::number(++m_runExact[runIndex]); // a count: the record's number
    }
    const QVariant v = row.value(column);
    const FieldType type = m_source.columns.at(column).type;
    if (!isNull(v)) {
        if (integral(type) && v.metaType().id() != QMetaType::Double) {
            m_runExact[runIndex] += v.toLongLong();
        } else if (type == FieldType::Currency) {
            m_runReal[runIndex] += v.toDouble() * std::pow(10.0, m_currency.decimals);
        } else {
            m_runReal[runIndex] += v.toDouble();
        }
    }
    const qint64 exact = m_runExact.at(runIndex) + qint64(std::llround(m_runReal.at(runIndex)));
    if (type == FieldType::Currency) {
        return formatMoney(exact, m_currency.decimals, m_currency.symbol);
    }
    if (integral(type)) {
        return QString::number(exact);
    }
    return plainNumber(m_runReal.at(runIndex) + double(m_runExact.at(runIndex)));
}

void Runner::resetRunningCells(bool groupChanged)
{
    for (int i = 0; i < m_runColumns.size() && groupChanged; ++i) {
        if (m_spec.runningSums.at(i).over == QLatin1String("group")) {
            m_runExact[i] = 0;
            m_runReal[i] = 0;
        }
    }
}

void Runner::resetRunningTotals(int fromLevel)
{
    for (int level = fromLevel; level < m_groupMeasures.size(); ++level) {
        for (int i = 0; i < m_groupMeasures.at(level).size(); ++i) {
            if (m_groupMeasures.at(level).at(i).running == QLatin1String("group")) {
                m_totalRunExact[level][i] = 0;
                m_totalRunReal[level][i] = 0;
            }
        }
    }
}

} // namespace QindaOffice::Base::detail
