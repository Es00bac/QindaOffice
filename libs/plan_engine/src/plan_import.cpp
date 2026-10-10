// SPDX-License-Identifier: GPL-3.0-or-later
// Excel and CSV task import (plan_import.h).
#include "plan_import.h"

#include "custom_fields.h"
#include "outline.h"
#include "scheduler.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

QString cellOf(const QStringList &cells, int column)
{
    return column >= 0 && column < cells.size() ? cells.at(column) : QString();
}

// "Resource Names" → "resourcenames", "% Complete" → "complete".
QString squeeze(const QString &text)
{
    QString out;
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber()) {
            out.append(c);
        }
    }
    return out;
}

QChar sniffSeparator(const QString &text)
{
    int comma = 0;
    int semicolon = 0;
    int tab = 0;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && (c == QLatin1Char('\n') || c == QLatin1Char('\r'))) {
            break;
        } else if (!quoted) {
            comma += c == QLatin1Char(',');
            semicolon += c == QLatin1Char(';');
            tab += c == QLatin1Char('\t');
        }
    }
    return tab > comma && tab >= semicolon ? QLatin1Char('\t') : semicolon > comma ? QLatin1Char(';') : QLatin1Char(',');
}

// "3FS+2d, 5" with source IDs → the new tasks' IDs; unknown ones dropped.
QString translatePredecessors(const QString &text, const QHash<QString, int> &idOf, QStringList *unknown)
{
    static const QRegularExpression part(QStringLiteral("^\\s*(\\d+)(.*)$"));
    QStringList out;
    for (const QString &raw : text.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = part.match(raw);
        if (!m.hasMatch() || !idOf.contains(m.captured(1))) {
            unknown->append(raw.trimmed());
            continue;
        }
        out.append(QString::number(idOf.value(m.captured(1))) + m.captured(2).trimmed());
    }
    return out.join(QStringLiteral(", "));
}

} // namespace

ImportTable parseCsvTable(const QByteArray &data)
{
    QByteArray bytes = data;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (utf8.hasError()) {
        text = QString::fromLatin1(bytes);
    }
    const QChar separator = sniffSeparator(text);
    ImportTable table;
    QStringList row;
    QString field;
    bool quoted = false;
    bool any = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                field.append(c);
                ++i;
            } else if (c == QLatin1Char('"')) {
                quoted = false;
            } else {
                field.append(c);
            }
            continue;
        }
        if (c == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
            any = true;
        } else if (c == separator) {
            row.append(field);
            field.clear();
            any = true;
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            row.append(field);
            if (any || !field.isEmpty()) {
                table.rows.append(row);
            }
            row.clear();
            field.clear();
            any = false;
        } else {
            field.append(c);
            any = true;
        }
    }
    if (any || !field.isEmpty()) {
        row.append(field);
        table.rows.append(row);
    }
    return table;
}

QStringList importFieldIds()
{
    QStringList out{QString(),
                    QStringLiteral("id"),
                    QStringLiteral("name"),
                    QStringLiteral("outlineLevel"),
                    QStringLiteral("wbs"),
                    QStringLiteral("mode"),
                    QStringLiteral("duration"),
                    QStringLiteral("start"),
                    QStringLiteral("finish"),
                    QStringLiteral("predecessors"),
                    QStringLiteral("resources"),
                    QStringLiteral("percent"),
                    QStringLiteral("work"),
                    QStringLiteral("type"),
                    QStringLiteral("effortDriven"),
                    QStringLiteral("constraint"),
                    QStringLiteral("constraintDate"),
                    QStringLiteral("deadline"),
                    QStringLiteral("priority"),
                    QStringLiteral("fixedCost"),
                    QStringLiteral("milestone"),
                    QStringLiteral("active"),
                    QStringLiteral("notes")};
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        out.append(info.field);
    }
    return out;
}

