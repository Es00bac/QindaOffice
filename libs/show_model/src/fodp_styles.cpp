// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_styles.h"

#include "odf_dom.h"

#include <QLocale>
#include <QXmlStreamWriter>

namespace QindaOffice::Show::Fodp {

namespace {

QString number(qreal value)
{
    return QString::number(value, 'f', QLocale::FloatingPointShortest);
}

// The model's exact colour (alpha included, or "none" for an invalid one):
// ODF's #rrggbb cannot say either, and a field the format rounds is a field
// the user loses on reopen.
QString exactColor(const QColor &color)
{
    return color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("none");
}

bool paintsFill(ShapeKind kind)
{
    return kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse || kind == ShapeKind::TextBox
        || kind == ShapeKind::AutoShape;
}

bool paintsStroke(ShapeKind kind)
{
    return kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse || kind == ShapeKind::Line
        || kind == ShapeKind::Arrow || kind == ShapeKind::AutoShape;
}

bool holdsText(ShapeKind kind)
{
    return kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse || kind == ShapeKind::TextBox
        || kind == ShapeKind::AutoShape;
}

QString alignmentName(int alignment)
{
    if (alignment & int(Qt::AlignHCenter)) {
        return QStringLiteral("center");
    }
    if (alignment & int(Qt::AlignJustify)) {
        return QStringLiteral("justify");
    }
    if (alignment & int(Qt::AlignRight)) {
        return QStringLiteral("end");
    }
    return QStringLiteral("start");
}

// SMIL spelling of the player's transitions, so LibreOffice plays
// something close; qq:transition keeps the exact name.
QPair<QString, QString> smilTransition(const QString &transition)
{
    if (transition == u"fade") {
        return {QStringLiteral("fade"), QStringLiteral("crossfade")};
    }
    if (transition == u"slide") {
        return {QStringLiteral("slideWipe"), QStringLiteral("fromRight")};
    }
    if (transition == u"zoom") {
        return {QStringLiteral("irisWipe"), QStringLiteral("rectangle")};
    }
    return {};
}

QString serialise(const QString &family, const QList<StyleProperties> &properties)
{
    QString key = family;
    for (const StyleProperties &p : properties) {
        key += QLatin1Char('|') + p.element;
        for (const auto &[name, value] : p.attributes) {
            key += QLatin1Char(' ') + name + QLatin1Char('=') + value;
        }
    }
    return key;
}

} // namespace

QString colorName(const QColor &color)
{
    return (color.isValid() ? color : QColor(Qt::black)).name(QColor::HexRgb);
}

QString percent(qreal value)
{
    return number(value) + QLatin1Char('%');
}

QString StyleRegistry::registerStyle(const QString &prefix, const QString &family,
                                     const QList<StyleProperties> &properties)
{
    const QString key = serialise(family, properties);
    const auto found = m_byKey.constFind(key);
    if (found != m_byKey.constEnd()) {
        return found.value();
    }
    const QString name = prefix + QString::number(++m_counters[prefix]);
    m_entries.append({name, family, properties});
    m_byKey.insert(key, name);
    return name;
}

QString StyleRegistry::graphicStyle(const Shape &shape)
{
    StyleProperties g{QStringLiteral("style:graphic-properties"), {}};
    auto add = [&g](const char *name, const QString &value) { g.attributes.append({QLatin1String(name), value}); };
    // The ODF view: what QindaShow's renderer actually draws for this kind.
    if (paintsFill(shape.kind) && shape.fill.isValid() && shape.fill.alpha() > 0) {
        add("draw:fill", QStringLiteral("solid"));
        add("draw:fill-color", colorName(shape.fill));
        if (shape.fill.alpha() < 255) {
            add("draw:opacity", percent(100.0 * shape.fill.alphaF()));
        }
    } else {
        add("draw:fill", QStringLiteral("none"));
    }
    if (paintsStroke(shape.kind) && shape.strokeWidth > 0 && shape.stroke.isValid() && shape.stroke.alpha() > 0) {
        add("draw:stroke", QStringLiteral("solid"));
        add("svg:stroke-color", colorName(shape.stroke));
        add("svg:stroke-width", Odf::ptToLength(shape.strokeWidth));
        if (shape.stroke.alpha() < 255) {
            add("svg:stroke-opacity", percent(100.0 * shape.stroke.alphaF()));
        }
    } else {
        add("draw:stroke", QStringLiteral("none"));
    }
    if (shape.kind == ShapeKind::Arrow) {
        add("draw:marker-end", QString::fromUtf16(kArrowMarker));
        add("draw:marker-end-width", Odf::ptToLength(12));
    }
    if (holdsText(shape.kind)) {
        // The text body the renderer lays out with: QindaShow's own shapes
        // keep 6 pt side and 4 pt top/bottom insets, top anchored, frames
        // that do not grow; an Office shape brings its own.
        const TextBodyStyle &body = shape.textBody;
        add("fo:padding-left", Odf::ptToLength(body.insetLeft));
        add("fo:padding-right", Odf::ptToLength(body.insetRight));
        add("fo:padding-top", Odf::ptToLength(body.insetTop));
        add("fo:padding-bottom", Odf::ptToLength(body.insetBottom));
        add("draw:textarea-vertical-align", body.anchor == TextAnchor::Middle ? QStringLiteral("middle")
                                                : body.anchor == TextAnchor::Bottom ? QStringLiteral("bottom")
                                                                                    : QStringLiteral("top"));
        add("draw:auto-grow-height", body.autoFit == AutoFit::Shape ? QStringLiteral("true") : QStringLiteral("false"));
        add("fo:wrap-option", body.wrap ? QStringLiteral("wrap") : QStringLiteral("no-wrap"));
    }
    // The model's own values, exactly (alpha, invisible-for-this-kind).
    add("qq:fill-color", exactColor(shape.fill));
    add("qq:stroke-color", exactColor(shape.stroke));
    add("qq:stroke-width", number(shape.strokeWidth));
    return registerStyle(QStringLiteral("gr"), QStringLiteral("graphic"), {g});
}

QString StyleRegistry::paragraphStyle(const TextParagraph &paragraph)
{
    if (paragraph.alignment == 0) {
        return {};
    }
    StyleProperties p{QStringLiteral("style:paragraph-properties"),
                      {{QStringLiteral("fo:text-align"), alignmentName(paragraph.alignment)},
                       {QStringLiteral("qq:align"), QString::number(paragraph.alignment)}}};
    return registerStyle(QStringLiteral("P"), QStringLiteral("paragraph"), {p});
}

QString StyleRegistry::textStyle(const TextRun &run)
{
    StyleProperties t{QStringLiteral("style:text-properties"), {}};
    auto add = [&t](const char *name, const QString &value) { t.attributes.append({QLatin1String(name), value}); };
    if (run.bold) {
        add("fo:font-weight", QStringLiteral("bold"));
    }
    if (run.italic) {
        add("fo:font-style", QStringLiteral("italic"));
    }
    add("fo:color", colorName(run.color));
    if (!run.color.isValid() || run.color.alpha() != 255) {
        add("qq:color", exactColor(run.color));
    }
    add("fo:font-size", Odf::ptToLength(run.pointSize));
    if (!run.fontFamily.isEmpty()) {
        QString family = run.fontFamily;
        family.replace(QLatin1Char('\''), QString());
        add("fo:font-family", QLatin1Char('\'') + family + QLatin1Char('\''));
    }
    return registerStyle(QStringLiteral("T"), QStringLiteral("text"), {t});
}

QString StyleRegistry::pageStyle(const Slide &slide)
{
    StyleProperties d{QStringLiteral("style:drawing-page-properties"), {}};
    const auto [type, subtype] = smilTransition(slide.transition);
    if (!type.isEmpty()) {
        d.attributes.append({QStringLiteral("presentation:transition-type"), QStringLiteral("manual")});
        d.attributes.append({QStringLiteral("presentation:transition-speed"), QStringLiteral("medium")});
        d.attributes.append({QStringLiteral("smil:type"), type});
        d.attributes.append({QStringLiteral("smil:subtype"), subtype});
    }
    d.attributes.append({QStringLiteral("qq:transition"), slide.transition});
    if (slide.hidden) {
        d.attributes.append({QStringLiteral("presentation:visibility"), QStringLiteral("hidden")});
    }
    if (slide.background.set) {
        // The slide's own background, as LibreOffice shows it.
        d.attributes.append({QStringLiteral("draw:background-size"), QStringLiteral("full")});
        d.attributes.append({QStringLiteral("draw:fill"), QStringLiteral("solid")});
        d.attributes.append({QStringLiteral("draw:fill-color"), colorName(slide.background.color)});
    }
    return registerStyle(QStringLiteral("dp"), QStringLiteral("drawing-page"), {d});
}

QString StyleRegistry::masterPageStyle(const QColor &background)
{
    StyleProperties d{QStringLiteral("style:drawing-page-properties"),
                      {{QStringLiteral("draw:background-size"), QStringLiteral("full")},
                       {QStringLiteral("draw:fill"), QStringLiteral("solid")},
                       {QStringLiteral("draw:fill-color"), colorName(background)}}};
    return registerStyle(QStringLiteral("dpm"), QStringLiteral("drawing-page"), {d});
}

void StyleRegistry::writeAutomaticStyles(QXmlStreamWriter &xml, const QSizeF &slideSize) const
{
    xml.writeStartElement(QStringLiteral("style:page-layout"));
    xml.writeAttribute(QStringLiteral("style:name"), QString::fromUtf16(kPageLayout));
    xml.writeEmptyElement(QStringLiteral("style:page-layout-properties"));
    xml.writeAttribute(QStringLiteral("fo:margin-top"), QStringLiteral("0pt"));
    xml.writeAttribute(QStringLiteral("fo:margin-bottom"), QStringLiteral("0pt"));
    xml.writeAttribute(QStringLiteral("fo:margin-left"), QStringLiteral("0pt"));
    xml.writeAttribute(QStringLiteral("fo:margin-right"), QStringLiteral("0pt"));
    xml.writeAttribute(QStringLiteral("fo:page-width"), Odf::ptToLength(slideSize.width()));
    xml.writeAttribute(QStringLiteral("fo:page-height"), Odf::ptToLength(slideSize.height()));
    xml.writeAttribute(QStringLiteral("style:print-orientation"), slideSize.width() >= slideSize.height()
                                                                      ? QStringLiteral("landscape")
                                                                      : QStringLiteral("portrait"));
    xml.writeEndElement(); // page-layout
    for (const Entry &entry : m_entries) {
        xml.writeStartElement(QStringLiteral("style:style"));
        xml.writeAttribute(QStringLiteral("style:name"), entry.name);
        xml.writeAttribute(QStringLiteral("style:family"), entry.family);
        for (const StyleProperties &properties : entry.properties) {
            xml.writeEmptyElement(properties.element);
            for (const auto &[name, value] : properties.attributes) {
                xml.writeAttribute(name, value);
            }
        }
        xml.writeEndElement(); // style:style
    }
    // Bullets: one list style, a bullet at every outline level, the label
    // hanging 0.6 cm left of the text.
    xml.writeStartElement(QStringLiteral("text:list-style"));
    xml.writeAttribute(QStringLiteral("style:name"), QString::fromUtf16(kListStyle));
    for (int level = 1; level <= 9; ++level) {
        xml.writeStartElement(QStringLiteral("text:list-level-style-bullet"));
        xml.writeAttribute(QStringLiteral("text:level"), QString::number(level));
        xml.writeAttribute(QStringLiteral("text:bullet-char"), QStringLiteral("•"));
        xml.writeEmptyElement(QStringLiteral("style:list-level-properties"));
        xml.writeAttribute(QStringLiteral("text:space-before"), Odf::ptToLength(18.0 * (level - 1)));
        xml.writeAttribute(QStringLiteral("text:min-label-width"), Odf::ptToLength(18.0));
        xml.writeEndElement(); // list-level-style-bullet
    }
    xml.writeEndElement(); // text:list-style
}

void writeOdfText(QXmlStreamWriter &xml, const QString &text, bool *previousSpace, bool endsParagraph)
{
    QString pending;
    auto flush = [&] {
        if (!pending.isEmpty()) {
            xml.writeCharacters(pending);
            pending.clear();
        }
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\t')) {
            flush();
            xml.writeEmptyElement(QStringLiteral("text:tab"));
            *previousSpace = true;
        } else if (c == QLatin1Char('\n') || c == QChar(QChar::LineSeparator) || c == QChar(QChar::ParagraphSeparator)) {
            flush();
            xml.writeEmptyElement(QStringLiteral("text:line-break"));
            *previousSpace = true;
        } else if (c == QLatin1Char(' ')) {
            // A literal space survives ODF white-space collapsing only when it
            // follows a non-space and does not end the paragraph; every other
            // space is spelled text:s (runs of them counted).
            qsizetype run = 1;
            while (i + run < text.size() && text.at(i + run) == QLatin1Char(' ')) {
                ++run;
            }
            const bool trailing = endsParagraph && i + run == text.size();
            qsizetype encoded = run;
            if (!*previousSpace && !trailing) {
                pending += QLatin1Char(' ');
                encoded = run - 1;
            }
            if (encoded > 0) {
                flush();
                xml.writeEmptyElement(QStringLiteral("text:s"));
                if (encoded > 1) {
                    xml.writeAttribute(QStringLiteral("text:c"), QString::number(encoded));
                }
            }
            *previousSpace = true;
            i += run - 1;
        } else {
            pending += c;
            *previousSpace = false;
        }
    }
    flush();
}

