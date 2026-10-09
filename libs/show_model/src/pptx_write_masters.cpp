// SPDX-License-Identifier: GPL-3.0-or-later
// Masters, layouts and themes for the pptx writer (pptx_write_masters.h).

#include "pptx_write_masters.h"

#include "pptx_write_shapes.h"
#include "pptx_write_styles.h"

#include "ooxml_namespaces.h"

#include <QRegularExpression>

#include <climits>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

QString pml(const char *kind)
{
    return QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.") + QLatin1String(kind);
}

bool isTitle(const Shape &shape)
{
    return shape.placeholder.type == u"title" || shape.placeholder.type == u"ctrTitle";
}

bool isBody(const Shape &shape)
{
    return shape.placeholder.type == u"body" || shape.placeholder.type == u"obj";
}

bool unedited(const SlideLayout &layout)
{
    return layout.origin && layout == layout.origin->pristine;
}

// The (source) layout parts and ids a master lists, in order.
QList<std::pair<QString, quint32>> sourceLayouts(const WriteSession &session, const QString &masterPart)
{
    QList<std::pair<QString, quint32>> out;
    const XmlElementPtr root = session.source->xml(masterPart);
    const XmlElement *list = root ? root->child(nsP(), u"sldLayoutIdLst") : nullptr;
    for (const XmlElement *entry : list != nullptr ? list->elements(nsP(), u"sldLayoutId") : QList<const XmlElement *>()) {
        const auto rel = session.source->relationship(masterPart, entry->attribute(nsR(), u"id"));
        if (rel) {
            out.append({session.source->targetPart(masterPart, *rel), entry->attr(u"id").toUInt()});
        }
    }
    return out;
}

QList<const SlideLayout *> layoutsOf(const Presentation &deck, const QString &masterId)
{
    QList<const SlideLayout *> out;
    for (const SlideLayout &layout : deck.layouts) {
        if (layout.master == masterId) {
            out.append(&layout);
        }
    }
    return out;
}

// The master (but for its theme, a part of its own) and every layout of
// it are as read, in the order read.
bool carriedWhole(const WriteSession &session, const SlideMaster &master)
{
    SlideMaster themeless = master;
    themeless.theme = master.origin->pristine.theme;
    if (!(themeless == master.origin->pristine)) {
        return false;
    }
    const QList<const SlideLayout *> mine = layoutsOf(session.deck, master.id);
    const QList<std::pair<QString, quint32>> theirs = sourceLayouts(session, master.origin->part);
    if (mine.size() != theirs.size()) {
        return false;
    }
    for (qsizetype i = 0; i < mine.size(); ++i) {
        if (!unedited(*mine.at(i)) || mine.at(i)->origin->part != theirs.at(i).first) {
            return false;
        }
    }
    return true;
}

// The shapes of a master or layout in their z order (SlideMaster::order;
// placeholders first when it names none of them).
QList<const Shape *> ordered(const QStringList &order, const QList<Shape> &placeholders, const QList<Shape> &shapes)
{
    QList<const Shape *> all;
    for (const Shape &shape : placeholders) {
        all.append(&shape);
    }
    for (const Shape &shape : shapes) {
        all.append(&shape);
    }
    const auto rank = [&order](const Shape *shape) {
        const qsizetype at = order.indexOf(shape->id);
        return at >= 0 ? at : qsizetype(INT_MAX);
    };
    std::stable_sort(all.begin(), all.end(), [&rank](const Shape *a, const Shape *b) { return rank(a) < rank(b); });
    return all;
}

void writeTree(PartWriter &out, XmlElement &tree, const QList<const Shape *> &shapes, bool master, const ThemeFonts &fonts)
{
    QList<Shape> plain;
    for (const Shape *shape : shapes) {
        plain.append(*shape);
    }
    const QList<bool> keeps = reserveShapeIds(out, plain);
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        const Shape &shape = *shapes.at(i);
        const bool elsewhere = master && (isTitle(shape) || isBody(shape));
        if (XmlElementPtr e = writeTemplateShape(out, shape, out.claim(keeps.at(i) ? shape.nvId : 0), elsewhere, fonts)) {
            tree.insertElementBefore(e, {u"extLst"});
        }
    }
}

void clearTree(XmlElement &tree)
{
    tree.children.removeIf([](const Ooxml::XmlChild &c) {
        return c.element && c.element->name != u"nvGrpSpPr" && c.element->name != u"grpSpPr" && c.element->name != u"extLst";
    });
}

XmlElementPtr skeleton(const QString &kind)
{
    return Ooxml::parseXml(QStringLiteral(
                               R"(<p:%1 xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">)"
                               R"(<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld>)"
                               R"(<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:%1>)")
                               .arg(kind)
                               .toUtf8());
}

