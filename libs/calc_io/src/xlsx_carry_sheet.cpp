// SPDX-License-Identifier: GPL-3.0-or-later
// Carry phase 3 (xlsx_carry_phases.h, M66, D-075): per sheet, the elements
// the writer never writes (page setup and print settings, breaks, the
// controls and OLE objects, a background picture, the extLst entries of
// slicers, timelines and sparklines), threaded comments, and query tables.

#include "xlsx_carry_phases.h"

#include "xlsx_carry_types.h"
#include "xlsx_carry_xml.h"
#include "xlsx_parts.h"
#include "xlsx_xml.h"

#include "ooxml_namespaces.h"

#include <QObject>
#include <QRegularExpression>

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

// The model's page setup over the source's: paper size, scale, DPI, the
// printer settings and every other attribute stay; orientation and the
// fit-to-page counts are the model's (the app edits them).
Ooxml::XmlElementPtr mergedPageSetup(const Ooxml::XmlElement &source, const QByteArray &written)
{
    Ooxml::XmlElementPtr merged = source.clone();
    const auto read = [&written](const char *name) {
        const QRegularExpression pattern(QStringLiteral("%1=\"([^\"]*)\"").arg(QLatin1String(name)));
        return pattern.match(QString::fromUtf8(written)).captured(1);
    };
    const QString orientation = read("orientation");
    if (!orientation.isEmpty() || source.hasAttribute(QString(), u"orientation")) {
        merged->setAttribute(QString(), QStringLiteral("orientation"),
                             orientation.isEmpty() ? QStringLiteral("portrait") : orientation);
    }
    for (const char *fit : {"fitToWidth", "fitToHeight"}) {
        const QString value = read(fit);
        if (!value.isEmpty() || source.hasAttribute(QString(), QString::fromLatin1(fit))) {
            merged->setAttribute(QString(), QString::fromLatin1(fit), value.isEmpty() ? QStringLiteral("0") : value);
        }
    }
    return merged;
}

// Page breaks follow inserted and removed rows (columns); a break whose
// line is gone goes with it.
Ooxml::XmlElementPtr movedBreaks(const Ooxml::XmlElement &source, const QList<SourceStep> &steps, bool rows)
{
    Ooxml::XmlElementPtr moved = source.clone();
    QList<Ooxml::XmlChild> kept;
    int manual = 0;
    for (Ooxml::XmlChild &child : moved->children) {
        if (!child.element || child.element->name != u"brk") {
            kept.append(child);
            continue;
        }
        const auto line = mapSourceLine(steps, rows, child.element->attr(u"id").toInt());
        if (!line) {
            continue;
        }
        child.element->setAttribute(QString(), QStringLiteral("id"), QString::number(*line));
        manual += child.element->attr(u"man") == QLatin1String("1") ? 1 : 0;
        kept.append(child);
    }
    moved->children = kept;
    moved->setAttribute(QString(), QStringLiteral("count"), QString::number(kept.size()));
    moved->setAttribute(QString(), QStringLiteral("manualBreakCount"), QString::number(manual));
    return kept.isEmpty() ? nullptr : moved;
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
            copy = mergedPageSetup(element, QRegularExpression(QStringLiteral("<pageSetup[^>]*>"))
                                                .match(QString::fromUtf8(sheet)).captured(0).toUtf8());
            ctx.rewrite(*copy, sourceSheet, dest.part);
            const QRegularExpressionMatch ours = QRegularExpression(QStringLiteral("<pageSetup[^>]*/>")).match(QString::fromUtf8(sheet));
            if (ours.hasMatch()) {
                QString text = QString::fromUtf8(sheet);
                text.replace(ours.capturedStart(), ours.capturedLength(), Ooxml::serializeFragment(*copy));
                sheet = text.toUtf8();
            } else {
                static_cast<void>(insertInOrder(&sheet, name, fragment(*copy)));
            }
            continue;
        }
        if (name == QLatin1String("rowBreaks") || name == QLatin1String("colBreaks")) {
            copy = movedBreaks(element, steps, name == QLatin1String("rowBreaks"));
        } else if (name == QLatin1String("extLst")) {
            copy = element.clone();
            QList<Ooxml::XmlChild> kept;
            for (const Ooxml::XmlChild &ext : copy->children) {
                bool positional = false;
                if (!ext.element || !sheetExtCarried(ext.element->attr(u"uri"), &positional)) {
                    continue;
                }
                if (positional && !steps.isEmpty()) {
                    ctx.warn(QObject::tr("Sparklines on “%1” were not kept: rows or columns were inserted or deleted there.").arg(dest.name));
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
            static_cast<void>(insertInOrder(&sheet, name, fragment(*copy)));
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
