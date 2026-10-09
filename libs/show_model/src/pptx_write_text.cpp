// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_write_text.h"

#include <QUuid>

#include <cmath>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

void set(XmlElement &e, const QString &name, const QString &value)
{
    e.setAttribute(QString(), name, value);
}

QString number(qreal value)
{
    return QString::number(qint64(std::llround(value)));
}

QString alignmentValue(int alignment)
{
    if (alignment & int(Qt::AlignHCenter)) {
        return QStringLiteral("ctr");
    }
    if (alignment & int(Qt::AlignJustify)) {
        return QStringLiteral("just");
    }
    if (alignment & int(Qt::AlignRight)) {
        return QStringLiteral("r");
    }
    return QStringLiteral("l");
}

XmlElementPtr spacingElement(const char *name, const Spacing &spacing)
{
    XmlElementPtr outer = make(nsA(), QLatin1String(name));
    if (spacing.unit == Spacing::Unit::Points) {
        set(outer->appendElement(nsA(), QStringLiteral("spcPts")), QStringLiteral("val"), number(spacing.value * 100));
    } else {
        set(outer->appendElement(nsA(), QStringLiteral("spcPct")), QStringLiteral("val"), number(spacing.value * 1000));
    }
    return outer;
}

XmlElementPtr paragraphProperties(const TextParagraph &p)
{
    XmlElementPtr pPr = make(nsA(), QStringLiteral("pPr"));
    set(*pPr, QStringLiteral("marL"), emu(p.marginLeft));
    set(*pPr, QStringLiteral("indent"), emu(p.indent));
    if (p.listLevel > 0) {
        set(*pPr, QStringLiteral("lvl"), QString::number(qBound(0, p.listLevel, 8)));
    }
    set(*pPr, QStringLiteral("algn"), alignmentValue(p.alignment));
    pPr->appendElement(spacingElement("lnSpc", p.lineSpacing));
    pPr->appendElement(spacingElement("spcBef", p.spaceBefore));
    pPr->appendElement(spacingElement("spcAft", p.spaceAfter));
    if (!p.bullet) {
        pPr->appendElement(nsA(), QStringLiteral("buNone"));
        return pPr;
    }
    if (p.bulletColor.isValid()) {
        pPr->appendElement(nsA(), QStringLiteral("buClr")).appendElement(colorElement(p.bulletColor));
    } else {
        pPr->appendElement(nsA(), QStringLiteral("buClrTx"));
    }
    // AGENT-NOTE: not a:buSzPct — the Transitional schema spells its value
    // as a percent string while PowerPoint writes thousandths; buSzTx /
    // buSzPts mean the same to both.
    if (qAbs(p.bulletSize - 100.0) < 0.01) {
        pPr->appendElement(nsA(), QStringLiteral("buSzTx"));
    } else {
        const TextRun lead = p.runs.isEmpty() ? TextRun() : p.runs.first();
        const qreal size = (p.runs.isEmpty() && p.endPointSize > 0 ? p.endPointSize : lead.pointSize) * p.bulletSize / 100.0;
        set(pPr->appendElement(nsA(), QStringLiteral("buSzPts")), QStringLiteral("val"), number(qBound(1.0, size, 4000.0) * 100));
    }
    if (p.bulletFont.isEmpty()) {
        pPr->appendElement(nsA(), QStringLiteral("buFontTx"));
    } else {
        set(pPr->appendElement(nsA(), QStringLiteral("buFont")), QStringLiteral("typeface"), p.bulletFont);
    }
    if (!p.autoNumber.isEmpty()) {
        XmlElement &number = pPr->appendElement(nsA(), QStringLiteral("buAutoNum"));
        set(number, QStringLiteral("type"), p.autoNumber);
        if (p.autoNumberStart != 1) {
            set(number, QStringLiteral("startAt"), QString::number(qBound(1, p.autoNumberStart, 32767)));
        }
    } else {
        set(pPr->appendElement(nsA(), QStringLiteral("buChar")), QStringLiteral("char"),
            p.bulletChar.isEmpty() ? QStringLiteral("•") : p.bulletChar);
    }
    return pPr;
}

