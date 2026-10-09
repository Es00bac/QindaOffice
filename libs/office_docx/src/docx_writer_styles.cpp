// SPDX-License-Identifier: GPL-3.0-or-later
// w:styles for the .docx writer: the source document's styles part when
// there is one, plus definitions of every built-in style the output uses and
// the source lacks; a complete styles part for a new document. The built-ins
// mirror QindaWrite's StyleCatalog so a document looks the same in Word.

#include "docx_format.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"

#include <QFont>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// AGENT-CONTRACT: sizes, weights and alignment follow StyleCatalog
// (apps/qindawrite/src/style_catalog.cpp); change both together.
const char *definition(const QString &id)
{
    if (id == u"Normal") {
        return R"(<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>)";
    }
    if (id == u"Title") {
        return R"(<w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:jc w:val="center"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val="52"/><w:szCs w:val="52"/></w:rPr></w:style>)";
    }
    if (id == u"Heading1") {
        return R"(<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val="40"/><w:szCs w:val="40"/></w:rPr></w:style>)";
    }
    if (id == u"Heading2") {
        return R"(<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val="32"/><w:szCs w:val="32"/></w:rPr></w:style>)";
    }
    if (id == u"Heading3") {
        return R"(<w:style w:type="paragraph" w:styleId="Heading3"><w:name w:val="heading 3"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="2"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val="28"/><w:szCs w:val="28"/></w:rPr></w:style>)";
    }
    if (id == u"Heading4") {
        return R"(<w:style w:type="paragraph" w:styleId="Heading4"><w:name w:val="heading 4"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="3"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr></w:style>)";
    }
    if (id == u"Heading5") {
        return R"(<w:style w:type="paragraph" w:styleId="Heading5"><w:name w:val="heading 5"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="4"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val="22"/><w:szCs w:val="22"/></w:rPr></w:style>)";
    }
    if (id == u"Heading6") {
        return R"(<w:style w:type="paragraph" w:styleId="Heading6"><w:name w:val="heading 6"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:outlineLvl w:val="5"/></w:pPr><w:rPr><w:b/><w:bCs/><w:i/><w:iCs/><w:sz w:val="22"/><w:szCs w:val="22"/></w:rPr></w:style>)";
    }
    if (id == u"Quote") {
        return R"(<w:style w:type="paragraph" w:styleId="Quote"><w:name w:val="Quote"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:ind w:left="450"/></w:pPr><w:rPr><w:i/><w:iCs/></w:rPr></w:style>)";
    }
    if (id == u"Code") {
        return R"(<w:style w:type="paragraph" w:styleId="Code"><w:name w:val="Code"/><w:basedOn w:val="Normal"/><w:qFormat/><w:pPr><w:keepLines/></w:pPr><w:rPr><w:rFonts w:ascii="Courier New" w:hAnsi="Courier New" w:cs="Courier New"/><w:sz w:val="20"/><w:szCs w:val="20"/></w:rPr></w:style>)";
    }
    if (id == u"Hyperlink") {
        return R"(<w:style w:type="character" w:styleId="Hyperlink"><w:name w:val="Hyperlink"/><w:rPr><w:color w:val="0563C1"/><w:u w:val="single"/></w:rPr></w:style>)";
    }
    if (id == u"FootnoteText") {
        return R"(<w:style w:type="paragraph" w:styleId="FootnoteText"><w:name w:val="footnote text"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:after="0" w:line="240" w:lineRule="auto"/></w:pPr><w:rPr><w:sz w:val="20"/><w:szCs w:val="20"/></w:rPr></w:style>)";
    }
    if (id == u"FootnoteReference") {
        return R"(<w:style w:type="character" w:styleId="FootnoteReference"><w:name w:val="footnote reference"/><w:rPr><w:vertAlign w:val="superscript"/></w:rPr></w:style>)";
    }
    if (id == u"CommentText") {
        return R"(<w:style w:type="paragraph" w:styleId="CommentText"><w:name w:val="annotation text"/><w:basedOn w:val="Normal"/><w:uiPriority w:val="99"/><w:unhideWhenUsed/><w:pPr><w:spacing w:line="240" w:lineRule="auto"/></w:pPr><w:rPr><w:sz w:val="20"/><w:szCs w:val="20"/></w:rPr></w:style>)";
    }
    if (id == u"CommentReference") {
        return R"(<w:style w:type="character" w:styleId="CommentReference"><w:name w:val="annotation reference"/><w:basedOn w:val="DefaultParagraphFont"/><w:uiPriority w:val="99"/><w:semiHidden/><w:unhideWhenUsed/><w:rPr><w:sz w:val="16"/><w:szCs w:val="16"/></w:rPr></w:style>)";
    }
    if (id == u"TableGrid") {
        return R"(<w:style w:type="table" w:styleId="TableGrid"><w:name w:val="Table Grid"/><w:pPr><w:spacing w:after="0" w:line="240" w:lineRule="auto"/></w:pPr><w:tblPr><w:tblBorders><w:top w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:left w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:bottom w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:right w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:insideH w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:insideV w:val="single" w:sz="4" w:space="0" w:color="auto"/></w:tblBorders><w:tblCellMar><w:left w:w="108" w:type="dxa"/><w:right w:w="108" w:type="dxa"/></w:tblCellMar></w:tblPr></w:style>)";
    }
    if (id == u"DefaultParagraphFont") {
        return R"(<w:style w:type="character" w:default="1" w:styleId="DefaultParagraphFont"><w:name w:val="Default Paragraph Font"/><w:uiPriority w:val="1"/><w:semiHidden/><w:unhideWhenUsed/></w:style>)";
    }
    if (id == u"TableNormal") {
        return R"(<w:style w:type="table" w:default="1" w:styleId="TableNormal"><w:name w:val="Normal Table"/><w:uiPriority w:val="99"/><w:semiHidden/><w:unhideWhenUsed/><w:tblPr><w:tblInd w:w="0" w:type="dxa"/><w:tblCellMar><w:top w:w="0" w:type="dxa"/><w:left w:w="108" w:type="dxa"/><w:bottom w:w="0" w:type="dxa"/><w:right w:w="108" w:type="dxa"/></w:tblCellMar></w:tblPr></w:style>)";
    }
    return nullptr;
}

