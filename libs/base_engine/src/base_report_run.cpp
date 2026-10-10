// SPDX-License-Identifier: GPL-3.0-or-later
// The report runner, part three: the bands — headers, detail rows, footers
// with their totals, M86's running-sum columns, Force New Page and Keep
// Together. Group keys and the row order are base_report_keys.cpp, totals
// base_report_totals.cpp; the contract is base_report.h.
#include "base_report_p.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

} // namespace

namespace detail {

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
    // M86: running sums print as detail columns after the shown ones.
    for (const ReportRunningSum &r : m_spec.runningSums) {
        const int c = r.field.isEmpty() ? -1 : m_source.columnIndex(r.field);
        if (!r.field.isEmpty()
            && (c < 0 || !isNumeric(m_source.columns.at(c).type) || !m_source.columns.at(c).lookup.isEmpty())) {
            out.error = s("A running sum adds a number field; %1 is not one in %2.").arg(r.field, m_source.name);
            return out;
        }
        m_runColumns.append(c);
        out.columnLabels.append(!r.label.isEmpty() ? r.label : c < 0 ? s("No.") : s("Running sum of %1").arg(label(c)));
    }
    m_runExact = QList<qint64>(m_runColumns.size(), 0);
    m_runReal = QList<double>(m_runColumns.size(), 0.0);
    if (m_spec.groups.size() > kReportGroupLimit) {
        out.error = s("A report has at most %1 group levels.").arg(kReportGroupLimit);
        return out;
    }
    for (const ReportGroup &g : m_spec.groups) {
        const int i = m_source.columnIndex(g.field);
        if (i < 0) {
            out.error = s("The report groups on %1, which is not in %2.").arg(g.field, m_source.name);
            return out;
        }
        if (const QString problem = checkInterval(g, i); !problem.isEmpty()) {
            out.error = problem;
            return out;
        }
        m_groupColumns.append(i);
        m_groupMeasures.append(QList<Measure>());
        if (!measuresFrom(g.totals, &m_groupMeasures.last())) {
            out.error = m_error;
            return out;
        }
        m_totalRunExact.append(QList<qint64>(m_groupMeasures.last().size(), 0));
        m_totalRunReal.append(QList<double>(m_groupMeasures.last().size(), 0.0));
    }
    QList<Measure> grandMeasures;
    if (!measuresFrom(m_spec.totals, &grandMeasures)) {
        out.error = m_error;
        return out;
    }
    QHash<int, QList<GroupKey>> keys;
    QString orderError;
    const QList<int> order = orderRows(&keys, &orderError);
    if (!orderError.isEmpty()) {
        out.error = orderError;
        return out;
    }

    // M86: Force New Page — a band that must start a page carries
    // pageBreakBefore; detail rows get a spacer band carrying it instead.
    bool pending = false;
    const auto emitBand = [&](ReportBand band, bool before, bool after) {
        if (pending || before) {
            if (band.kind == ReportBand::Kind::Detail) {
                ReportBand spacer{ReportBand::Kind::Empty, band.level, {}, {}, true, false};
                spacer.spacer = true;
                out.bands.append(spacer);
            } else {
                band.pageBreakBefore = true;
            }
        }
        pending = after;
        out.bands.append(band);
        return int(out.bands.size()) - 1;
    };
    ReportBand title{ReportBand::Kind::ReportHeader, -1, m_spec.title, {}, false, false};
    title.keepTogether = m_spec.reportHeader.keepTogether;
    emitBand(title, false, false);
    QStringList info{s("Source: %1").arg(m_source.name), s("%1 record(s)").arg(order.size())};
    for (const QueryParameter &p : m_source.parameters) {
        info.append(s("%1 = %2").arg(p.prompt, m_source.parameterValues.value(p.name).toString()));
    }
    if (m_source.truncated) {
        info.append(s("the first %1 records of the source").arg(m_source.rows.size()));
    }
    emitBand({ReportBand::Kind::ReportInfo, -1, info.join(s(" · ")), {}, false, false}, false, m_spec.reportHeader.after());

