// SPDX-License-Identifier: GPL-3.0-or-later
// The pptx reader's deck and slides (pptx_reader.h); masters and layouts
// are pptx_read_masters.cpp, sections and comments pptx_read_review.cpp.

#include "pptx_reader.h"

#include "pptx_origin.h"
#include "pptx_read_masters.h"
#include "pptx_read_text.h"
#include "pptx_review.h"
#include "pptx_show.h"
#include "pptx_timing.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_properties.h"
#include "ooxml_units.h"

namespace QindaOffice::Show {

namespace {

using namespace Pptx;

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

// The build or playback the reader mapped from p:timing is part of what it
// produced: the shape's pristine copy gets it too.
void refreshPristine(Shape &shape)
{
    if (shape.origin.source) {
        auto refreshed = std::make_shared<ShapeSource>(*shape.origin.source);
        refreshed->pristine.buildStep = shape.buildStep;
        refreshed->pristine.animation = shape.animation;
        refreshed->pristine.animationMs = shape.animationMs;
        refreshed->pristine.media = shape.media;
        refreshed->pristine.animationDirection = shape.animationDirection; // M91
        refreshed->pristine.exit = shape.exit;
        shape.origin.source = std::move(refreshed);
    }
}

class Reader {
public:
    Reader(std::shared_ptr<const Ooxml::Package> package, PptxReadReport *report)
    {
        m_state.package = std::move(package);
        m_state.report = report;
    }

    bool read(Presentation &deck, QString *error);

private:
    void readSlide(const QString &part, quint32 sldId, Presentation &deck);
    [[nodiscard]] QList<TextParagraph> notesBody(const QString &notesPart, const MasterInfo &master) const;