XmlElementPtr parseWithW(const char *xml)
{
    QString text = QString::fromUtf8(xml);
    text.insert(text.indexOf(QLatin1Char(' ')),
                QStringLiteral(" xmlns:w=\"%1\"").arg(QString::fromUtf16(Ooxml::Ns::W)));
    return Ooxml::parseFragment(text);
}

} // namespace

QString builtInStyleId(int writeStyleId)
{
    // StyleCatalog::BuiltInId: Normal 0, Title 1, Heading1–6 2–7, Quote 8, Code 9.
    switch (writeStyleId) {
    case 0: return QStringLiteral("Normal");
    case 1: return QStringLiteral("Title");
    case 8: return QStringLiteral("Quote");
    case 9: return QStringLiteral("Code");
    default:
        if (writeStyleId >= 2 && writeStyleId <= 7) {
            return QStringLiteral("Heading%1").arg(writeStyleId - 1);
        }
        return {};
    }
}

XmlElementPtr builtInStyle(const QString &styleId)
{
    const char *xml = definition(styleId);
    return xml != nullptr ? parseWithW(xml) : nullptr;
}

// The styles part: the source's (cloned) or a new one, with every used
// built-in that is missing appended.
XmlElementPtr stylesPart(const WriteEnv &env, const QFont &defaultFont, const Ooxml::XmlElementPtr &source)
{
    XmlElementPtr root = source ? source->clone() : nullptr;
    QSet<QString> defined;
    if (!root) {
        root = std::make_shared<XmlElement>(kW, QStringLiteral("styles"));
        XmlElement &defaults = root->appendElement(kW, QStringLiteral("docDefaults"));
        XmlElement &rPr = defaults.appendElement(kW, QStringLiteral("rPrDefault")).appendElement(kW, QStringLiteral("rPr"));
        QString family = wordFontFamily(defaultFont.family());
        if (family.isEmpty() || family.contains(u"Sans", Qt::CaseInsensitive)) {
            family = QStringLiteral("Calibri");
        }
        XmlElement &fonts = rPr.appendElement(kW, QStringLiteral("rFonts"));
        for (const char *attr : {"ascii", "eastAsia", "hAnsi", "cs"}) {
            fonts.setAttribute(kW, QString::fromLatin1(attr), family);
        }
        const qreal size = defaultFont.pointSizeF() > 0 ? defaultFont.pointSizeF() : 11.0;
        setChildVal(rPr, QStringLiteral("sz"), QString::number(qRound(size * 2)));
        setChildVal(rPr, QStringLiteral("szCs"), QString::number(qRound(size * 2)));
        XmlElement &lang = rPr.appendElement(kW, QStringLiteral("lang"));
        lang.setAttribute(kW, QStringLiteral("val"), QStringLiteral("en-US"));
        lang.setAttribute(kW, QStringLiteral("eastAsia"), QStringLiteral("en-US"));
        lang.setAttribute(kW, QStringLiteral("bidi"), QStringLiteral("ar-SA"));
        XmlElement &pPr = defaults.appendElement(kW, QStringLiteral("pPrDefault")).appendElement(kW, QStringLiteral("pPr"));
        XmlElement &spacing = pPr.appendElement(kW, QStringLiteral("spacing"));
        spacing.setAttribute(kW, QStringLiteral("after"), QStringLiteral("0"));
        spacing.setAttribute(kW, QStringLiteral("line"), QStringLiteral("240"));
        spacing.setAttribute(kW, QStringLiteral("lineRule"), QStringLiteral("auto"));
        for (const char *base : {"Normal", "DefaultParagraphFont", "TableNormal"}) {
            root->appendElement(builtInStyle(QString::fromLatin1(base)));
        }
    }
    for (const XmlElement *style : root->elements(kW, u"style")) {
        defined.insert(style->attr(u"styleId"));
    }
    QStringList wanted(env.usedStyles.cbegin(), env.usedStyles.cend());
    wanted.sort();
    for (const QString &id : std::as_const(wanted)) {
        if (defined.contains(id)) {
            continue;
        }
        if (XmlElementPtr style = builtInStyle(id)) {
            // A built-in based on Normal in a document whose base style has
            // another id (localised Word) is based on that one instead.
            if (XmlElement *basedOn = style->child(kW, u"basedOn"); basedOn != nullptr && !defined.contains(QStringLiteral("Normal"))) {
                const QString base = env.sourceStyles.defaultParagraphStyle();
                if (base.isEmpty()) {
                    style->removeChildren(kW, u"basedOn");
                } else {
                    setVal(*basedOn, base);
                }
            }
            root->appendElement(style);
            defined.insert(id);
        }
    }
    return root;
}

} // namespace QindaOffice::Docx::detail