namespace {

void writeParagraph(QXmlStreamWriter &xml, StyleRegistry &styles, const TextParagraph &paragraph)
{
    xml.writeStartElement(QStringLiteral("text:p"));
    const QString style = styles.paragraphStyle(paragraph);
    if (!style.isEmpty()) {
        xml.writeAttribute(QStringLiteral("text:style-name"), style);
    }
    if (!paragraph.bullet && paragraph.listLevel > 0) {
        // An outline level without a bullet has no ODF spelling outside a list.
        xml.writeAttribute(QStringLiteral("qq:level"), QString::number(paragraph.listLevel));
    }
    // AGENT-GUARD: no indentation inside a paragraph — whitespace there is
    // CONTENT in ODF (it became stray spaces between runs in LibreOffice).
    const bool formatting = xml.autoFormatting();
    xml.setAutoFormatting(false);
    bool previousSpace = true; // a paragraph's leading spaces are dropped
    qsizetype lastWithText = paragraph.runs.size() - 1;
    while (lastWithText > 0 && paragraph.runs.at(lastWithText).text.isEmpty()) {
        --lastWithText; // trailing empty runs do not protect a trailing space
    }
    for (qsizetype i = 0; i < paragraph.runs.size(); ++i) {
        const TextRun &run = paragraph.runs.at(i);
        xml.writeStartElement(QStringLiteral("text:span"));
        xml.writeAttribute(QStringLiteral("text:style-name"), styles.textStyle(run));
        writeOdfText(xml, run.text, &previousSpace, i >= lastWithText);
        xml.writeEndElement(); // text:span
    }
    xml.writeEndElement(); // text:p
    xml.setAutoFormatting(formatting);
}

} // namespace

