// SPDX-License-Identifier: GPL-3.0-or-later
// Calculated fields belong to a shared pivot cache. Every dependent pivot
// updates atomically, and the complete operation occupies one undo step.
#include "calc_host.h"
#include "calc_undo.h"
#include "pivot_calculated.h"
#include "pivot_cache.h"
#include "sheet.h"
namespace QindaOffice::Apps::QindaCalc {
using namespace CalcEng;
bool SpreadsheetDocument::setPivotCalculatedField(const PivotRef &ref, const QString &name, const QString &formula)
{
    const PivotTable *pivot = pivotOf(m_workbook, ref);
    if (pivot == nullptr || !pivot->modelled || name.isEmpty() || name.size() > 255 || formula.trimmed().isEmpty()) return false;
    const qint32 id = pivot->cacheId;
    for (qint32 s = 0; s < m_workbook.sheetCount(); ++s) {
        const Sheet &sheet = m_workbook.sheetAt(s);
        if (sheet.protection().enabled && sheet.protection().pivotTables) {
            for (const auto &p : sheet.pivots()) if (p.cacheId == id) {
                emit editRefused(tr("Unprotect the sheets containing this cache's PivotTables first."));
                return false;
            }
        }
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    PivotCache *cache = nullptr;
    for (auto &candidate : m_workbook.pivotCaches) if (candidate.id == id) cache = &candidate;
    if (cache == nullptr) return false;
    qint32 field = -1;
    for (qsizetype f = 0; f < cache->fields.size(); ++f) if (cache->fields.at(f).name.compare(name, Qt::CaseInsensitive) == 0) {
        if (cache->fields.at(f).formula.isEmpty()) { emit editRefused(tr("A source field already has that name.")); return false; }
        field = qint32(f);
    }
    if (field < 0) { field = qint32(cache->fields.size()); cache->fields.append(PivotCacheField{}); }
    cache->fields[field].name = name;
    cache->fields[field].formula = formula.trimmed();
    cache->fields[field].database = false;
    QString error = pivotCalculatedProblem(*cache, field);
    if (error.isEmpty()) {
        auto &active = m_workbook.sheetAt(ref.sheet).mutablePivots()[ref.index];
        bool present = false;
        for (const auto &d : active.dataFields) present = present || d.field == field;
        if (!present) {
            PivotDataField data;
            data.field = field;
            data.name = tr("Sum of %1").arg(name);
            active.dataFields.append(data);
        }
        for (qint32 s = 0; s < m_workbook.sheetCount() && error.isEmpty(); ++s) {
            auto &pivots = m_workbook.sheetAt(s).mutablePivots();
            for (qsizetype p = 0; p < pivots.size() && error.isEmpty(); ++p) if (pivots.at(p).cacheId == id) {
                pivotNormalize(pivots[p], *cache);
                if (!renderPivotTable(m_workbook, {s, qint32(p)}, &error)) break;
            }
        }
    }
    if (!error.isEmpty()) { restoreSnapshot(&m_workbook, before); emit editRefused(error); return false; }
    return finishPivotEdit(before, true, tr("Calculated Field"), {});
}
QVariantList CalcHost::pivotCalculatedFields() const
{
    QVariantList out;
    const auto *pivot = pivotOf(*m_document->workbook(), currentPivot());
    const auto *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    if (cache != nullptr) for (const auto &field : cache->fields)
        out.append(QVariantMap{{QStringLiteral("name"), field.name}, {QStringLiteral("formula"), field.formula}});
    return out;
}
QString CalcHost::applyPivotCalculatedField(const QString &name, const QString &formula)
{
    QString error;
    const auto connection = connect(m_document, &SpreadsheetDocument::editRefused, this, [&error](const QString &message) { error = message; });
    const bool done = m_document->setPivotCalculatedField(currentPivot(), name.trimmed(), formula);
    disconnect(connection);
    refreshGrid();
    emit pivotChanged();
    return done ? QString() : (error.isEmpty() ? tr("Enter a field name and formula.") : error);
}
}
