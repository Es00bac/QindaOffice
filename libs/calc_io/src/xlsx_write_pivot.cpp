// SPDX-License-Identifier: GPL-3.0-or-later
// PivotTables into the saved package (xlsx_write_pivot.h; M74, D-083):
// the plan (what is written from the model, what travels as it was) and
// the post-pass stage that writes caches, records and pivot tables with
// their relationships, content types and the workbook's <pivotCaches>.
//
// AGENT-NOTE: a cache and the pivots on it travel together. Untouched as
// read — cache and every pivot equal to their fingerprints from the open —
// they are carried byte for byte (xlsx_carry_book.cpp), refreshOnLoad
// added only when the source data no longer matches the cache. Anything
// else — a new pivot, an edited one, a refresh — writes the whole group
// from the model, keeping the source's cacheId, names and cache extLst so
// slicers that filter it can stay.

#include "xlsx_write_pivot.h"

#include "xlsx_xml.h"

#include "number_format.h"
#include "pivot_cache.h"
#include "pivot_edit.h"
#include "sheet.h"

#include <QObject>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

const QString kRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
const QString kType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.");

QString uniquePart(const QHash<QString, QByteArray> &parts, const QString &stem)
{
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("%1%2.xml").arg(stem).arg(n);
        if (!parts.contains(name)) {
            return name;
        }
    }
}

QString relsPathFor(const QString &part)
{
    const qsizetype slash = part.lastIndexOf(QLatin1Char('/'));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash) + QStringLiteral(".rels");
}

// A target relative to the owner's directory ("xl/worksheets" →
// "../pivotTables/pivotTable1.xml").
QString relativeTarget(const QString &owner, const QString &part)
{
    const QString ownerDir = owner.left(owner.lastIndexOf(QLatin1Char('/')));
    if (part.startsWith(ownerDir + QLatin1Char('/'))) {
        return part.mid(ownerDir.size() + 1);
    }
    return QStringLiteral("../") + part.mid(part.indexOf(QLatin1Char('/')) + 1);
}

bool link(QHash<QString, QByteArray> *parts, const QString &owner, const QString &type, const QString &target,
          QString *id, QString *error)
{
    QByteArray rels = parts->value(relsPathFor(owner));
    *id = appendRelationship(&rels, kRel + type, relativeTarget(owner, target));
    if (id->isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("pivot tables: broken rels part for %1").arg(owner);
        }
        return false;
    }
    parts->insert(relsPathFor(owner), rels);
    return true;
}

// numFmtId for a format code in the written styles part (QXlsx's ids for
// custom codes, the built-in ids otherwise); 0 (General) when absent.
PivotFormatId formatIds(const QByteArray &styles)
{
    QHash<QString, int> ids;
    for (int id = 1; id < 50; ++id) {
        const QString code = NumberFormat::builtinCode(id);
        if (code != QLatin1String("General") && !ids.contains(code)) {
            ids.insert(code, id);
        }
    }
    QXmlStreamReader reader(styles);
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == QLatin1String("numFmt")) {
            ids.insert(reader.attributes().value(QLatin1String("formatCode")).toString(),
                       reader.attributes().value(QLatin1String("numFmtId")).toInt());
        }
    }
    return [ids](const QString &code) { return code.isEmpty() ? 0 : ids.value(code, 0); };
}

} // namespace

void planPivotParts(const Workbook &wb, PatchManifest *manifest)
{
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        for (const PivotTable &pivot : wb.sheetAt(s).pivots()) {
            if (!pivot.sourcePart.isEmpty()) {
                manifest->modelPivotParts.insert(pivot.sourcePart);
            }
        }
    }
    for (const PivotCache &cache : wb.pivotCaches) {
        QList<QPair<qint32, const PivotTable *>> pivots;
        for (qint32 s = 0; s < wb.sheetCount(); ++s) {
            for (const PivotTable &pivot : wb.sheetAt(s).pivots()) {
                if (pivot.cacheId == cache.id) {
                    pivots.append({s, &pivot});
                }
            }
        }
        if (pivots.isEmpty() || !cache.modelled) {
            continue; // unused, or carried as read (D-075)
        }
        bool untouched = !cache.sourcePart.isEmpty() && pivotCacheFingerprint(cache) == cache.readFingerprint;
        for (const auto &[sheet, pivot] : std::as_const(pivots)) {
            untouched = untouched && pivot->modelled && !pivot->sourcePart.isEmpty()
                && pivotTableFingerprint(*pivot) == pivot->readFingerprint;
        }
        if (untouched) {
            manifest->untouchedCacheParts.insert(cache.sourcePart);
            if (!pivotCacheCurrent(wb, cache)) {
                manifest->staleCacheParts.insert(cache.sourcePart);
            }
            continue;
        }
        manifest->pivotCaches.append(cache);
        if (!cache.sourcePart.isEmpty()) {
            manifest->replacedCacheParts.insert(cache.sourcePart);
            if (pivotCacheFingerprint(cache) == cache.readFingerprint) {
                manifest->sameItemsCacheParts.insert(cache.sourcePart);
            }
        }
        for (const auto &[sheet, pivot] : std::as_const(pivots)) {
            if (!pivot->modelled || !pivot->render) {
                continue;
            }
            PatchManifest::PivotPart part;
            part.sheetPart = QStringLiteral("xl/worksheets/sheet%1.xml").arg(sheet + 1);
            part.pivot = *pivot;
            manifest->pivots.append(part);
            if (!pivot->sourcePart.isEmpty()) {
                manifest->replacedPivotParts.insert(pivot->sourcePart, pivot->name);
            }
            if (pivot->customFormats && manifest->warnings != nullptr) {
                manifest->warnings->append(
                    QObject::tr("PivotTable “%1” on “%2” was saved without the cell formatting Excel kept for it.")
                        .arg(pivot->name, wb.sheetName(sheet)));
            }
        }
    }
}