void writeParagraphs(QXmlStreamWriter &xml, StyleRegistry &styles, const QList<TextParagraph> &paragraphs)
{
    // Every open <text:list> holds exactly one open <text:list-item>; a
    // paragraph at outline level L sits L+1 lists deep.
    int open = 0;
    const auto closeTo = [&xml, &open](int depth) {
        while (open > depth) {
            xml.writeEndElement(); // text:list-item
            xml.writeEndElement(); // text:list
            --open;
        }
    };
    for (const TextParagraph &paragraph : paragraphs) {
        if (!paragraph.bullet) {
            closeTo(0);
            writeParagraph(xml, styles, paragraph);
            continue;
        }
        const int depth = qBound(1, paragraph.listLevel + 1, 9);
        if (open == 0) {
            xml.writeStartElement(QStringLiteral("text:list"));
            xml.writeAttribute(QStringLiteral("text:style-name"), QString::fromUtf16(StyleRegistry::kListStyle));
            xml.writeStartElement(QStringLiteral("text:list-item"));
            open = 1;
        } else if (depth <= open) {
            closeTo(depth);
            xml.writeEndElement(); // the sibling's text:list-item
            xml.writeStartElement(QStringLiteral("text:list-item"));
        }
        while (open < depth) {
            xml.writeStartElement(QStringLiteral("text:list"));
            xml.writeStartElement(QStringLiteral("text:list-item"));
            ++open;
        }
        writeParagraph(xml, styles, paragraph);
    }
    closeTo(0);
}

} // namespace QindaOffice::Show::Fodp
