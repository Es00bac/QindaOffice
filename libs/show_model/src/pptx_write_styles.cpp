// SPDX-License-Identifier: GPL-3.0-or-later
// Master and layout text styles for the pptx writer (pptx_write_styles.h).

#include "pptx_write_styles.h"

#include "pptx_write_text.h"

#include <cmath>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

// CT_TextParagraphProperties and CT_TextCharacterProperties child order.
const QList<QStringView> kLevelOrder{u"lnSpc", u"spcBef", u"spcAft", u"buClrTx", u"buClr", u"buSzTx", u"buSzPct",
                                     u"buSzPts", u"buFontTx", u"buFont", u"buNone", u"buAutoNum", u"buChar",
                                     u"buBlip", u"tabLst", u"defRPr", u"extLst"};
const QList<QStringView> kRunOrder{u"ln", u"noFill", u"solidFill", u"gradFill", u"blipFill", u"pattFill", u"grpFill",
                                   u"effectLst", u"effectDag", u"highlight", u"uLnTx", u"uLn", u"uFillTx", u"uFill",
                                   u"latin", u"ea", u"cs", u"sym", u"hlinkClick", u"hlinkMouseOver", u"rtl", u"extLst"};

XmlElementPtr make(const QString &name)
{
    return std::make_shared<XmlElement>(nsA(), name);
}

void set(XmlElement &e, const char *name, const QString &value)
{
    e.setAttribute(QString(), QLatin1String(name), value);
}

QString number(qreal value)
{
    return QString::number(qint64(std::llround(value)));
}

// Puts `element` in `parent` in schema order, replacing any of `replaces`.
void place(XmlElement &parent, const XmlElementPtr &element, const QList<QStringView> &order,
           const QList<QStringView> &replaces)
{
    for (QStringView name : replaces) {
        parent.removeChildren(nsA(), name);
    }
    if (!element) {
        return;
    }
    const qsizetype at = order.indexOf(QStringView(element->name));
    parent.insertElementBefore(element, at >= 0 ? order.mid(at + 1) : QList<QStringView>{});
}

XmlElement &childIn(XmlElement &parent, const QString &name, const QList<QStringView> &order)
{
    if (XmlElement *found = parent.child(nsA(), name)) {
        return *found;
    }
    const qsizetype at = order.indexOf(QStringView(name));
    return parent.insertElementBefore(make(name), at >= 0 ? order.mid(at + 1) : QList<QStringView>{});
}

XmlElementPtr spacing(const char *name, const Spacing &value)
{
    XmlElementPtr outer = make(QLatin1String(name));
    if (value.unit == Spacing::Unit::Points) {
        set(outer->appendElement(nsA(), QStringLiteral("spcPts")), "val", number(value.value * 100));
    } else {
        set(outer->appendElement(nsA(), QStringLiteral("spcPct")), "val", number(value.value * 1000));
    }
    return outer;
}

QString alignment(int value)
{
    if (value & int(Qt::AlignHCenter)) {
        return QStringLiteral("ctr");
    }
    if (value & int(Qt::AlignJustify)) {
        return QStringLiteral("just");
    }
    return value & int(Qt::AlignRight) ? QStringLiteral("r") : QStringLiteral("l");
}

bool sameBullet(const TextParagraph &a, const TextParagraph &b)
{
    return a.bullet == b.bullet && a.bulletChar == b.bulletChar && a.autoNumber == b.autoNumber
        && a.autoNumberStart == b.autoNumberStart && a.bulletFont == b.bulletFont && a.bulletColor == b.bulletColor
        && a.bulletSize == b.bulletSize;
}

