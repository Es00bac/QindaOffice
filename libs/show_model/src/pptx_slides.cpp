// SPDX-License-Identifier: GPL-3.0-or-later
// New Slide, Layout, Reset Slide and Duplicate Slide on the deck's own
// layouts (pptx_slides.h, M82, D-091); Reuse Slides and a template's
// masters are pptx_import.cpp.

#include "pptx_slides.h"

#include "master_edit.h"
#include "pptx_design.h"
#include "pptx_package_edit.h"
#include "pptx_context.h"
#include "pptx_review.h"
#include "pptx_writer.h"
#include "sections.h"

#include "ooxml_package.h"

#include <algorithm>
#include <functional>

namespace QindaOffice::Show {

using namespace Pptx;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

QList<int> sameSlides(const Presentation &deck)
{
    QList<int> out;
    for (int i = 0; i < deck.slides.size(); ++i) {
        out.append(i);
    }
    return out;
}

int slideIndex(const Presentation &deck, const QString &slideId)
{
    for (int i = 0; i < deck.slides.size(); ++i) {
        if (deck.slides.at(i).id == slideId) {
            return i;
        }
    }
    return -1;
}

Presentation failed(const Presentation &deck, QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return deck;
}

// The model shape a written p:cNvPr id stands for on a slide.
const Shape *modelShape(const Slide &slide, const PptxWriteMap &map, quint32 writtenId)
{
    const QHash<QString, quint32> ids = map.shapeIds.value(slide.id);
    for (const Shape &shape : slide.shapes) {
        if (ids.value(shape.id) == writtenId) {
            return &shape;
        }
    }
    return nullptr;
}

quint32 cNvPrId(const XmlElement &shape)
{
    for (const char16_t *wrapper : {u"nvSpPr", u"nvPicPr", u"nvGraphicFramePr", u"nvGrpSpPr", u"nvCxnSpPr"}) {
        const XmlElement *nv = shape.child(nsP(), wrapper);
        if (const XmlElement *cNvPr = nv != nullptr ? nv->child(nsP(), u"cNvPr") : nullptr) {
            return cNvPr->attr(u"id").toUInt();
        }
    }
    return 0;
}

// p:ph rewritten to say `to`'s type and index (the type left out for obj).
void pointAt(XmlElement &shape, const PlaceholderRef &to)
{
    XmlElement *ph = phElement(shape);
    if (ph == nullptr) {
        return;
    }
    ph->attributes.removeIf([](const Ooxml::XmlAttribute &a) {
        return a.ns.isEmpty() && (a.name == u"type" || a.name == u"idx" || a.name == u"sz" || a.name == u"orient");
    });
    if (to.type != u"obj") {
        ph->setAttribute(QString(), QStringLiteral("type"), to.type);
    }
    if (!to.orient.isEmpty()) {
        ph->setAttribute(QString(), QStringLiteral("orient"), to.orient);
    }
    if (!to.size.isEmpty()) {
        ph->setAttribute(QString(), QStringLiteral("sz"), to.size);
    }
    if (to.index >= 0) {
        ph->setAttribute(QString(), QStringLiteral("idx"), QString::number(to.index));
    }
}

QString emu(qreal points)
{
    return QString::number(qRound64(points * 12700.0));
}

// An orphaned placeholder keeps its place: its effective position written
// in as its own (it no longer has a layout placeholder to inherit from).
void pinPosition(XmlElement &shape, const Shape &model)
{
    XmlElement *spPr = shape.child(nsP(), u"spPr");
    if (spPr == nullptr || spPr->child(nsA(), u"xfrm") != nullptr) {
        return;
    }
    auto xfrm = std::make_shared<XmlElement>(nsA(), QStringLiteral("xfrm"));
    if (model.rotation != 0) {
        xfrm->setAttribute(QString(), QStringLiteral("rot"), QString::number(qRound64(model.rotation * 60000.0)));
    }
    XmlElement &off = xfrm->appendElement(nsA(), QStringLiteral("off"));
    off.setAttribute(QString(), QStringLiteral("x"), emu(model.rect.x()));
    off.setAttribute(QString(), QStringLiteral("y"), emu(model.rect.y()));
    XmlElement &ext = xfrm->appendElement(nsA(), QStringLiteral("ext"));
    ext.setAttribute(QString(), QStringLiteral("cx"), emu(model.rect.width()));
    ext.setAttribute(QString(), QStringLiteral("cy"), emu(model.rect.height()));
    spPr->children.prepend({xfrm, QString()});
}

// Run and paragraph formatting of their own taken away (Reset Slide): the
// language and proofing marks stay, the look is the layout's again.
void plainRunProperties(XmlElement &rPr)
{
    static const QStringList kept{QStringLiteral("lang"), QStringLiteral("altLang"), QStringLiteral("dirty"),
                                  QStringLiteral("err"), QStringLiteral("noProof"), QStringLiteral("smtClean")};
    rPr.attributes.removeIf([](const Ooxml::XmlAttribute &a) { return !(a.ns.isEmpty() && kept.contains(a.name)); });
    rPr.children.removeIf([](const Ooxml::XmlChild &c) {
        return !c.element || !(c.element->is(nsA(), u"hlinkClick") || c.element->is(nsA(), u"hlinkMouseOver"));
    });
}

void resetText(XmlElement &txBody)
{
    if (XmlElement *bodyPr = txBody.child(nsA(), u"bodyPr")) {
        bodyPr->attributes.clear();
        bodyPr->children.clear();
    }
    if (XmlElement *lstStyle = txBody.child(nsA(), u"lstStyle")) {
        lstStyle->children.clear();
    }
    for (Ooxml::XmlChild &pc : txBody.children) {
        if (!pc.element || !pc.element->is(nsA(), u"p")) {
            continue;
        }
        for (Ooxml::XmlChild &c : pc.element->children) {
            if (!c.element) {
                continue;
            }
            XmlElement &e = *c.element;
            if (e.is(nsA(), u"pPr")) {
                e.attributes.removeIf([](const Ooxml::XmlAttribute &a) { return !(a.ns.isEmpty() && a.name == u"lvl"); });
                e.children.clear();
            } else if (e.is(nsA(), u"endParaRPr")) {
                plainRunProperties(e);
            } else if (XmlElement *rPr = e.child(nsA(), u"rPr")) {
                plainRunProperties(*rPr); // a:r, a:fld, a:br
            }
        }
    }
}

// The parts a duplicated slide must own rather than share (a chart edited
// on one must not change the other); pictures and media are shared.
bool ownedPart(const QString &type)
{
    static const QStringList kinds{QStringLiteral("/chart"), QStringLiteral("/package"), QStringLiteral("/oleObject"),
                                   QStringLiteral("/diagramData"), QStringLiteral("/diagramLayout"),
                                   QStringLiteral("/diagramQuickStyle"), QStringLiteral("/diagramColors"),
                                   QStringLiteral("/diagramDrawing"), QStringLiteral("/chartStyle"),
                                   QStringLiteral("/chartColorStyle"), QStringLiteral("/themeOverride")};
    return std::any_of(kinds.cbegin(), kinds.cend(), [&type](const QString &kind) { return type.endsWith(kind); });
}

QString copyOwned(Ooxml::PackageWriter &w, const QString &part)
{
    const int slash = int(part.lastIndexOf(QLatin1Char('/')));
    const int dot = int(part.lastIndexOf(QLatin1Char('.')));
    QString stem = dot > slash ? part.left(dot) : part;
    while (!stem.isEmpty() && stem.back().isDigit()) {
        stem.chop(1);
    }
    const QString copy = w.uniquePartName(stem, dot > slash ? part.mid(dot) : QString());
    w.addPart(copy, w.partData(part), w.contentTypeOf(part));
    for (const Ooxml::Relationship &rel : w.relationshipsOf(part)) {
        const QString target = !rel.external && ownedPart(rel.type) ? copyOwned(w, rel.target) : rel.target;
        w.addRelationshipWithId(copy, rel.id, rel.type, target, rel.external);
    }
    return copy;
}

QString nextSlidePart(const Ooxml::PackageWriter &w)
{
    return w.uniquePartName(QStringLiteral("ppt/slides/slide"), QStringLiteral(".xml"));
}

} // namespace

