// SPDX-License-Identifier: GPL-3.0-or-later
// Parts of ANOTHER PowerPoint package brought into the deck where
// PowerPoint keeps them (pptx_slides.h, M82, D-091): Reuse Slides (the
// slide parts, with their pictures, charts and media; with their masters,
// layouts and themes when they keep their formatting) and Browse for Themes
// with a template's masters and layouts. Ooxml::RelationshipCopier carries
// each part with everything it names; the deck is then read back.
//
// AGENT-GUARD: a copied master's sldLayoutId values are renumbered above
// every id the deck uses (PackageEdit::takeLayoutId) — two masters from two
// files routinely both start at 2147483649, and PowerPoint repairs a deck
// whose ids collide. Every slide part of the source is mapped before
// anything is copied, so a hyperlink to a slide that is not reused lands on
// a reused one instead of dragging that slide (and its layout) along.

#include "pptx_slides.h"

#include "pptx_design.h"
#include "pptx_origin.h"
#include "pptx_package_edit.h"
#include "pptx_context.h"
#include "pptx_reader.h"
#include "pptx_review.h"
#include "pptx_writer.h"
#include "sections.h"

#include "ooxml_copier.h"
#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_units.h"

#include <QRegularExpression>
#include <QSet>

#include <functional>
#include <tuple>

namespace QindaOffice::Show {

using namespace Pptx;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

Presentation failed(const Presentation &deck, QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return deck;
}

// A layout of `layouts` for one named `name` of `type`: the same name, else
// the same type, else the first content layout, else the first.
QString matchingLayout(const QList<std::tuple<QString, QString, QString>> &layouts, const QString &name, const QString &type)
{
    for (const auto &[layoutName, layoutType, part] : layouts) {
        if (!name.isEmpty() && layoutName.compare(name, Qt::CaseInsensitive) == 0) {
            return part;
        }
    }
    for (const auto &[layoutName, layoutType, part] : layouts) {
        if (!type.isEmpty() && layoutType == type) {
            return part;
        }
    }
    for (const auto &[layoutName, layoutType, part] : layouts) {
        if (layoutType == u"obj") {
            return part;
        }
    }
    return layouts.isEmpty() ? QString() : std::get<2>(layouts.first());
}

// The next free "ppt/slides/slideN.xml" names, `count` of them.
QStringList freeSlideParts(const Ooxml::PackageWriter &w, int count)
{
    static const QRegularExpression number(QStringLiteral("^ppt/slides/slide(\\d+)\\.xml$"));
    int highest = 0;
    for (const QString &part : w.partNames()) {
        const QRegularExpressionMatch match = number.match(part);
        if (match.hasMatch()) {
            highest = qMax(highest, match.captured(1).toInt());
        }
    }
    QStringList out;
    for (int i = 1; i <= count; ++i) {
        out.append(QStringLiteral("ppt/slides/slide%1.xml").arg(highest + i));
    }
    return out;
}

// Copies a master (its layouts, theme and pictures come along) and gives
// it fresh layout ids; the copy's part.
QString importMaster(PackageEdit &edit, Ooxml::RelationshipCopier &copier, const QString &sourceMaster)
{
    const QString part = copier.copyPart(sourceMaster);
    XmlElementPtr root = edit.xml(part);
    if (XmlElement *layouts = root ? root->child(nsP(), u"sldLayoutIdLst") : nullptr) {
        for (Ooxml::XmlChild &child : layouts->children) {
            if (child.element && child.element->is(nsP(), u"sldLayoutId")) {
                child.element->setAttribute(QString(), QStringLiteral("id"), QString::number(edit.takeLayoutId()));
            }
        }
        edit.setXml(part, *root);
    }
    edit.addMaster(part);
    return part;
}

// The layouts of a read deck as (name, type, part) of master `masterId`
// ("" = every master), in the deck's order.
QList<std::tuple<QString, QString, QString>> layoutParts(const Presentation &deck, const QString &masterId,
                                                         const std::function<QString(const QString &)> &partOf)
{
    QList<std::tuple<QString, QString, QString>> out;
    for (const SlideLayout &layout : deck.layouts) {
        if ((masterId.isEmpty() || layout.master == masterId) && layout.origin) {
            const QString part = partOf(layout.origin->part);
            if (!part.isEmpty()) {
                out.append({layout.name, layout.type, part});
            }
        }
    }
    return out;
}

} // namespace