// A model layout's part name: its id's when that names a free layout part.
QString layoutPartFor(const WriteSession &session, const SlideLayout &layout)
{
    static const QRegularExpression kName(QStringLiteral("^slideLayout\\d+$"));
    const QString wanted = QStringLiteral("ppt/slideLayouts/%1.xml").arg(layout.id);
    if (kName.match(layout.id).hasMatch() && !session.writer.hasPart(wanted)
        && (!session.source || !session.source->contains(wanted))) {
        return wanted;
    }
    return session.writer.uniquePartName(QStringLiteral("ppt/slideLayouts/slideLayout"), QStringLiteral(".xml"));
}

QString writeLayout(WriteSession &session, const SlideLayout &layout, const QString &masterPart, const ThemeFonts &fonts)
{
    const LayoutSource *source = layout.origin.get();
    const bool fromSource = source != nullptr && session.copier && session.source->contains(source->part);
    const QString part = fromSource && !session.writer.hasPart(source->part) ? source->part : layoutPartFor(session, layout);
    if (fromSource) {
        session.copier->mapPart(source->part, part);
    }
    PartWriter out(session, part);
    XmlElementPtr root = fromSource ? session.source->xml(source->part) : skeleton(QStringLiteral("sldLayout"));
    XmlElement *cSld = root ? root->child(nsP(), u"cSld") : nullptr;
    XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree == nullptr) {
        root = skeleton(QStringLiteral("sldLayout"));
        cSld = root->child(nsP(), u"cSld");
        tree = cSld->child(nsP(), u"spTree");
    }
    root->setAttribute(QString(), QStringLiteral("type"), isLayoutType(layout.type) ? layout.type : QStringLiteral("cust"));
    if (layout.preserve) {
        root->setAttribute(QString(), QStringLiteral("preserve"), QStringLiteral("1"));
    } else {
        root->removeAttribute(QString(), u"preserve");
    }
    if (layout.showMasterShapes) {
        root->removeAttribute(QString(), u"showMasterSp");
    } else {
        root->setAttribute(QString(), QStringLiteral("showMasterSp"), QStringLiteral("0"));
    }
    cSld->setAttribute(QString(), QStringLiteral("name"), layout.name.isEmpty() ? QStringLiteral("Layout") : layout.name);
    const bool newBackground = !fromSource || !(layout.background == source->pristine.background);
    if (newBackground) {
        cSld->removeChildren(nsP(), u"bg");
    }
    clearTree(*tree);
    // AGENT-GUARD (pptx_write_context.h): carry the source ids before
    // anything new goes in.
    if (fromSource) {
        out.carry(*root, source->part);
    }
    if (newBackground && layout.background.set) {
        cSld->insertElementBefore(backgroundElement(out, layout.background), {u"spTree"});
    }
    writeTree(out, *tree, ordered(layout.order, layout.placeholders, layout.shapes), false, fonts);
    out.relate(relType("slideMaster"), masterPart);
    session.writer.addXmlPart(part, *root, pml("slideLayout+xml"));
    return part;
}

void setRunLook(XmlElement &style, const QColor &color, const QString &font)
{
    for (Ooxml::XmlChild &child : style.children) {
        if (!child.element || !child.element->name.startsWith(QLatin1String("lvl"))) {
            continue;
        }
        XmlElement *level = child.element.get();
        XmlElement *defRPr = level->child(nsA(), u"defRPr");
        if (defRPr == nullptr) {
            defRPr = &level->insertElementBefore(std::make_shared<XmlElement>(nsA(), QStringLiteral("defRPr")), {u"extLst"});
        }
        for (const char16_t *fill : {u"noFill", u"solidFill", u"gradFill", u"blipFill", u"pattFill", u"grpFill"}) {
            defRPr->removeChildren(nsA(), fill);
        }
        auto solid = std::make_shared<XmlElement>(nsA(), QStringLiteral("solidFill"));
        solid->appendElement(colorElement(color));
        defRPr->insertElementBefore(solid, {u"effectLst", u"effectDag", u"highlight", u"uLnTx", u"uLn", u"uFillTx",
                                           u"uFill", u"latin", u"ea", u"cs", u"sym", u"hlinkClick", u"hlinkMouseOver",
                                           u"rtl", u"extLst"});
        if (!font.isEmpty()) {
            defRPr->removeChildren(nsA(), u"latin");
            auto latin = std::make_shared<XmlElement>(nsA(), QStringLiteral("latin"));
            latin->setAttribute(QString(), QStringLiteral("typeface"), font);
            defRPr->insertElementBefore(latin, {u"ea", u"cs", u"sym", u"hlinkClick", u"hlinkMouseOver", u"rtl", u"extLst"});
        }
    }
}

