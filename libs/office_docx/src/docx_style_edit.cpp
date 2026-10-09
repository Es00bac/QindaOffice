// SPDX-License-Identifier: GPL-3.0-or-later
// The styles part as the user edits it (docx_style_edit.h): the Styles
// pane's list and the gallery, Word's built-ins added on first use, a
// style added, replaced or deleted, and a selection's formatting as the
// markup a style takes.

#include "docx_style_edit.h"

#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <algorithm>

namespace QindaOffice::Docx {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// Word's gallery order for the built-ins whose definitions in Write say
// none (docx_writer_styles.cpp mirrors QindaWrite's StyleCatalog).
int fallbackPriority(const QString &id)
{
    static const QHash<QString, int> kPriority{
        {QStringLiteral("Normal"), 0},   {QStringLiteral("Heading1"), 9},  {QStringLiteral("Heading2"), 9},
        {QStringLiteral("Heading3"), 9}, {QStringLiteral("Heading4"), 9},  {QStringLiteral("Heading5"), 9},
        {QStringLiteral("Heading6"), 9}, {QStringLiteral("Title"), 10},    {QStringLiteral("Quote"), 29},
        {QStringLiteral("TableGrid"), 39}};
    return kPriority.value(id, 99);
}

// The styles the body uses: paragraphs, runs, tables.
QSet<QString> usedStyles(const QTextDocument &body)
{
    QSet<QString> out;
    QSet<const QTextTable *> tables;
    for (QTextBlock b = body.begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        if (const QString id = f.stringProperty(kParagraphStyleId); !id.isEmpty()) {
            out.insert(id);
        } else if (f.hasProperty(kWriteStyleIdProperty)) {
            out.insert(detail::builtInStyleId(f.intProperty(kWriteStyleIdProperty)));
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            if (const QString id = it.fragment().charFormat().stringProperty(kRunStyleId); !id.isEmpty()) {
                out.insert(id);
            }
        }
        if (const QTextTable *table = QTextCursor(b).currentTable(); table != nullptr && !tables.contains(table)) {
            tables.insert(table); // each table's markup read once
            if (const XmlElementPtr tblPr = Ooxml::parseFragment(table->format().stringProperty(kTableXml))) {
                out.insert(childVal(tblPr.get(), u"tblStyle"));
            }
        }
    }
    return out;
}

StyleEntry entryOf(const XmlElement &e, const QSet<QString> &used)
{
    StyleEntry s;
    s.id = wAttr(&e, u"styleId");
    s.name = displayStyleName(childVal(&e, u"name", s.id));
    s.type = wAttr(&e, u"type", QStringLiteral("paragraph"));
    s.basedOn = childVal(&e, u"basedOn");
    s.next = childVal(&e, u"next");
    s.link = childVal(&e, u"link");
    bool ok = false;
    const int priority = childVal(&e, u"uiPriority").toInt(&ok);
    s.priority = ok ? priority : fallbackPriority(s.id);
    s.quick = e.child(kW, u"qFormat") != nullptr;
    s.hidden = onOff(&e, u"semiHidden") || onOff(&e, u"hidden");
    s.inUse = used.contains(s.id);
    return s;
}

void sortEntries(QList<StyleEntry> &entries)
{
    std::stable_sort(entries.begin(), entries.end(), [](const StyleEntry &a, const StyleEntry &b) {
        return a.priority != b.priority ? a.priority < b.priority : a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

void setVal(XmlElement &parent, const QString &name, const QString &value)
{
    parent.removeChildren(kW, name);
    parent.appendElement(kW, name).setAttribute(kW, QStringLiteral("val"), value);
}

} // namespace

QString displayStyleName(const QString &wordName)
{
    if (wordName.isEmpty() || !wordName.at(0).isLower()) {
        return wordName;
    }
    static const QRegularExpression kToc(QStringLiteral("^toc (\\d)$"));
    if (const auto match = kToc.match(wordName); match.hasMatch()) {
        return QStringLiteral("TOC ") + match.captured(1);
    }
    QStringList words = wordName.split(QLatin1Char(' '));
    for (QString &word : words) {
        if (!word.isEmpty()) {
            word[0] = word.at(0).toUpper();
        }
    }
    return words.join(QLatin1Char(' '));
}

QString paragraphStyleIdOf(const QTextBlockFormat &format)
{
    if (const QString id = format.stringProperty(kParagraphStyleId); !id.isEmpty()) {
        return id;
    }
    const QString builtIn = format.hasProperty(kWriteStyleIdProperty) ? detail::builtInStyleId(format.intProperty(kWriteStyleIdProperty))
                                                                       : QString();
    return builtIn.isEmpty() ? QStringLiteral("Normal") : builtIn;
}

XmlElementPtr stylesRoot(const WordParts &parts, const QFont &defaultFont)
{
    if (parts.styles) {
        return parts.styles;
    }
    if (parts.source) {
        const QString main = parts.source->mainPart();
        if (const auto rel = parts.source->firstOfType(main, QString::fromUtf16(Ooxml::RelType::Styles))) {
            if (XmlElementPtr root = parts.source->xml(parts.source->targetPart(main, *rel))) {
                return root;
            }
        }
    }
    detail::WriteEnv env;
    env.parts = &parts;
    return detail::stylesPart(env, defaultFont, nullptr);
}

QList<StyleEntry> styleEntries(const XmlElement &root, const QTextDocument &body)
{
    const QSet<QString> used = usedStyles(body);
    QList<StyleEntry> out;
    QSet<QString> characterHalves;
    QSet<QString> names;
    for (const XmlElement *e : root.elements(kW, u"style")) {
        StyleEntry s = entryOf(*e, used);
        if (s.id.isEmpty() || s.type == u"numbering") {
            continue;
        }
        names.insert(childVal(e, u"name").toLower());
        out.append(s);
    }
    for (StyleEntry &s : out) {
        if (s.type == u"paragraph" && !s.link.isEmpty()) {
            const auto partner = std::find_if(out.cbegin(), out.cend(), [&](const StyleEntry &o) { return o.id == s.link; });
            if (partner != out.cend() && partner->type == u"character") {
                s.type = QStringLiteral("linked");
                characterHalves.insert(s.link);
            }
        }
    }
    out.removeIf([&](const StyleEntry &s) { return characterHalves.contains(s.id) || (s.hidden && !s.inUse); });
    for (const QString &id : detail::builtInStyleIds()) {
        if (const XmlElementPtr builtIn = detail::builtInStyle(id)) {
            const QString name = childVal(builtIn.get(), u"name").toLower();
            if (!names.contains(name) && std::none_of(out.cbegin(), out.cend(), [&](const StyleEntry &s) { return s.id == id; })) {
                StyleEntry s = entryOf(*builtIn, used);
                s.inDocument = false;
                out.append(s);
            }
        }
    }
    sortEntries(out);
    return out;
}

QList<StyleEntry> quickStyles(const XmlElement &root, const QTextDocument &body)
{
    QList<StyleEntry> out = styleEntries(root, body);
    out.removeIf([](const StyleEntry &s) { return !s.quick || s.hidden || s.type == u"table"; });
    // The document's own first, then Word's built-ins it lacks.
    std::stable_partition(out.begin(), out.end(), [](const StyleEntry &s) { return s.inDocument; });
    return out;
}

XmlElementPtr builtInStyleFor(const XmlElement &root, const QString &styleId)
{
    XmlElementPtr style = detail::builtInStyle(styleId);
    if (!style) {
        return nullptr;
    }
    const StyleSheet sheet = StyleSheet::parse(&root);
    if (XmlElement *basedOn = style->child(kW, u"basedOn"); basedOn != nullptr && sheet.style(childVal(style.get(), u"basedOn")) == nullptr) {
        const QString base = childVal(style.get(), u"basedOn") == u"Normal" ? sheet.defaultParagraphStyle()
                                                                            : QString();
        if (base.isEmpty()) {
            style->removeChildren(kW, u"basedOn");
        } else {
            basedOn->setAttribute(kW, QStringLiteral("val"), base);
        }
    }
    return style;
}

XmlElementPtr withStyle(const XmlElement &root, const XmlElementPtr &style)
{
    XmlElementPtr out = root.clone();
    const QString id = wAttr(style.get(), u"styleId");
    for (XmlChild &c : out->children) {
        if (c.element && c.element->is(kW, u"style") && wAttr(c.element.get(), u"styleId") == id) {
            c.element = style->clone();
            return out;
        }
    }
    out->appendElement(style->clone());
    return out;
}

XmlElementPtr withoutStyle(const XmlElement &root, const QString &id)
{
    XmlElementPtr out = root.clone();
    QString parent;
    for (const XmlElement *e : out->elements(kW, u"style")) {
        if (wAttr(e, u"styleId") == id) {
            parent = childVal(e, u"basedOn");
        }
    }
    out->children.removeIf([&](const XmlChild &c) {
        return c.element && c.element->is(kW, u"style") && wAttr(c.element.get(), u"styleId") == id;
    });
    for (XmlChild &c : out->children) {
        if (!c.element || !c.element->is(kW, u"style")) {
            continue;
        }
        XmlElement &style = *c.element;
        if (childVal(&style, u"basedOn") == id) {
            if (parent.isEmpty()) {
                style.removeChildren(kW, u"basedOn");
            } else {
                style.child(kW, u"basedOn")->setAttribute(kW, QStringLiteral("val"), parent);
            }
        }
        for (const char16_t *name : {u"next", u"link"}) {
            if (childVal(&style, QStringView(name)) == id) {
                style.removeChildren(kW, QStringView(name));
            }
        }
    }
    return out;
}

QString newStyleId(const XmlElement &root, const QString &name)
{
    QString base;
    for (const QChar c : name) {
        if (c.isLetterOrNumber()) {
            base.append(c);
        }
    }
    if (base.isEmpty() || base.at(0).isDigit()) {
        base.prepend(QStringLiteral("Style"));
    }
    QSet<QString> taken;
    for (const XmlElement *e : root.elements(kW, u"style")) {
        taken.insert(wAttr(e, u"styleId").toLower());
    }
    QString id = base;
    for (int n = 2; taken.contains(id.toLower()); ++n) {
        id = base + QString::number(n);
    }
    return id;
}

XmlElementPtr makeStyle(const QString &type, const QString &id, const QString &name, const QString &basedOn,
                        const QString &next, bool quick, const XmlElementPtr &pPr, const XmlElementPtr &rPr)
{
    auto style = std::make_shared<XmlElement>(kW, QStringLiteral("style"));
    style->setAttribute(kW, QStringLiteral("type"), type);
    style->setAttribute(kW, QStringLiteral("customStyle"), QStringLiteral("1"));
    style->setAttribute(kW, QStringLiteral("styleId"), id);
    setVal(*style, QStringLiteral("name"), name);
    if (!basedOn.isEmpty()) {
        setVal(*style, QStringLiteral("basedOn"), basedOn);
    }
    if (!next.isEmpty() && type == u"paragraph") {
        setVal(*style, QStringLiteral("next"), next);
    }
    if (quick) {
        style->appendElement(kW, QStringLiteral("qFormat"));
    }
    if (pPr && type == u"paragraph") {
        XmlElementPtr own = pPr->clone();
        own->name = QStringLiteral("pPr");
        style->appendElement(own);
    }
    if (rPr) {
        XmlElementPtr own = rPr->clone();
        own->name = QStringLiteral("rPr");
        style->appendElement(own);
    }
    return style;
}

XmlElementPtr styleElement(const XmlElement &root, const QString &id)
{
    for (const XmlChild &c : root.children) {
        if (c.element && c.element->is(kW, u"style") && wAttr(c.element.get(), u"styleId") == id) {
            return c.element;
        }
    }
    return nullptr;
}

XmlElementPtr editedStyle(const XmlElement &style, const QString &name, const QString &basedOn, const QString &next,
                          bool quick, const XmlElementPtr &pPr, const XmlElementPtr &rPr)
{
    XmlElementPtr out = style.clone();
    setVal(*out, QStringLiteral("name"), name);
    out->removeChildren(kW, u"basedOn");
    if (!basedOn.isEmpty()) {
        setVal(*out, QStringLiteral("basedOn"), basedOn);
    }
    out->removeChildren(kW, u"next");
    if (!next.isEmpty() && wAttr(out.get(), u"type", QStringLiteral("paragraph")) == u"paragraph") {
        setVal(*out, QStringLiteral("next"), next);
    }
    out->removeChildren(kW, u"qFormat");
    if (quick) {
        out->appendElement(kW, QStringLiteral("qFormat"));
    }
    for (const auto &[own, name] : {std::pair{pPr, QStringLiteral("pPr")}, std::pair{rPr, QStringLiteral("rPr")}}) {
        out->removeChildren(kW, name);
        if (own && !own->children.isEmpty()) {
            XmlElementPtr copy = own->clone();
            copy->name = name;
            out->appendElement(copy);
        }
    }
    // CT_Style's sequence.
    static const QStringList kOrder{
        QStringLiteral("name"),       QStringLiteral("aliases"),         QStringLiteral("basedOn"),
        QStringLiteral("next"),       QStringLiteral("link"),            QStringLiteral("autoRedefine"),
        QStringLiteral("hidden"),     QStringLiteral("uiPriority"),      QStringLiteral("semiHidden"),
        QStringLiteral("unhideWhenUsed"), QStringLiteral("qFormat"),     QStringLiteral("locked"),
        QStringLiteral("personal"),   QStringLiteral("personalCompose"), QStringLiteral("personalReply"),
        QStringLiteral("rsid"),       QStringLiteral("pPr"),             QStringLiteral("rPr"),
        QStringLiteral("tblPr"),      QStringLiteral("trPr"),            QStringLiteral("tcPr"),
        QStringLiteral("tblStylePr")};
    std::stable_sort(out->children.begin(), out->children.end(), [](const XmlChild &a, const XmlChild &b) {
        const qsizetype x = a.element ? kOrder.indexOf(a.element->name) : kOrder.size();
        const qsizetype y = b.element ? kOrder.indexOf(b.element->name) : kOrder.size();
        return (x < 0 ? kOrder.size() : x) < (y < 0 ? kOrder.size() : y);
    });
    return out;
}

QSet<QString> derivedStyles(const StyleSheet &styles, const QString &id)
{
    QSet<QString> out{id};
    for (const QString &other : styles.styleIds()) {
        if (styles.chainOf(other).contains(id)) {
            out.insert(other);
        }
    }
    return out;
}

// ---- a selection's formatting as markup -------------------------------------------

namespace {

void prepare(detail::WriteEnv &env, const WordParts &parts, const QTextDocument &document)
{
    env.parts = &parts;
    env.sourceStyles = StyleSheet::parse(stylesRoot(parts, document.defaultFont()).get());
    env.numbering = std::make_unique<detail::NumberingBuilder>(nullptr);
}

XmlElementPtr strip(XmlElementPtr properties, std::initializer_list<const char16_t *> names)
{
    if (!properties) {
        return nullptr;
    }
    for (const char16_t *name : names) {
        properties->removeChildren(kW, QStringView(name));
    }
    return properties->children.isEmpty() ? nullptr : properties;
}

} // namespace

XmlElementPtr paragraphFormatting(const QTextBlock &block, const WordParts &parts, bool keepNumbering)
{
    detail::WriteEnv env;
    prepare(env, parts, *block.document());
    detail::BodyWriter writer(env, block.document(), QStringLiteral("word/document.xml"));
    XmlElementPtr pPr = strip(writer.blockMarkup(block), {u"pStyle", u"sectPr", u"rPr", u"pPrChange", u"cnfStyle", u"divId",
                                                          u"framePr"});
    return keepNumbering ? pPr : strip(pPr, {u"numPr"});
}

XmlElementPtr runFormatting(const QTextCharFormat &format, const QTextDocument &document, const WordParts &parts)
{
    detail::WriteEnv env;
    prepare(env, parts, document);
    detail::BodyWriter writer(env, &document, QStringLiteral("word/document.xml"));
    XmlElementPtr rPr = strip(writer.runMarkup(format), {u"rStyle", u"ins", u"del", u"rPrChange", u"moveFrom", u"moveTo"});
    if (rPr) {
        rPr->name = QStringLiteral("rPr");
    }
    return rPr;
}

} // namespace QindaOffice::Docx