    const int levels = int(m_groupColumns.size());
    QList<Accumulator> acc(levels);
    for (int level = 0; level < levels; ++level) {
        acc[level].reset(int(m_groupMeasures.at(level).size()));
    }
    Accumulator grand;
    grand.reset(int(grandMeasures.size()));
    QStringList previous; // group ids
    m_key = QStringList(levels);
    QList<int> headerAt(levels, -1);
    bool first = true;
    const auto footer = [&](int level) {
        const ReportGroup &g = m_spec.groups.at(level);
        const QString groupLabel = g.label.isEmpty() ? label(m_groupColumns.at(level)) : g.label;
        const QString lead = s("%1 %2 total").arg(groupLabel, m_key.at(level));
        ReportBand band{ReportBand::Kind::GroupFooter, level,
                        totalsText(lead, acc.at(level), m_groupMeasures.at(level), level, out), {}, false, true};
        band.keepTogether = g.footer.keepTogether;
        const int at = emitBand(band, g.footer.before(), g.footer.after());
        if (g.keepTogether == QLatin1String("wholeGroup") && headerAt.at(level) >= 0) {
            out.bands[headerAt.at(level)].keepWith = at; // the layout keeps [header, footer] on one page
        }
        acc[level].reset(int(m_groupMeasures.at(level).size()));
        resetRunningTotals(level + 1); // the inner levels' "over the group" sums restart
    };
    for (const int r : std::as_const(order)) {
        const QVariantList &row = m_source.rows.at(r);
        const QList<GroupKey> &rowKeys = keys.value(r);
        QStringList key;
        for (const GroupKey &k : rowKeys) {
            key.append(k.id);
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
        // Only the first header of a run of them may start a page (M80), and
        // it does when any header of the run asks for it (M86).
        bool breakRun = false;
        for (int level = changed; !first && level < levels; ++level) {
            breakRun = breakRun || m_spec.groups.at(level).header.before() || m_spec.groups.at(level).pageBreakBefore;
        }
        for (int level = changed; level < levels; ++level) {
            const ReportGroup &g = m_spec.groups.at(level);
            m_key[level] = rowKeys.at(level).text;
            ReportBand band{ReportBand::Kind::GroupHeader, level,
                            s("%1: %2").arg(g.label.isEmpty() ? label(m_groupColumns.at(level)) : g.label, m_key.at(level)),
                            {}, false, false};
            band.keepTogether = g.header.keepTogether;
            headerAt[level] = emitBand(band, breakRun && level == changed, g.header.after());
            if (level == levels - 1) {
                ++out.groupCount;
            }
        }
        resetRunningCells(changed < levels);
        for (int level = 0; level < levels; ++level) {
            add(acc[level], m_groupMeasures.at(level), row);
        }
        add(grand, grandMeasures, row);
        if (m_spec.detail) {
            QStringList cells;
            for (const int c : std::as_const(columns)) {
                cells.append(m_source.display(c, row.value(c), m_currency));
            }
            for (int i = 0; i < m_runColumns.size(); ++i) {
                cells.append(runningCell(i, row));
            }
            // A record follows its heading on the heading's page (Force New
            // Page Before on the detail section starts the later ones).
            const ReportBand::Kind last = out.bands.isEmpty() ? ReportBand::Kind::Empty : out.bands.last().kind;
            const bool headed = last == ReportBand::Kind::GroupHeader || last == ReportBand::Kind::ReportInfo;
            emitBand({ReportBand::Kind::Detail, levels - 1, {}, cells, false, false},
                     m_spec.detailSection.before() && !headed, m_spec.detailSection.after());
        } else {
            for (int i = 0; i < m_runColumns.size(); ++i) {
                runningCell(i, row); // a summary keeps the sums running
            }
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
        emitBand({ReportBand::Kind::Empty, -1, s("No records match this report."), {}, false, false}, false, false);
    }
    bool footerStarted = false;
    if (levels > 0 || !m_spec.totals.isEmpty()) {
        ReportBand band{ReportBand::Kind::ReportFooter, -1, totalsText(s("Report total"), grand, grandMeasures, -1, out),
                        {}, false, true};
        band.keepTogether = m_spec.reportFooter.keepTogether;
        emitBand(band, m_spec.reportFooter.before(), false);
        footerStarted = true;
    }
    if (!m_spec.footer.isEmpty()) {
        ReportBand band{ReportBand::Kind::ReportFooter, -1, m_spec.footer, {}, false, false};
        band.keepTogether = m_spec.reportFooter.keepTogether;
        emitBand(band, m_spec.reportFooter.before() && !footerStarted, false);
    }
    out.ok = true;
    return out;
}

} // namespace detail

ReportRun runReport(const ReportSource &source, const ReportSpec &spec, const CurrencyFormat &currency)
{
    detail::Runner runner(source, spec, currency);
    return runner.run();
}

} // namespace QindaOffice::Base