QList<LayoutChoice> layoutChoices(const Presentation &deck)
{
    QList<LayoutChoice> out;
    for (const SlideMaster &master : deck.masters) {
        for (const SlideLayout *layout : layoutsOfMaster(deck, master.id)) {
            out.append({master.id, master.name, layout->id, layout->name.isEmpty() ? layout->type : layout->name, layout->type});
        }
    }
    return out;
}

QString nextSlideLayout(const Presentation &deck, const Slide *slide)
{
    const QList<LayoutChoice> choices = layoutChoices(deck);
    if (choices.isEmpty()) {
        return {};
    }
    const SlideLayout *own = slide != nullptr ? deck.findLayout(slide->layoutId) : nullptr;
    const QString master = own != nullptr ? own->master : choices.first().masterId;
    // After a title slide (or with nothing to follow) PowerPoint takes the
    // master's Title and Content layout.
    if (own == nullptr || own->type == u"title") {
        for (const LayoutChoice &choice : choices) {
            if (choice.masterId == master && choice.type == u"obj") {
                return choice.layoutId;
            }
        }
    }
    return own != nullptr ? own->id : choices.first().layoutId;
}

Presentation withNewSlide(const Presentation &deck, const QString &layoutId, int index, QString *newSlideId, QString *error)
{
    PptxWriteMap map;
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck, &map);
    const QString layoutPart = map.layoutParts.value(layoutId);
    if (layoutPart.isEmpty()) {
        return failed(deck, error, QStringLiteral("the deck has no layout %1").arg(layoutId));
    }
    PackageEdit edit(*written);
    const XmlElementPtr layout = edit.xml(layoutPart);
    if (!layout) {
        return failed(deck, error, QStringLiteral("the layout %1 is not readable").arg(layoutPart));
    }
    index = index < 0 || index > deck.slides.size() ? int(deck.slides.size()) : index;
    // The date, footer and slide number come with it when the slide before
    // (the first slide, at the top) shows them, as PowerPoint's Header and
    // Footer settings do.
    QList<XmlElementPtr> footers;
    const Slide *neighbour = index > 0 ? &deck.slides.at(index - 1) : (deck.slides.isEmpty() ? nullptr : &deck.slides.first());
    if (neighbour != nullptr) {
        const XmlElementPtr before = edit.xml(map.slideParts.value(neighbour->id));
        XmlElement *tree = before ? shapeTree(*before) : nullptr;
        for (const XmlElement *shape : tree != nullptr ? tree->elements() : QList<const XmlElement *>()) {
            if (placeholderOf(*shape).isFooter() && shape->is(nsP(), u"sp")) {
                XmlElementPtr copy = shape->clone();
                // Relationship ids would name the other slide's relationships.
                std::function<void(XmlElement &)> strip = [&strip](XmlElement &e) {
                    e.attributes.removeIf([](const Ooxml::XmlAttribute &a) { return Ooxml::isRelationshipIdAttribute(a); });
                    for (Ooxml::XmlChild &c : e.children) {
                        if (c.element) {
                            strip(*c.element);
                        }
                    }
                };
                strip(*copy);
                footers.append(copy);
            }
        }
    }
    QList<XmlElementPtr> shapes;
    quint32 id = 2;
    for (const PlaceholderRef &ph : placeholdersOf(*layout)) {
        if (!ph.isFooter()) {
            shapes.append(emptyPlaceholder(ph, id++));
        }
    }
    for (const XmlElementPtr &footer : std::as_const(footers)) {
        if (XmlElement *nv = footer->child(nsP(), u"nvSpPr")) {
            if (XmlElement *cNvPr = nv->child(nsP(), u"cNvPr")) {
                cNvPr->setAttribute(QString(), QStringLiteral("id"), QString::number(id++));
            }
        }
        // A slide number field shows this slide's number (its cached text is
        // what is drawn until PowerPoint updates its fields).
        std::function<void(XmlElement &)> renumber = [&renumber, index](XmlElement &e) {
            if (e.is(nsA(), u"fld") && e.attr(u"type") == u"slidenum") {
                if (XmlElement *t = e.child(nsA(), u"t")) {
                    t->children.clear();
                    t->appendText(QString::number(index + 1));
                }
            }
            for (Ooxml::XmlChild &c : e.children) {
                if (c.element) {
                    renumber(*c.element);
                }
            }
        };
        renumber(*footer);
        shapes.append(footer);
    }
    const QString part = nextSlidePart(*written);
    written->addXmlPart(part, *newSlideRoot(shapes), slideContentType());
    written->addRelationship(part, slideRelType("slideLayout"), layoutPart);
    edit.insertSlide(part, index);
    edit.commit();
    QList<int> fromDeck = sameSlides(deck);
    fromDeck.insert(index, -1);
    Presentation fresh = readBackEdited(deck, *written, fromDeck, error);
    if (fresh.slides.size() != deck.slides.size() + 1) {
        return deck;
    }
    normalizeSections(fresh); // the new slide joins the section before it
    if (newSlideId != nullptr) {
        *newSlideId = fresh.slides.at(index).id;
    }
    return fresh;
}