bool packageHasMasters(const Ooxml::Package &package)
{
    const QString main = package.mainPart();
    if (main.isEmpty() || !package.contentType(main).contains(QLatin1String("presentationml"))) {
        return false;
    }
    const XmlElementPtr root = package.xml(main);
    const XmlElement *masters = root ? root->child(nsP(), u"sldMasterIdLst") : nullptr;
    return masters != nullptr && !masters->elements(nsP(), u"sldMasterId").isEmpty();
}

Presentation withReusedSlides(const Presentation &deck, const std::shared_ptr<const Ooxml::Package> &source,
                              const ReuseOptions &options, QStringList *newSlideIds, QString *error)
{
    QString problem;
    const Presentation from = source ? readPptx(source, &problem) : Presentation();
    if (!problem.isEmpty() || from.slides.isEmpty()) {
        return failed(deck, error, problem.isEmpty() ? QStringLiteral("no slides to reuse") : problem);
    }
    QList<int> chosen;
    for (const int s : options.slides) {
        if (s >= 0 && s < from.slides.size() && from.slides.at(s).origin && !chosen.contains(s)) {
            chosen.append(s);
        }
    }
    if (chosen.isEmpty()) {
        return failed(deck, error, QStringLiteral("no slides chosen"));
    }
    PptxWriteMap map;
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck, &map);
    PackageEdit edit(*written);
    Ooxml::RelationshipCopier copier(source, written.get());
    const QStringList parts = freeSlideParts(*written, int(chosen.size()));
    // Every source slide mapped first (see the file comment).
    for (const Slide &slide : from.slides) {
        if (slide.origin) {
            copier.mapPart(slide.origin->part, parts.first());
        }
    }
    for (qsizetype i = 0; i < chosen.size(); ++i) {
        copier.mapPart(from.slides.at(chosen.at(i)).origin->part, parts.at(i));
    }
    // This deck's layouts, for slides that take its formatting.
    QList<std::tuple<QString, QString, QString>> ours;
    for (const SlideLayout &layout : deck.layouts) {
        if (const QString part = map.layoutParts.value(layout.id); !part.isEmpty()) {
            ours.append({layout.name, layout.type, part});
        }
    }
    int index = options.index < 0 || options.index > deck.slides.size() ? int(deck.slides.size()) : options.index;
    const int first = index;
    QHash<QString, QString> masters; // source master part → its copy
    for (qsizetype i = 0; i < chosen.size(); ++i) {
        const Slide &slide = from.slides.at(chosen.at(i));
        const SlideSource &origin = *slide.origin.get();
        const SlideLayout *layout = from.layoutFor(slide);
        QString layoutPart;
        if (options.keepSourceFormatting && layout != nullptr && layout->origin) {
            const SlideMaster *master = from.findMaster(layout->master);
            if (master != nullptr && master->origin && !masters.contains(master->origin->part)) {
                masters.insert(master->origin->part, importMaster(edit, copier, master->origin->part));
            }
            layoutPart = copier.copyPart(layout->origin->part); // made with its master
        } else {
            layoutPart = matchingLayout(ours, layout != nullptr ? layout->name : QString(),
                                        layout != nullptr ? layout->type : QString());
        }
        if (layoutPart.isEmpty()) {
            return failed(deck, error, QStringLiteral("this deck has no layout for the reused slides"));
        }
        XmlElementPtr sld = source->xml(origin.part);
        if (!sld) {
            return failed(deck, error, QStringLiteral("slide %1 is not readable").arg(chosen.at(i) + 1));
        }
        // Its comments stay behind (their authors are the other deck's).
        if (XmlElement *list = sld->child(nsP(), u"extLst")) {
            list->children.removeIf([](const Ooxml::XmlChild &c) {
                return c.element && c.element->attr(u"uri") == QStringView(kCommentRelUri);
            });
        }
        copier.rewrite(*sld, origin.part, parts.at(i)); // its pictures, charts, media
        renewCreationId(*sld);
        written->addXmlPart(parts.at(i), *sld, slideContentType());
        written->addRelationship(parts.at(i), slideRelType("slideLayout"), layoutPart);
        edit.insertSlide(parts.at(i), index++);
    }
    edit.commit();
    QList<int> fromDeck;
    for (int i = 0; i < deck.slides.size(); ++i) {
        fromDeck.append(i);
    }
    for (qsizetype i = 0; i < chosen.size(); ++i) {
        fromDeck.insert(first + int(i), -1);
    }
    Presentation fresh = readBackEdited(deck, *written, fromDeck, error);
    if (fresh.slides.size() != fromDeck.size()) {
        return deck;
    }
    for (qsizetype i = 0; i < chosen.size(); ++i) {
        Slide &reused = fresh.slides[first + int(i)];
        reused.notes = from.slides.at(chosen.at(i)).notes; // written as its own notes slide
        if (newSlideIds != nullptr) {
            newSlideIds->append(reused.id);
        }
    }
    normalizeSections(fresh);
    return fresh;
}