void patchStyles(XmlElement &root, const SlideMaster &master, const SlideMaster &was)
{
    XmlElement *styles = root.child(nsP(), u"txStyles");
    if (styles == nullptr) {
        return;
    }
    const std::tuple<const char16_t *, const QList<TextParagraph> *, const QList<TextParagraph> *> lists[] = {
        {u"titleStyle", &master.titleStyle, &was.titleStyle},
        {u"bodyStyle", &master.bodyStyle, &was.bodyStyle},
        {u"otherStyle", &master.otherStyle, &was.otherStyle}};
    for (const auto &[name, now, before] : lists) {
        if (*now != *before) {
            if (XmlElement *style = styles->child(nsP(), name)) {
                patchListStyle(*style, *now, *before, master.theme.fonts);
            }
        }
    }
    // The Masters dialog's summary (title and body colour, the font).
    const bool fontChanged = was.fontFamily != master.fontFamily;
    if (XmlElement *title = styles->child(nsP(), u"titleStyle"); title != nullptr && (was.titleColor != master.titleColor || fontChanged)) {
        setRunLook(*title, master.titleColor, fontChanged ? master.fontFamily : QString());
    }
    if (XmlElement *body = styles->child(nsP(), u"bodyStyle"); body != nullptr && (was.bodyColor != master.bodyColor || fontChanged)) {
        setRunLook(*body, master.bodyColor, fontChanged ? master.fontFamily : QString());
    }
}

QString writeTheme(WriteSession &session, const SlideMaster &master)
{
    const MasterSource &source = *master.origin.source;
    if (source.themePart.isEmpty() || !session.source->contains(source.themePart)) {
        const QString part = session.writer.uniquePartName(QStringLiteral("ppt/theme/theme"), QStringLiteral(".xml"));
        session.writer.addPart(part, themePartXml(master.theme.isEmpty() ? officeTheme() : master.theme),
                               QString::fromUtf16(Ooxml::ContentType::Theme));
        return part;
    }
    if (master.theme == source.pristine.theme) {
        return session.copier->copyPart(source.themePart);
    }
    const QString part = session.writer.hasPart(source.themePart)
        ? session.writer.uniquePartName(QStringLiteral("ppt/theme/theme"), QStringLiteral(".xml"))
        : source.themePart;
    session.writer.addPart(part, patchedThemePart(session.source->data(source.themePart), master.theme, source.pristine.theme),
                           QString::fromUtf16(Ooxml::ContentType::Theme));
    // The presentation's own theme relationship follows the edited theme.
    session.copier->mapPart(source.themePart, part);
    return part;
}

void writeEditedMaster(WriteSession &session, const SlideMaster &master, MasterOut &out, quint32 *nextId)
{
    const MasterSource &source = *master.origin.source;
    const SlideMaster &was = source.pristine;
    XmlElementPtr root = session.source->xml(source.part);
    XmlElement *cSld = root ? root->child(nsP(), u"cSld") : nullptr;
    XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree == nullptr) {
        // Not a master we can rewrite: carried as it is.
        out.part = session.copier->copyPart(source.part);
        out.masterId = source.masterId;
        for (const SlideLayout *layout : layoutsOf(session.deck, master.id)) {
            if (layout->origin) {
                out.layouts.append({layout->id, layout->type, session.copier->copyPart(layout->origin->part)});
            }
        }
        return;
    }
    out.part = session.writer.hasPart(source.part)
        ? session.writer.uniquePartName(QStringLiteral("ppt/slideMasters/slideMaster"), QStringLiteral(".xml"))
        : source.part;
    out.masterId = source.masterId;
    session.copier->mapPart(source.part, out.part);
    const QString themePart = writeTheme(session, master);
    // Its layouts, in the model's order: carried, patched or new.
    QHash<QString, quint32> sourceIds;
    for (const auto &[part, id] : sourceLayouts(session, source.part)) {
        sourceIds.insert(part, id);
    }
    QList<std::pair<quint32, QString>> entries;
    QSet<quint32> used;
    for (const SlideLayout *layout : layoutsOf(session.deck, master.id)) {
        const bool carry = unedited(*layout) && session.source->contains(layout->origin->part);
        const QString part = carry ? session.copier->copyPart(layout->origin->part)
                                   : writeLayout(session, *layout, out.part, master.theme.fonts);
        quint32 id = layout->origin ? sourceIds.value(layout->origin->part) : 0;
        if (id < 2147483648u || used.contains(id)) {
            id = (*nextId)++;
        }
        used.insert(id);
        out.layouts.append({layout->id, layout->type, part});
        entries.append({id, part});
    }
    PartWriter writer(session, out.part);
    root->removeChildren(nsP(), u"sldLayoutIdLst");
    const bool newBackground = master.background != was.background || master.backgroundStyle != was.backgroundStyle;
    if (newBackground) {
        cSld->removeChildren(nsP(), u"bg");
    }
    if (master.name != was.name) {
        cSld->setAttribute(QString(), QStringLiteral("name"), master.name);
    }
    clearTree(*tree);
    patchStyles(*root, master, was);
    writer.carry(*root, source.part); // AGENT-GUARD: before anything new goes in
    if (newBackground) {
        cSld->insertElementBefore(backgroundElement(writer, Background{true, master.background, master.backgroundStyle}), {u"spTree"});
    }
    writeTree(writer, *tree, ordered(master.order, master.placeholders, master.shapes), true, master.theme.fonts);
    auto list = std::make_shared<XmlElement>(nsP(), QStringLiteral("sldLayoutIdLst"));
    for (const auto &[id, part] : entries) {
        XmlElement &entry = list->appendElement(nsP(), QStringLiteral("sldLayoutId"));
        entry.setAttribute(QString(), QStringLiteral("id"), QString::number(id));
        entry.setAttribute(nsR(), QStringLiteral("id"), writer.relate(relType("slideLayout"), part));
    }
    root->insertElementBefore(list, {u"transition", u"timing", u"hf", u"txStyles", u"extLst"});
    writer.relate(relType("theme"), themePart);
    session.writer.addXmlPart(out.part, *root, pml("slideMaster+xml"));
}

} // namespace