Presentation withSlideLayout(const Presentation &deck, const QString &slideId, const QString &layoutId, QString *error)
{
    const int at = slideIndex(deck, slideId);
    PptxWriteMap map;
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck, &map);
    const QString part = map.slideParts.value(slideId);
    const QString layoutPart = map.layoutParts.value(layoutId);
    if (at < 0 || part.isEmpty() || layoutPart.isEmpty()) {
        return failed(deck, error, QStringLiteral("no such slide or layout"));
    }
    PackageEdit edit(*written);
    const XmlElementPtr sld = edit.xml(part);
    const XmlElementPtr layout = edit.xml(layoutPart);
    XmlElement *tree = sld ? shapeTree(*sld) : nullptr;
    if (tree == nullptr || !layout) {
        return failed(deck, error, QStringLiteral("the slide or its layout is not readable"));
    }
    const QList<PlaceholderRef> wanted = placeholdersOf(*layout);
    QList<int> taken;
    for (Ooxml::XmlChild &child : tree->children) {
        if (!child.element) {
            continue;
        }
        const PlaceholderRef ph = placeholderOf(*child.element);
        if (ph.type.isEmpty()) {
            continue;
        }
        const int match = matchPlaceholder(ph, wanted, taken);
        if (match >= 0) {
            taken.append(match);
            pointAt(*child.element, wanted.at(match)); // its text, the new place and look
        } else if (const Shape *model = modelShape(deck.slides.at(at), map, cNvPrId(*child.element))) {
            pinPosition(*child.element, *model); // stays where it was
        }
    }
    quint32 id = largestShapeId(*tree) + 1;
    for (int i = 0; i < wanted.size(); ++i) {
        if (!taken.contains(i) && !wanted.at(i).isFooter()) {
            tree->appendElement(emptyPlaceholder(wanted.at(i), id++));
        }
    }
    edit.setXml(part, *sld);
    edit.setLayout(part, layoutPart);
    return readBackEdited(deck, *written, sameSlides(deck), error);
}

