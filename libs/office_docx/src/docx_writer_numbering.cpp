// SPDX-License-Identifier: GPL-3.0-or-later
// w:numbering for the .docx writer: lists read from a .docx keep their
// numbering instance; lists created or changed in the editor get a new
// nine-level definition built from their QTextListFormat.

#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

QString formatFor(QTextListFormat::Style style)
{
    switch (style) {
    case QTextListFormat::ListDisc:
    case QTextListFormat::ListCircle:
    case QTextListFormat::ListSquare:
        return QStringLiteral("bullet");
    case QTextListFormat::ListLowerAlpha:
        return QStringLiteral("lowerLetter");
    case QTextListFormat::ListUpperAlpha:
        return QStringLiteral("upperLetter");
    case QTextListFormat::ListLowerRoman:
        return QStringLiteral("lowerRoman");
    case QTextListFormat::ListUpperRoman:
        return QStringLiteral("upperRoman");
    case QTextListFormat::ListStyleUndefined:
        return QStringLiteral("none");
    default:
        return QStringLiteral("decimal");
    }
}

// Word's own bullet glyphs and fonts.
std::pair<QString, QString> bulletGlyph(QTextListFormat::Style style)
{
    if (style == QTextListFormat::ListCircle) {
        return {QStringLiteral("o"), QStringLiteral("Courier New")};
    }
    if (style == QTextListFormat::ListSquare) {
        return {QString(QChar(0xF0A7)), QStringLiteral("Wingdings")};
    }
    return {QString(QChar(0xF0B7)), QStringLiteral("Symbol")};
}

} // namespace

NumberingBuilder::NumberingBuilder(XmlElementPtr source)
    : m_root(source ? source->clone() : nullptr)
{
    if (!m_root) {
        return;
    }
    for (const XmlElement *a : m_root->elements(kW, u"abstractNum")) {
        m_nextAbstract = qMax(m_nextAbstract, wAttr(a, u"abstractNumId").toInt() + 1);
    }
    for (const XmlElement *n : m_root->elements(kW, u"num")) {
        m_nextNum = qMax(m_nextNum, wAttr(n, u"numId").toInt() + 1);
    }
}

NumberingBuilder::Ref NumberingBuilder::refFor(const QTextList *list, qreal indentWidth)
{
    const auto memo = m_refs.constFind(list);
    if (memo != m_refs.constEnd()) {
        return memo.value();
    }
    const QTextListFormat lf = list->format();
    const QVariantMap then = lf.property(kListSnapshot).toMap();
    if (!then.isEmpty() && m_root) {
        const QVariantMap now{{QStringLiteral("style"), int(lf.style())},
                              {QStringLiteral("prefix"), lf.numberPrefix()},
                              {QStringLiteral("suffix"), lf.numberSuffix()},
                              {QStringLiteral("start"), lf.start()}};
        if (now == then) {
            const Ref ref{lf.intProperty(kListNumId), lf.intProperty(kListLevel)};
            m_refs.insert(list, ref);
            return ref;
        }
    }
    if (!m_root) {
        m_root = std::make_shared<XmlElement>(kW, QStringLiteral("numbering"));
    }
    const int ilvl = lf.hasProperty(kListLevel) ? qBound(0, lf.intProperty(kListLevel), 8)
                                                : qBound(0, lf.indent() - 1, 8);
    const Ref ref{m_nextNum++, ilvl};
    const int abstractId = m_nextAbstract++;
    auto abstract = std::make_shared<XmlElement>(kW, QStringLiteral("abstractNum"));
    abstract->setAttribute(kW, QStringLiteral("abstractNumId"), QString::number(abstractId));
    setChildVal(*abstract, QStringLiteral("multiLevelType"), QStringLiteral("hybridMultilevel"));
    static const QStringList kDefaults{QStringLiteral("decimal"), QStringLiteral("lowerLetter"),
                                       QStringLiteral("lowerRoman")};
    for (int k = 0; k < 9; ++k) {
        XmlElement &lvl = abstract->appendElement(kW, QStringLiteral("lvl"));
        lvl.setAttribute(kW, QStringLiteral("ilvl"), QString::number(k));
        const bool own = k == ilvl;
        const QString format = own ? formatFor(lf.style()) : kDefaults.at(k % 3);
        setChildVal(lvl, QStringLiteral("start"), QString::number(own ? qMax(0, lf.start()) : 1));
        setChildVal(lvl, QStringLiteral("numFmt"), format);
        QString text;
        QString font;
        if (format == u"bullet") {
            std::tie(text, font) = bulletGlyph(lf.style());
        } else if (format != u"none") {
            text = (own ? lf.numberPrefix() : QString()) + QStringLiteral("%%1").arg(k + 1)
                + (own ? lf.numberSuffix() : QStringLiteral("."));
        }
        setChildVal(lvl, QStringLiteral("lvlText"), text);
        setChildVal(lvl, QStringLiteral("lvlJc"), QStringLiteral("left"));
        const qreal indentPx = own && lf.indent() > 0 ? lf.indent() * indentWidth : (k + 1) * indentWidth;
        XmlElement &ind = lvl.appendElement(kW, QStringLiteral("pPr")).appendElement(kW, QStringLiteral("ind"));
        ind.setAttribute(kW, QStringLiteral("left"), twips(indentPx));
        ind.setAttribute(kW, QStringLiteral("hanging"), QStringLiteral("360"));
        if (!font.isEmpty()) {
            XmlElement &fonts = lvl.appendElement(kW, QStringLiteral("rPr")).appendElement(kW, QStringLiteral("rFonts"));
            fonts.setAttribute(kW, QStringLiteral("ascii"), font);
            fonts.setAttribute(kW, QStringLiteral("hAnsi"), font);
            fonts.setAttribute(kW, QStringLiteral("hint"), QStringLiteral("default"));
        }
    }
    // Schema order: abstractNum* before num*, numIdMacAtCleanup last.
    m_root->insertElementBefore(abstract, {u"num", u"numIdMacAtCleanup"});
    auto num = std::make_shared<XmlElement>(kW, QStringLiteral("num"));
    num->setAttribute(kW, QStringLiteral("numId"), QString::number(ref.numId));
    setChildVal(*num, QStringLiteral("abstractNumId"), QString::number(abstractId));
    m_root->insertElementBefore(num, {u"numIdMacAtCleanup"});
    m_refs.insert(list, ref);
    return ref;
}

XmlElementPtr NumberingBuilder::result() const
{
    return m_root;
}

} // namespace QindaOffice::Docx::detail