void writeBullet(XmlElement &lvl, const TextParagraph &p)
{
    for (QStringView name : kLevelOrder.mid(3, 11)) { // every bu* element
        lvl.removeChildren(nsA(), name);
    }
    if (!p.bullet) {
        place(lvl, make(QStringLiteral("buNone")), kLevelOrder, {});
        return;
    }
    if (p.bulletColor.isValid()) {
        XmlElementPtr color = make(QStringLiteral("buClr"));
        color->appendElement(colorElement(p.bulletColor));
        place(lvl, color, kLevelOrder, {});
    }
    if (qAbs(p.bulletSize - 100.0) >= 0.01) {
        const qreal lead = p.runs.isEmpty() ? 18.0 : p.runs.first().pointSize;
        XmlElementPtr size = make(QStringLiteral("buSzPts"));
        set(*size, "val", number(qBound(1.0, lead * p.bulletSize / 100.0, 4000.0) * 100));
        place(lvl, size, kLevelOrder, {});
    }
    if (!p.bulletFont.isEmpty()) {
        XmlElementPtr font = make(QStringLiteral("buFont"));
        set(*font, "typeface", p.bulletFont);
        place(lvl, font, kLevelOrder, {});
    }
    XmlElementPtr mark;
    if (!p.autoNumber.isEmpty()) {
        mark = make(QStringLiteral("buAutoNum"));
        set(*mark, "type", p.autoNumber);
        if (p.autoNumberStart != 1) {
            set(*mark, "startAt", QString::number(qBound(1, p.autoNumberStart, 32767)));
        }
    } else {
        mark = make(QStringLiteral("buChar"));
        set(*mark, "char", p.bulletChar.isEmpty() ? QStringLiteral("•") : p.bulletChar);
    }
    place(lvl, mark, kLevelOrder, {});
}

QString typeface(const QString &family, const ThemeFonts &fonts)
{
    if (!family.isEmpty() && family == fonts.major) {
        return QStringLiteral("+mj-lt");
    }
    if (!family.isEmpty() && family == fonts.minor) {
        return QStringLiteral("+mn-lt");
    }
    return family;
}

void writeRun(XmlElement &lvl, const TextRun &r, const TextRun &o, bool all, const ThemeFonts &fonts)
{
    XmlElement &def = childIn(lvl, QStringLiteral("defRPr"), kLevelOrder);
    if (all || r.pointSize != o.pointSize) {
        set(def, "sz", number(qBound(1.0, r.pointSize, 4000.0) * 100));
    }
    if (all || r.bold != o.bold) {
        set(def, "b", r.bold ? QStringLiteral("1") : QStringLiteral("0"));
    }
    if (all || r.italic != o.italic) {
        set(def, "i", r.italic ? QStringLiteral("1") : QStringLiteral("0"));
    }
    if (all || r.underline != o.underline) {
        set(def, "u", r.underline.isEmpty() ? QStringLiteral("none") : r.underline);
    }
    if (all || r.strike != o.strike) {
        set(def, "strike", r.strike.isEmpty() ? QStringLiteral("noStrike") : r.strike);
    }
    if (all || r.caps != o.caps) {
        set(def, "cap", r.caps.isEmpty() ? QStringLiteral("none") : r.caps);
    }
    if (all || r.spacing != o.spacing) {
        set(def, "spc", number(r.spacing * 100));
    }
    if (all || r.baseline != o.baseline) {
        set(def, "baseline", QString::number(r.baseline));
    }
    if (all || r.color != o.color) {
        XmlElementPtr fill = make(QStringLiteral("solidFill"));
        fill->appendElement(colorElement(r.color.isValid() ? r.color : QColor(Qt::black)));
        place(def, fill, kRunOrder, kRunOrder.mid(1, 6));
    }
    if ((all || r.highlight != o.highlight) && r.highlight.isValid()) {
        XmlElementPtr highlight = make(QStringLiteral("highlight"));
        highlight->appendElement(colorElement(r.highlight));
        place(def, highlight, kRunOrder, {u"highlight"});
    }
    if ((all || r.fontFamily != o.fontFamily) && !r.fontFamily.isEmpty()) {
        XmlElementPtr latin = make(QStringLiteral("latin"));
        set(*latin, "typeface", typeface(r.fontFamily, fonts));
        place(def, latin, kRunOrder, {u"latin"});
    }
}