    DeckReadState m_state;
};

QList<TextParagraph> Reader::notesBody(const QString &notesPart, const MasterInfo &master) const
{
    const Ooxml::XmlElementPtr root = m_state.package->xml(notesPart);
    const Ooxml::XmlElement *cSld = root ? root->child(nsP(), u"cSld") : nullptr;
    const Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree == nullptr) {
        return {};
    }
    const PartContext ctx = m_state.context(notesPart, master, master.map);
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
    const Ooxml::XmlElementPtr root = m_state.package->xml(part);
    if (!root) {
        if (m_state.report != nullptr) {
            m_state.report->warnings.append(QStringLiteral("missing slide part %1").arg(part));
        }
        return;
    }
    const auto layoutRel = m_state.package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::SlideLayout));
    const QString layoutPart = layoutRel ? m_state.package->targetPart(part, *layoutRel) : QString();
    const int layoutIndex = m_state.layoutByPart.value(layoutPart, -1);
    const LayoutInfo *layout = layoutIndex >= 0 ? &m_state.layouts.at(layoutIndex) : nullptr;
    if (m_state.masters.empty()) {
        return;
    }
    const MasterInfo &master = m_state.masters.at(layout != nullptr ? layout->master : 0);
    const PartContext ctx =
        m_state.context(part, master, overriddenMap(layout != nullptr ? layout->map : master.map, root.get()));

    Slide slide;
    slide.id = m_state.slideIds.value(part);
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
        scope.tableStyles = &m_state.tableStyles;
        scope.report = m_state.report;
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
    // M91 (D-099): Advance Slide — after a rehearsed timing, on a click.
    readAdvance(source->transitionXml.get(), &slide.advanceAfterMs, &slide.advanceOnClick);
    source->advanceAfterMs = slide.advanceAfterMs;
    source->advanceOnClick = slide.advanceOnClick;
    source->builds = readBuilds(source->timingXml.get());
    source->media = readMedia(source->timingXml.get());
    for (Shape &shape : slide.shapes) {
        if (shape.nvId == 0) {
            continue;
        }
        bool timed = false;
        for (const BuildEntry &build : std::as_const(source->builds)) {
            if (shape.nvId == build.shapeId && !shape.media.isMedia()) {
                if (build.exit) { // M91: the click that takes it off
                    shape.exit = {build.step, build.animation, build.direction, buildDuration(build)};
                } else {
                    shape.buildStep = build.step;
                    shape.animation = build.animation;
                    shape.animationMs = buildDuration(build);
                    shape.animationDirection = build.direction; // M91: Effect Options
                }
                timed = true;
            }
        }
        // M75: a video or sound plays with the slide or at its click; the
        // click is its build step (it is on the slide before it).
        for (const MediaEntry &entry : std::as_const(source->media)) {
            if (shape.nvId == entry.shapeId && shape.media.isMedia()) {
                shape.media.narration = shape.media.narration || entry.narration;
                shape.media.autoPlay = entry.autoPlay;
                shape.media.loop = entry.loop;
                shape.media.hideWhenStopped = entry.hideWhenStopped;
                shape.media.volume = entry.volume;
                if (shape.media.durationMs == 0) {
                    shape.media.durationMs = entry.durationMs;
                }
                shape.buildStep = entry.autoPlay ? 0 : entry.step;
                timed = true;
            }
        }
        if (timed) {
            refreshPristine(shape);
        }
    }
    if (const auto notesRel = m_state.package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::NotesSlide))) {
        source->notesPart = m_state.package->targetPart(part, *notesRel);
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
    // M75: comments (anchored by Office shape id) and the creation id they
    // name the slide by.
    QHash<quint32, QString> shapeIds;
    for (const Shape &shape : slide.shapes) {
        shapeIds.insert(shape.nvId, shape.id);
    }
    source->creationId = slideCreationId(*root);
    readSlideComments(m_state, part, shapeIds, slide, *source);
    source->background = slide.background;
    source->layout = slide.layout;
    slide.origin.source = std::move(source);
    deck.slides.append(slide);
    if (m_state.report != nullptr) {
        ++m_state.report->slides;
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
    const std::shared_ptr<const Ooxml::Package> &package = m_state.package;
    m_state.main = package->mainPart();
    if (m_state.main.isEmpty() || !package->contentType(m_state.main).contains(QLatin1String("presentationml"))) {
        return fail(QStringLiteral("not a PowerPoint presentation"));
    }
    m_state.presentation = package->xml(m_state.main);
    if (!m_state.presentation) {
        return fail(QStringLiteral("the presentation part is not readable"));
    }
    const Ooxml::XmlElementPtr &presentation = m_state.presentation;
    if (const Ooxml::XmlElement *size = presentation->child(nsP(), u"sldSz")) {
        deck.slideSize = QSizeF(emuToPt(size->attr(u"cx"), 720), emuToPt(size->attr(u"cy"), 405));
    }
    if (const Ooxml::XmlElement *size = presentation->child(nsP(), u"notesSz")) {
        deck.notesSize = QSizeF(emuToPt(size->attr(u"cx"), 540), emuToPt(size->attr(u"cy"), 720));
    }
    if (const auto rel = package->firstOfType(m_state.main, QString::fromUtf16(Ooxml::RelType::TableStyles))) {
        m_state.tableStyles.root = package->xml(package->targetPart(m_state.main, *rel));
    }
    // Slide ids first: hyperlinks anywhere may jump to any slide.
    QList<QPair<QString, quint32>> slides;
    if (const Ooxml::XmlElement *list = presentation->child(nsP(), u"sldIdLst")) {
        for (const Ooxml::XmlElement *entry : list->elements(nsP(), u"sldId")) {
            const auto rel = package->relationship(m_state.main, entry->attribute(nsR(), u"id"));
            const QString part = rel ? package->targetPart(m_state.main, *rel) : QString();
            if (!part.isEmpty()) {
                const quint32 id = entry->attr(u"id").toUInt();
                slides.append({part, id});
                m_state.slideIds.insert(part, QStringLiteral("s%1").arg(id));
            }
        }
    }
    if (const Ooxml::XmlElement *list = presentation->child(nsP(), u"sldMasterIdLst")) {
        for (const Ooxml::XmlElement *entry : list->elements(nsP(), u"sldMasterId")) {
            const auto rel = package->relationship(m_state.main, entry->attribute(nsR(), u"id"));
            const QString part = rel ? package->targetPart(m_state.main, *rel) : QString();
            if (!part.isEmpty()) {
                readMaster(m_state, part, entry->attr(u"id").toUInt(), deck);
            }
        }
    }
    if (m_state.masters.empty()) {
        return fail(QStringLiteral("the presentation has no slide master"));
    }
    for (const auto &[part, id] : slides) {
        readSlide(part, id, deck);
    }
    // M75: sections, and the comment kind the deck uses.
    deck.sections = readSections(m_state);
    // M91 (D-099): custom shows and Set Up Slide Show.
    deck.customShows = readCustomShows(m_state);
    QString presProps;
    deck.showSettings = readShowSettings(m_state, &presProps);
    bool legacy = false;
    bool modern = false;
    for (const Slide &slide : std::as_const(deck.slides)) {
        if (!slide.comments.isEmpty()) {
            (slide.origin->modernComments ? modern : legacy) = true;
        }
    }
    deck.legacyComments = legacy && !modern;
    deck.title = Ooxml::readCoreProperties(*package).title;
    auto source = std::make_shared<DeckSource>();
    source->package = package;
    source->presentationPart = m_state.main;
    source->presentation = presentation;
    source->slideIds = m_state.slideIds;
    source->showSettings = deck.showSettings;
    source->presPropsPart = presProps;
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