QString importFieldTitle(const QString &field)
{
    if (field.isEmpty()) {
        return QStringLiteral("(do not import)");
    }
    if (field == QLatin1String("id")) {
        return QStringLiteral("ID");
    }
    if (field == QLatin1String("outlineLevel")) {
        return QStringLiteral("Outline Level");
    }
    if (field == QLatin1String("wbs")) {
        return QStringLiteral("WBS");
    }
    return isCustomField(field) ? field : taskFieldTitle(field);
}

QStringList guessImportMapping(const QStringList &headers, const Project *project)
{
    static const QHash<QString, QString> aliases{
        {QStringLiteral("taskid"), QStringLiteral("id")},
        {QStringLiteral("task"), QStringLiteral("name")},
        {QStringLiteral("name"), QStringLiteral("name")},
        {QStringLiteral("title"), QStringLiteral("name")},
        {QStringLiteral("level"), QStringLiteral("outlineLevel")},
        {QStringLiteral("outlinenumber"), QStringLiteral("wbs")},
        {QStringLiteral("taskmode"), QStringLiteral("mode")},
        {QStringLiteral("startdate"), QStringLiteral("start")},
        {QStringLiteral("begin"), QStringLiteral("start")},
        {QStringLiteral("finishdate"), QStringLiteral("finish")},
        {QStringLiteral("end"), QStringLiteral("finish")},
        {QStringLiteral("enddate"), QStringLiteral("finish")},
        {QStringLiteral("due"), QStringLiteral("finish")},
        {QStringLiteral("duedate"), QStringLiteral("finish")},
        {QStringLiteral("predecessor"), QStringLiteral("predecessors")},
        {QStringLiteral("dependencies"), QStringLiteral("predecessors")},
        {QStringLiteral("resources"), QStringLiteral("resources")},
        {QStringLiteral("resource"), QStringLiteral("resources")},
        {QStringLiteral("assignedto"), QStringLiteral("resources")},
        {QStringLiteral("complete"), QStringLiteral("percent")},
        {QStringLiteral("percentcomplete"), QStringLiteral("percent")},
        {QStringLiteral("progress"), QStringLiteral("percent")},
        {QStringLiteral("tasktype"), QStringLiteral("type")},
        {QStringLiteral("constraint"), QStringLiteral("constraint")},
        {QStringLiteral("note"), QStringLiteral("notes")},
        {QStringLiteral("comments"), QStringLiteral("notes")},
        {QStringLiteral("description"), QStringLiteral("notes")},
    };
    QHash<QString, QString> byName = aliases;
    for (const QString &field : importFieldIds()) {
        if (!field.isEmpty()) {
            byName.insert(squeeze(field), field);
            byName.insert(squeeze(importFieldTitle(field)), field);
        }
    }
    if (project != nullptr) {
        for (const CustomFieldDef &d : project->customFields) {
            if (!d.alias.isEmpty()) {
                byName.insert(squeeze(d.alias), d.field);
            }
        }
    }
    QStringList out;
    QStringList taken;
    for (const QString &header : headers) {
        const QString field = byName.value(squeeze(header));
        if (field.isEmpty() || taken.contains(field)) {
            out.append(QString());
            continue;
        }
        taken.append(field);
        out.append(field);
    }
    return out;
}