XmlElementPtr writeTemplateShape(PartWriter &out, const Shape &shape, quint32 id, bool stylesElsewhere,
                                 const ThemeFonts &fonts)
{
    if (shape.placeholder.isPlaceholder() && !shape.origin && shape.holdsText()) {
        // A placeholder QindaShow made (Insert Placeholder, a layout of a
        // generated master): its words only — the look is inherited.
        XmlElementPtr e = writeShape(out, shape, id);
        if (e && e->name == u"sp") {
            e->removeChildren(e->ns, u"txBody");
            e->insertElementBefore(plainTextBody(out, e->ns, shape), {u"extLst"});
        }
        return e;
    }
    if (!shape.placeholder.isPlaceholder() || !shape.origin || !shape.origin->xml || out.session.copier == nullptr
        || shape.paragraphs == shape.origin->pristine.paragraphs || shape.origin->xml->name != u"sp") {
        return writeShape(out, shape, id);
    }
    // The text goes in through the list styles; the rest as any shape.
    Shape untouched = shape;
    untouched.paragraphs = shape.origin->pristine.paragraphs;
    untouched.textBody = shape.origin->pristine.textBody;
    XmlElementPtr e = writeShape(out, untouched, id);
    if (e) {
        e->removeChildren(e->ns, u"txBody");
        e->insertElementBefore(templateTextBody(out, shape, *shape.origin.source, stylesElsewhere, fonts), {u"extLst"});
    }
    return e;
}

QList<MasterOut> writeMasters(WriteSession &session, quint32 *nextId)
{
    QList<MasterOut> masters;
    const DeckSource *deckSource = session.deck.origin.get();
    const bool carry = session.copier != nullptr && deckSource != nullptr && deckSource->presentation;
    // Ids first: every id the source used stays reserved.
    for (const SlideMaster &master : session.deck.masters) {
        if (carry && master.origin && session.source->contains(master.origin->part)) {
            *nextId = qMax(*nextId, master.origin->masterId + 1);
            for (const auto &[part, id] : sourceLayouts(session, master.origin->part)) {
                *nextId = qMax(*nextId, id + 1);
            }
        }
    }
    for (const SlideMaster &master : session.deck.masters) {
        MasterOut out;
        out.modelId = master.id;
        if (carry && master.origin && session.source->contains(master.origin->part)) {
            if (carriedWhole(session, master)) {
                out.part = session.copier->copyPart(master.origin->part);
                out.masterId = master.origin->masterId;
                for (const SlideLayout *layout : layoutsOf(session.deck, master.id)) {
                    out.layouts.append({layout->id, layout->type, session.copier->copyPart(layout->origin->part)});
                }
                // Only its theme changed (Design ▸ Themes): the master and its
                // layouts stay byte for byte, the theme part is rewritten.
                const MasterSource &source = *master.origin.source;
                if (master.theme != source.pristine.theme && session.source->contains(source.themePart)) {
                    session.writer.addPart(session.copier->copyPart(source.themePart),
                                           patchedThemePart(session.source->data(source.themePart), master.theme,
                                                            source.pristine.theme),
                                           QString::fromUtf16(Ooxml::ContentType::Theme));
                }
            } else {
                writeEditedMaster(session, master, out, nextId);
            }
        }
        masters.append(out);
    }
    return masters;
}

} // namespace QindaOffice::Show::Pptx
