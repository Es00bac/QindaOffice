// SPDX-License-Identifier: GPL-3.0-or-later
// w:numbering for the .docx writer. Word lists (docx_lists.h) keep their
// numbering instance while it is as read and are otherwise written as Word
// writes them: a restart as a new w:num over the same w:abstractNum with
// w:lvlOverride/w:startOverride, a changed or new list as a new
// w:abstractNum and w:num. Qt lists (from HTML or Markdown) get a new
// nine-level definition built from their QTextListFormat.

#include "docx_lists.h"
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

// Word identifies a definition by w:nsid (eight hex digits); a new one
// gets one derived from its id, stable from save to save.
QString nsidFor(int abstractId, const QString &markup)
{
    const quint32 hash = quint32(qHash(markup, size_t(abstractId) * 2654435761u)) | 0x10000000u;
    return QStringLiteral("%1").arg(hash, 8, 16, QLatin1Char('0')).toUpper();
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
    for (const XmlChild &c : std::as_const(source->children)) {
        if (c.element && c.element->is(kW, u"abstractNum")) {
            m_sourceAbstracts.insert(wAttr(c.element.get(), u"abstractNumId"), Ooxml::serializeFragment(*c.element));
        }
    }
    for (const XmlElement *n : m_root->elements(kW, u"num")) {
        const int id = wAttr(n, u"numId").toInt();
        m_sourceNums.insert(id);
        m_nextNum = qMax(m_nextNum, id + 1);
    }
}

int NumberingBuilder::freshNumId(int wanted)
{
    int id = wanted;
    if (id <= 0 || m_sourceNums.contains(id) || m_usedNums.contains(id)) {
        while (m_sourceNums.contains(m_nextNum) || m_usedNums.contains(m_nextNum)) {
            ++m_nextNum;
        }
        id = m_nextNum++;
    }
    m_usedNums.insert(id);
    return id;
}

NumberingBuilder::Ref NumberingBuilder::wordRef(const QTextListFormat &lf)
{
    const int numId = lf.intProperty(kListNumId);
    const int ilvl = qBound(0, lf.intProperty(kListLevel), 8);
    if (const auto known = m_wordNums.constFind(numId); known != m_wordNums.constEnd()) {
        return {known.value(), ilvl};
    }
    const QString definition = lf.stringProperty(kListDefinition);
    const QList<int> starts = listStarts(lf);
    QVariantList startList;
    for (const int v : starts) {
        startList.append(v);
    }
    const QVariantMap then = lf.property(kListSnapshot).toMap();
    if (m_root && m_sourceNums.contains(numId) && then.value(QStringLiteral("num")).toInt() == numId
        && then.value(QStringLiteral("def")).toString() == definition && then.value(QStringLiteral("starts")).toList() == startList) {
        m_usedNums.insert(numId);
        m_wordNums.insert(numId, numId); // untouched: its w:num and w:abstractNum stay as they are
        return {numId, ilvl};
    }
    if (!m_root) {
        m_root = std::make_shared<XmlElement>(kW, QStringLiteral("numbering"));
    }
    // The definition: the source's own while it is unchanged, else new.
    const QString key = lf.stringProperty(kListAbstractKey);
    QString abstractId;
    if (key.startsWith(u"src:") && m_sourceAbstracts.value(key.mid(4)) == definition) {
        abstractId = key.mid(4);
    } else if (const auto made = m_abstracts.constFind(key + QLatin1Char('\n') + definition); made != m_abstracts.constEnd()) {
        abstractId = QString::number(made.value());
    } else {
        XmlElementPtr abstract = Ooxml::parseFragment(definition);
        if (!abstract) {
            abstract = Ooxml::parseFragment(numberingDefinition(QStringLiteral("decimal"), QStringLiteral("%1.")));
        }
        const int id = m_nextAbstract++;
        abstract->setAttribute(kW, QStringLiteral("abstractNumId"), QString::number(id));
        for (const char16_t *name : {u"nsid", u"tmpl", u"styleLink", u"numStyleLink"}) {
            abstract->removeChildren(kW, QStringView(name));
        }
        auto nsid = std::make_shared<XmlElement>(kW, QStringLiteral("nsid"));
        nsid->setAttribute(kW, QStringLiteral("val"), nsidFor(id, definition));
        abstract->children.prepend({nsid, QString()});
        m_root->insertElementBefore(abstract, {u"num", u"numIdMacAtCleanup"});
        m_abstracts.insert(key + QLatin1Char('\n') + definition, id);
        abstractId = QString::number(id);
    }
    const int written = freshNumId(numId);
    auto num = std::make_shared<XmlElement>(kW, QStringLiteral("num"));
    num->setAttribute(kW, QStringLiteral("numId"), QString::number(written));
    setChildVal(*num, QStringLiteral("abstractNumId"), abstractId);
    for (int k = 0; k < 9; ++k) {
        if (starts.at(k) >= 0) {
            XmlElement &over = num->appendElement(kW, QStringLiteral("lvlOverride"));
            over.setAttribute(kW, QStringLiteral("ilvl"), QString::number(k));
            setChildVal(over, QStringLiteral("startOverride"), QString::number(starts.at(k)));
        }
    }
    m_root->insertElementBefore(num, {u"numIdMacAtCleanup"});
    m_wordNums.insert(numId, written);
    return {written, ilvl};
}

