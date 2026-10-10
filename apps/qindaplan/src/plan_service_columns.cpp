// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: the task sheet's tables (Entry, Tracking, Variance, Cost,
// Schedule — Project's standard tables; Schedule also shows the Deadline and
// Priority Microsoft Project XML carries, M63; M79 adds Work, Baseline and
// Delay) and the column widths both sheets ask for (plan_service.h).
#include "custom_fields.h"
#include "plan_edit.h"
#include "plan_service.h"

namespace QindaOffice::Apps::Plan {

namespace {

struct TableDef {
    const char *id;
    const char *title;
    QList<const char *> fields;
};

const QList<TableDef> &tables()
{
    static const QList<TableDef> defs{
        {"entry", "Entry",
         {"indicators", "mode", "name", "duration", "start", "finish", "predecessors", "resources", "percent", "notes"}},
        // M79: Project's Tracking table (Act. Dur. and Rem. Dur. typed in place).
        {"tracking", "Tracking",
         {"indicators", "name", "actualStart", "actualFinish", "percent", "actualDuration", "remainingDuration",
          "actualCost", "actualWork"}},
        {"variance", "Variance",
         {"indicators", "name", "start", "finish", "baselineStart", "baselineFinish", "startVariance", "finishVariance"}},
        {"cost", "Cost",
         {"indicators", "name", "fixedCost", "cost", "baselineCost", "costVariance", "actualCost", "remainingCost"}},
        {"schedule", "Schedule",
         {"indicators", "wbs", "name", "start", "finish", "deadline", "lateStart", "lateFinish", "freeSlack",
          "totalSlack", "priority"}},
        // M79: Project's Work, Baseline and Delay (the Leveling Gantt's) tables.
        {"work", "Work",
         {"indicators", "name", "work", "baselineWork", "workVariance", "actualWork", "remainingWork", "percent"}},
        {"baseline", "Baseline",
         {"indicators", "name", "baselineDuration", "baselineStart", "baselineFinish", "baselineWork", "baselineCost"}},
        {"delay", "Delay",
         {"indicators", "name", "levelingDelay", "duration", "start", "finish", "successors", "resources"}},
        // M83: the custom fields in use (added in tableFields(), D-092).
        {"custom", "Custom Fields", {"indicators", "name"}},
    };
    return defs;
}

// Column width as a multiple of Tk.Theme.size.tableColumn (QML multiplies).
double widthFactor(const QString &field)
{
    if (field == QLatin1String("indicators")) {
        return 0.35;
    }
    if (field == QLatin1String("mode")) {
        return 0.35;
    }
    if (field == QLatin1String("name")) {
        return 2.9;
    }
    if (field == QLatin1String("resources") || field == QLatin1String("notes")) {
        return 1.9;
    }
    if (field == QLatin1String("successors")) {
        return 1.1;
    }
    if (field == QLatin1String("percent")) {
        return 1.0;
    }
    if (field == QLatin1String("id") || field == QLatin1String("wbs")) {
        return 0.8;
    }
    if (field == QLatin1String("predecessors") || field == QLatin1String("duration")) {
        return 1.1;
    }
    return 1.25;
}

} // namespace

QStringList PlanService::tableIds()
{
    QStringList out;
    for (const TableDef &t : tables()) {
        out.append(QLatin1String(t.id));
    }
    return out;
}

QStringList PlanService::tableFields() const
{
    for (const TableDef &t : tables()) {
        if (m_table == QLatin1String(t.id)) {
            QStringList out;
            for (const char *f : t.fields) {
                out.append(QLatin1String(f));
            }
            if (m_table == QLatin1String("custom")) {
                // M83: the fields the project uses, else a first few to type in.
                QStringList used = Engine::customFieldsInUse(project());
                if (used.isEmpty()) {
                    used = {QStringLiteral("Text1"), QStringLiteral("Number1"), QStringLiteral("Flag1"),
                            QStringLiteral("Date1"), QStringLiteral("Cost1")};
                }
                out += used;
            }
            return out;
        }
    }
    return {};
}

void PlanService::setTable(const QString &table)
{
    if (table == m_table || !tableIds().contains(table)) {
        return;
    }
    m_table = table;
    m_sheet->setColumnIds(tableFields());
    emit viewChanged();
}

QVariantList PlanService::sheetColumns() const
{
    QVariantList out;
    for (const QString &f : tableFields()) {
        const QString title = Engine::isCustomField(f) ? Engine::customFieldTitle(project(), f) // M83: its alias
                              : f == QLatin1String("indicators") || f == QLatin1String("mode") ? QString()
                                                                                               : Engine::taskFieldTitle(f);
        out.append(QVariantMap{{QStringLiteral("id"), f},
                               {QStringLiteral("title"), title},
                               {QStringLiteral("factor"), widthFactor(f)}});
    }
    return out;
}

QVariantList PlanService::resourceColumns() const
{
    QVariantList out;
    for (const QString &f : Engine::resourceFieldIds()) {
        if (f == QLatin1String("id") || f == QLatin1String("overallocated")) {
            continue;
        }
        const double factor = f == QLatin1String("name") ? 1.9 : f == QLatin1String("indicators") ? 0.35
                              : f == QLatin1String("email") ? 1.9 : f == QLatin1String("initials") ? 0.75
                              : f == QLatin1String("type") ? 0.85 : f == QLatin1String("maxUnits") ? 0.95
                              : f == QLatin1String("materialLabel") ? 1.25 : 1.05;
        out.append(QVariantMap{{QStringLiteral("id"), f},
                               {QStringLiteral("title"), f == QLatin1String("indicators") ? QString() : Engine::resourceFieldTitle(f)},
                               {QStringLiteral("factor"), factor}});
    }
    return out;
}

} // namespace QindaOffice::Apps::Plan