Presentation withResetSlide(const Presentation &deck, const QString &slideId, QString *error)
{
    PptxWriteMap map;
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck, &map);
    const QString part = map.slideParts.value(slideId);
    if (part.isEmpty()) {
        return failed(deck, error, QStringLiteral("no such slide"));
    }
    PackageEdit edit(*written);
    const XmlElementPtr sld = edit.xml(part);
    const XmlElementPtr layout = edit.xml(edit.layoutOf(part));
    XmlElement *tree = sld ? shapeTree(*sld) : nullptr;
    if (tree == nullptr || !layout) {
        return failed(deck, error, QStringLiteral("the slide or its layout is not readable"));
    }
    const QList<PlaceholderRef> wanted = placeholdersOf(*layout);
    QList<int> taken;
    for (Ooxml::XmlChild &child : tree->children) {
        if (!child.element) {
            continue;
        }
        XmlElement &shape = *child.element;
        const PlaceholderRef ph = placeholderOf(shape);
        if (ph.type.isEmpty()) {
            continue; // shapes of the slide's own stay as they are
        }
        const int match = matchPlaceholder(ph, wanted, taken);
        if (match >= 0) {
            taken.append(match);
        }
        if (shape.is(nsP(), u"sp")) {
            if (XmlElement *spPr = shape.child(nsP(), u"spPr")) {
                spPr->attributes.clear();
                spPr->children.clear();
            }
            shape.removeChildren(nsP(), u"style");
            if (XmlElement *body = shape.child(nsP(), u"txBody")) {
                resetText(*body);
            }
        } else if (shape.is(nsP(), u"pic")) {
            if (XmlElement *spPr = shape.child(nsP(), u"spPr")) {
                spPr->removeChildren(nsA(), u"xfrm");
            }
        } else if (shape.is(nsP(), u"graphicFrame") && match >= 0 && !wanted.at(match).rectEmu.isEmpty()) {
            // A table or chart keeps a frame position (p:xfrm is required):
            // the layout's, when it has one.
            if (XmlElement *xfrm = shape.child(nsP(), u"xfrm")) {
                const QRectF r = wanted.at(match).rectEmu;
                if (XmlElement *off = xfrm->child(nsA(), u"off")) {
                    off->setAttribute(QString(), QStringLiteral("x"), QString::number(qRound64(r.x())));
                    off->setAttribute(QString(), QStringLiteral("y"), QString::number(qRound64(r.y())));
                }
                if (XmlElement *ext = xfrm->child(nsA(), u"ext")) {
                    ext->setAttribute(QString(), QStringLiteral("cx"), QString::number(qRound64(r.width())));
                    ext->setAttribute(QString(), QStringLiteral("cy"), QString::number(qRound64(r.height())));
                }
            }
        }
    }
    // The layout's placeholders the slide lost come back, empty.
    quint32 id = largestShapeId(*tree) + 1;
    for (int i = 0; i < wanted.size(); ++i) {
        if (!taken.contains(i) && !wanted.at(i).isFooter()) {
            tree->appendElement(emptyPlaceholder(wanted.at(i), id++));
        }
    }
    edit.setXml(part, *sld);
    return readBackEdited(deck, *written, sameSlides(deck), error);
}

