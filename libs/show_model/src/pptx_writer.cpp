// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_writer.h"

#include "pptx_write_parts.h"
#include "pptx_write_slide.h"

#include "ooxml_namespaces.h"
#include "ooxml_properties.h"

#include <QSaveFile>

namespace QindaOffice::Show {

namespace {

using namespace Pptx;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

const QString kPresentationPart = QStringLiteral("ppt/presentation.xml");
const QString kSections = QStringLiteral("{521415D9-36F7-43E2-AB2F-B90AF26B5E84}");

QString pmlType(const char *kind)
{
    return QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.") + QLatin1String(kind);
}

struct MasterOut {
    QString modelId;
    QString part;
    quint32 masterId = 0;
    bool generated = false;
    GeneratedMaster made;
    QList<std::tuple<QString, QString, QString>> layouts; // carried: model id, type, part
};

QList<quint32> layoutIdsOf(const QByteArray &masterXml)
{
    QList<quint32> ids;
    const XmlElementPtr root = Ooxml::parseXml(masterXml);
    const XmlElement *list = root ? root->child(nsP(), u"sldLayoutIdLst") : nullptr;
    if (list != nullptr) {
        for (const XmlElement *entry : list->elements(nsP(), u"sldLayoutId")) {
            ids.append(entry->attr(u"id").toUInt());
        }
    }
    return ids;
}

void setRunLook(XmlElement &style, const QColor &color, const QString &font)
{
    for (Ooxml::XmlChild &child : style.children) {
        if (!child.element) {
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

// The Masters dialog restyled a master read from .pptx: write its new
// background and title/body colours (and font) into the carried part.
void patchMaster(WriteSession &session, const QString &part, const SlideMaster &master)
{
    const MasterSource *was = master.origin.get();
    const bool fontChanged = was->fontFamily != master.fontFamily;
    if (was->background == master.background && was->titleColor == master.titleColor
        && was->bodyColor == master.bodyColor && !fontChanged) {
        return;
    }
    XmlElementPtr root = Ooxml::parseXml(session.writer.partData(part));
    if (!root) {
        return;
    }
    if (XmlElement *cSld = root->child(nsP(), u"cSld"); cSld != nullptr && was->background != master.background) {
        cSld->removeChildren(nsP(), u"bg");
        auto bg = std::make_shared<XmlElement>(nsP(), QStringLiteral("bg"));
        XmlElement &pr = bg->appendElement(nsP(), QStringLiteral("bgPr"));
        auto solid = std::make_shared<XmlElement>(nsA(), QStringLiteral("solidFill"));
        solid->appendElement(colorElement(master.background));
        pr.appendElement(solid);
        pr.appendElement(nsA(), QStringLiteral("effectLst"));
        cSld->insertElementBefore(bg, {u"spTree"});
    }
    if (XmlElement *styles = root->child(nsP(), u"txStyles")) {
        if (XmlElement *title = styles->child(nsP(), u"titleStyle"); title != nullptr && (was->titleColor != master.titleColor || fontChanged)) {
            setRunLook(*title, master.titleColor, fontChanged ? master.fontFamily : QString());
        }
        if (XmlElement *body = styles->child(nsP(), u"bodyStyle"); body != nullptr && (was->bodyColor != master.bodyColor || fontChanged)) {
            setRunLook(*body, master.bodyColor, fontChanged ? master.fontFamily : QString());
        }
    }
    session.writer.addPart(part, Ooxml::serializeDocument(*root));
}

QString pickLayout(const MasterOut &master, const Slide &slide)
{
    if (master.generated) {
        // A model layout keeps its slides while they keep its kind.
        for (const auto &[id, type, part] : master.made.modelLayouts) {
            if (id == slide.layoutId && appLayoutName(type) == slide.layout) {
                return part;
            }
        }
        return slide.layout == u"title" ? master.made.titleLayout
            : slide.layout == u"blank"  ? master.made.blankLayout
                                        : master.made.contentLayout;
    }
    const SlideSource *source = slide.origin.get();
    const bool keep = source == nullptr || source->layout == slide.layout;
    for (const auto &[id, type, part] : master.layouts) {
        if (keep && id == slide.layoutId) {
            return part;
        }
    }
    // The user picked another layout: the master's layout of that kind.
    for (const auto &[id, type, part] : master.layouts) {
        if (appLayoutName(type) == slide.layout) {
            return part;
        }
    }
    return master.layouts.isEmpty() ? QString() : std::get<2>(master.layouts.first());
}

XmlElementPtr idList(const char *list, const char *item, const QList<std::pair<quint32, QString>> &entries)
{
    auto e = std::make_shared<XmlElement>(nsP(), QLatin1String(list));
    for (const auto &[id, rel] : entries) {
        XmlElement &entry = e->appendElement(nsP(), QLatin1String(item));
        if (id != 0) {
            entry.setAttribute(QString(), QStringLiteral("id"), QString::number(id));
        }
        entry.setAttribute(nsR(), QStringLiteral("id"), rel);
    }
    return e;
}

XmlElementPtr sizeElement(const char *name, const QSizeF &size)
{
    auto e = std::make_shared<XmlElement>(nsP(), QLatin1String(name));
    e->setAttribute(QString(), QStringLiteral("cx"), emu(size.width()));
    e->setAttribute(QString(), QStringLiteral("cy"), emu(size.height()));
    return e;
}

const QList<QStringView> kPresentationOrder{u"sldMasterIdLst", u"notesMasterIdLst", u"handoutMasterIdLst", u"sldIdLst",
                                            u"sldSz", u"notesSz", u"smartTags", u"embeddedFontLst", u"custShowLst",
                                            u"photoAlbum", u"custDataLst", u"kinsoku", u"defaultTextStyle",
                                            u"modifyVerifier", u"extLst"};

void insertInOrder(XmlElement &presentation, const XmlElementPtr &element)
{
    const qsizetype at = kPresentationOrder.indexOf(QStringView(element->name));
    presentation.insertElementBefore(element, kPresentationOrder.mid(at + 1));
}

} // namespace

QByteArray writePptx(const Presentation &deck, QString *error)
{
    WriteSession session(deck);
    Ooxml::PackageWriter &w = session.writer;
    const DeckSource *deckSource = deck.origin.get();
    const bool carry = session.copier != nullptr && deckSource != nullptr && deckSource->presentation;

    // Slide (and notes) parts first: hyperlinks and carried parts point at them.
    QHash<QString, QString> notesParts;
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        const Slide &slide = deck.slides.at(i);
        session.slideParts.insert(slide.id, QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1));
        if (!slide.notes.isEmpty()) {
            notesParts.insert(slide.id, QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(i + 1));
        }
    }
    if (carry) {
        QSet<QString> mapped;
        for (const Slide &slide : deck.slides) {
            if (const SlideSource *source = slide.origin.get(); source != nullptr && !mapped.contains(source->part)) {
                session.copier->mapPart(source->part, session.slideParts.value(slide.id));
                mapped.insert(source->part);
                if (!source->notesPart.isEmpty()) {
                    session.copier->mapPart(source->notesPart, notesParts.value(slide.id, session.slideParts.value(slide.id)));
                }
            }
        }
        // Slides deleted since reading: whatever linked to them now lands on the first slide.
        for (auto it = deckSource->slideIds.cbegin(); it != deckSource->slideIds.cend(); ++it) {
            if (!mapped.contains(it.key()) && !deck.slides.isEmpty()) {
                session.copier->mapPart(it.key(), session.slideParts.value(deck.slides.first().id));
            }
        }
    }

    // Masters: carried with their layouts and themes, or generated.
    QList<MasterOut> masters;
    quint32 nextId = 2147483648u;
    for (const SlideMaster &master : deck.masters) {
        MasterOut out;
        out.modelId = master.id;
        if (carry && master.origin && session.source->contains(master.origin->part)) {
            out.part = session.copier->copyPart(master.origin->part);
            out.masterId = master.origin->masterId;
            for (const SlideLayout &layout : deck.layouts) {
                if (layout.master == master.id && layout.origin) {
                    out.layouts.append({layout.id, layout.type, session.copier->copyPart(layout.origin->part)});
                }
            }
            patchMaster(session, out.part, master);
            nextId = qMax(nextId, out.masterId + 1);
            for (const quint32 id : layoutIdsOf(w.partData(out.part))) {
                nextId = qMax(nextId, id + 1);
            }
        }
        masters.append(out);
    }
    QSet<quint32> masterIds;
    for (MasterOut &m : masters) {
        // A carried master keeps its id unless it is out of range or taken.
        if (!m.part.isEmpty() && (m.masterId < 2147483648u || masterIds.contains(m.masterId))) {
            m.masterId = nextId++;
        }
        masterIds.insert(m.masterId);
    }
    for (qsizetype i = 0; i < masters.size(); ++i) {
        if (masters.at(i).part.isEmpty()) {
            masters[i].generated = true;
            masters[i].made = generateMaster(session, &deck.masters.at(i), &nextId);
            masters[i].part = masters.at(i).made.part;
            masters[i].masterId = masters.at(i).made.masterId;
        }
    }
    const auto masterFor = [&](const Slide &slide) -> MasterOut & {
        for (MasterOut &m : masters) {
            if (m.modelId == slide.master && !slide.master.isEmpty()) {
                return m;
            }
        }
        // Plain white slides share one generated master.
        for (MasterOut &m : masters) {
            if (m.modelId.isEmpty()) {
                return m;
            }
        }
        MasterOut plain;
        plain.generated = true;
        plain.made = generateMaster(session, nullptr, &nextId);
        plain.part = plain.made.part;
        plain.masterId = plain.made.masterId;
        masters.append(plain);
        return masters.last();
    };
    // Resolve every slide's master before writing (the list may grow);
    // PowerPoint needs at least one master even for an empty deck.
    for (const Slide &slide : deck.slides) {
        (void)masterFor(slide);
    }
    if (masters.isEmpty()) {
        (void)masterFor(Slide());
    }

    // The notes master, when anything has notes.
    QString notesMaster;
    if (!notesParts.isEmpty()) {
        const XmlElement *list = carry ? deckSource->presentation->child(nsP(), u"notesMasterIdLst") : nullptr;
        const XmlElement *entry = list != nullptr ? list->child(nsP(), u"notesMasterId") : nullptr;
        if (entry != nullptr) {
            const auto rel = session.source->relationship(deckSource->presentationPart, entry->attribute(nsR(), u"id"));
            const QString part = rel ? session.source->targetPart(deckSource->presentationPart, *rel) : QString();
            if (!part.isEmpty()) {
                notesMaster = session.copier->copyPart(part);
            }
        }
        if (notesMaster.isEmpty()) {
            notesMaster = generateNotesMaster(session);
        }
    }

    // Slides.
    QList<std::pair<quint32, QString>> slideIds;
    QSet<quint32> usedSlideIds;
    quint32 nextSlideId = 256;
    for (const Slide &slide : deck.slides) {
        if (slide.origin && slide.origin->sldId >= 256) {
            nextSlideId = qMax(nextSlideId, slide.origin->sldId + 1);
        }
    }
    PartWriter presentation(session, kPresentationPart);
    for (const Slide &slide : deck.slides) {
        const QString part = session.slideParts.value(slide.id);
        PartWriter out(session, part);
        out.relate(relType("slideLayout"), pickLayout(masterFor(slide), slide));
        const XmlElementPtr sld = slideElement(out, slide);
        if (const QString notesPart = notesParts.value(slide.id); !notesPart.isEmpty()) {
            PartWriter notesOut(session, notesPart);
            const XmlElementPtr notes = notesElement(notesOut, slide);
            notesOut.relate(relType("notesMaster"), notesMaster);
            notesOut.relate(relType("slide"), part);
            w.addXmlPart(notesPart, *notes, pmlType("notesSlide+xml"));
            out.relate(relType("notesSlide"), notesPart);
        }
        carryImplicitRelationships(out, slide);
        w.addXmlPart(part, *sld, pmlType("slide+xml"));
        quint32 id = slide.origin ? slide.origin->sldId : 0;
        if (id < 256 || usedSlideIds.contains(id)) {
            id = nextSlideId++;
        }
        usedSlideIds.insert(id);
        slideIds.append({id, presentation.relate(relType("slide"), part)});
    }

    // presentation.xml: the source's, with its lists rebuilt; or new.
    XmlElementPtr pres;
    if (carry) {
        pres = deckSource->presentation->clone();
        for (const char16_t *rebuilt : {u"sldMasterIdLst", u"notesMasterIdLst", u"handoutMasterIdLst", u"sldIdLst",
                                        u"sldSz", u"notesSz", u"custShowLst"}) {
            pres->removeChildren(nsP(), rebuilt);
        }
        // Sections name slides by id: keep them only for an unchanged slide list.
        QList<quint32> sourceIds;
        if (const XmlElement *list = deckSource->presentation->child(nsP(), u"sldIdLst")) {
            for (const XmlElement *entry : list->elements(nsP(), u"sldId")) {
                sourceIds.append(entry->attr(u"id").toUInt());
            }
        }
        QList<quint32> ids;
        for (const auto &entry : slideIds) {
            ids.append(entry.first);
        }
        if (XmlElement *ext = pres->child(nsP(), u"extLst"); ext != nullptr && ids != sourceIds) {
            ext->children.removeIf([](const Ooxml::XmlChild &c) { return c.element && c.element->attr(u"uri") == kSections; });
            if (ext->elements().isEmpty()) {
                pres->removeChildren(nsP(), u"extLst");
            }
        }
        presentation.carry(*pres, deckSource->presentationPart);
        if (const XmlElement *handouts = deckSource->presentation->child(nsP(), u"handoutMasterIdLst")) {
            QList<std::pair<quint32, QString>> entries;
            for (const XmlElement *entry : handouts->elements(nsP(), u"handoutMasterId")) {
                const QString rel = session.copier->copyRelationship(deckSource->presentationPart,
                                                                     entry->attribute(nsR(), u"id"), kPresentationPart);
                if (!rel.isEmpty()) {
                    entries.append({0, rel});
                }
            }
            if (!entries.isEmpty()) {
                insertInOrder(*pres, idList("handoutMasterIdLst", "handoutMasterId", entries));
            }
        }
    } else {
        pres = std::make_shared<XmlElement>(nsP(), QStringLiteral("presentation"));
        pres->setAttribute(QString(), QStringLiteral("saveSubsetFonts"), QStringLiteral("1"));
        pres->appendElement(defaultTextStyleElement());
    }
    QList<std::pair<quint32, QString>> masterEntries;
    for (const MasterOut &m : std::as_const(masters)) {
        masterEntries.append({m.masterId, presentation.relate(relType("slideMaster"), m.part)});
    }
    insertInOrder(*pres, idList("sldMasterIdLst", "sldMasterId", masterEntries));
    if (!notesMaster.isEmpty()) {
        insertInOrder(*pres, idList("notesMasterIdLst", "notesMasterId", {{0, presentation.relate(relType("notesMaster"), notesMaster)}}));
    }
    if (!slideIds.isEmpty()) {
        insertInOrder(*pres, idList("sldIdLst", "sldId", slideIds));
    }
    insertInOrder(*pres, sizeElement("sldSz", deck.slideSize));
    insertInOrder(*pres, sizeElement("notesSz", deck.notesSize));
    w.addXmlPart(kPresentationPart, *pres, pmlType("presentation.main+xml"));
    w.addRelationship(QString(), QString::fromUtf16(Ooxml::RelType::OfficeDocument), kPresentationPart);

    // Presentation-level parts: carried, or generated.
    bool haveProps = false;
    if (carry) {
        for (const Ooxml::Relationship &rel : session.source->relationships(deckSource->presentationPart)) {
            const bool viewProps = rel.type == relType("viewProps");
            if (rel.type == relType("presProps") || rel.type == relType("tableStyles") || rel.type == relType("theme")
                || rel.type.endsWith(QLatin1String("/commentAuthors")) || rel.type.endsWith(QLatin1String("/authors"))
                || (viewProps && session.source->relationships(session.source->targetPart(deckSource->presentationPart, rel)).isEmpty())) {
                session.copier->copyRelationship(deckSource->presentationPart, rel.id, kPresentationPart);
                haveProps = haveProps || rel.type == relType("presProps");
            }
        }
    }
    if (!haveProps) {
        const PresentationParts parts = generatePresentationParts(session);
        presentation.relate(relType("presProps"), parts.presProps);
        presentation.relate(relType("viewProps"), parts.viewProps);
        presentation.relate(relType("tableStyles"), parts.tableStyles);
    }
    // PowerPoint relates the presentation to its (first master's) theme.
    const auto relatedTo = [&w](const QString &part, const QString &type) {
        for (const Ooxml::Relationship &rel : w.relationshipsOf(part)) {
            if (rel.type == type) {
                return rel.target;
            }
        }
        return QString();
    };
    if (relatedTo(kPresentationPart, relType("theme")).isEmpty()) {
        const QString theme = relatedTo(masters.first().part, relType("theme"));
        if (!theme.isEmpty()) {
            presentation.relate(relType("theme"), theme);
        }
    }

    Ooxml::CoreProperties core = session.source ? Ooxml::readCoreProperties(*session.source) : Ooxml::CoreProperties();
    core.title = deck.title;
    core.modified = QDateTime::currentDateTimeUtc();
    if (!core.created.isValid()) {
        core.created = core.modified;
    }
    Ooxml::writeProperties(w, core, QStringLiteral("QindaShow"));
    return w.toZip(error);
}

bool writePptxFile(const Presentation &deck, const QString &path, QString *error)
{
    const QByteArray bytes = writePptx(deck, error);
    if (bytes.isEmpty()) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Show