NumberingBuilder::Ref NumberingBuilder::refFor(const QTextList *list, qreal indentWidth)
{
    const auto memo = m_refs.constFind(list);
    if (memo != m_refs.constEnd()) {
        return memo.value();
    }
    const QTextListFormat lf = list->format();
    if (isWordList(lf)) {
        const Ref ref = wordRef(lf);
        m_refs.insert(list, ref);
        return ref;
    }
    const QVariantMap then = lf.property(kListSnapshot).toMap();
    if (!then.isEmpty() && m_root) {
        const QVariantMap now{{QStringLiteral("style"), int(lf.style())},
                              {QStringLiteral("prefix"), lf.numberPrefix()},
                              {QStringLiteral("suffix"), lf.numberSuffix()},
                              {QStringLiteral("start"), lf.start()}};
        QVariantMap thenQt = then;
        for (const char *key : {"num", "def", "starts"}) {
            thenQt.remove(QString::fromLatin1(key));
        }
        if (now == thenQt) {
            const Ref ref{lf.intProperty(kListNumId), lf.intProperty(kListLevel)};
            m_usedNums.insert(ref.numId);
            m_refs.insert(list, ref);
            return ref;
        }
    }
    if (!m_root) {
        m_root = std::make_shared<XmlElement>(kW, QStringLiteral("numbering"));
    }
    const int ilvl = lf.hasProperty(kListLevel) ? qBound(0, lf.intProperty(kListLevel), 8)
                                                : qBound(0, lf.indent() - 1, 8);
    const Ref ref{freshNumId(0), ilvl};
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

void NumberingBuilder::patchStyles(XmlElement &styles) const
{
    for (XmlChild &c : styles.children) {
        XmlElement *pPr = c.element && c.element->is(kW, u"style") ? c.element->child(kW, u"pPr") : nullptr;
        XmlElement *numPr = pPr != nullptr ? pPr->child(kW, u"numPr") : nullptr;
        XmlElement *num = numPr != nullptr ? numPr->child(kW, u"numId") : nullptr;
        if (num == nullptr) {
            continue;
        }
        const int id = wAttr(num, u"val").toInt();
        if (const auto written = m_wordNums.constFind(id); written != m_wordNums.constEnd()) {
            num->setAttribute(kW, QStringLiteral("val"), QString::number(written.value()));
        } else if (id > 0 && !m_sourceNums.contains(id)) {
            pPr->removeChildren(kW, u"numPr");
        }
    }
}

} // namespace QindaOffice::Docx::detail
