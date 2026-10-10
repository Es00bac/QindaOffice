// SPDX-License-Identifier: GPL-3.0-or-later
// Reports evaluated by the engine, part one: a report's source records
// (a table, or a saved select query run with its parameters) and its spec
// read from the toolkit's report map. The run itself is base_report_run.cpp;
// the contract is base_report.h.
#include "base_report.h"

#include "base_input_mask.h"
#include "base_objects.h"
#include "base_records.h"

#include <cmath>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

// The text a stored value is looked up by (a REAL 3.0 and an INTEGER 3
// are the same key).
QString valueKey(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double && std::floor(v.toDouble()) == v.toDouble()
               ? QString::number(qint64(v.toDouble()))
               : v.toString();
}

QHash<QString, QString> lookupMap(const BaseFile &file, const LookupDef &lookup)
{
    QHash<QString, QString> out;
    if (lookup.isSet() && !lookup.displayField.isEmpty()) {
        for (const auto &[value, text] : Records::lookupChoices(file, lookup)) {
            out.insert(valueKey(value), text);
        }
    }
    return out;
}

ReportTotal totalFrom(const QVariantMap &m)
{
    return {m.value(s("field")).toString(), m.value(s("function"), s("sum")).toString(), m.value(s("label")).toString(),
            m.value(s("running"), s("none")).toString()};
}

ReportSection sectionFrom(const QVariant &v)
{
    const QVariantMap m = v.toMap();
    ReportSection out;
    out.newPage = m.value(s("newPage"), s("none")).toString();
    out.keepTogether = m.value(s("keepTogether")).toBool();
    return out;
}

ReportGroup groupFrom(const QVariant &v)
{
    const QVariantMap m = v.toMap();
    ReportGroup g;
    g.field = v.metaType().id() == QMetaType::QString ? v.toString() : m.value(s("field")).toString();
    g.label = m.value(s("label")).toString();
    g.pageBreakBefore = m.value(s("pageBreakBefore")).toBool();
    for (const QVariant &t : m.value(s("totals")).toList()) {
        g.totals.append(totalFrom(t.toMap()));
    }
    const QVariantMap interval = m.value(s("interval")).toMap();
    g.interval.by = interval.value(s("by"), s("value")).toString();
    g.interval.size = interval.value(s("size"), 1).toDouble();
    g.descending = m.value(s("descending")).toBool();
    g.keepTogether = m.value(s("keepTogether"), s("no")).toString();
    g.header = sectionFrom(m.value(s("header")));
    g.footer = sectionFrom(m.value(s("footer")));
    // M80's "a new page per group" is the header's Force New Page Before.
    if (g.pageBreakBefore && g.header.newPage == QLatin1String("none")) {
        g.header.newPage = s("before");
    }
    return g;
}

QStringList fieldsOf(const QVariantList &groups)
{
    QStringList out;
    for (const QVariant &g : groups) {
        out.append(g.metaType().id() == QMetaType::QString ? g.toString() : g.toMap().value(s("field")).toString());
    }
    return out;
}

// What the toolkit's design can hold of the levels: the leading ones while
// their fields are distinct, at most three, without M86's options.
QVariantList projection(const QVariantList &levels)
{
    QVariantList out;
    QStringList used;
    for (const QVariant &v : levels) {
        const QVariantMap level = v.toMap();
        const QString field = level.value(s("field")).toString();
        if (field.isEmpty() || used.contains(field, Qt::CaseInsensitive) || out.size() == 3) {
            break;
        }
        used.append(field);
        const QString newPage = level.value(s("header")).toMap().value(s("newPage")).toString();
        QVariantList totals;
        for (const QVariant &t : level.value(s("totals")).toList()) {
            QVariantMap total = t.toMap();
            total.remove(s("running"));
            totals.append(total);
        }
        QVariantMap g{{s("field"), field},
                      {s("pageBreakBefore"), level.value(s("pageBreakBefore")).toBool() || newPage == QLatin1String("before")
                                                 || newPage == QLatin1String("both")},
                      {s("totals"), totals}};
        if (!level.value(s("label")).toString().isEmpty()) {
            g.insert(s("label"), level.value(s("label")));
        }
        out.append(g);
    }
    return out;
}

} // namespace