Presentation withDuplicatedSlide(const Presentation &deck, const QString &slideId, QString *newSlideId, QString *error)
{
    const int at = slideIndex(deck, slideId);
    PptxWriteMap map;
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck, &map);
    const QString part = map.slideParts.value(slideId);
    if (at < 0 || part.isEmpty()) {
        return failed(deck, error, QStringLiteral("no such slide"));
    }
    PackageEdit edit(*written);
    const XmlElementPtr sld = edit.xml(part);
    if (!sld) {
        return failed(deck, error, QStringLiteral("the slide is not readable"));
    }
    renewCreationId(*sld);
    // Its comments stay with the original (the modern comments' link).
    if (XmlElement *list = sld->child(nsP(), u"extLst")) {
        list->children.removeIf([](const Ooxml::XmlChild &c) {
            return c.element && c.element->attr(u"uri") == QStringView(kCommentRelUri);
        });
    }
    const QString copy = nextSlidePart(*written);
    written->addXmlPart(copy, *sld, slideContentType());
    for (const Ooxml::Relationship &rel : written->relationshipsOf(part)) {
        if (rel.type.endsWith(QLatin1String("/notesSlide")) || rel.type.endsWith(QLatin1String("/comments"))) {
            continue; // notes come back from the model below; comments stay
        }
        const QString target = !rel.external && ownedPart(rel.type) ? copyOwned(*written, rel.target) : rel.target;
        written->addRelationshipWithId(copy, rel.id, rel.type, target, rel.external);
    }
    edit.insertSlide(copy, at + 1);
    edit.commit();
    QList<int> fromDeck = sameSlides(deck);
    fromDeck.insert(at + 1, -1);
    Presentation fresh = readBackEdited(deck, *written, fromDeck, error);
    if (fresh.slides.size() != deck.slides.size() + 1) {
        return deck;
    }
    Slide &duplicate = fresh.slides[at + 1];
    duplicate.notes = deck.slides.at(at).notes; // written as its own notes slide
    if (duplicate.title.isEmpty()) {
        duplicate.title = deck.slides.at(at).title;
    }
    normalizeSections(fresh);
    if (newSlideId != nullptr) {
        *newSlideId = duplicate.id;
    }
    return fresh;
}

} // namespace QindaOffice::Show