bool applyPivotParts(QHash<QString, QByteArray> *parts, const PatchManifest &manifest, QString *error)
{
    if (manifest.pivots.isEmpty()) {
        return true;
    }
    const PivotFormatId formatId = formatIds(parts->value(QStringLiteral("xl/styles.xml")));
    QByteArray &types = (*parts)[QStringLiteral("[Content_Types].xml")];
    const QString workbook = QStringLiteral("xl/workbook.xml");
    QHash<qint32, QString> definitions; // cache id → written definition part
    QByteArray caches;
    for (const PivotCache &cache : manifest.pivotCaches) {
        QList<const PivotTable *> users;
        for (const PatchManifest::PivotPart &part : manifest.pivots) {
            if (part.pivot.cacheId == cache.id) {
                users.append(&part.pivot);
            }
        }
        const QSet<qint32> listed = pivotListedFields(cache, users);
        const QString definition = uniquePart(*parts, QStringLiteral("xl/pivotCache/pivotCacheDefinition"));
        parts->insert(definition, QByteArray()); // reserve the name
        const QString records = uniquePart(*parts, QStringLiteral("xl/pivotCache/pivotCacheRecords"));
        parts->insert(records, pivotCacheRecordsXml(cache, listed));
        QString recordsId;
        if (!link(parts, definition, QStringLiteral("pivotCacheRecords"), records, &recordsId, error)) {
            return false;
        }
        parts->insert(definition, pivotCacheDefinitionXml(cache, listed, formatId, recordsId));
        QString id;
        if (!link(parts, workbook, QStringLiteral("pivotCacheDefinition"), definition, &id, error)) {
            return false;
        }
        caches += "<pivotCache cacheId=\"" + QByteArray::number(cache.id) + "\" r:id=\"" + id.toUtf8() + "\"/>";
        appendContentTypeOverride(&types, QLatin1Char('/') + definition, kType + QStringLiteral("pivotCacheDefinition+xml"));
        appendContentTypeOverride(&types, QLatin1Char('/') + records, kType + QStringLiteral("pivotCacheRecords+xml"));
        definitions.insert(cache.id, definition);
    }
    for (const PatchManifest::PivotPart &part : manifest.pivots) {
        const PivotCache *cache = nullptr;
        for (const PivotCache &one : manifest.pivotCaches) {
            cache = one.id == part.pivot.cacheId ? &one : cache;
        }
        if (cache == nullptr || !parts->contains(part.sheetPart)) {
            continue;
        }
        const QString table = uniquePart(*parts, QStringLiteral("xl/pivotTables/pivotTable"));
        parts->insert(table, pivotTableXml(part.pivot, *cache, formatId));
        QString id;
        if (!link(parts, table, QStringLiteral("pivotCacheDefinition"), definitions.value(cache->id), &id, error)
            || !link(parts, part.sheetPart, QStringLiteral("pivotTable"), table, &id, error)) {
            return false;
        }
        appendContentTypeOverride(&types, QLatin1Char('/') + table, kType + QStringLiteral("pivotTable+xml"));
    }
    // <pivotCaches> sits after calcPr (oleSize and customWorkbookViews are
    // never written); the carry stage adds the carried caches to it.
    QByteArray &book = (*parts)[workbook];
    const QByteArray element = "<pivotCaches>" + caches + "</pivotCaches>";
    const qsizetype calc = book.indexOf("<calcPr");
    const qsizetype calcEnd = calc < 0 ? -1 : book.indexOf('>', calc);
    if (calcEnd > 0) {
        book.insert(calcEnd + 1, element);
    } else if (!insertBeforeAny(&book, {QStringLiteral("<extLst"), QStringLiteral("</workbook>")}, element)) {
        if (error != nullptr) {
            *error = QStringLiteral("pivot tables: no place for <pivotCaches> in xl/workbook.xml");
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