QVariantList reportLevels(const QVariantMap &report)
{
    const QVariantList access = report.value(s("accessGroups")).toList();
    const QVariantList groups = report.value(s("groups")).toList();
    if (!access.isEmpty() && fieldsOf(projection(access)) == fieldsOf(groups)) {
        return access;
    }
    QVariantList out;
    for (const QVariant &g : groups) {
        QVariantMap level = g.metaType().id() == QMetaType::QString ? QVariantMap{{s("field"), g}} : g.toMap();
        // The toolkit's own designer changed the groups: its levels win, and
        // a level on the same field keeps its M86 options.
        for (const QVariant &a : access) {
            const QVariantMap known = a.toMap();
            if (known.value(s("field")).toString() == level.value(s("field")).toString()) {
                for (auto it = known.cbegin(); it != known.cend(); ++it) {
                    if (!level.contains(it.key())) {
                        level.insert(it.key(), it.value());
                    }
                }
                break;
            }
        }
        out.append(level);
    }
    // The toolkit's first-cut keys: one group with a sum.
    if (out.isEmpty() && !report.contains(s("groups")) && !report.value(s("groupField")).toString().isEmpty()) {
        QVariantList totals;
        if (const QString sum = report.value(s("sumField")).toString(); !sum.isEmpty()) {
            totals.append(QVariantMap{{s("field"), sum}, {s("function"), s("sum")}});
        }
        out.append(QVariantMap{{s("field"), report.value(s("groupField"))}, {s("totals"), totals}});
    }
    return out;
}

QVariantMap reportWithLevels(const QVariantMap &report, const QVariantList &levels)
{
    QVariantMap out = report;
    out.insert(s("accessGroups"), levels);
    out.insert(s("groups"), projection(levels));
    return out;
}