Presentation withTemplateDesign(const Presentation &deck, const std::shared_ptr<const Ooxml::Package> &source,
                                const QStringList &slideIds, QString *error)
{
    QString problem;
    const Presentation design = source ? readPptx(source, &problem) : Presentation();
    if (!problem.isEmpty() || design.masters.isEmpty() || design.layouts.isEmpty()) {
        return failed(deck, error, problem.isEmpty() ? QStringLiteral("the file has no slide masters") : problem);
    }
    PptxWriteMap map;
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck, &map);
    PackageEdit edit(*written);
    Ooxml::RelationshipCopier copier(source, written.get());
    // The template's own slides never come along.
    const QString anySlide = map.slideParts.value(deck.slides.isEmpty() ? QString() : deck.slides.first().id);
    for (const Slide &slide : design.slides) {
        if (slide.origin && !anySlide.isEmpty()) {
            copier.mapPart(slide.origin->part, anySlide);
        }
    }
    const QStringList before = edit.masterParts();
    QString firstMaster;
    for (const SlideMaster &master : design.masters) {
        if (master.origin) {
            const QString part = importMaster(edit, copier, master.origin->part);
            firstMaster = firstMaster.isEmpty() ? part : firstMaster;
        }
    }
    const auto copied = [&copier](const QString &part) { return copier.copyPart(part); };
    const QList<std::tuple<QString, QString, QString>> primary = layoutParts(design, design.masters.first().id, copied);
    const bool all = slideIds.isEmpty();
    QSet<QString> stillUsed; // layout parts slides keep
    for (const Slide &slide : deck.slides) {
        const QString part = map.slideParts.value(slide.id);
        if (!all && !slideIds.contains(slide.id)) {
            stillUsed.insert(edit.layoutOf(part));
            continue;
        }
        const SlideLayout *layout = deck.layoutFor(slide);
        const QString name = layout != nullptr ? layout->name : QString();
        const QString type = layout != nullptr ? layout->type : (slide.layout == u"title" ? QStringLiteral("title")
                                                                 : slide.layout == u"blank" ? QStringLiteral("blank")
                                                                                            : QStringLiteral("obj"));
        const QString target = matchingLayout(primary, name, type);
        if (!target.isEmpty()) {
            edit.setLayout(part, target);
        }
    }
    // Masters no slide uses any more leave, unless preserved (Apply to All).
    for (const QString &master : before) {
        const XmlElementPtr root = edit.xml(master);
        bool used = false;
        for (const Ooxml::Relationship &rel : written->relationshipsOf(master)) {
            used = used || (rel.type == slideRelType("slideLayout") && stillUsed.contains(rel.target));
        }
        const bool preserved = root && Ooxml::Units::parseOnOff(root->attr(u"preserve"), false);
        // Apply to Selected Slides leaves every master in place.
        if (all && !used && !preserved) {
            edit.removeMaster(master);
        }
    }
    // The presentation's own theme follows its first master's.
    if (edit.masterParts().value(0) == firstMaster) {
        for (const Ooxml::Relationship &rel : written->relationshipsOf(firstMaster)) {
            if (rel.type == slideRelType("theme")) {
                for (const Ooxml::Relationship &own : written->relationshipsOf(QString::fromUtf16(kPresentationPart))) {
                    if (own.type == slideRelType("theme")) {
                        written->retargetRelationship(QString::fromUtf16(kPresentationPart), own.id, rel.target);
                    }
                }
            }
        }
    }
    edit.commit();
    QList<int> same;
    for (int i = 0; i < deck.slides.size(); ++i) {
        same.append(i);
    }
    return readBackEdited(deck, *written, same, error);
}

} // namespace QindaOffice::Show