XmlElementPtr runProperties(PartWriter &out, const TextRun &run, const char *name = "rPr")
{
    XmlElementPtr rPr = make(nsA(), QLatin1String(name));
    set(*rPr, QStringLiteral("lang"), QStringLiteral("en-US"));
    set(*rPr, QStringLiteral("sz"), number(qBound(1.0, run.pointSize, 4000.0) * 100));
    set(*rPr, QStringLiteral("b"), run.bold ? QStringLiteral("1") : QStringLiteral("0"));
    set(*rPr, QStringLiteral("i"), run.italic ? QStringLiteral("1") : QStringLiteral("0"));
    set(*rPr, QStringLiteral("u"), run.underline.isEmpty() ? QStringLiteral("none") : run.underline);
    set(*rPr, QStringLiteral("strike"), run.strike.isEmpty() ? QStringLiteral("noStrike") : run.strike);
    set(*rPr, QStringLiteral("cap"), run.caps.isEmpty() ? QStringLiteral("none") : run.caps);
    set(*rPr, QStringLiteral("spc"), number(run.spacing * 100));
    set(*rPr, QStringLiteral("baseline"), QString::number(run.baseline));
    rPr->appendElement(fillElement(out, run.color.isValid() ? run.color : QColor(Qt::black), FillStyle()));
    if (run.highlight.isValid()) {
        rPr->appendElement(nsA(), QStringLiteral("highlight")).appendElement(colorElement(run.highlight));
    }
    if (!run.fontFamily.isEmpty()) {
        set(rPr->appendElement(nsA(), QStringLiteral("latin")), QStringLiteral("typeface"), run.fontFamily);
    }
    if (XmlElementPtr link = out.hyperlink(run.hyperlink)) {
        rPr->appendElement(link);
    }
    return rPr;
}

TextRun formatOnly(TextRun run)
{
    run.text.clear();
    return run;
}

TextParagraph propertiesOnly(TextParagraph p)
{
    p.runs.clear();
    p.endPointSize = 0;
    return p;
}

// What the source spelled, keyed by the effective values the reader made
// of it (pptx_write_text.h).
struct Reuse {
    QList<QPair<TextParagraph, const XmlElement *>> paragraphs; // props → a:pPr (null = none)
    QList<std::tuple<int, TextRun, const XmlElement *>> runs;   // level, format → a:rPr
    QList<std::tuple<int, qreal, const XmlElement *>> ends;    // level, end size → a:endParaRPr

    std::function<TextRun(const TextRun &)> currentKey;

    void learn(const XmlElement &txBody, const QList<TextParagraph> &pristine,
               const std::function<TextRun(const TextRun &)> &sourceKey)
    {
        const QList<const XmlElement *> ps = txBody.elements(nsA(), u"p");
        for (qsizetype i = 0; i < ps.size() && i < pristine.size(); ++i) {
            const TextParagraph &model = pristine.at(i);
            paragraphs.append({propertiesOnly(model), ps.at(i)->child(nsA(), u"pPr")});
            ends.append({model.listLevel, model.endPointSize, ps.at(i)->child(nsA(), u"endParaRPr")});
            qsizetype r = 0;
            for (const XmlElement *e : ps.at(i)->elements()) {
                if (e->ns != nsA() || (e->name != u"r" && e->name != u"fld")) {
                    continue;
                }
                if (r < model.runs.size()) {
                    const TextRun &run = model.runs.at(r);
                    runs.append({model.listLevel, formatOnly(sourceKey ? sourceKey(run) : run), e->child(nsA(), u"rPr")});
                }
                ++r;
            }
        }
    }

