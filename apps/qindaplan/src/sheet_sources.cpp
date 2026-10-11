// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_sources.h"

#include "custom_fields.h"
#include "plan_edit.h"
#include "plan_service.h"

namespace QindaOffice::Apps::Plan {

namespace {

const QString kNewRow = QStringLiteral("new");

int resourceRow(const PlanService *service, const QString &rowId)
{
    if (!rowId.startsWith(QLatin1Char('r'))) {
        return -1;
    }
    return service->project().resourceIndex(rowId.mid(1).toInt());
}

// The Indicators column shows one icon (Lucide names), most urgent first.
QVariantMap indicatorStyle(const Engine::Task &t)
{
    QVariantMap style{{QStringLiteral("horizontalAlignment"), QStringLiteral("center")}};
    // AGENT-NOTE: GridCellDelegate tones are positive (success), negative
    // (danger) and anything else (warning).
    if (!t.warning.isEmpty() || t.constraintConflict) {
        style.insert(QStringLiteral("icon"), QStringLiteral("triangle-alert"));
        style.insert(QStringLiteral("iconTone"), QStringLiteral("negative"));
    } else if (t.overallocated) {
        style.insert(QStringLiteral("icon"), QStringLiteral("user"));
        style.insert(QStringLiteral("iconTone"), QStringLiteral("negative"));
    } else if (t.deadline != Engine::kNoDate && t.finish != Engine::kNoDate && t.finish > t.deadline
               && t.percentComplete < 100) {
        // M63: finishes after its deadline (Project's missed-deadline flag).
        style.insert(QStringLiteral("icon"), QStringLiteral("flag"));
        style.insert(QStringLiteral("iconTone"), QStringLiteral("negative"));
    } else if (t.percentComplete >= 100) {
        style.insert(QStringLiteral("icon"), QStringLiteral("circle-check"));
        style.insert(QStringLiteral("iconTone"), QStringLiteral("positive"));
    } else if (t.constraint != Engine::ConstraintType::AsSoonAsPossible) {
        style.insert(QStringLiteral("icon"), QStringLiteral("calendar-clock"));
    } else if (t.recurring) {
        style.insert(QStringLiteral("icon"), QStringLiteral("repeat")); // M83: a recurring task
    } else if (!t.splits.isEmpty()) {
        style.insert(QStringLiteral("icon"), QStringLiteral("scissors")); // M83: a split task
    } else if (!t.notes.isEmpty()) {
        style.insert(QStringLiteral("icon"), QStringLiteral("sticky-note"));
    }
    return style;
}

} // namespace

TaskSheetSource::TaskSheetSource(PlanService *service)
    : QindaTK::GridCellSource(service)
    , m_service(service)
{
}

QVariant TaskSheetSource::value(const QString &rowId, const QString &columnId) const
{
    const int row = m_service->rowOf(rowId);
    if (row < 0 || columnId == QLatin1String("indicators")) {
        return QString();
    }
    if (columnId == QLatin1String("mode")) {
        // The mode column is an icon (pin = manual, arrows = auto), as in
        // Project; typing "Manual"/"Auto" still edits it (editValue).
        return QString();
    }
    return Engine::taskFieldText(m_service->project(), row, columnId, m_service->locale());
}

QVariant TaskSheetSource::editValue(const QString &rowId, const QString &columnId) const
{
    const int row = m_service->rowOf(rowId);
    if (row < 0) {
        return QString();
    }
    return Engine::taskFieldEditText(m_service->project(), row, columnId, m_service->locale());
}

QVariantMap TaskSheetSource::cellStyle(const QString &rowId, const QString &columnId) const
{
    const int row = m_service->rowOf(rowId);
    if (row < 0) {
        return {};
    }
    const Engine::Task &t = m_service->project().tasks.at(row);
    if (columnId == QLatin1String("indicators")) {
        return indicatorStyle(t);
    }
    QVariantMap style;
    if (t.summary) {
        style.insert(QStringLiteral("bold"), true);
    }
    // M89 (D-097): View ▸ Highlight marks the filter's tasks (the fill is
    // the theme's, handed in by the window).
    if (m_service->isHighlighted(row) && m_service->highlightFill().isValid()) {
        style.insert(QStringLiteral("fillColor"), m_service->highlightFill().name());
    }
    if (!t.effectiveActive) {
        style.insert(QStringLiteral("strikeout"), true);
        style.insert(QStringLiteral("italic"), true);
    }
    if (columnId == QLatin1String("name")) {
        // Grouped (M89), a task sits one level under its band's header.
        style.insert(QStringLiteral("indent"), m_service->groupBy() != QLatin1String("none") ? 1 : qMax(0, t.outlineLevel - 1));
        if (t.summary) {
            // The disclosure: ▾ open, ▸ collapsed (Lucide chevrons).
            style.insert(QStringLiteral("icon"), m_service->isCollapsed(t.uid) ? QStringLiteral("chevron-right")
                                                                               : QStringLiteral("chevron-down"));
            style.insert(QStringLiteral("iconTone"), QStringLiteral("muted"));
        } else if (t.isMilestone()) {
            style.insert(QStringLiteral("icon"), QStringLiteral("diamond"));
            style.insert(QStringLiteral("iconTone"), QStringLiteral("accent"));
        }
    } else if (columnId == QLatin1String("mode")) {
        style.insert(QStringLiteral("icon"), t.manual ? QStringLiteral("pin") : QStringLiteral("refresh-cw"));
        style.insert(QStringLiteral("iconTone"), t.manual ? QStringLiteral("accent") : QStringLiteral("muted"));
        style.insert(QStringLiteral("horizontalAlignment"), QStringLiteral("center"));
    }
    static const QStringList numeric{QStringLiteral("duration"), QStringLiteral("percent"), QStringLiteral("priority"),
                                     QStringLiteral("work"),
                                     QStringLiteral("cost"), QStringLiteral("fixedCost"), QStringLiteral("totalSlack"),
                                     QStringLiteral("freeSlack"), QStringLiteral("baselineCost"),
                                     QStringLiteral("costVariance"), QStringLiteral("actualCost"),
                                     QStringLiteral("remainingCost"), QStringLiteral("startVariance"),
                                     QStringLiteral("finishVariance"), QStringLiteral("durationVariance"),
                                     QStringLiteral("baselineDuration")};
    // M83: Number, Cost and Duration custom fields line up as numbers.
    const std::optional<Engine::CustomFieldInfo> custom = Engine::customFieldInfo(columnId);
    const bool customNumber = Engine::isCustomField(columnId) && custom
        && (custom->kind == Engine::CustomFieldKind::Number || custom->kind == Engine::CustomFieldKind::Cost
            || custom->kind == Engine::CustomFieldKind::Duration);
    if (numeric.contains(columnId) || customNumber) {
        style.insert(QStringLiteral("horizontalAlignment"), QStringLiteral("right"));
    }
    return style;
}

bool TaskSheetSource::commitChanges(const QString &label, const QVariantList &changes, QString *error)
{
    Q_UNUSED(label)
    const QString refused = m_service->commitSheetChanges(changes);
    if (!refused.isEmpty() && error) {
        *error = refused;
    }
    return refused.isEmpty();
}

ResourceSheetSource::ResourceSheetSource(PlanService *service)
    : QindaTK::GridCellSource(service)
    , m_service(service)
{
}

QVariant ResourceSheetSource::value(const QString &rowId, const QString &columnId) const
{
    const int row = resourceRow(m_service, rowId);
    if (row < 0 || columnId == QLatin1String("indicators")) {
        return QString();
    }
    return Engine::resourceFieldText(m_service->project(), row, columnId, m_service->locale());
}

QVariant ResourceSheetSource::editValue(const QString &rowId, const QString &columnId) const
{
    return value(rowId, columnId);
}

QVariantMap ResourceSheetSource::cellStyle(const QString &rowId, const QString &columnId) const
{
    const int row = resourceRow(m_service, rowId);
    if (row < 0) {
        return {};
    }
    const Engine::Resource &r = m_service->project().resources.at(row);
    QVariantMap style;
    if (columnId == QLatin1String("indicators") && r.overallocated) {
        style.insert(QStringLiteral("icon"), QStringLiteral("user"));
        style.insert(QStringLiteral("iconTone"), QStringLiteral("negative"));
        style.insert(QStringLiteral("horizontalAlignment"), QStringLiteral("center"));
    }
    if (r.overallocated && columnId == QLatin1String("name")) {
        style.insert(QStringLiteral("bold"), true);
        style.insert(QStringLiteral("error"), true);
    }
    static const QStringList numeric{QStringLiteral("maxUnits"), QStringLiteral("standardRate"),
                                     QStringLiteral("costPerUse"), QStringLiteral("work"), QStringLiteral("cost")};
    if (numeric.contains(columnId)) {
        style.insert(QStringLiteral("horizontalAlignment"), QStringLiteral("right"));
    }
    return style;
}

bool ResourceSheetSource::commitChanges(const QString &label, const QVariantList &changes, QString *error)
{
    Q_UNUSED(label)
    const QString refused = m_service->commitResourceSheetChanges(changes);
    if (!refused.isEmpty() && error) {
        *error = refused;
    }
    return refused.isEmpty();
}

} // namespace QindaOffice::Apps::Plan