EditResult importTasks(Project &project, const ImportTable &table, const QStringList &mapping, bool firstRowHeaders,
                       ImportReport *report, const QLocale &locale)
{
    const int nameColumn = int(mapping.indexOf(QStringLiteral("name")));
    if (nameColumn < 0) {
        return EditResult::fail(QStringLiteral("Map one column to Task Name."));
    }
    Project copy = project;
    ImportReport out;
    struct Row {
        int source = 0; // the data row number, 1 = the first under the headers
        int index = -1;
        QStringList cells;
        int level = 1;
    };
    QList<Row> rows;
    const int levelColumn = int(mapping.indexOf(QStringLiteral("outlineLevel")));
    const int wbsColumn = int(mapping.indexOf(QStringLiteral("wbs")));
    int source = 0;
    for (int r = firstRowHeaders ? 1 : 0; r < table.rows.size(); ++r) {
        ++source;
        const QStringList &cells = table.rows.at(r);
        if (cellOf(cells, nameColumn).trimmed().isEmpty()) {
            const bool anything = std::any_of(cells.cbegin(), cells.cend(), [](const QString &c) { return !c.trimmed().isEmpty(); });
            if (anything) {
                out.warnings.append(QStringLiteral("Row %1 has no task name and was not imported.").arg(source));
            }
            continue;
        }
        Row row{source, -1, cells, 1};
        bool ok = false;
        const int level = cellOf(cells, levelColumn).trimmed().toInt(&ok);
        const QString wbs = cellOf(cells, wbsColumn).trimmed();
        row.level = levelColumn >= 0 && ok ? std::clamp(level, 1, 64) : wbs.isEmpty() ? 1 : int(wbs.count(QLatin1Char('.'))) + 1;
        rows.append(row);
    }
    if (rows.isEmpty()) {
        return EditResult::fail(QStringLiteral("No row has a task name to import."));
    }
    // The outline rule: the first task is at the top, each at most one
    // level deeper than the one before it.
    int lowest = 64;
    for (const Row &row : std::as_const(rows)) {
        lowest = std::min(lowest, row.level);
    }
    int previous = 0;
    for (Row &row : rows) {
        row.level = std::max(1, std::min(row.level - lowest + 1, previous + 1));
        previous = row.level;
        Task t;
        t.name = cellOf(row.cells, nameColumn).trimmed();
        t.duration = copy.info.minutesPerDay;
        t.estimated = true;
        t.type = copy.info.defaultTaskType;
        t.effortDriven = copy.info.newTasksEffortDriven || t.type == TaskType::FixedWork;
        row.index = insertTask(copy, int(copy.tasks.size()), t);
        copy.tasks[row.index].outlineLevel = row.level;
    }
    computeStructure(copy);
    const int idColumn = int(mapping.indexOf(QStringLiteral("id")));
    QHash<QString, int> idOf;
    for (const Row &row : std::as_const(rows)) {
        const QString key = idColumn >= 0 ? cellOf(row.cells, idColumn).trimmed() : QString::number(row.source);
        idOf.insert(key, copy.tasks.at(row.index).id);
    }
    const auto apply = [&](const QString &field) {
        const int column = int(mapping.indexOf(field));
        if (column < 0) {
            return;
        }
        static const QStringList fromSubtasks{QStringLiteral("duration"), QStringLiteral("work"), QStringLiteral("finish"),
                                              QStringLiteral("resources"), QStringLiteral("percent")};
        for (const Row &row : std::as_const(rows)) {
            QString text = cellOf(row.cells, column);
            if (text.trimmed().isEmpty() || (copy.tasks.at(row.index).summary && fromSubtasks.contains(field))) {
                continue; // Project's import too: a summary's come from its subtasks
            }
            if (field == QLatin1String("predecessors")) {
                QStringList unknown;
                text = translatePredecessors(text, idOf, &unknown);
                if (!unknown.isEmpty()) {
                    out.warnings.append(QStringLiteral("Row %1: no imported task for the predecessor %2.")
                                            .arg(row.source)
                                            .arg(unknown.join(QStringLiteral(", "))));
                }
            }
            const EditResult r = setTaskField(copy, row.index, field, text, locale);
            if (!r.ok) {
                out.warnings.append(QStringLiteral("Row %1, %2: %3").arg(row.source).arg(importFieldTitle(field), r.error));
            }
        }
    };
    // Typing order: what the later fields read first (the mode before the
    // dates, the duration before the resources share the work, a schedule
    // before % complete records actual dates, links last).
    for (const char *field : {"mode", "type", "effortDriven", "duration", "constraint", "constraintDate", "start", "finish",
                              "deadline", "priority", "fixedCost", "milestone", "active", "notes", "work", "resources"}) {
        apply(QLatin1String(field));
    }
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        apply(info.field);
    }
    schedule(copy);
    apply(QStringLiteral("percent"));
    apply(QStringLiteral("predecessors"));
    out.tasks = int(rows.size());
    project = copy;
    if (report != nullptr) {
        *report = out;
    }
    return {};
}

} // namespace QindaOffice::Plan
