// SPDX-License-Identifier: GPL-3.0-or-later
// A video's or sound's Trim, Fade Duration and bookmarks (M91, D-099), as
// PowerPoint 2010+ keeps them inside p14:media (the p:pic's nvPr
// extension, pptx_write_shapes.h):
//
//   <p14:media r:embed="rId1">
//     <p14:trim st="1500" end="2000"/>        (ms cut off the start / the end)
//     <p14:fade in="500" out="750"/>          (ms)
//     <p14:bmkLst><p14:bmk name="Chorus" time="3200"/></p14:bmkLst>
//   </p14:media>
//
// AGENT-NOTE: [MS-PPTX] spells trim's `end` as the time cut off the END of
// the medium (not an end position); PowerPoint writes fractional
// milliseconds at times, so values are read as decimals and written as
// whole milliseconds. These are Office extension elements: the ECMA-376
// schemas do not check them (the package declares p14 ignorable).

#include "pptx_read_shapes.h"
#include "pptx_write_shapes.h"

#include <cmath>
#include <functional>

namespace QindaOffice::Show::Pptx {

namespace {

const QString kP14 = QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2010/main");

int milliseconds(const Ooxml::XmlElement *e, QStringView attribute)
{
    if (e == nullptr) {
        return 0;
    }
    bool ok = false;
    const double value = e->attr(attribute).toDouble(&ok);
    return ok && value > 0 ? int(std::lround(qMin(value, 1.0e9))) : 0;
}

} // namespace

void readMediaEdits(const Ooxml::XmlElement *media, MediaInfo &info)
{
    if (media == nullptr) {
        return;
    }
    const Ooxml::XmlElement *trim = media->child(kP14, u"trim");
    info.trimStartMs = milliseconds(trim, u"st");
    info.trimEndMs = milliseconds(trim, u"end");
    const Ooxml::XmlElement *fade = media->child(kP14, u"fade");
    info.fadeInMs = milliseconds(fade, u"in");
    info.fadeOutMs = milliseconds(fade, u"out");
    info.bookmarks.clear();
    if (const Ooxml::XmlElement *list = media->child(kP14, u"bmkLst")) {
        for (const Ooxml::XmlElement *bookmark : list->elements(kP14, u"bmk")) {
            info.bookmarks.append({bookmark->attr(u"name"), milliseconds(bookmark, u"time")});
        }
    }
    std::stable_sort(info.bookmarks.begin(), info.bookmarks.end(),
                     [](const MediaBookmark &a, const MediaBookmark &b) { return a.timeMs < b.timeMs; });
}

void writeMediaEdits(Ooxml::XmlElement &media, const MediaInfo &info)
{
    for (const char16_t *name : {u"trim", u"fade", u"bmkLst"}) {
        media.removeChildren(kP14, QString::fromUtf16(name));
    }
    // CT_Media: trim, fade, bmkLst, extLst — each before what follows it.
    if (info.trimStartMs > 0 || info.trimEndMs > 0) {
        auto trim = std::make_shared<Ooxml::XmlElement>(kP14, QStringLiteral("trim"));
        if (info.trimStartMs > 0) {
            trim->setAttribute(QString(), QStringLiteral("st"), QString::number(info.trimStartMs));
        }
        if (info.trimEndMs > 0) {
            trim->setAttribute(QString(), QStringLiteral("end"), QString::number(info.trimEndMs));
        }
        media.insertElementBefore(trim, {u"fade", u"bmkLst", u"extLst"});
    }
    if (info.fadeInMs > 0 || info.fadeOutMs > 0) {
        auto fade = std::make_shared<Ooxml::XmlElement>(kP14, QStringLiteral("fade"));
        if (info.fadeInMs > 0) {
            fade->setAttribute(QString(), QStringLiteral("in"), QString::number(info.fadeInMs));
        }
        if (info.fadeOutMs > 0) {
            fade->setAttribute(QString(), QStringLiteral("out"), QString::number(info.fadeOutMs));
        }
        media.insertElementBefore(fade, {u"bmkLst", u"extLst"});
    }
    if (!info.bookmarks.isEmpty()) {
        auto list = std::make_shared<Ooxml::XmlElement>(kP14, QStringLiteral("bmkLst"));
        for (const MediaBookmark &bookmark : info.bookmarks) {
            Ooxml::XmlElement &e = list->appendElement(kP14, QStringLiteral("bmk"));
            e.setAttribute(QString(), QStringLiteral("name"), bookmark.name);
            e.setAttribute(QString(), QStringLiteral("time"), QString::number(bookmark.timeMs));
        }
        media.insertElementBefore(list, {u"extLst"});
    }
}

void writeNarration(Ooxml::XmlElement &nvPr, bool narration)
{
    // MS-PPTX 2.2.14: the marker belongs in nvPr/extLst, not cNvPr.
    const QString uri = QStringLiteral("{42D2F446-02D8-4167-A562-619A0277C38B}");
    Ooxml::XmlElement *extensions = nvPr.child(nsP(), u"extLst");
    if (extensions == nullptr && !narration) return;
    if (extensions == nullptr) extensions = &nvPr.appendElement(nsP(), QStringLiteral("extLst"));
    for (qsizetype i = extensions->children.size() - 1; i >= 0; --i) {
        const auto &child = extensions->children.at(i);
        if (child.element && child.element->attr(u"uri") == uri) extensions->children.removeAt(i);
    }
    if (narration) {
        auto &ext = extensions->appendElement(nsP(), QStringLiteral("ext"));
        ext.setAttribute(QString(), QStringLiteral("uri"), uri);
        auto &flag = ext.appendElement(QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2012/main"),
                                       QStringLiteral("isNarration"));
        flag.setAttribute(QString(), QStringLiteral("val"), QStringLiteral("1"));
    }
}

Ooxml::XmlElement *p14Media(Ooxml::XmlElement &pic)
{
    const std::function<Ooxml::XmlElement *(Ooxml::XmlElement &)> find = [&find](Ooxml::XmlElement &e) -> Ooxml::XmlElement * {
        if (e.is(kP14, u"media")) {
            return &e;
        }
        for (Ooxml::XmlChild &child : e.children) {
            if (Ooxml::XmlElement *found = child.element ? find(*child.element) : nullptr) {
                return found;
            }
        }
        return nullptr;
    };
    Ooxml::XmlElement *nv = pic.child(pic.ns, u"nvPicPr");
    Ooxml::XmlElement *nvPr = nv != nullptr ? nv->child(pic.ns, u"nvPr") : nullptr;
    return nvPr != nullptr ? find(*nvPr) : nullptr;
}

} // namespace QindaOffice::Show::Pptx
