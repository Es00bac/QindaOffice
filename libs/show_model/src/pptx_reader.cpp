// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_reader.h"

#include "pptx_origin.h"
#include "pptx_read_shapes.h"
#include "pptx_read_text.h"
#include "pptx_timing.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_properties.h"
#include "ooxml_units.h"

#include <QFileInfo>

#include <deque>

namespace QindaOffice::Show {

namespace {

using namespace Pptx;

struct MasterInfo {
    QString part;
    QString id;
    std::shared_ptr<ThemeContext> theme;
    Ooxml::ColorMap map;
    MasterTextStyles styles;
    PlaceholderSet placeholders;
};

struct LayoutInfo {
    QString part;
    QString id;
    int master = -1;
    Ooxml::ColorMap map;
    PlaceholderSet placeholders;
};

QString baseName(const QString &part)
{
    return QFileInfo(part).completeBaseName();
}

Ooxml::ColorMap colorMap(const Ooxml::XmlElement *element)
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

// p:clrMapOvr: a:overrideClrMapping replaces the map, masterClrMapping keeps it.
Ooxml::ColorMap overridden(const Ooxml::ColorMap &base, const Ooxml::XmlElement *root)
{
    const Ooxml::XmlElement *ovr = root != nullptr ? root->child(nsP(), u"clrMapOvr") : nullptr;
    const Ooxml::XmlElement *mapping = ovr != nullptr ? ovr->child(nsA(), u"overrideClrMapping") : nullptr;
    return mapping != nullptr ? colorMap(mapping) : base;
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

QList<Shape> decorations(QList<Shape> shapes)
{
    shapes.removeIf([](const Shape &s) { return s.placeholder.isPlaceholder(); });
    return shapes;
}

QString plainLines(const QList<TextParagraph> &paragraphs)
{
    QStringList lines;
    for (const TextParagraph &p : paragraphs) {
        QString line;
        for (const TextRun &r : p.runs) {
            line += r.text;
        }
        line.replace(QChar(QChar::LineSeparator), QLatin1Char('\n'));
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

class Reader {
public:
    Reader(std::shared_ptr<const Ooxml::Package> package, PptxReadReport *report)
        : m_package(std::move(package))
        , m_report(report)
    {
    }

    bool read(Presentation &deck, QString *error);

private:
    bool readMaster(const QString &part, quint32 masterId, Presentation &deck);
    void readLayout(const QString &part, int master, Presentation &deck);
    void readSlide(const QString &part, quint32 sldId, Presentation &deck);
    [[nodiscard]] PartContext context(const QString &part, const MasterInfo &master, const Ooxml::ColorMap &map) const;
    [[nodiscard]] QList<TextParagraph> notesBody(const QString &notesPart, const MasterInfo &master) const;

    std::shared_ptr<const Ooxml::Package> m_package;
    PptxReadReport *m_report;
    QString m_main;
    Ooxml::XmlElementPtr m_presentation;
    QHash<QString, QString> m_slideIds;
    std::deque<MasterInfo> m_masters;
    std::deque<LayoutInfo> m_layouts;
    QHash<QString, int> m_layoutByPart;
    TableStyles m_tableStyles;
};

PartContext Reader::context(const QString &part, const MasterInfo &master, const Ooxml::ColorMap &map) const
{
    PartContext ctx;
    ctx.package = m_package;
    ctx.part = part;
    ctx.theme = master.theme.get();
    ctx.map = map;
    ctx.slideIds = &m_slideIds;
    return ctx;
}

bool Reader::readMaster(const QString &part, quint32 masterId, Presentation &deck)
{
    const Ooxml::XmlElementPtr root = m_package->xml(part);
    if (!root) {
        return false;
    }
    MasterInfo info;
    info.part = part;
    info.id = baseName(part);
    const std::optional<Ooxml::Relationship> themeRel =
        m_package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::Theme));
    const Ooxml::XmlElementPtr theme = themeRel ? m_package->xml(m_package->targetPart(part, *themeRel)) : nullptr;
    info.theme = std::make_shared<ThemeContext>(parseThemeContext(theme.get()));
    info.map = colorMap(root->child(nsP(), u"clrMap"));
    const PartContext ctx = context(part, info, info.map);
    const Ooxml::XmlElement *defaults = m_presentation->child(nsP(), u"defaultTextStyle");
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
    if (tree != nullptr) {
        ShapeScope scope;
        scope.ctx = ctx;
        scope.styles = &info.styles;
        scope.tableStyles = &m_tableStyles;
        scope.idPrefix = QStringLiteral("m%1sp").arg(m_masters.size() + 1);
        QSet<QString> ids;
        scope.usedIds = &ids;
        master.shapes = decorations(readShapeTree(scope, *tree));
    }
    auto source = std::make_shared<MasterSource>();
    source->part = part;
    source->masterId = masterId;
    source->background = master.background;
    source->titleColor = master.titleColor;
    source->bodyColor = master.bodyColor;
    source->fontFamily = master.fontFamily;
    master.origin.source = std::move(source);
    deck.masters.append(master);
    m_masters.push_back(std::move(info));
    // Its layouts, in the master's own order.
    if (const Ooxml::XmlElement *list = root->child(nsP(), u"sldLayoutIdLst")) {
        for (const Ooxml::XmlElement *entry : list->elements(nsP(), u"sldLayoutId")) {
            const auto rel = m_package->relationship(part, entry->attribute(nsR(), u"id"));
            const QString layoutPart = rel ? m_package->targetPart(part, *rel) : QString();
            if (!layoutPart.isEmpty() && !m_layoutByPart.contains(layoutPart)) {
                readLayout(layoutPart, int(m_masters.size()) - 1, deck);
            }
        }
    }
    return true;
}

void Reader::readLayout(const QString &part, int masterIndex, Presentation &deck)
{
    const Ooxml::XmlElementPtr root = m_package->xml(part);
    if (!root) {
        return;
    }
    const MasterInfo &master = m_masters.at(masterIndex);
    LayoutInfo info;
    info.part = part;
    info.id = baseName(part);
    info.master = masterIndex;
    info.map = overridden(master.map, root.get());
    const PartContext ctx = context(part, master, info.map);
    const Ooxml::XmlElement *cSld = root->child(nsP(), u"cSld");
    const Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    SlideLayout layout;
    layout.id = info.id;
    layout.name = cSld != nullptr ? cSld->attr(u"name") : QString();
    layout.type = root->attr(u"type", QStringLiteral("cust"));
    layout.master = master.id;
    layout.showMasterShapes = Ooxml::Units::parseOnOff(root->attr(u"showMasterSp"), true);
    layout.background = backgroundOf(ctx, cSld);
    if (tree != nullptr) {
        info.placeholders = collectPlaceholders(ctx, *tree);
        ShapeScope scope;
        scope.ctx = ctx;
        scope.master = &master.placeholders;
        scope.styles = &master.styles;
        scope.tableStyles = &m_tableStyles;
        scope.idPrefix = QStringLiteral("l%1sp").arg(m_layouts.size() + 1);
        QSet<QString> ids;
        scope.usedIds = &ids;
        layout.shapes = decorations(readShapeTree(scope, *tree));
    }
    auto source = std::make_shared<LayoutSource>();
    source->part = part;
    layout.origin.source = std::move(source);
    deck.layouts.append(layout);
    m_layoutByPart.insert(part, int(m_layouts.size()));
    m_layouts.push_back(std::move(info));
}

QList<TextParagraph> Reader::notesBody(const QString &notesPart, const MasterInfo &master) const
{
    const Ooxml::XmlElementPtr root = m_package->xml(notesPart);
    const Ooxml::XmlElement *cSld = root ? root->child(nsP(), u"cSld") : nullptr;
    const Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree == nullptr) {
        return {};
    }
    const PartContext ctx = context(notesPart, master, master.map);
    for (const Ooxml::XmlElement *sp : tree->elements(nsP(), u"sp")) {
        const Ooxml::XmlElement *nv = sp->child(nsP(), u"nvSpPr");
        const Ooxml::XmlElement *nvPr = nv != nullptr ? nv->child(nsP(), u"nvPr") : nullptr;
        const Ooxml::XmlElement *ph = nvPr != nullptr ? nvPr->child(nsP(), u"ph") : nullptr;
        if (ph != nullptr && ph->attr(u"type") == u"body") {
            return readTextBody(ctx, sp->child(nsP(), u"txBody"), ListStyle());
        }
    }
    return {};
}

void Reader::readSlide(const QString &part, quint32 sldId, Presentation &deck)
{
    const Ooxml::XmlElementPtr root = m_package->xml(part);
    if (!root) {
        if (m_report != nullptr) {
            m_report->warnings.append(QStringLiteral("missing slide part %1").arg(part));
        }
        return;
    }
    const auto layoutRel = m_package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::SlideLayout));
    const QString layoutPart = layoutRel ? m_package->targetPart(part, *layoutRel) : QString();
    const int layoutIndex = m_layoutByPart.value(layoutPart, -1);
    const LayoutInfo *layout = layoutIndex >= 0 ? &m_layouts.at(layoutIndex) : nullptr;
    if (m_masters.empty()) {
        return;
    }
    const MasterInfo &master = m_masters.at(layout != nullptr ? layout->master : 0);
    const PartContext ctx = context(part, master, overridden(layout != nullptr ? layout->map : master.map, root.get()));

