// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_write_slide.h"

#include "pptx_review.h"
#include "pptx_timing.h"
#include "pptx_write_parts.h"

#include <algorithm>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

bool isTransition(const XmlElement &e)
{
    return e.is(nsP(), u"transition") || (e.is(nsMc(), u"AlternateContent") && e.descendant(nsP(), u"transition") != nullptr);
}

XmlElementPtr defaultGroupProperties(const char *name)
{
    if (QLatin1String(name) == QLatin1String("nvGrpSpPr")) {
        XmlElementPtr nv = make(nsP(), QStringLiteral("nvGrpSpPr"));
        XmlElement &cNvPr = nv->appendElement(nsP(), QStringLiteral("cNvPr"));
        cNvPr.setAttribute(QString(), QStringLiteral("id"), QStringLiteral("1"));
        cNvPr.setAttribute(QString(), QStringLiteral("name"), QString());
        nv->appendElement(nsP(), QStringLiteral("cNvGrpSpPr"));
        nv->appendElement(nsP(), QStringLiteral("nvPr"));
        return nv;
    }
    XmlElementPtr pr = make(nsP(), QStringLiteral("grpSpPr"));
    XmlElement &xfrm = pr->appendElement(nsA(), QStringLiteral("xfrm"));
    for (const auto &[name2, a, b] : {std::tuple{"off", "x", "y"}, std::tuple{"ext", "cx", "cy"},
                                      std::tuple{"chOff", "x", "y"}, std::tuple{"chExt", "cx", "cy"}}) {
        XmlElement &e = xfrm.appendElement(nsA(), QLatin1String(name2));
        e.setAttribute(QString(), QLatin1String(a), QStringLiteral("0"));
        e.setAttribute(QString(), QLatin1String(b), QStringLiteral("0"));
    }
    return pr;
}

XmlElementPtr carried(PartWriter &out, const XmlElement *element, const QString &sourcePart)
{
    XmlElementPtr copy = element->clone();
    out.carry(*copy, sourcePart);
    return copy;
}

QList<BuildEntry> sorted(QList<BuildEntry> builds)
{
    std::sort(builds.begin(), builds.end(), [](const BuildEntry &a, const BuildEntry &b) {
        return a.shapeId != b.shapeId ? a.shapeId < b.shapeId : a.exit < b.exit;
    });
    return builds;
}

// The notes slide's body placeholder (p:sp with p:ph type="body").
XmlElement *notesBodyShape(XmlElement &notes)
{
    XmlElement *cSld = notes.child(nsP(), u"cSld");
    XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree == nullptr) {
        return nullptr;
    }
    for (Ooxml::XmlChild &c : tree->children) {
        if (!c.element || !c.element->is(nsP(), u"sp")) {
            continue;
        }
        const XmlElement *nv = c.element->child(nsP(), u"nvSpPr");
        const XmlElement *nvPr = nv != nullptr ? nv->child(nsP(), u"nvPr") : nullptr;
        const XmlElement *ph = nvPr != nullptr ? nvPr->child(nsP(), u"ph") : nullptr;
        if (ph != nullptr && ph->attr(u"type") == u"body") {
            return c.element.get();
        }
    }
    return nullptr;
}

QString paragraphText(const TextParagraph &paragraph)
{
    QString text;
    for (const TextRun &run : paragraph.runs) {
        text += run.text;
    }
    return text.replace(QChar(QChar::LineSeparator), QLatin1Char('\n'));
}

// The notes text as paragraphs: a line that still reads as a source
// paragraph IS that paragraph (its runs and spelling kept); any other line
// takes the look of the paragraph in its place.
QList<TextParagraph> notesParagraphs(const QString &notes, const QList<TextParagraph> &was)
{
    TextRun anyRun;
    anyRun.pointSize = 12;
    for (const TextParagraph &p : was) {
        if (!p.runs.isEmpty()) {
            anyRun = p.runs.first();
            break;
        }
    }
    QList<TextParagraph> out;
    const QStringList lines = notes.split(QLatin1Char('\n'));
    qsizetype next = 0;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        qsizetype match = -1;
        for (qsizetype j = next; j < was.size(); ++j) {
            if (paragraphText(was.at(j)) == lines.at(i)) {
                match = j;
                break;
            }
        }
        if (match >= 0) {
            out.append(was.at(match));
            next = match + 1;
            continue;
        }
        TextParagraph paragraph = was.isEmpty() ? TextParagraph() : was.at(qMin(i, was.size() - 1));
        TextRun run = paragraph.runs.isEmpty() ? anyRun : paragraph.runs.first();
        run.text = lines.at(i);
        run.hyperlink.clear();
        run.fieldType.clear();
        run.fieldId.clear();
        paragraph.runs.clear();
        if (!run.text.isEmpty()) {
            paragraph.runs = {run};
        }
        out.append(paragraph);
    }
    return out;
}