// `now`'s level properties into `lvl` — every one (`all`) or those that
// differ from `was`.
void writeLevel(XmlElement &lvl, const TextParagraph &now, const TextParagraph &was, bool all, const ThemeFonts &fonts)
{
    if (all || now.marginLeft != was.marginLeft) {
        set(lvl, "marL", emu(now.marginLeft));
    }
    if (all || now.indent != was.indent) {
        set(lvl, "indent", emu(now.indent));
    }
    if (all || now.alignment != was.alignment) {
        set(lvl, "algn", alignment(now.alignment));
    }
    if (all || now.lineSpacing != was.lineSpacing) {
        place(lvl, spacing("lnSpc", now.lineSpacing), kLevelOrder, {u"lnSpc"});
    }
    if (all || now.spaceBefore != was.spaceBefore) {
        place(lvl, spacing("spcBef", now.spaceBefore), kLevelOrder, {u"spcBef"});
    }
    if (all || now.spaceAfter != was.spaceAfter) {
        place(lvl, spacing("spcAft", now.spaceAfter), kLevelOrder, {u"spcAft"});
    }
    if (all || !sameBullet(now, was)) {
        writeBullet(lvl, now);
    }
    const TextRun r = now.runs.isEmpty() ? TextRun() : now.runs.first();
    const TextRun o = was.runs.isEmpty() ? TextRun() : was.runs.first();
    if (all || !(r == o)) {
        writeRun(lvl, r, o, all, fonts);
    }
}

XmlElement &levelIn(XmlElement &style, int level)
{
    const QString name = QStringLiteral("lvl%1pPr").arg(level + 1);
    if (XmlElement *found = style.child(nsA(), name)) {
        return *found;
    }
    QList<QStringView> later;
    static const QStringList names = {QStringLiteral("lvl1pPr"), QStringLiteral("lvl2pPr"), QStringLiteral("lvl3pPr"),
                                      QStringLiteral("lvl4pPr"), QStringLiteral("lvl5pPr"), QStringLiteral("lvl6pPr"),
                                      QStringLiteral("lvl7pPr"), QStringLiteral("lvl8pPr"), QStringLiteral("lvl9pPr"),
                                      QStringLiteral("extLst")};
    for (qsizetype i = level + 1; i < names.size(); ++i) {
        later.append(names.at(i));
    }
    return style.insertElementBefore(make(name), later);
}

TextParagraph formatOf(TextParagraph p)
{
    for (TextRun &run : p.runs) {
        run.text.clear();
    }
    if (p.runs.size() > 1) {
        p.runs = {p.runs.first()};
    }
    p.endPointSize = 0;
    return p;
}