    [[nodiscard]] const XmlElement *const *paragraph(const TextParagraph &p) const
    {
        const TextParagraph key = propertiesOnly(p);
        for (const auto &entry : paragraphs) {
            if (entry.first == key) {
                return &entry.second;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const XmlElement *const *run(int level, const TextRun &run) const
    {
        const TextRun key = formatOnly(currentKey ? currentKey(run) : run);
        for (const auto &entry : runs) {
            if (std::get<0>(entry) == level && std::get<1>(entry) == key) {
                return &std::get<2>(entry);
            }
        }
        return nullptr;
    }
};

XmlElementPtr reused(PartWriter &out, const XmlElement *source, const QString &sourcePart)
{
    XmlElementPtr copy = source->clone();
    out.carry(*copy, sourcePart);
    return copy;
}

void bodyProperties(XmlElement &bodyPr, const TextBodyStyle &body)
{
    set(bodyPr, QStringLiteral("lIns"), emu(body.insetLeft));
    set(bodyPr, QStringLiteral("tIns"), emu(body.insetTop));
    set(bodyPr, QStringLiteral("rIns"), emu(body.insetRight));
    set(bodyPr, QStringLiteral("bIns"), emu(body.insetBottom));
    static const char *const kAnchors[] = {"t", "ctr", "b", "just", "dist"};
    set(bodyPr, QStringLiteral("anchor"), QLatin1String(kAnchors[int(body.anchor)]));
    set(bodyPr, QStringLiteral("anchorCtr"), body.anchorCenter ? QStringLiteral("1") : QStringLiteral("0"));
    set(bodyPr, QStringLiteral("wrap"), body.wrap ? QStringLiteral("square") : QStringLiteral("none"));
    if (body.vertical.isEmpty()) {
        bodyPr.removeAttribute(QString(), u"vert");
    } else {
        set(bodyPr, QStringLiteral("vert"), body.vertical);
    }
    for (const char16_t *fit : {u"noAutofit", u"normAutofit", u"spAutoFit"}) {
        bodyPr.removeChildren(nsA(), fit);
    }
    XmlElementPtr fit;
    if (body.autoFit == AutoFit::Normal) {
        fit = make(nsA(), QStringLiteral("normAutofit"));
        if (body.fontScale != 100) {
            set(*fit, QStringLiteral("fontScale"), number(body.fontScale * 1000));
        }
        if (body.lineSpaceReduction != 0) {
            set(*fit, QStringLiteral("lnSpcReduction"), number(body.lineSpaceReduction * 1000));
        }
    } else {
        fit = make(nsA(), body.autoFit == AutoFit::Shape ? QStringLiteral("spAutoFit") : QStringLiteral("noAutofit"));
    }
    bodyPr.insertElementBefore(fit, {u"scene3d", u"sp3d", u"flatTx", u"extLst"});
}

} // namespace

QString emu(qreal points)
{
    return QString::number(qint64(std::llround(points * 12700.0)));
}

XmlElementPtr colorElement(const QColor &color)
{
    XmlElementPtr srgb = make(nsA(), QStringLiteral("srgbClr"));
    set(*srgb, QStringLiteral("val"), color.name(QColor::HexRgb).mid(1).toUpper());
    if (color.alpha() < 255) {
        set(srgb->appendElement(nsA(), QStringLiteral("alpha")), QStringLiteral("val"), number(color.alphaF() * 100000));
    }
    return srgb;
}

XmlElementPtr fillElement(PartWriter &out, const QColor &plain, const FillStyle &style)
{
    switch (style.kind) {
    case FillKind::Gradient: {
        XmlElementPtr grad = make(nsA(), QStringLiteral("gradFill"));
        set(*grad, QStringLiteral("rotWithShape"), QStringLiteral("1"));
        XmlElement &list = grad->appendElement(nsA(), QStringLiteral("gsLst"));
        for (const GradientStop &stop : style.stops) {
            XmlElement &gs = list.appendElement(nsA(), QStringLiteral("gs"));
            set(gs, QStringLiteral("pos"), number(qBound(0.0, stop.position, 1.0) * 100000));
            gs.appendElement(colorElement(stop.color.isValid() ? stop.color : QColor(Qt::black)));
        }
        if (style.path.isEmpty()) {
            XmlElement &lin = grad->appendElement(nsA(), QStringLiteral("lin"));
            set(lin, QStringLiteral("ang"), number(std::fmod(std::fmod(style.angle, 360.0) + 360.0, 360.0) * 60000));
            set(lin, QStringLiteral("scaled"), QStringLiteral("0"));
        } else {
            XmlElement &path = grad->appendElement(nsA(), QStringLiteral("path"));
            set(path, QStringLiteral("path"), style.path);
            XmlElement &focus = path.appendElement(nsA(), QStringLiteral("fillToRect"));
            for (const char *side : {"l", "t", "r", "b"}) {
                set(focus, QLatin1String(side), QStringLiteral("50000"));
            }
        }
        return grad;
    }
    case FillKind::Picture: {
        XmlElementPtr blipFill = make(nsA(), QStringLiteral("blipFill"));
        XmlElement &blip = blipFill->appendElement(nsA(), QStringLiteral("blip"));
        blip.setAttribute(nsR(), QStringLiteral("embed"),
                          out.relate(relType("image"), out.session.media(style.image, style.imageMime)));
        if (style.tile) {
            blipFill->appendElement(nsA(), QStringLiteral("tile"));
        } else {
            blipFill->appendElement(nsA(), QStringLiteral("stretch")).appendElement(nsA(), QStringLiteral("fillRect"));
        }
        return blipFill;
    }
    case FillKind::Pattern: {
        XmlElementPtr pattern = make(nsA(), QStringLiteral("pattFill"));
        set(*pattern, QStringLiteral("prst"), style.pattern.isEmpty() ? QStringLiteral("pct50") : style.pattern);
        pattern->appendElement(nsA(), QStringLiteral("fgClr"))
            .appendElement(colorElement(style.foreground.isValid() ? style.foreground : QColor(Qt::black)));
        pattern->appendElement(nsA(), QStringLiteral("bgClr"))
            .appendElement(colorElement(style.background.isValid() ? style.background : QColor(Qt::white)));
        return pattern;
    }
    case FillKind::Plain:
        break;
    }
    if (!plain.isValid() || plain.alpha() == 0) {
        return make(nsA(), QStringLiteral("noFill"));
    }
    XmlElementPtr solid = make(nsA(), QStringLiteral("solidFill"));
    solid->appendElement(colorElement(plain));
    return solid;
}

XmlElementPtr backgroundElement(PartWriter &out, const Background &background)
{
    auto bg = std::make_shared<XmlElement>(nsP(), QStringLiteral("bg"));
    XmlElement &pr = bg->appendElement(nsP(), QStringLiteral("bgPr"));
    pr.appendElement(fillElement(out, background.color, background.style));
    pr.appendElement(nsA(), QStringLiteral("effectLst"));
    return bg;
}

XmlElementPtr lineElement(const QColor &color, qreal width, const LineStyle &line, bool visible)
{
    XmlElementPtr ln = make(nsA(), QStringLiteral("ln"));
    if (!visible || !color.isValid() || color.alpha() == 0 || width <= 0) {
        ln->appendElement(nsA(), QStringLiteral("noFill"));
        return ln;
    }
    set(*ln, QStringLiteral("w"), emu(width));
    if (!line.cap.isEmpty()) {
        set(*ln, QStringLiteral("cap"), line.cap);
    }
    ln->appendElement(nsA(), QStringLiteral("solidFill")).appendElement(colorElement(color));
    if (!line.dash.isEmpty()) {
        set(ln->appendElement(nsA(), QStringLiteral("prstDash")), QStringLiteral("val"), line.dash);
    }
    if (line.join == u"bevel") {
        ln->appendElement(nsA(), QStringLiteral("bevel"));
    } else if (line.join == u"miter") {
        set(ln->appendElement(nsA(), QStringLiteral("miter")), QStringLiteral("lim"), QStringLiteral("800000"));
    }
    for (const auto &[end, name] : {std::pair{&line.head, "headEnd"}, std::pair{&line.tail, "tailEnd"}}) {
        if (end->isNone()) {
            continue;
        }
        XmlElement &e = ln->appendElement(nsA(), QLatin1String(name));
        set(e, QStringLiteral("type"), end->type);
        if (!end->width.isEmpty()) {
            set(e, QStringLiteral("w"), end->width);
        }
        if (!end->length.isEmpty()) {
            set(e, QStringLiteral("len"), end->length);
        }
    }
    return ln;
}

XmlElementPtr textBodyElement(PartWriter &out, const QString &ns, const QList<TextParagraph> &paragraphs,
                              const TextBodyStyle &body, const TextSource &source)
{
    XmlElementPtr txBody = make(ns, QStringLiteral("txBody"));
    const XmlElement *sourceBodyPr = source.txBody != nullptr ? source.txBody->child(nsA(), u"bodyPr") : nullptr;
    if (sourceBodyPr != nullptr && source.pristineBody != nullptr && *source.pristineBody == body) {
        txBody->appendElement(reused(out, sourceBodyPr, source.part));
    } else {
        XmlElementPtr bodyPr = sourceBodyPr != nullptr ? reused(out, sourceBodyPr, source.part)
                                                       : make(nsA(), QStringLiteral("bodyPr"));
        bodyProperties(*bodyPr, body);
        txBody->appendElement(bodyPr);
    }
    const XmlElement *lstStyle = source.txBody != nullptr ? source.txBody->child(nsA(), u"lstStyle") : nullptr;
    txBody->appendElement(lstStyle != nullptr ? reused(out, lstStyle, source.part) : make(nsA(), QStringLiteral("lstStyle")));
    Reuse reuse;
    reuse.currentKey = source.currentKey;
    if (source.txBody != nullptr && source.pristine != nullptr) {
        reuse.learn(*source.txBody, *source.pristine, source.sourceKey);
    }
    for (const TextParagraph &paragraph : paragraphs) {
        XmlElement &p = txBody->appendElement(nsA(), QStringLiteral("p"));
        if (const XmlElement *const *pPr = reuse.paragraph(paragraph)) {
            if (*pPr != nullptr) {
                p.appendElement(reused(out, *pPr, source.part));
            }
        } else {
            p.appendElement(paragraphProperties(paragraph));
        }
        for (const TextRun &run : paragraph.runs) {
            const XmlElement *const *known = reuse.run(paragraph.listLevel, run);
            const auto properties = [&]() -> XmlElementPtr {
                if (known != nullptr) {
                    return *known != nullptr ? reused(out, *known, source.part) : nullptr;
                }
                XmlElementPtr rPr = runProperties(out, run);
                if (source.generatedRun) {
                    source.generatedRun(*rPr, run);
                }
                return rPr;
            };
            QString text = run.text;
            text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
            const QStringList pieces = text.split(QChar(QChar::LineSeparator));
            for (qsizetype i = 0; i < pieces.size(); ++i) {
                if (i > 0) {
                    XmlElement &br = p.appendElement(nsA(), QStringLiteral("br"));
                    if (XmlElementPtr rPr = properties()) {
                        br.appendElement(rPr);
                    }
                }
                if (pieces.at(i).isEmpty() && (i > 0 || pieces.size() > 1)) {
                    continue; // a break with nothing after it
                }
                const bool field = !run.fieldType.isEmpty() && i == 0;
                XmlElement &r = p.appendElement(nsA(), field ? QStringLiteral("fld") : QStringLiteral("r"));
                if (field) {
                    set(r, QStringLiteral("id"), run.fieldId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithBraces).toUpper()
                                                                      : run.fieldId);
                    set(r, QStringLiteral("type"), run.fieldType);
                }
                if (XmlElementPtr rPr = properties()) {
                    r.appendElement(rPr);
                }
                r.appendElement(nsA(), QStringLiteral("t")).appendText(pieces.at(i));
            }
        }
        const auto end = std::find_if(reuse.ends.cbegin(), reuse.ends.cend(), [&paragraph](const auto &e) {
            return std::get<0>(e) == paragraph.listLevel && std::get<1>(e) == paragraph.endPointSize;
        });
        if (end != reuse.ends.cend() && reuse.paragraph(paragraph) != nullptr) {
            // The source's end-of-paragraph properties, or none where it had none.
            if (std::get<2>(*end) != nullptr) {
                p.appendElement(reused(out, std::get<2>(*end), source.part));
            }
        } else if (paragraph.endPointSize > 0) {
            XmlElement &endRun = p.appendElement(nsA(), QStringLiteral("endParaRPr"));
            set(endRun, QStringLiteral("lang"), QStringLiteral("en-US"));
            set(endRun, QStringLiteral("sz"), number(qBound(1.0, paragraph.endPointSize, 4000.0) * 100));
        }
    }
    if (paragraphs.isEmpty()) {
        txBody->appendElement(nsA(), QStringLiteral("p"));
    }
    return txBody;
}

} // namespace QindaOffice::Show::Pptx
