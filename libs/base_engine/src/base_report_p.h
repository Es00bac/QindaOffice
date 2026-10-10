// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The report runner (internal to base_report_run.cpp, base_report_keys.cpp
// and base_report_totals.cpp; split in M86 so each file keeps to the size
// rule): one run over a report source — filter, Group, Sort and Total with
// M86's intervals, more levels, running sums and page control — into bands.
// The contract is base_report.h.

#include "base_report.h"

namespace QindaOffice::Base::detail {

// A row's group value at one level (after its interval).
struct GroupKey {
    bool null = false;
    QVariant sort; // a qint64, a double or text: what the groups are ordered by
    QString id;    // equal ids are one group
    QString text;  // as the group's header and footer print it
};

struct Measure {
    int column = -1; // -1: countRows
    QString field;
    QString function;
    QString label;
    QString running; // M86: none | group | all
};

struct Accumulator {
    qint64 rows = 0;
    QList<qint64> values;   // non-null values per measure
    QList<qint64> exact;    // integral sums
    QList<double> real;     // decimal sums
    QList<QVariant> lowest; // per measure
    QList<QVariant> highest;
    void reset(int measures);
};

// A total as computed: its printed text and its value (exact parts kept for
// running sums).
struct TotalValue {
    QString text;
    QVariant value;
    qint64 exact = 0;
    double real = 0;
    bool empty = true; // no value (prints "—")
};

class Runner {
public:
    Runner(const ReportSource &source, const ReportSpec &spec, const CurrencyFormat &currency);
    ReportRun run();

private:
    QString fail(const QString &error);
    [[nodiscard]] QString label(int column) const { return m_source.columns.at(column).label; }

    // base_report_keys.cpp
    [[nodiscard]] QString checkInterval(const ReportGroup &group, int column) const;
    [[nodiscard]] GroupKey keyOf(int level, const QVariant &value) const;
    [[nodiscard]] int compareValues(int column, const QVariant &a, const QVariant &b) const;
    [[nodiscard]] static int compareKeys(const GroupKey &a, const GroupKey &b);
    // The rows the report prints, filtered and in order; `keys` receives
    // each row's group keys (by source row).
    [[nodiscard]] QList<int> orderRows(QHash<int, QList<GroupKey>> *keys, QString *error) const;

    // base_report_totals.cpp
    bool measuresFrom(const QList<ReportTotal> &totals, QList<Measure> *out);
    void add(Accumulator &acc, const QList<Measure> &measures, const QVariantList &row) const;
    [[nodiscard]] TotalValue total(const Accumulator &acc, const Measure &m, int index) const;
    QString totalsText(const QString &lead, const Accumulator &acc, const QList<Measure> &measures, int level,
                       ReportRun &run);
    // M86: running sums — detail cells, and the running group totals.
    QString runningCell(int runIndex, const QVariantList &row); // also advances the sum
    void resetRunningCells(bool groupChanged);
    void resetRunningTotals(int fromLevel);

    const ReportSource &m_source;
    const ReportSpec &m_spec;
    const CurrencyFormat &m_currency;
    QString m_error;
    QStringList m_key;                       // the current group values as printed
    QList<int> m_groupColumns;
    QList<QList<Measure>> m_groupMeasures;
    QList<int> m_runColumns;                 // per running-sum column: its source column (-1: a count)
    QList<qint64> m_runExact;
    QList<double> m_runReal;
    QList<QList<qint64>> m_totalRunExact;    // per level, per measure
    QList<QList<double>> m_totalRunReal;
};

} // namespace QindaOffice::Base::detail
