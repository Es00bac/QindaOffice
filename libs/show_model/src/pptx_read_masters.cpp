// SPDX-License-Identifier: GPL-3.0-or-later
// Slide masters and layouts for the pptx reader (pptx_read_masters.h).

#include "pptx_read_masters.h"

#include "pptx_origin.h"
#include "pptx_read_text.h"
#include "pptx_reader.h"

#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QFileInfo>

namespace QindaOffice::Show::Pptx {

namespace {

QString baseName(const QString &part)
{
    return QFileInfo(part).completeBaseName();
}

void split(QList<Shape> shapes, QList<Shape> *decorations, QList<Shape> *placeholders, QStringList *order)
{
    for (Shape &shape : shapes) {
        order->append(shape.id);
        (shape.placeholder.isPlaceholder() ? placeholders : decorations)->append(std::move(shape));
    }
}

void readLayout(DeckReadState &state, const QString &part, int masterIndex, Presentation &deck)
{
    const Ooxml::XmlElementPtr root = state.package->xml(part);
    if (!root) {
        return;
    }
    const MasterInfo &master = state.masters.at(masterIndex);
    LayoutInfo info;
    info.part = part;
    info.id = baseName(part);
    info.master = masterIndex;
    info.map = overriddenMap(master.map, root.get());
    const PartContext ctx = state.context(part, master, info.map);
    const Ooxml::XmlElement *cSld = root->child(nsP(), u"cSld");
    const Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    SlideLayout layout;
    layout.id = info.id;
    layout.name = cSld != nullptr ? cSld->attr(u"name") : QString();
    layout.type = root->attr(u"type", QStringLiteral("cust"));
    layout.master = master.id;
    layout.showMasterShapes = Ooxml::Units::parseOnOff(root->attr(u"showMasterSp"), true);
    layout.preserve = Ooxml::Units::parseOnOff(root->attr(u"preserve"), false);
    layout.background = backgroundOf(ctx, cSld);
    if (tree != nullptr) {
        info.placeholders = collectPlaceholders(ctx, *tree);
        ShapeScope scope;
        scope.ctx = ctx;
        scope.master = &master.placeholders;
        scope.styles = &master.styles;
        scope.tableStyles = &state.tableStyles;
        scope.idPrefix = QStringLiteral("l%1sp").arg(state.layouts.size() + 1);
        QSet<QString> ids;
        scope.usedIds = &ids;
        split(readShapeTree(scope, *tree), &layout.shapes, &layout.placeholders, &layout.order);
    }
    auto source = std::make_shared<LayoutSource>();
    source->part = part;
    source->pristine = layout;
    layout.origin.source = std::move(source);
    deck.layouts.append(layout);
    state.layoutByPart.insert(part, int(state.layouts.size()));
    state.layouts.push_back(std::move(info));
}

} // namespace

PartContext DeckReadState::context(const QString &part, const MasterInfo &master, const Ooxml::ColorMap &map) const
{
    PartContext ctx;
    ctx.package = package;
    ctx.part = part;
    ctx.theme = master.theme.get();
    ctx.map = map;
    ctx.slideIds = &slideIds;
    return ctx;
}

Ooxml::ColorMap colorMapOf(const Ooxml::XmlElement *element)
{
    Ooxml::ColorMap map;
    if (element != nullptr) {
        for (const Ooxml::XmlAttribute &a : element->attributes) {
            if (a.ns.isEmpty()) {
                map.insert(a.name, a.value);
            }
        }
    }
    return map;
}

Ooxml::ColorMap overriddenMap(const Ooxml::ColorMap &base, const Ooxml::XmlElement *root)
{
    const Ooxml::XmlElement *ovr = root != nullptr ? root->child(nsP(), u"clrMapOvr") : nullptr;
    const Ooxml::XmlElement *mapping = ovr != nullptr ? ovr->child(nsA(), u"overrideClrMapping") : nullptr;
    return mapping != nullptr ? colorMapOf(mapping) : base;
}

Background backgroundOf(const PartContext &ctx, const Ooxml::XmlElement *cSld)
{
    Background background;
    const Ooxml::XmlElement *bg = cSld != nullptr ? cSld->child(nsP(), u"bg") : nullptr;
    if (bg == nullptr) {
        return background;
    }
    ResolvedFill fill;
    if (const Ooxml::XmlElement *pr = bg->child(nsP(), u"bgPr")) {
        fill = fillFrom(ctx, pr);
    } else if (const Ooxml::XmlElement *ref = bg->child(nsP(), u"bgRef")) {
        fill = fillFromReference(ctx, ref);
    }
    if (fill.present) {
        background.set = true;
        background.color = fill.plain.alpha() > 0 ? fill.plain : QColor(Qt::white);
        background.style = fill.style.kind == FillKind::Plain ? FillStyle() : fill.style;
    }
    return background;
}

QList<TextParagraph> levelStyles(const PartContext &ctx, const ListStyle &style)
{
    // One empty run per level, resolved like any text in that style.
    QString xml = QStringLiteral(R"(<a:txBody xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><a:bodyPr/><a:lstStyle/>)");
    for (int level = 0; level < 9; ++level) {
        xml += QStringLiteral(R"(<a:p><a:pPr lvl="%1"/><a:r><a:rPr/><a:t></a:t></a:r></a:p>)").arg(level);
    }
    xml += QStringLiteral("</a:txBody>");
    const Ooxml::XmlElementPtr body = Ooxml::parseXml(xml.toUtf8());
    return readTextBody(ctx, body.get(), style);
}

bool readMaster(DeckReadState &state, const QString &part, quint32 masterId, Presentation &deck)
{
    const Ooxml::XmlElementPtr root = state.package->xml(part);
    if (!root) {
        return false;
    }
    MasterInfo info;
    info.part = part;
    info.id = baseName(part);
    const std::optional<Ooxml::Relationship> themeRel =
        state.package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::Theme));
    const QString themePart = themeRel ? state.package->targetPart(part, *themeRel) : QString();
    const Ooxml::XmlElementPtr theme = themePart.isEmpty() ? nullptr : state.package->xml(themePart);
    info.theme = std::make_shared<ThemeContext>(parseThemeContext(theme.get()));
    info.map = colorMapOf(root->child(nsP(), u"clrMap"));
    const PartContext ctx = state.context(part, info, info.map);
    const Ooxml::XmlElement *defaults = state.presentation->child(nsP(), u"defaultTextStyle");
    info.styles.base = parseListStyle(ctx, defaults);
    const Ooxml::XmlElement *txStyles = root->child(nsP(), u"txStyles");
    info.styles.title = info.styles.base;
    info.styles.body = info.styles.base;
    info.styles.other = info.styles.base;
    if (txStyles != nullptr) {
        info.styles.title.overlay(parseListStyle(ctx, txStyles->child(nsP(), u"titleStyle")));
        info.styles.body.overlay(parseListStyle(ctx, txStyles->child(nsP(), u"bodyStyle")));
        info.styles.other.overlay(parseListStyle(ctx, txStyles->child(nsP(), u"otherStyle")));
    }
    const Ooxml::XmlElement *cSld = root->child(nsP(), u"cSld");
    const Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree != nullptr) {
        info.placeholders = collectPlaceholders(ctx, *tree);
    }
    SlideMaster master;
    master.id = info.id;
    master.name = cSld != nullptr ? cSld->attr(u"name") : QString();
    if (master.name.isEmpty()) {
        master.name = info.theme->theme.name;
    }
    master.typeDefaults = false;
    const Background background = backgroundOf(ctx, cSld);
    master.background = background.set ? background.color : QColor(Qt::white);
    master.backgroundStyle = background.style;
    master.titleColor = info.styles.title.levels[0].run.color.value_or(QColor(Qt::black));
    master.bodyColor = info.styles.body.levels[0].run.color.value_or(QColor(Qt::black));
    master.fontFamily = info.theme->theme.minorLatin;
    // M75: what the Slide Master view edits.
    master.theme = themeFromPart(themePart.isEmpty() ? QByteArray() : state.package->data(themePart));
    master.titleStyle = levelStyles(ctx, info.styles.title);
    master.bodyStyle = levelStyles(ctx, info.styles.body);
    master.otherStyle = levelStyles(ctx, info.styles.other);
    if (tree != nullptr) {
        ShapeScope scope;
        scope.ctx = ctx;
        scope.styles = &info.styles;
        scope.tableStyles = &state.tableStyles;
        scope.idPrefix = QStringLiteral("m%1sp").arg(state.masters.size() + 1);
        QSet<QString> ids;
        scope.usedIds = &ids;
        split(readShapeTree(scope, *tree), &master.shapes, &master.placeholders, &master.order);
    }
    auto source = std::make_shared<MasterSource>();
    source->part = part;
    source->masterId = masterId;
    source->background = master.background;
    source->titleColor = master.titleColor;
    source->bodyColor = master.bodyColor;
    source->fontFamily = master.fontFamily;
    source->pristine = master;
    source->themePart = themePart;
    master.origin.source = std::move(source);
    deck.masters.append(master);
    state.masters.push_back(std::move(info));
    // Its layouts, in the master's own order.
    if (const Ooxml::XmlElement *list = root->child(nsP(), u"sldLayoutIdLst")) {
        for (const Ooxml::XmlElement *entry : list->elements(nsP(), u"sldLayoutId")) {
            const auto rel = state.package->relationship(part, entry->attribute(nsR(), u"id"));
            const QString layoutPart = rel ? state.package->targetPart(part, *rel) : QString();
            if (!layoutPart.isEmpty() && !state.layoutByPart.contains(layoutPart)) {
                readLayout(state, layoutPart, int(state.masters.size()) - 1, deck);
            }
        }
    }
    return true;
}

} // namespace QindaOffice::Show::Pptx