    Slide slide;
    slide.id = m_slideIds.value(part);
    slide.master = master.id;
    if (layout != nullptr) {
        slide.layoutId = layout->id;
        slide.layout = appLayoutName(deck.layouts.at(layoutIndex).type);
    }
    slide.hidden = root->attr(u"show") == u"0" || root->attr(u"show") == u"false";
    slide.showMasterShapes = Ooxml::Units::parseOnOff(root->attr(u"showMasterSp"), true);
    const Ooxml::XmlElement *cSld = root->child(nsP(), u"cSld");
    slide.background = backgroundOf(ctx, cSld);
    if (const Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr) {
        ShapeScope scope;
        scope.ctx = ctx;
        scope.layout = layout != nullptr ? &layout->placeholders : nullptr;
        scope.master = &master.placeholders;
        scope.styles = &master.styles;
        scope.tableStyles = &m_tableStyles;
        scope.report = m_report;
        QSet<QString> ids;
        scope.usedIds = &ids;
        slide.shapes = readShapeTree(scope, *tree);
    }
    auto source = std::make_shared<SlideSource>();
    source->part = part;
    source->xml = root;
    source->sldId = sldId;
    source->layoutPart = layoutPart;
    // Transition: p:transition, or the mc:AlternateContent around a newer one.
    for (const Ooxml::XmlChild &child : root->children) {
        if (!child.element) {
            continue;
        }
        const Ooxml::XmlElement &e = *child.element;
        if (e.is(nsP(), u"transition")
            || (e.is(nsMc(), u"AlternateContent") && e.descendant(nsP(), u"transition") != nullptr)) {
            source->transitionXml = child.element;
        } else if (e.is(nsP(), u"timing")) {
            source->timingXml = child.element;
        }
    }
    slide.transition = transitionName(source->transitionXml.get());
    source->transition = slide.transition;
    source->builds = readBuilds(source->timingXml.get());
    for (const BuildEntry &build : source->builds) {
        for (Shape &shape : slide.shapes) {
            if (shape.nvId == build.shapeId && shape.nvId != 0) {
                shape.buildStep = build.step;
                shape.animation = build.animation;
                shape.animationMs = build.durationMs > 0 ? qBound(50, build.durationMs, 5000) : 350;
                // The build is part of what the reader produced.
                if (shape.origin.source) {
                    auto refreshed = std::make_shared<ShapeSource>(*shape.origin.source);
                    refreshed->pristine.buildStep = shape.buildStep;
                    refreshed->pristine.animation = shape.animation;
                    refreshed->pristine.animationMs = shape.animationMs;
                    shape.origin.source = std::move(refreshed);
                }
            }
        }
    }
    if (const auto notesRel = m_package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::NotesSlide))) {
        source->notesPart = m_package->targetPart(part, *notesRel);
        // M67 (D-076): the paragraphs too, so an edit keeps their spelling.
        source->notesParagraphs = notesBody(source->notesPart, master);
        source->notes = plainLines(source->notesParagraphs).trimmed();
        slide.notes = source->notes;
    }
    for (const Shape &shape : slide.shapes) {
        if (shape.placeholder.type == u"title" || shape.placeholder.type == u"ctrTitle") {
            slide.title = shape.displayText().replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();
            break;
        }
    }
    if (slide.title.isEmpty() && cSld != nullptr) {
        slide.title = cSld->attr(u"name");
    }
    source->background = slide.background;
    source->layout = slide.layout;
    slide.origin.source = std::move(source);
    deck.slides.append(slide);
    if (m_report != nullptr) {
        ++m_report->slides;
    }
}