QString wordsOf(const QList<TextParagraph> &paragraphs)
{
    QString out;
    for (const TextParagraph &p : paragraphs) {
        out += QString::number(p.listLevel) + QLatin1Char(':');
        for (const TextRun &r : p.runs) {
            out += r.text;
        }
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace

QList<TextParagraph> levelFormats(const QList<TextParagraph> &paragraphs)
{
    QList<TextParagraph> levels(9);
    for (TextParagraph &level : levels) {
        level.listLevel = -1;
    }
    for (const TextParagraph &p : paragraphs) {
        const int level = qBound(0, p.listLevel, 8);
        if (levels.at(level).listLevel < 0) {
            levels[level] = formatOf(p);
        }
    }
    return levels;
}

void patchListStyle(XmlElement &style, const QList<TextParagraph> &now, const QList<TextParagraph> &was,
                    const ThemeFonts &fonts)
{
    for (int level = 0; level < 9 && level < now.size() && level < was.size(); ++level) {
        const TextParagraph a = formatOf(now.at(level));
        const TextParagraph b = formatOf(was.at(level));
        if (a.listLevel < 0 || b.listLevel < 0 || a == b) {
            continue;
        }
        writeLevel(levelIn(style, level), a, b, false, fonts);
    }
}

XmlElementPtr listStyleElement(const QString &ns, const QString &name, const QList<TextParagraph> &levels,
                               const ThemeFonts &fonts)
{
    XmlElementPtr style = std::make_shared<XmlElement>(ns, name);
    for (int level = 0; level < 9 && level < levels.size(); ++level) {
        if (levels.at(level).listLevel >= 0) {
            writeLevel(levelIn(*style, level), formatOf(levels.at(level)), TextParagraph(), true, fonts);
        }
    }
    return style;
}

XmlElementPtr plainTextBody(PartWriter &out, const QString &ns, const Shape &shape)
{
    const XmlElementPtr spelled = textBodyElement(out, ns, {}, shape.textBody);
    XmlElementPtr body = std::make_shared<XmlElement>(ns, QStringLiteral("txBody"));
    if (const XmlElement *bodyPr = spelled->child(nsA(), u"bodyPr")) {
        body->appendElement(bodyPr->clone());
    }
    body->appendElement(nsA(), QStringLiteral("lstStyle"));
    for (const TextParagraph &p : shape.paragraphs) {
        XmlElement &para = body->appendElement(nsA(), QStringLiteral("p"));
        if (p.listLevel > 0) {
            set(para.appendElement(nsA(), QStringLiteral("pPr")), "lvl", QString::number(qBound(0, p.listLevel, 8)));
        }
        QString text;
        for (const TextRun &r : p.runs) {
            text += r.text;
        }
        if (text.isEmpty()) {
            set(para.appendElement(nsA(), QStringLiteral("endParaRPr")), "lang", QStringLiteral("en-US"));
            continue;
        }
        XmlElement &run = para.appendElement(nsA(), QStringLiteral("r"));
        set(run.appendElement(nsA(), QStringLiteral("rPr")), "lang", QStringLiteral("en-US"));
        run.appendElement(nsA(), QStringLiteral("t")).appendText(Ooxml::xmlSafeText(text));
    }
    if (shape.paragraphs.isEmpty()) {
        body->appendElement(nsA(), QStringLiteral("p"));
    }
    return body;
}

XmlElementPtr templateTextBody(PartWriter &out, const Shape &shape, const ShapeSource &source, bool stylesElsewhere,
                               const ThemeFonts &fonts)
{
    const Shape &was = source.pristine;
    const XmlElement *sourceBody = source.xml->child(source.xml->ns, u"txBody");
    if (sourceBody == nullptr) {
        return textBodyElement(out, source.xml->ns, shape.paragraphs, shape.textBody);
    }
    XmlElementPtr body = sourceBody->clone();
    out.carry(*body, source.part);
    if (shape.textBody != was.textBody) {
        // The body properties as the text writer patches them.
        const TextSource from{sourceBody, &was.paragraphs, &was.textBody, source.part};
        const XmlElementPtr spelled = textBodyElement(out, source.xml->ns, {}, shape.textBody, from);
        if (const XmlElement *bodyPr = spelled->child(nsA(), u"bodyPr")) {
            body->removeChildren(nsA(), u"bodyPr");
            body->insertElementBefore(bodyPr->clone(), {u"lstStyle", u"p"});
        }
    }
    if (!stylesElsewhere && levelFormats(shape.paragraphs) != levelFormats(was.paragraphs)) {
        XmlElement *list = body->child(nsA(), u"lstStyle");
        if (list == nullptr) {
            list = &body->insertElementBefore(make(QStringLiteral("lstStyle")), {u"p"});
        }
        patchListStyle(*list, levelFormats(shape.paragraphs), levelFormats(was.paragraphs), fonts);
    }
    if (wordsOf(shape.paragraphs) != wordsOf(was.paragraphs)) {
        // New prompt text: words only — the look stays the styles'.
        body->removeChildren(nsA(), u"p");
        for (const TextParagraph &p : shape.paragraphs) {
            XmlElement &para = body->appendElement(nsA(), QStringLiteral("p"));
            if (p.listLevel > 0) {
                set(para.appendElement(nsA(), QStringLiteral("pPr")), "lvl", QString::number(qBound(0, p.listLevel, 8)));
            }
            QString text;
            for (const TextRun &r : p.runs) {
                text += r.text;
            }
            if (text.isEmpty()) {
                set(para.appendElement(nsA(), QStringLiteral("endParaRPr")), "lang", QStringLiteral("en-US"));
                continue;
            }
            XmlElement &run = para.appendElement(nsA(), QStringLiteral("r"));
            set(run.appendElement(nsA(), QStringLiteral("rPr")), "lang", QStringLiteral("en-US"));
            run.appendElement(nsA(), QStringLiteral("t")).appendText(Ooxml::xmlSafeText(text));
        }
        if (shape.paragraphs.isEmpty()) {
            body->appendElement(nsA(), QStringLiteral("p"));
        }
    }
    return body;
}

} // namespace QindaOffice::Show::Pptx