int ReportSource::columnIndex(const QString &name) const
{
    for (int i = 0; i < columns.size(); ++i) {
        if (columns.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString ReportSource::display(int column, const QVariant &stored, const CurrencyFormat &currency) const
{
    if (!stored.isValid() || stored.isNull() || column < 0 || column >= columns.size()) {
        return {};
    }
    const ReportColumn &c = columns.at(column);
    if (!c.lookup.isEmpty()) {
        return c.lookup.value(valueKey(stored), stored.toString());
    }
    const QString text = displayText(c.type, stored, currency);
    if (!c.inputMask.isEmpty() && (c.type == FieldType::Text || c.type == FieldType::LongText)) {
        if (const std::optional<InputMask> mask = InputMask::parse(c.inputMask)) {
            return maskedDisplay(*mask, text);
        }
    }
    return text;
}

std::optional<ReportSource> readReportSource(const BaseFile &file, const QString &kind, const QString &name,
                                             const QVariantMap &parameters, QString *error, int limit)
{
    ReportSource out;
    out.kind = kind;
    out.name = name;
    if (kind == QLatin1String("table")) {
        if (!file.tableExists(name)) {
            *error = s("There is no table named %1.").arg(name);
            return std::nullopt;
        }
        RowQuery query;
        query.limit = limit + 1;
        const RowSet set = Records::read(file, name, query);
        if (!set.error.isEmpty()) {
            *error = set.error;
            return std::nullopt;
        }
        for (const ColumnInfo &c : set.columns) {
            out.columns.append({c.name, c.label(), c.type, lookupMap(file, c.lookup), c.inputMask});
        }
        out.rows = set.values;
    } else if (kind == QLatin1String("query")) {
        const std::optional<SavedObject> q = ObjectStore::load(file, s("query"), name);
        if (!q) {
            *error = s("There is no query named %1.").arg(name);
            return std::nullopt;
        }
        // AGENT-NOTE: compiled with the values, so a crosstab's columns are
        // the headings of exactly the rows the report reads.
        const CompiledQuery c = compileQuery(file, QueryDesign::fromMap(q->definition), parameters);
        if (!c.ok) {
            *error = c.error;
            return std::nullopt;
        }
        if (c.action) {
            *error = s("A report needs a select query; %1 changes data.").arg(name);
            return std::nullopt;
        }
        const QueryResult result = runSelect(file, c.sql, c.parameters, parameters, limit + 1, c.columns);
        if (!result.ok) {
            *error = result.error;
            return std::nullopt;
        }
        for (const OutputColumn &col : result.columns) {
            ReportColumn rc{col.name, col.name, col.type, {}, {}};
            if (!col.sourceTable.isEmpty()) {
                for (const ColumnInfo &ci : Records::columns(file, col.sourceTable)) {
                    if (ci.name == col.sourceField) {
                        rc.lookup = lookupMap(file, ci.lookup);
                        rc.inputMask = ci.inputMask;
                    }
                }
            }
            out.columns.append(rc);
        }
        out.rows = result.rows;
        out.parameters = c.parameters;
        out.parameterValues = parameters;
    } else {
        *error = s("A report reads a table or a query.");
        return std::nullopt;
    }
    if (out.rows.size() > limit) {
        out.rows = out.rows.mid(0, limit);
        out.truncated = true;
    }
    return out;
}

ReportSpec ReportSpec::fromReportMap(const QVariantMap &report)
{
    ReportSpec spec;
    spec.title = report.value(s("headerText")).toString();
    spec.footer = report.value(s("footerText")).toString();
    spec.pageHeader = report.value(s("pageHeaderText")).toString();
    if (report.contains(s("pageFooterText"))) {
        spec.pageFooter = report.value(s("pageFooterText")).toString();
    }
    for (const QVariant &v : report.value(s("columns")).toList()) {
        const QVariantMap m = v.toMap();
        if (v.metaType().id() == QMetaType::QString) {
            spec.columns.append({v.toString(), QString()});
        } else if (!m.value(s("field")).toString().isEmpty()) {
            spec.columns.append({m.value(s("field")).toString(), m.value(s("label")).toString()});
        }
    }
    // M80's groups, or M86's levels (reportLevels: "accessGroups" while the
    // toolkit's "groups" is still their view; the first-cut groupField too).
    for (const QVariant &v : reportLevels(report)) {
        ReportGroup g = groupFrom(v);
        if (!g.field.isEmpty()) {
            spec.groups.append(g);
        }
    }
    const QVariantMap sections = report.value(s("sections")).toMap();
    spec.reportHeader = sectionFrom(sections.value(s("reportHeader")));
    spec.detailSection = sectionFrom(sections.value(s("detail")));
    spec.reportFooter = sectionFrom(sections.value(s("reportFooter")));
    for (const QVariant &v : report.value(s("runningSums")).toList()) {
        const QVariantMap m = v.toMap();
        spec.runningSums.append({m.value(s("field")).toString(), m.value(s("over"), s("all")).toString(),
                                 m.value(s("label")).toString()});
    }
    for (const QVariant &t : report.value(s("totals")).toList()) {
        spec.totals.append(totalFrom(t.toMap()));
    }
    spec.detail = report.value(s("detail"), true).toBool();
    const QVariantMap query = report.value(s("query")).toMap();
    for (const QVariant &v : query.value(s("sort")).toList()) {
        const QVariantMap m = v.toMap();
        const QString field = m.value(s("field"), m.value(s("fieldId"))).toString();
        if (!field.isEmpty()) {
            spec.sort.append({field, !m.value(s("ascending"), true).toBool()});
        }
    }
    const QVariantMap filter = query.value(s("filter")).toMap();
    if (filter.value(s("kind")).toString() == QLatin1String("compare")
        && filter.value(s("op")).toString() == QLatin1String("eq")) {
        spec.filterField = filter.value(s("field")).toString();
        const QVariantMap parameter = query.value(s("parameters")).toMap().value(filter.value(s("parameter")).toString()).toMap();
        spec.filterValue = parameter.value(s("value")).toString();
    } else if (!report.value(s("filterField")).toString().isEmpty()) {
        spec.filterField = report.value(s("filterField")).toString();
        spec.filterValue = report.value(s("filterValue")).toString();
    }
    const QVariantMap page = report.value(s("page")).toMap();
    if (!page.value(s("paperId")).toString().isEmpty()) {
        spec.paperId = page.value(s("paperId")).toString();
    }
    spec.landscape = page.value(s("orientation")).toString() == QLatin1String("landscape");
    return spec;
}

} // namespace QindaOffice::Base
