// SPDX-License-Identifier: GPL-3.0-or-later
// Carry phase 3 (xlsx_carry_phases.h, M66, D-075): per sheet, the elements
// the writer never writes (the controls and OLE objects, a background
// picture, the extLst entries of slicers and timelines (M90: sparklines are
// modelled, xlsx_sparklines.h); the
// printer settings link of the page setup — M78 models the rest of it),
// threaded comments, and query tables.

#include "xlsx_carry_phases.h"

#include "xlsx_carry_types.h"
#include "xlsx_carry_xml.h"
#include "xlsx_parts.h"
#include "xlsx_xml.h"

#include "ooxml_namespaces.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace QindaOffice::Calc::IO::Carry {

namespace {

// The worksheet's children in schema order, from sheetProtection on.
const QStringList &schemaOrder()
{
    static const QStringList order{
        QStringLiteral("sheetCalcPr"), QStringLiteral("sheetProtection"), QStringLiteral("protectedRanges"),
        QStringLiteral("scenarios"), QStringLiteral("autoFilter"), QStringLiteral("sortState"),
        QStringLiteral("dataConsolidate"), QStringLiteral("customSheetViews"), QStringLiteral("mergeCells"),
        QStringLiteral("phoneticPr"), QStringLiteral("conditionalFormatting"), QStringLiteral("dataValidations"),
        QStringLiteral("hyperlinks"), QStringLiteral("printOptions"), QStringLiteral("pageMargins"),
        QStringLiteral("pageSetup"), QStringLiteral("headerFooter"), QStringLiteral("rowBreaks"),
        QStringLiteral("colBreaks"), QStringLiteral("customProperties"), QStringLiteral("cellWatches"),
        QStringLiteral("ignoredErrors"), QStringLiteral("smartTags"), QStringLiteral("drawing"),
        QStringLiteral("legacyDrawing"), QStringLiteral("legacyDrawingHF"), QStringLiteral("drawingHF"),
        QStringLiteral("picture"), QStringLiteral("oleObjects"), QStringLiteral("controls"),
        QStringLiteral("webPublishItems"), QStringLiteral("tableParts"), QStringLiteral("extLst")};
    return order;
}

// Inserts a fragment for element `name` before the first element that
// follows it in schema order (fragments are inserted in schema order, so
// each lands after the ones before it).
bool insertInOrder(QByteArray *sheet, const QString &name, const QByteArray &fragment)
{
    QStringList after = schemaOrder().mid(schemaOrder().indexOf(name) + 1);
    after.append(QStringLiteral("/worksheet"));
    return insertBeforeAny(sheet, after, fragment);
}

QByteArray fragment(const Ooxml::XmlElement &element)
{
    return Ooxml::serializeFragment(element).toUtf8();
}

// The page setup the model wrote, with what it does not hold taken from the
// source's (M78, D-087): the printer settings part's link (r:id), the
// printer's DPI, copies and usePrinterDefaults, a custom paper's
// paperWidth / paperHeight — every attribute but the ones Sheet::PageSetup
// models, which are the model's (present or, at their default, absent).
Ooxml::XmlElementPtr mergedPageSetup(const Ooxml::XmlElement &source, const QByteArray &written)
{
    static const QSet<QString> modelled{
        QStringLiteral("paperSize"),    QStringLiteral("scale"),         QStringLiteral("firstPageNumber"),
        QStringLiteral("fitToWidth"),   QStringLiteral("fitToHeight"),   QStringLiteral("pageOrder"),
        QStringLiteral("orientation"),  QStringLiteral("blackAndWhite"), QStringLiteral("draft"),
        QStringLiteral("cellComments"), QStringLiteral("useFirstPageNumber"), QStringLiteral("errors")};
    Ooxml::XmlElementPtr merged = source.clone();
    merged->attributes.clear();
    merged->children.clear();
    const QString tag = QString::fromUtf8(written);
    static const QRegularExpression attribute(QStringLiteral("([\\w:]+)=\"([^\"]*)\""));
    for (auto it = attribute.globalMatch(tag); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        if (!match.captured(1).contains(QLatin1Char(':'))) {
            merged->setAttribute(QString(), match.captured(1), match.captured(2));
        }
    }
    if (written.isEmpty()) {
        // The model's settings are Excel's defaults; say which way the paper
        // turns, as Excel does.
        merged->setAttribute(QString(), QStringLiteral("orientation"), QStringLiteral("portrait"));
    }
    for (const Ooxml::XmlAttribute &kept : source.attributes) {
        const bool model = kept.ns.isEmpty() && modelled.contains(kept.name);
        if (!model && !merged->hasAttribute(kept.ns, kept.name)) {
            merged->attributes.append(kept);
        }
    }
    return merged;
}

// True when a source <pageSetup> carries something the model does not:
// only then does a sheet whose print settings are Excel's defaults keep one.
bool carriesPrinterSettings(const Ooxml::XmlElement &merged)
{
    for (const Ooxml::XmlAttribute &attribute : merged.attributes) {
        if (!(attribute.ns.isEmpty() && attribute.name == u"orientation")) {
            return true;
        }
    }
    return false;
}

// The pictures of the sheet's headers and footers (&G, M78, D-087): the
// VML part the source's <legacyDrawingHF> names, carried as text (Excel
// reads VML with a legacy parser that wants its own layout), its shapes
// moved into a fresh block of ids — written notes and carried controls
// take theirs, and two drawings may not share one — and its pictures'
// relationships re-made. False when it cannot be read.
bool carryHeaderFooterPictures(Context &ctx, const QString &sourceSheet, const DestSheet &dest,
                               const Ooxml::XmlElement &element, QByteArray *sheet)
{
    static const QString vmlType =
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing");
    const auto rel = ctx.source().relationship(sourceSheet, element.attribute(QString::fromUtf16(Ooxml::Ns::R), u"id"));
    if (!rel || rel->external) {
        return false;
    }
    const QString sourceVml = ctx.source().targetPart(sourceSheet, *rel);
    const QString text = QString::fromUtf8(ctx.source().data(sourceVml));
    if (!text.contains(QLatin1String("<xml"))) {
        return false;
    }
    static const QRegularExpression idPattern(QStringLiteral("_x0000_s(\\d+)"));
    QStringList oldIds;
    for (auto it = idPattern.globalMatch(text); it.hasNext();) {
        const QString id = it.next().captured(1);
        if (!oldIds.contains(id)) {
            oldIds.append(id);
        }
    }
    const int blocks = int(oldIds.size() / 1024) + 1;
    const int block = ctx.allocateVmlBlocks(blocks);
    QString moved;
    qsizetype copied = 0;
    for (auto it = idPattern.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        moved += QStringView(text).mid(copied, match.capturedStart(1) - copied);
        moved += QString::number(block * 1024 + oldIds.indexOf(match.captured(1)) + 1);
        copied = match.capturedEnd(1);
    }
    moved += QStringView(text).mid(copied);
    QStringList taken;
    for (int b = 0; b < blocks; ++b) {
        taken.append(QString::number(block + b));
    }
    static const QRegularExpression idmap(QStringLiteral("(<o:idmap[^>]*data=\")([^\"]*)\""));
    if (const QRegularExpressionMatch map = idmap.match(moved); map.hasMatch()) {
        moved.replace(map.capturedStart(2), map.capturedLength(2), taken.join(QLatin1Char(',')));
    }
    const QString destVml = ctx.addNewPart(QStringLiteral("xl/drawings/vmlDrawing"), QStringLiteral(".vml"), QByteArray(),
                                           QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"));
    static const QRegularExpression relId(QStringLiteral("o:relid=\"([^\"]*)\""));
    QSet<QString> done;
    for (auto it = relId.globalMatch(text); it.hasNext();) {
        const QString id = it.next().captured(1);
        if (!done.contains(id)) {
            done.insert(id);
            moved.replace(QStringLiteral("o:relid=\"%1\"").arg(id),
                          QStringLiteral("o:relid=\"%1\"").arg(ctx.copyRelationship(sourceVml, id, destVml)));
        }
    }
    ctx.replaceCopied(destVml, moved.toUtf8());
    const QString id = ctx.addRelationship(dest.part, vmlType, destVml);
    return insertInOrder(sheet, QStringLiteral("legacyDrawingHF"),
                         QByteArrayLiteral("<legacyDrawingHF r:id=\"") + id.toUtf8() + QByteArrayLiteral("\"/>"));
}

// shapeId attributes of a <controls>/<oleObjects> element through the
// renumbering of their legacy-drawing shapes; false when a shape was not
// carried (the element then stays behind).
bool remapShapeIds(Ooxml::XmlElement &element, const QHash<QString, QString> &shapeIds)
{
    for (Ooxml::XmlAttribute &attribute : element.attributes) {
        if (attribute.name == u"shapeId" && attribute.ns.isEmpty()) {
            const QString id = shapeIds.value(attribute.value);
            if (id.isEmpty()) {
                return false;
            }
            attribute.value = id;
        }
    }
    for (Ooxml::XmlChild &child : element.children) {
        if (child.element && !remapShapeIds(*child.element, shapeIds)) {
            return false;
        }
    }
    return true;
}

// The slicer caches a sheet's slicer (or timeline) parts name.
bool slicerPartKept(const Context &ctx, const CarryState &state, const QString &sourceSheet, const QString &id)
{
    const auto rel = ctx.source().relationship(sourceSheet, id);
    if (!rel || rel->external) {
        return false;
    }
    bool kept = true;
    static_cast<void>(editStartTags(ctx.source().data(ctx.source().targetPart(sourceSheet, *rel)), QStringLiteral("slicer"),
                                    [&](const QString &tag) {
                                        kept = kept && state.keptSlicerCaches.contains(tagAttribute(tag, QStringLiteral("cache")));
                                        return tag;
                                    }));
    static_cast<void>(editStartTags(ctx.source().data(ctx.source().targetPart(sourceSheet, *rel)), QStringLiteral("timeline"),
                                    [&](const QString &tag) {
                                        kept = kept && state.keptSlicerCaches.contains(tagAttribute(tag, QStringLiteral("cache")));
                                        return tag;
                                    }));
    return kept;
}

bool allRelsKept(const Context &ctx, const CarryState &state, const QString &sourceSheet, const Ooxml::XmlElement &e)
{
    for (const Ooxml::XmlAttribute &attribute : e.attributes) {
        if (attribute.ns == QStringView(Ooxml::Ns::R) && !slicerPartKept(ctx, state, sourceSheet, attribute.value)) {
            return false;
        }
    }
    for (const Ooxml::XmlChild &child : e.children) {
        if (child.element && !allRelsKept(ctx, state, sourceSheet, *child.element)) {
            return false;
        }
    }
    return true;
}

void carryElements(Context &ctx, const CarryState &state, const DestSheet &dest, const QHash<QString, QString> &shapeIds)
{
    const QString sourceSheet = dest.patch->source.part;
    const Ooxml::XmlElementPtr root = parse(ctx.source().data(sourceSheet));
    if (!root) {
        return;
    }
    const QList<SourceStep> &steps = dest.patch->source.steps;
    QByteArray sheet = ctx.part(dest.part);
    for (const Ooxml::XmlChild &child : root->children) {
        if (!child.element) {
            continue;
        }
        const Ooxml::XmlElement &element = *child.element;
        QString name = element.name;
        if (element.is(QString::fromUtf16(Ooxml::Ns::MC), u"AlternateContent")) {
            const Ooxml::XmlElement *choice = element.elements().value(0);
            const Ooxml::XmlElement *inner = choice != nullptr ? choice->elements().value(0) : nullptr;
            name = inner != nullptr ? inner->name : QString();
        }
        Ooxml::XmlElementPtr copy;
        if (name == QLatin1String("pageSetup")) {
            // The model's own <pageSetup> (xlsx_print.h), at the top level.
            const qsizetype at = topLevelTagOffset(sheet, {QStringLiteral("pageSetup")});
            const qsizetype end = at < 0 ? -1 : sheet.indexOf('>', at);
            copy = mergedPageSetup(element, at < 0 ? QByteArray() : sheet.mid(at, end + 1 - at));
            ctx.rewrite(*copy, sourceSheet, dest.part);
            if (at >= 0) {
                sheet.replace(at, end + 1 - at, fragment(*copy));
            } else if (carriesPrinterSettings(*copy)) {
                // Excel writes <pageMargins> with every <pageSetup>: the
                // model's margins are the defaults when it wrote neither.
                if (topLevelTagOffset(sheet, {QStringLiteral("pageMargins")}) < 0) {
                    static_cast<void>(insertInOrder(&sheet, QStringLiteral("pageMargins"),
                                                    QByteArrayLiteral("<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\""
                                                                      " bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>")));
                }
                static_cast<void>(insertInOrder(&sheet, name, fragment(*copy)));
            }
            continue;
        }
        if (name == QLatin1String("legacyDrawingHF")) {
            if (!carryHeaderFooterPictures(ctx, sourceSheet, dest, element, &sheet)) {
                ctx.warn(QObject::tr("The pictures in the headers or footers of “%1” were not kept: their drawing could not be read.")
                             .arg(dest.name));
            }
            continue;
        }
        if (name == QLatin1String("extLst")) {
            copy = element.clone();
            QList<Ooxml::XmlChild> kept;
            for (const Ooxml::XmlChild &ext : copy->children) {
                bool positional = false;
                if (!ext.element || !sheetExtCarried(ext.element->attr(u"uri"), &positional) || (positional && !steps.isEmpty())) {
                    continue;
                }
                if (!allRelsKept(ctx, state, sourceSheet, *ext.element)) {
                    continue; // a slicer whose cache was not kept
                }
                kept.append(ext);
            }
            copy->children = kept;
            if (kept.isEmpty()) {
                copy.reset();
            }
        } else {
            const CarriedElement *spec = nullptr;
            for (const CarriedElement &candidate : kCarriedSheetElements) {
                spec = name == QLatin1String(candidate.name) ? &candidate : spec;
            }
            if (spec == nullptr) {
                continue;
            }
            if (spec->positional && !steps.isEmpty()) {
                continue; // cells it names moved: leave it (ignoredErrors only)
            }
            copy = element.clone();
            if ((name == QLatin1String("controls") || name == QLatin1String("oleObjects")) && !remapShapeIds(*copy, shapeIds)) {
                ctx.warn(QObject::tr("Controls or embedded objects on “%1” were not kept: their shapes could not be read.").arg(dest.name));
                continue;
            }
        }
        if (copy) {
            ctx.rewrite(*copy, sourceSheet, dest.part);
            const qsizetype ownExtEnd = name == QLatin1String("extLst")
                ? topLevelElementEnd(sheet, QStringLiteral("extLst"))
                : -1;
            if (ownExtEnd >= 0) {
                // The post-pass wrote the sheet's extLst (M78: Excel 2010's
                // rules): the carried entries join it.
                QByteArray entries;
                for (const Ooxml::XmlChild &ext : copy->children) {
                    if (ext.element) {
                        entries += fragment(*ext.element);
                    }
                }
                sheet.insert(ownExtEnd, entries);
            } else {
                static_cast<void>(insertInOrder(&sheet, name, fragment(*copy)));
            }
        }
    }
    ctx.setPart(dest.part, sheet);
}

// The text of each legacy note of a comments part, by cell reference.
QHash<QString, QString> noteTexts(const QByteArray &xml)
{
    QHash<QString, QString> out;
    const Ooxml::XmlElementPtr root = parse(xml);
    const Ooxml::XmlElement *list = root ? root->child(QString::fromUtf16(Ooxml::Ns::S), u"commentList") : nullptr;
    for (const Ooxml::XmlElement *comment : list != nullptr ? list->elements() : QList<const Ooxml::XmlElement *>()) {
        if (const Ooxml::XmlElement *text = comment->child(QString::fromUtf16(Ooxml::Ns::S), u"text")) {
            out.insert(comment->attr(u"ref"), text->text());
        }
    }
    return out;
}

// A thread travels while the note standing in for it is the one it was
// written with, at the cell its cell moved to; a reply travels with its
// thread. The others become plain notes (the note itself is always kept).
void carryThreads(Context &ctx, const DestSheet &dest)
{
    const QString sourceSheet = dest.patch->source.part;
    const QHash<QString, QString> before = noteTexts(ctx.source().data(sourceTarget(ctx.source(), sourceSheet, QStringLiteral("comments"))));
    const QHash<QString, QString> now = noteTexts(ctx.part(dest.patch->commentsPart));
    for (const Ooxml::Relationship &rel : ctx.source().relationships(sourceSheet)) {
        if (rel.external || relKind(rel.type) != QLatin1String("threadedComment")) {
            continue;
        }
        const QString part = ctx.source().targetPart(sourceSheet, rel);
        QHash<QString, QString> keptRoots; // thread id → its (moved) cell
        int dropped = 0;
        QByteArray xml = editStartTags(ctx.source().data(part), QStringLiteral("threadedComment"), [&](const QString &tag) {
            if (!tagAttribute(tag, QStringLiteral("parentId")).isEmpty()) {
                return tag;
            }
            const QString ref = tagAttribute(tag, QStringLiteral("ref"));
            const auto at = addrFromText(ref);
            const auto moved = at ? mapSourceCell(dest.patch->source.steps, *at) : std::nullopt;
            if (moved && before.contains(ref) && now.value(addrText(*moved)) == before.value(ref)) {
                keptRoots.insert(tagAttribute(tag, QStringLiteral("id")), addrText(*moved));
                return withTagAttribute(tag, QStringLiteral("ref"), addrText(*moved));
            }
            ++dropped;
            return tag;
        });
        // Replies carry their thread's ref; drop every comment of a dropped thread.
        const QRegularExpression element(QStringLiteral("<(?:[\\w.-]+:)?threadedComment[\\s>][\\s\\S]*?</(?:[\\w.-]+:)?threadedComment>"));
        QString text = QString::fromUtf8(xml);
        QString kept;
        qsizetype copied = 0;
        auto it = element.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString tag = match.captured(0).left(match.captured(0).indexOf(QLatin1Char('>')) + 1);
            const QString parent = tagAttribute(tag, QStringLiteral("parentId"));
            const QString thread = parent.isEmpty() ? tagAttribute(tag, QStringLiteral("id")) : parent;
            kept += QStringView(text).mid(copied, match.capturedStart() - copied);
            if (keptRoots.contains(thread)) {
                QString comment = match.captured(0);
                if (!parent.isEmpty()) {
                    // A reply sits at its thread's (moved) cell.
                    comment.replace(0, tag.size(), withTagAttribute(tag, QStringLiteral("ref"), keptRoots.value(thread)));
                }
                kept += comment;
            }
            copied = match.capturedEnd();
        }
        kept += QStringView(text).mid(copied);
        if (dropped > 0) {
            ctx.warn(QObject::tr("%n comment thread(s) on “%1” became plain notes: the note was edited or its cell deleted.",
                                 nullptr, dropped)
                         .arg(dest.name));
        }
        if (!keptRoots.isEmpty() && !ctx.copyRelationship(sourceSheet, rel.id, dest.part).isEmpty()) {
            ctx.replaceCopied(ctx.copyPart(part), kept.toUtf8());
        }
    }
}

// A query table hangs from its table part: kept while the written table
// still has every column the query fills, with the table marked a query
// table and each column tied to its query field.
void carryQueryTables(Context &ctx, const CarryState &state)
{
    for (auto it = state.sourceToDestTable.constBegin(); it != state.sourceToDestTable.constEnd(); ++it) {
        const QString sourceTable = it.key();
        const QString destTable = it.value();
        for (const Ooxml::Relationship &rel : ctx.source().relationships(sourceTable)) {
            if (rel.external || relKind(rel.type) != QLatin1String("queryTable")) {
                continue;
            }
            const QByteArray source = ctx.source().data(sourceTable);
            QHash<QString, QPair<QString, QString>> fields; // column name → uniqueName, queryTableFieldId
            static_cast<void>(editStartTags(source, QStringLiteral("tableColumn"), [&](const QString &tag) {
                fields.insert(tagAttribute(tag, QStringLiteral("name")),
                              {tagAttribute(tag, QStringLiteral("uniqueName")), tagAttribute(tag, QStringLiteral("queryTableFieldId"))});
                return tag;
            }));
            bool complete = true;
            QByteArray written = editStartTags(ctx.part(destTable), QStringLiteral("tableColumn"), [&](const QString &tag) {
                const auto field = fields.constFind(tagAttribute(tag, QStringLiteral("name")));
                if (field == fields.constEnd() || field->second.isEmpty()) {
                    complete = false;
                    return tag;
                }
                QString out = tag;
                if (!field->first.isEmpty()) {
                    out = withTagAttribute(out, QStringLiteral("uniqueName"), field->first);
                }
                return withTagAttribute(out, QStringLiteral("queryTableFieldId"), field->second);
            });
            if (!complete) {
                ctx.warn(QObject::tr("A query table was not kept: its table's columns changed."));
                continue;
            }
            written = editStartTags(written, QStringLiteral("table"), [](const QString &tag) {
                return withTagAttribute(tag, QStringLiteral("tableType"), QStringLiteral("queryTable"));
            });
            if (!ctx.copyRelationship(sourceTable, rel.id, destTable).isEmpty()) {
                ctx.setPart(destTable, written);
            }
        }
    }
}

} // namespace

bool carrySheets(Context &ctx, CarryState &state, int *drawingPartCount, QString *error)
{
    for (const DestSheet &dest : ctx.destSheets()) {
        // A chart sheet is carried whole (xlsx_carry_chartsheet.cpp, M69).
        if (dest.patch == nullptr || dest.patch->chartSheet || !ctx.source().contains(dest.patch->source.part)) {
            continue;
        }
        // Drawings first: a sheet that gains a drawing or a legacy drawing
        // gets its relationship before anything else touches its rels.
        QHash<QString, QString> shapeIds;
        if (!carrySheetDrawings(ctx, dest, drawingPartCount, &shapeIds, error)) {
            return false;
        }
        carryElements(ctx, state, dest, shapeIds);
        if (!dest.patch->commentsPart.isEmpty()) {
            carryThreads(ctx, dest);
        } else if (!sourceTarget(ctx.source(), dest.patch->source.part, QStringLiteral("threadedComment")).isEmpty()) {
            ctx.warn(QObject::tr("The comment threads on “%1” were not kept: their notes were deleted.").arg(dest.name));
        }
    }
    carryQueryTables(ctx, state);
    return true;
}

} // namespace QindaOffice::Calc::IO::Carry
