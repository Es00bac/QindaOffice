// SPDX-License-Identifier: GPL-3.0-or-later
// Carry phases 1 and 2 (xlsx_carry_phases.h, M66, D-075): pivot tables,
// then the workbook-level parts and the workbook elements that name them.

#include "xlsx_carry_phases.h"

#include "xlsx_carry_types.h"
#include "xlsx_carry_xml.h"
#include "xlsx_parts.h"
#include "xlsx_write_features.h"
#include "xlsx_xml.h"

#include "ooxml_namespaces.h"

#include <QObject>

namespace QindaOffice::Calc::IO::Carry {

namespace {

const QString kWorkbookPart = QStringLiteral("xl/workbook.xml");

QString sheetIdOf(const Context &ctx, const QString &sourcePart)
{
    for (const SourceSheet &sheet : ctx.sourceSheets()) {
        if (sheet.part == sourcePart) {
            return sheet.sheetId;
        }
    }
    return {};
}

// The first value of `attribute` on a start tag named `element`.
QString firstAttribute(const QByteArray &xml, const QString &element, const QString &attribute)
{
    QString value;
    bool found = false;
    static_cast<void>(editStartTags(xml, element, [&](const QString &tag) {
        if (!found) {
            value = tagAttribute(tag, attribute);
            found = true;
        }
        return tag;
    }));
    return value;
}

// refreshOnLoad="1" (Excel recomputes the cache from the edited data) —
// for a cache the model holds, only when its source data changed (M74,
// D-083: an untouched pivot is written back byte for byte) — and a
// worksheet source that follows its sheet's rename and moved rows.
QByteArray patchedCache(const Context &ctx, const QString &part, const QByteArray &xml)
{
    const PatchManifest &manifest = ctx.manifest();
    const bool refresh = !manifest.untouchedCacheParts.contains(part) || manifest.staleCacheParts.contains(part);
    QByteArray out = !refresh ? xml : editStartTags(xml, QStringLiteral("pivotCacheDefinition"), [](const QString &tag) {
        return withTagAttribute(tag, QStringLiteral("refreshOnLoad"), QStringLiteral("1"));
    });
    return editStartTags(out, QStringLiteral("worksheetSource"), [&ctx](const QString &tag) {
        const QString sheet = tagAttribute(tag, QStringLiteral("sheet"));
        for (const SourceSheet &source : ctx.sourceSheets()) {
            if (sheet.isEmpty() || source.name.compare(sheet, Qt::CaseInsensitive) != 0) {
                continue; // a named source (a table, a name) follows by itself
            }
            const DestSheet *dest = ctx.destForSource(source.part);
            if (dest == nullptr || dest->patch == nullptr) {
                return tag; // the data sheet is gone: Excel reports it on refresh
            }
            QString moved = dest->name == sheet ? tag : withTagAttribute(tag, QStringLiteral("sheet"), dest->name);
            const auto range = rangeFromText(tagAttribute(tag, QStringLiteral("ref")));
            if (range.has_value()) {
                if (const auto mapped = mapSourceRange(dest->patch->source.steps, *range); mapped && *mapped != *range) {
                    moved = withTagAttribute(moved, QStringLiteral("ref"), rangeText(*mapped));
                }
            }
            return moved;
        }
        return tag;
    });
}

// A slicer or timeline cache survives when every pivot table it filters was
// kept, and every table it filters was written.
bool cacheTargetsKept(const Context &ctx, const CarryState &state, const QByteArray &xml, const QHash<QString, QString> &tableIds)
{
    bool kept = true;
    static_cast<void>(editStartTags(xml, QStringLiteral("pivotTable"), [&](const QString &tag) {
        kept = kept && state.keptPivots.contains(tagAttribute(tag, QStringLiteral("tabId")) + QLatin1Char('|')
                                                 + tagAttribute(tag, QStringLiteral("name")));
        return tag;
    }));
    static_cast<void>(editStartTags(xml, QStringLiteral("tableSlicerCache"), [&](const QString &tag) {
        kept = kept && tableIds.contains(tagAttribute(tag, QStringLiteral("tableId")));
        return tag;
    }));
    Q_UNUSED(ctx);
    return kept;
}

// tabId is the sheetId of the pivot table's sheet; tableId a table's id.
QByteArray remappedCache(const Context &ctx, const QByteArray &xml, const QHash<QString, QString> &tableIds)
{
    QByteArray out = editStartTags(xml, QStringLiteral("pivotTable"), [&ctx](const QString &tag) {
        const SourceSheet *source = ctx.sourceSheetById(tagAttribute(tag, QStringLiteral("tabId")));
        const DestSheet *dest = source != nullptr ? ctx.destForSource(source->part) : nullptr;
        return dest != nullptr ? withTagAttribute(tag, QStringLiteral("tabId"), dest->sheetId) : tag;
    });
    return editStartTags(out, QStringLiteral("tableSlicerCache"), [&tableIds](const QString &tag) {
        const QString id = tableIds.value(tagAttribute(tag, QStringLiteral("tableId")));
        return id.isEmpty() ? tag : withTagAttribute(tag, QStringLiteral("tableId"), id);
    });
}

// Every relationship id in `element` through `ids`; false when one has no
// carried counterpart (the element is then left out).
bool remapIds(Ooxml::XmlElement &element, const QHash<QString, QString> &ids)
{
    for (Ooxml::XmlAttribute &attribute : element.attributes) {
        if (attribute.ns == QStringView(Ooxml::Ns::R)) {
            const QString id = ids.value(attribute.value);
            if (id.isEmpty()) {
                return false;
            }
            attribute.value = id;
        }
    }
    for (Ooxml::XmlChild &child : element.children) {
        if (child.element && !remapIds(*child.element, ids)) {
            return false;
        }
    }
    return true;
}

// <externalReferences>, <pivotCaches> and <extLst> of the source workbook,
// with the relationship ids this phase made.
void writeWorkbookElements(Context &ctx, const QHash<QString, QString> &ids)
{
    const Ooxml::XmlElementPtr root = parse(ctx.source().data(ctx.sourceWorkbook()));
    if (!root) {
        return;
    }
    const QString s = QString::fromUtf16(Ooxml::Ns::S);
    const QString r = QString::fromUtf16(Ooxml::Ns::R);
    QByteArray workbook = ctx.part(kWorkbookPart);
    if (const Ooxml::XmlElement *references = root->child(s, u"externalReferences")) {
        QByteArray xml;
        for (const Ooxml::XmlElement *reference : references->elements(s, u"externalReference")) {
            const QString id = ids.value(reference->attribute(r, u"id"));
            if (id.isEmpty()) {
                // A formula's [n] counts these: one missing would shift the rest.
                ctx.warn(QObject::tr("An external link could not be kept; formulas that use later links may point at the wrong workbook."));
                break;
            }
            xml += QByteArrayLiteral("<externalReference r:id=\"") + id.toUtf8() + QByteArrayLiteral("\"/>");
        }
        // Directly after </sheets>: before the definedNames written there.
        if (!xml.isEmpty()) {
            static_cast<void>(insertAfterTag(&workbook, QStringLiteral("sheets"),
                                             QByteArrayLiteral("<externalReferences>") + xml + QByteArrayLiteral("</externalReferences>")));
        }
    }
    if (const Ooxml::XmlElement *caches = root->child(s, u"pivotCaches")) {
        QByteArray xml;
        for (const Ooxml::XmlElement *cache : caches->elements(s, u"pivotCache")) {
            const QString id = ids.value(cache->attribute(r, u"id"));
            if (!id.isEmpty()) {
                xml += QByteArrayLiteral("<pivotCache cacheId=\"") + xmlAttribute(cache->attr(u"cacheId"))
                    + QByteArrayLiteral("\" r:id=\"") + id.toUtf8() + QByteArrayLiteral("\"/>");
            }
        }
        const qsizetype calc = workbook.indexOf("<calcPr");
        const qsizetype calcEnd = calc < 0 ? -1 : workbook.indexOf('>', calc);
        const qsizetype written = workbook.indexOf("</pivotCaches>");
        if (!xml.isEmpty() && written > 0) {
            workbook.insert(written, xml); // the pivots written from the model have the element (M74)
        } else if (!xml.isEmpty() && calcEnd > 0) {
            // After <calcPr …/> (oleSize and customWorkbookViews are never written).
            workbook.insert(calcEnd + 1, QByteArrayLiteral("<pivotCaches>") + xml + QByteArrayLiteral("</pivotCaches>"));
        }
    }
    if (const Ooxml::XmlElement *extensions = root->child(s, u"extLst")) {
        QByteArray xml;
        for (const Ooxml::XmlElement *ext : extensions->elements(s, u"ext")) {
            const Ooxml::XmlElementPtr copy = ext->clone();
            if (remapIds(*copy, ids)) {
                xml += Ooxml::serializeFragment(*copy).toUtf8();
            }
        }
        if (!xml.isEmpty()) {
            static_cast<void>(insertBeforeAny(&workbook, {QStringLiteral("/workbook")},
                                              QByteArrayLiteral("<extLst>") + xml + QByteArrayLiteral("</extLst>")));
        }
    }
    ctx.setPart(kWorkbookPart, workbook);
}

} // namespace

QString sourceTarget(const Ooxml::Package &source, const QString &owner, const QString &kind)
{
    const QStringList targets = sourceTargets(source, owner, kind);
    return targets.isEmpty() ? QString() : targets.first();
}

QStringList sourceTargets(const Ooxml::Package &source, const QString &owner, const QString &kind)
{
    QStringList out;
    for (const Ooxml::Relationship &rel : source.relationships(owner)) {
        if (!rel.external && relKind(rel.type) == kind) {
            out.append(source.targetPart(owner, rel));
        }
    }
    return out;
}

void carryPivotTables(Context &ctx, CarryState &state)
{
    for (const DestSheet &dest : ctx.destSheets()) {
        if (dest.patch == nullptr || !ctx.source().contains(dest.patch->source.part)) {
            continue;
        }
        const QString sourceSheet = dest.patch->source.part;
        for (const Ooxml::Relationship &rel : ctx.source().relationships(sourceSheet)) {
            if (rel.external || relKind(rel.type) != QLatin1String("pivotTable")) {
                continue;
            }
            const QString sourcePivot = ctx.source().targetPart(sourceSheet, rel);
            const QByteArray xml = ctx.source().data(sourcePivot);
            const QString name = firstAttribute(xml, QStringLiteral("pivotTableDefinition"), QStringLiteral("name"));
            if (ctx.manifest().source.pivotParts.contains(sourcePivot)
                && !ctx.manifest().modelPivotParts.contains(sourcePivot)) {
                continue; // deleted in QindaCalc (M74, D-083)
            }
            if (const auto replaced = ctx.manifest().replacedPivotParts.constFind(sourcePivot);
                replaced != ctx.manifest().replacedPivotParts.constEnd()) {
                // Written from the model (M74, D-083). A slicer that filters
                // it stays when its cache kept its items and it kept its name.
                const QString cache = sourceTarget(ctx.source(), sourcePivot, QStringLiteral("pivotCacheDefinition"));
                if (ctx.manifest().sameItemsCacheParts.contains(cache) && replaced.value() == name) {
                    state.keptPivots.insert(sheetIdOf(ctx, sourceSheet) + QLatin1Char('|') + name);
                }
                continue;
            }
            const auto location = rangeFromText(firstAttribute(xml, QStringLiteral("location"), QStringLiteral("ref")));
            const auto moved = location ? mapSourceRange(dest.patch->source.steps, *location) : std::nullopt;
            if (!moved) {
                ctx.warn(QObject::tr("PivotTable “%1” on “%2” was not kept: the cells it stood on were deleted.")
                             .arg(name, dest.name));
                continue;
            }
            if (ctx.copyRelationship(sourceSheet, rel.id, dest.part).isEmpty()) {
                continue;
            }
            ctx.replaceCopied(ctx.copyPart(sourcePivot),
                              *moved == *location ? xml
                                                  : editStartTags(xml, QStringLiteral("location"), [&moved](const QString &tag) {
                                                        return withTagAttribute(tag, QStringLiteral("ref"), rangeText(*moved));
                                                    }));
            state.keptPivots.insert(sheetIdOf(ctx, sourceSheet) + QLatin1Char('|') + name);
            for (const QString &cache : sourceTargets(ctx.source(), sourcePivot, QStringLiteral("pivotCacheDefinition"))) {
                state.keptCacheParts.insert(cache);
                ctx.replaceCopied(ctx.copyPart(cache), patchedCache(ctx, cache, ctx.source().data(cache)));
            }
        }
    }
}

void carryWorkbookParts(Context &ctx, CarryState &state)
{
    const Ooxml::Package &source = ctx.source();
    const QString workbook = ctx.sourceWorkbook();
    // Source table id → written table id, for table slicers.
    QHash<QString, QString> tableIds;
    for (auto it = state.sourceToDestTable.constBegin(); it != state.sourceToDestTable.constEnd(); ++it) {
        const QString from = firstAttribute(source.data(it.key()), QStringLiteral("table"), QStringLiteral("id"));
        const QString to = firstAttribute(ctx.part(it.value()), QStringLiteral("table"), QStringLiteral("id"));
        if (!from.isEmpty() && !to.isEmpty()) {
            tableIds.insert(from, to);
        }
    }
    QHash<QString, QString> ids; // source workbook relationship id → new id
    for (const Ooxml::Relationship &rel : source.relationships(workbook)) {
        if (rel.external || workbookRelWritten(rel.type) || isRichDataRel(rel.type)) {
            continue;
        }
        const QString kind = relKind(rel.type);
        const QString target = source.targetPart(workbook, rel);
        if (target.isEmpty() || !source.contains(target)) {
            continue; // refused when the workbook was read (and said so then)
        }
        if (kind == QLatin1String("pivotCacheDefinition") && !state.keptCacheParts.contains(target)) {
            continue; // no kept pivot table uses it
        }
        if (kind == QLatin1String("vbaProject") && !ctx.manifest().macroEnabled) {
            ctx.warn(QObject::tr("The workbook's macros (its VBA project) cannot be kept in an .xlsx file; save it as an "
                                 "Excel Macro-Enabled Workbook (.xlsm) to keep them."));
            continue;
        }
        const bool slicerCache = kind == QLatin1String("slicerCache") || kind == QLatin1String("timelineCache");
        if (slicerCache && !cacheTargetsKept(ctx, state, source.data(target), tableIds)) {
            ctx.warn(QObject::tr("A slicer or timeline was not kept: the PivotTable or table it filters is gone."));
            continue;
        }
        const QString id = ctx.copyRelationship(workbook, rel.id, kWorkbookPart);
        if (id.isEmpty()) {
            continue;
        }
        ids.insert(rel.id, id);
        if (slicerCache) {
            ctx.replaceCopied(ctx.copyPart(target), remappedCache(ctx, source.data(target), tableIds));
            QString name = firstAttribute(source.data(target), QStringLiteral("slicerCacheDefinition"), QStringLiteral("name"));
            if (name.isEmpty()) {
                name = firstAttribute(source.data(target), QStringLiteral("timelineCacheDefinition"), QStringLiteral("name"));
            }
            state.keptSlicerCaches.insert(name);
        }
    }
    writeWorkbookElements(ctx, ids);
    // The package's own: the ribbon customisation, the custom properties.
    for (const Ooxml::Relationship &rel : source.relationships(QString())) {
        if (!rel.external && packageRelCarried(rel.type)) {
            static_cast<void>(ctx.copyRelationship(QString(), rel.id, QString()));
        }
    }
}

} // namespace QindaOffice::Calc::IO::Carry