bool Reader::read(Presentation &deck, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    m_main = m_package->mainPart();
    if (m_main.isEmpty() || !m_package->contentType(m_main).contains(QLatin1String("presentationml"))) {
        return fail(QStringLiteral("not a PowerPoint presentation"));
    }
    m_presentation = m_package->xml(m_main);
    if (!m_presentation) {
        return fail(QStringLiteral("the presentation part is not readable"));
    }
    if (const Ooxml::XmlElement *size = m_presentation->child(nsP(), u"sldSz")) {
        deck.slideSize = QSizeF(emuToPt(size->attr(u"cx"), 720), emuToPt(size->attr(u"cy"), 405));
    }
    if (const Ooxml::XmlElement *size = m_presentation->child(nsP(), u"notesSz")) {
        deck.notesSize = QSizeF(emuToPt(size->attr(u"cx"), 540), emuToPt(size->attr(u"cy"), 720));
    }
    if (const auto rel = m_package->firstOfType(m_main, QString::fromUtf16(Ooxml::RelType::TableStyles))) {
        m_tableStyles.root = m_package->xml(m_package->targetPart(m_main, *rel));
    }
    // Slide ids first: hyperlinks anywhere may jump to any slide.
    QList<QPair<QString, quint32>> slides;
    if (const Ooxml::XmlElement *list = m_presentation->child(nsP(), u"sldIdLst")) {
        for (const Ooxml::XmlElement *entry : list->elements(nsP(), u"sldId")) {
            const auto rel = m_package->relationship(m_main, entry->attribute(nsR(), u"id"));
            const QString part = rel ? m_package->targetPart(m_main, *rel) : QString();
            if (!part.isEmpty()) {
                const quint32 id = entry->attr(u"id").toUInt();
                slides.append({part, id});
                m_slideIds.insert(part, QStringLiteral("s%1").arg(id));
            }
        }
    }
    if (const Ooxml::XmlElement *list = m_presentation->child(nsP(), u"sldMasterIdLst")) {
        for (const Ooxml::XmlElement *entry : list->elements(nsP(), u"sldMasterId")) {
            const auto rel = m_package->relationship(m_main, entry->attribute(nsR(), u"id"));
            const QString part = rel ? m_package->targetPart(m_main, *rel) : QString();
            if (!part.isEmpty()) {
                readMaster(part, entry->attr(u"id").toUInt(), deck);
            }
        }
    }
    if (m_masters.empty()) {
        return fail(QStringLiteral("the presentation has no slide master"));
    }
    for (const auto &[part, id] : slides) {
        readSlide(part, id, deck);
    }
    deck.title = Ooxml::readCoreProperties(*m_package).title;
    auto source = std::make_shared<DeckSource>();
    source->package = m_package;
    source->presentationPart = m_main;
    source->presentation = m_presentation;
    source->slideIds = m_slideIds;
    deck.origin.source = std::move(source);
    return true;
}

} // namespace

bool looksLikeZip(const QByteArray &bytes)
{
    return bytes.startsWith("PK\x03\x04");
}

Presentation readPptx(const std::shared_ptr<const Ooxml::Package> &package, QString *error, PptxReadReport *report)
{
    Presentation deck;
    deck.masters.clear();
    if (!package) {
        if (error != nullptr) {
            *error = QStringLiteral("no package");
        }
        return deck;
    }
    Reader reader(package, report);
    if (!reader.read(deck, error)) {
        return Presentation();
    }
    return deck;
}

Presentation readPptxFile(const QString &path, QString *error, PptxReadReport *report)
{
    QString loadError;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::load(path, &loadError);
    if (!package) {
        if (error != nullptr) {
            *error = loadError;
        }
        return Presentation();
    }
    return readPptx(package, error, report);
}

Presentation readPptxData(const QByteArray &zip, QString *error, PptxReadReport *report)
{
    QString loadError;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::fromData(zip, &loadError);
    if (!package) {
        if (error != nullptr) {
            *error = loadError;
        }
        return Presentation();
    }
    return readPptx(package, error, report);
}

} // namespace QindaOffice::Show