bool patchNotesBody(PartWriter &out, XmlElement &notes, const QString &text, const SlideSource &source)
{
    XmlElement *sp = notesBodyShape(notes);
    // The source's own txBody (not the carried copy) is what the reader's
    // paragraphs were made of; reused pieces are carried from it.
    const XmlElementPtr sourceRoot = out.session.source->xml(source.notesPart);
    XmlElement *sourceSp = sourceRoot ? notesBodyShape(*sourceRoot) : nullptr;
    if (sp == nullptr || sourceSp == nullptr) {
        return false;
    }
    const TextBodyStyle body;
    const TextSource from{sourceSp->child(nsP(), u"txBody"), &source.notesParagraphs, &body, source.notesPart};
    XmlElementPtr txBody = textBodyElement(out, nsP(), notesParagraphs(text, source.notesParagraphs), body, from);
    sp->removeChildren(nsP(), u"txBody");
    sp->insertElementBefore(txBody, {u"extLst"});
    return true;
}

} // namespace

XmlElementPtr slideElement(PartWriter &out, const Slide &slide, QHash<QString, quint32> *writtenIds)
{
    const SlideSource *source = slide.origin.get();
    const bool carry = source != nullptr && source->xml && out.session.copier != nullptr;
    const XmlElement *sourceCSld = carry ? source->xml->child(nsP(), u"cSld") : nullptr;
    XmlElementPtr sld;
    if (carry) {
        sld = source->xml->clone();
        sld->children.removeIf([](const Ooxml::XmlChild &c) {
            return c.element && (c.element->is(nsP(), u"cSld") || c.element->is(nsP(), u"timing") || isTransition(*c.element));
        });
        if (XmlElement *list = sld->child(nsP(), u"extLst")) {
            list->children.removeIf([](const Ooxml::XmlChild &c) {
                return c.element && c.element->attr(u"uri") == QStringView(kCommentRelUri);
            });
            if (list->elements().isEmpty()) {
                sld->removeChildren(nsP(), u"extLst");
            }
        }
        out.carry(*sld, source->part);
    } else {
        sld = make(nsP(), QStringLiteral("sld"));
    }
    if (slide.hidden) {
        sld->setAttribute(QString(), QStringLiteral("show"), QStringLiteral("0"));
    } else {
        sld->removeAttribute(QString(), u"show");
    }
    if (!slide.showMasterShapes) {
        sld->setAttribute(QString(), QStringLiteral("showMasterSp"), QStringLiteral("0"));
    } else {
        sld->removeAttribute(QString(), u"showMasterSp");
    }

    // p:cSld: the source's own attributes and trailing children around the
    // rebuilt background and shape tree.
    // AGENT-GUARD (M67): the background and the shape tree come off BEFORE
    // the rest is carried — carrying them would copy every part a shape
    // names (a deleted picture's media, an edited chart's stale part and
    // workbook) into the package, each still related from the slide.
    XmlElementPtr cSld = sourceCSld != nullptr ? sourceCSld->clone() : make(nsP(), QStringLiteral("cSld"));
    // M75: a QindaShow slide's title survives the round trip a design
    // change makes (pptx_design.h) — the reader names untitled slides so.
    const bool titled = std::any_of(slide.shapes.cbegin(), slide.shapes.cend(), [](const Shape &shape) {
        return shape.placeholder.type == u"title" || shape.placeholder.type == u"ctrTitle";
    });
    if (sourceCSld == nullptr && !titled && !slide.title.isEmpty()) {
        cSld->setAttribute(QString(), QStringLiteral("name"), slide.title);
    }
    cSld->children.removeIf([](const Ooxml::XmlChild &c) {
        return c.element && (c.element->is(nsP(), u"bg") || c.element->is(nsP(), u"spTree"));
    });
    if (sourceCSld != nullptr) {
        out.carry(*cSld, source->part);
    }
    const XmlElement *sourceTree = sourceCSld != nullptr ? sourceCSld->child(nsP(), u"spTree") : nullptr;
    XmlElementPtr tree = make(nsP(), QStringLiteral("spTree"));
    for (const char *name : {"nvGrpSpPr", "grpSpPr"}) {
        const XmlElement *own = sourceTree != nullptr ? sourceTree->child(nsP(), QString::fromLatin1(name)) : nullptr;
        tree->appendElement(own != nullptr ? carried(out, own, source->part) : defaultGroupProperties(name));
    }
    const QList<bool> keeps = reserveShapeIds(out, slide.shapes);
    QList<quint32> written;
    for (qsizetype i = 0; i < slide.shapes.size(); ++i) {
        const Shape &shape = slide.shapes.at(i);
        const quint32 id = out.claim(keeps.at(i) ? shape.nvId : 0);
        XmlElementPtr element = writeShape(out, shape, id);
        written.append(element ? id : 0);
        if (element) {
            tree->appendElement(element);
            if (writtenIds != nullptr) {
                writtenIds->insert(shape.id, id);
            }
        }
    }
    if (const XmlElement *ext = sourceTree != nullptr ? sourceTree->child(nsP(), u"extLst") : nullptr) {
        tree->appendElement(carried(out, ext, source->part));
    }
    QList<Ooxml::XmlChild> cSldChildren;
    if (slide.background.set) {
        const XmlElement *sourceBg = sourceCSld != nullptr ? sourceCSld->child(nsP(), u"bg") : nullptr;
        XmlElementPtr bg = sourceBg != nullptr && source->background == slide.background ? carried(out, sourceBg, source->part)
                                                                                           : backgroundElement(out, slide.background);
        cSldChildren.append({bg, QString()});
    }
    cSldChildren.append({tree, QString()});
    cSldChildren.append(cSld->children);
    cSld->children = cSldChildren;

    // The colour map override (kept), then the transition and the timing.
    XmlElementPtr colorMap;
    QList<Ooxml::XmlChild> rest;
    for (const Ooxml::XmlChild &c : std::as_const(sld->children)) {
        if (c.element && c.element->is(nsP(), u"clrMapOvr")) {
            colorMap = c.element;
        } else if (c.element) {
            rest.append(c);
        }
    }
    if (!colorMap) {
        colorMap = make(nsP(), QStringLiteral("clrMapOvr"));
        colorMap->appendElement(nsA(), QStringLiteral("masterClrMapping"));
    }
    XmlElementPtr transition;
    const bool keptTransition = carry && source->transitionXml && slide.transition == source->transition;
    if (keptTransition) {
        transition = carried(out, source->transitionXml.get(), source->part);
    } else {
        transition = transitionElement(slide.transition);
    }
    // M91 (D-099): Advance Slide, written where it changed (a kept
    // transition keeps its own spelling while the timing is what was read).
    if (!keptTransition || source->advanceAfterMs != slide.advanceAfterMs || source->advanceOnClick != slide.advanceOnClick) {
        applyAdvance(transition, slide.advanceAfterMs, slide.advanceOnClick);
    }
    // Builds: the source's timing while the builds (and, M75, every media
    // shape's playback) are what was read.
    QList<BuildEntry> current;
    QList<BuildEntry> expected;
    QList<MediaEntry> media;
    bool mediaChanged = false;
    for (qsizetype i = 0; i < slide.shapes.size(); ++i) {
        const Shape &shape = slide.shapes.at(i);
        if (shape.media.isMedia()) {
            const MediaInfo &m = shape.media;
            if (written.at(i) != 0) {
                media.append({written.at(i), m.kind, m.autoPlay ? 0 : shape.buildStep, m.autoPlay, m.loop,
                              m.hideWhenStopped, m.volume, m.durationMs, m.narration});
            }
            const Shape *was = carry && shape.origin && written.at(i) == shape.nvId ? &shape.origin->pristine : nullptr;
            mediaChanged = mediaChanged || was == nullptr || was->media != m || was->buildStep != shape.buildStep;
            continue; // a media shape's step is its play click, not a build
        }
        if (shape.buildStep > 0 && written.at(i) != 0) {
            current.append({written.at(i), shape.buildStep, shape.animation, shape.animationMs, shape.animationDirection, false});
        }
        if (shape.exit.isSet() && written.at(i) != 0) { // M91
            const ShapeExit &exit = shape.exit;
            current.append({written.at(i), exit.step, exit.animation, exit.durationMs, exit.direction, true});
        }
        if (carry && written.at(i) != 0 && written.at(i) == shape.nvId) {
            for (const BuildEntry &build : source->builds) {
                if (build.shapeId == shape.nvId) {
                    expected.append({build.shapeId, build.step, build.animation, buildDuration(build), build.direction, build.exit});
                }
            }
        }
    }
    XmlElementPtr timing;
    if (carry && source->timingXml && sorted(current) == sorted(expected) && !mediaChanged) {
        timing = pruneTiming(*source->timingXml, out.usedIds());
        if (timing) {
            out.carry(*timing, source->part);
        }
    } else {
        timing = timingElement(current, media);
    }
    sld->children.clear();
    sld->appendElement(cSld);
    sld->appendElement(colorMap);
    if (transition) {
        sld->appendElement(transition);
    }
    if (timing) {
        sld->appendElement(timing);
    }
    sld->children.append(rest);
    return sld;
}

XmlElementPtr notesElement(PartWriter &out, const Slide &slide)
{
    if (slide.notes.isEmpty()) {
        return nullptr;
    }
    const SlideSource *source = slide.origin.get();
    if (source != nullptr && !source->notesPart.isEmpty() && out.session.copier != nullptr) {
        if (XmlElementPtr root = out.session.source->xml(source->notesPart)) {
            out.carry(*root, source->notesPart);
            if (source->notes == slide.notes) {
                return root;
            }
            // M67 (D-076): edited notes go into the notes slide they came
            // from — its slide image, header, footer and number placeholders
            // and its formatting kept; only the body's paragraphs change.
            if (patchNotesBody(out, *root, slide.notes, *source)) {
                return root;
            }
        }
    }
    return notesSlideElement(slide.notes);
}

} // namespace QindaOffice::Show::Pptx
