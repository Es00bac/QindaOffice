// SPDX-License-Identifier: GPL-3.0-or-later
// Word's list library (docx_lists.h): the Bullets, Numbering and Multilevel
// List definitions as Word writes them, and one level as QindaWrite's
// dialogs read and change it.

#include "docx_lists.h"

#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// One level of a library definition.
struct Seed {
    QString format = QStringLiteral("decimal");
    QString text;
    int left = 720;
    int hanging = 360;
    QString jc = QStringLiteral("left");
    QString font;
    QString color;
    int start = 1;
    bool legal = false;
};

Seed seed(const QString &format, const QString &text, int left, int hanging, const QString &jc = QStringLiteral("left"))
{
    Seed s;
    s.format = format;
    s.text = text;
    s.left = left;
    s.hanging = hanging;
    s.jc = jc;
    return s;
}

XmlElement &setVal(XmlElement &parent, const QString &name, const QString &value)
{
    XmlElement *child = parent.child(kW, name);
    if (child == nullptr) {
        child = &parent.appendElement(kW, name);
    }
    child->attributes.clear();
    child->setAttribute(kW, QStringLiteral("val"), value);
    return *child;
}

void setFonts(XmlElement &rPr, const QString &font)
{
    rPr.removeChildren(kW, u"rFonts");
    if (font.isEmpty()) {
        return;
    }
    XmlElement &fonts = rPr.appendElement(kW, QStringLiteral("rFonts"));
    fonts.setAttribute(kW, QStringLiteral("ascii"), font);
    fonts.setAttribute(kW, QStringLiteral("hAnsi"), font);
    if (font == u"Courier New") {
        fonts.setAttribute(kW, QStringLiteral("cs"), font);
    }
    fonts.setAttribute(kW, QStringLiteral("hint"), QStringLiteral("default"));
}

void setIndent(XmlElement &lvl, int left, int hanging)
{
    XmlElement *pPr = lvl.child(kW, u"pPr");
    if (pPr == nullptr) {
        pPr = &lvl.appendElement(kW, QStringLiteral("pPr"));
    }
    XmlElement *ind = pPr->child(kW, u"ind");
    if (ind == nullptr) {
        ind = &pPr->appendElement(kW, QStringLiteral("ind"));
    }
    ind->attributes.clear();
    ind->setAttribute(kW, QStringLiteral("left"), QString::number(left));
    if (hanging >= 0) {
        ind->setAttribute(kW, QStringLiteral("hanging"), QString::number(hanging));
    } else {
        ind->setAttribute(kW, QStringLiteral("firstLine"), QString::number(-hanging));
    }
}

// The schema order of w:lvl's children (CT_Lvl).
void sortLevel(XmlElement &lvl)
{
    static const QStringList order{QStringLiteral("start"),   QStringLiteral("numFmt"),  QStringLiteral("lvlRestart"),
                                   QStringLiteral("pStyle"),  QStringLiteral("isLgl"),   QStringLiteral("suff"),
                                   QStringLiteral("lvlText"), QStringLiteral("lvlPicBulletId"), QStringLiteral("legacy"),
                                   QStringLiteral("lvlJc"),   QStringLiteral("pPr"),     QStringLiteral("rPr")};
    std::stable_sort(lvl.children.begin(), lvl.children.end(), [](const Ooxml::XmlChild &a, const Ooxml::XmlChild &b) {
        const qsizetype x = a.element ? order.indexOf(a.element->name) : -1;
        const qsizetype y = b.element ? order.indexOf(b.element->name) : -1;
        return (x < 0 ? order.size() : x) < (y < 0 ? order.size() : y);
    });
}

XmlElementPtr levelElement(int ilvl, const Seed &s)
{
    auto lvl = std::make_shared<XmlElement>(kW, QStringLiteral("lvl"));
    lvl->setAttribute(kW, QStringLiteral("ilvl"), QString::number(ilvl));
    setVal(*lvl, QStringLiteral("start"), QString::number(s.start));
    setVal(*lvl, QStringLiteral("numFmt"), s.format);
    if (s.legal) {
        lvl->appendElement(kW, QStringLiteral("isLgl"));
    }
    setVal(*lvl, QStringLiteral("lvlText"), s.text);
    setVal(*lvl, QStringLiteral("lvlJc"), s.jc);
    setIndent(*lvl, s.left, s.hanging);
    if (!s.font.isEmpty() || !s.color.isEmpty()) {
        XmlElement &rPr = lvl->appendElement(kW, QStringLiteral("rPr"));
        setFonts(rPr, s.font);
        if (!s.color.isEmpty()) {
            setVal(rPr, QStringLiteral("color"), s.color);
        }
    }
    sortLevel(*lvl);
    return lvl;
}

QString definitionXml(const QString &multiLevelType, const std::array<Seed, 9> &levels)
{
    XmlElement abstract(kW, QStringLiteral("abstractNum"));
    abstract.setAttribute(kW, QStringLiteral("abstractNumId"), QStringLiteral("0"));
    setVal(abstract, QStringLiteral("multiLevelType"), multiLevelType);
    for (int k = 0; k < 9; ++k) {
        abstract.appendElement(levelElement(k, levels[size_t(k)]));
    }
    return Ooxml::serializeFragment(abstract);
}

QString symbolChar(char16_t code)
{
    return QString(QChar(code));
}

// Word's default bullets below level 1: o (Courier New), ▪ (Wingdings), •.
Seed defaultBullet(int k)
{
    Seed s;
    s.format = QStringLiteral("bullet");
    s.left = 720 * (k + 1);
    switch (k % 3) {
    case 1:
        s.text = QStringLiteral("o");
        s.font = QStringLiteral("Courier New");
        break;
    case 2:
        s.text = symbolChar(0xF0A7);
        s.font = QStringLiteral("Wingdings");
        break;
    default:
        s.text = symbolChar(0xF0B7);
        s.font = QStringLiteral("Symbol");
    }
    return s;
}

// Word's default numbers below level 1: a. i. 1. a. i. …
Seed defaultNumber(int k)
{
    Seed s;
    s.left = 720 * (k + 1);
    const QString own = QStringLiteral("%%1.").arg(k + 1);
    switch (k % 3) {
    case 1:
        s.format = QStringLiteral("lowerLetter");
        break;
    case 2:
        s.format = QStringLiteral("lowerRoman");
        s.jc = QStringLiteral("right");
        s.hanging = 180;
        break;
    default:
        s.format = QStringLiteral("decimal");
    }
    s.text = own;
    return s;
}

} // namespace

// ---- the library ---------------------------------------------------------------------

QString bulletDefinition(const QString &glyph, const QString &font, const QString &color)
{
    std::array<Seed, 9> levels;
    for (int k = 0; k < 9; ++k) {
        levels[size_t(k)] = defaultBullet(k);
    }
    levels[0].text = glyph;
    levels[0].font = font;
    levels[0].color = color;
    return definitionXml(QStringLiteral("hybridMultilevel"), levels);
}

QString numberingDefinition(const QString &numFmt, const QString &text, const QString &jc, int start)
{
    std::array<Seed, 9> levels;
    for (int k = 0; k < 9; ++k) {
        levels[size_t(k)] = defaultNumber(k);
    }
    levels[0].format = numFmt;
    levels[0].text = text;
    levels[0].jc = jc;
    levels[0].start = start;
    return definitionXml(QStringLiteral("hybridMultilevel"), levels);
}

QString multilevelDefinition(int preset)
{
    std::array<Seed, 9> levels;
    if (preset == 0) { // 1. 1.1. 1.1.1. (Word's outline numbering)
        static constexpr int kLeft[9] = {360, 792, 1224, 1728, 2232, 2736, 3240, 3744, 4320};
        static constexpr int kHanging[9] = {360, 432, 504, 648, 792, 936, 1080, 1224, 1440};
        QString text;
        for (int k = 0; k < 9; ++k) {
            text += QStringLiteral("%%1.").arg(k + 1);
            levels[size_t(k)] = seed(QStringLiteral("decimal"), text, kLeft[k], kHanging[k]);
        }
        return definitionXml(QStringLiteral("multilevel"), levels);
    }
    if (preset == 1) { // 1) a) i) (1) (a) (i) 1. a. i.
        static const char *kFormats[3] = {"decimal", "lowerLetter", "lowerRoman"};
        for (int k = 0; k < 9; ++k) {
            const QString own = QStringLiteral("%%1").arg(k + 1);
            const QString text = k < 3 ? own + QLatin1Char(')') : k < 6 ? QLatin1Char('(') + own + QLatin1Char(')') : own + QLatin1Char('.');
            levels[size_t(k)] = seed(QString::fromLatin1(kFormats[k % 3]), text, 360 * (k + 1), 360);
        }
        return definitionXml(QStringLiteral("multilevel"), levels);
    }
    if (preset == 2) { // ➢ ▪ • (Wingdings, Wingdings, Symbol)
        for (int k = 0; k < 9; ++k) {
            Seed s = seed(QStringLiteral("bullet"), QString(), 360 * (k + 1), 360);
            s.text = symbolChar(k % 3 == 0 ? 0xF0D8 : k % 3 == 1 ? 0xF0A7 : 0xF0B7);
            s.font = k % 3 == 2 ? QStringLiteral("Symbol") : QStringLiteral("Wingdings");
            levels[size_t(k)] = s;
        }
        return definitionXml(QStringLiteral("multilevel"), levels);
    }
    // Article I. / Section 1.01 / (a) / (i) / 1) / a) / i) / a. / i.
    levels[0] = seed(QStringLiteral("upperRoman"), QStringLiteral("Article %1."), 0, 0);
    levels[1] = seed(QStringLiteral("decimalZero"), QStringLiteral("Section %1.%2"), 0, 0);
    levels[1].legal = true;
    levels[2] = seed(QStringLiteral("lowerLetter"), QStringLiteral("(%3)"), 720, 432);
    levels[3] = seed(QStringLiteral("lowerRoman"), QStringLiteral("(%4)"), 864, 144, QStringLiteral("right"));
    levels[4] = seed(QStringLiteral("decimal"), QStringLiteral("%5)"), 1008, 432);
    levels[5] = seed(QStringLiteral("lowerLetter"), QStringLiteral("%6)"), 1152, 432);
    levels[6] = seed(QStringLiteral("lowerRoman"), QStringLiteral("%7)"), 1296, 288, QStringLiteral("right"));
    levels[7] = seed(QStringLiteral("lowerLetter"), QStringLiteral("%8."), 1440, 432);
    levels[8] = seed(QStringLiteral("lowerRoman"), QStringLiteral("%9."), 1584, 144, QStringLiteral("right"));
    return definitionXml(QStringLiteral("multilevel"), levels);
}

// ---- one level for the dialogs ------------------------------------------------------

QVariantMap levelSpec(const QString &abstractXml, int ilvl)
{
    const std::shared_ptr<const ListDefinition> definition = listDefinition(abstractXml);
    const NumberingLevel &l = definition->levels[size_t(qBound(0, ilvl, 8))];
    const XmlElement *fonts = l.rPr ? l.rPr->child(kW, u"rFonts") : nullptr;
    const QString color = childVal(l.rPr.get(), u"color");
    return {{QStringLiteral("format"), l.format},
            {QStringLiteral("text"), l.text},
            {QStringLiteral("start"), l.start},
            {QStringLiteral("justification"), l.justification.isEmpty() ? QStringLiteral("left") : l.justification},
            {QStringLiteral("suffix"), l.suffix},
            {QStringLiteral("restartAfter"), l.restartAfterLevel},
            {QStringLiteral("legal"), l.legal},
            {QStringLiteral("numberPosition"), levelLeft(l) - levelHanging(l)},
            {QStringLiteral("textIndent"), levelLeft(l)},
            {QStringLiteral("tab"), levelTab(l)},
            {QStringLiteral("font"), wAttr(fonts, u"ascii", wAttr(fonts, u"hAnsi"))},
            {QStringLiteral("color"), color == u"auto" ? QString() : color},
            {QStringLiteral("defined"), definition->defined[size_t(qBound(0, ilvl, 8))]}};
}

QString withLevel(const QString &abstractXml, int ilvl, const QVariantMap &spec)
{
    XmlElementPtr root = Ooxml::parseFragment(abstractXml);
    if (!root) {
        root = Ooxml::parseFragment(numberingDefinition(QStringLiteral("decimal"), QStringLiteral("%1.")));
    }
    // A changed definition is a new one: Word's identity markers go.
    for (const char16_t *name : {u"nsid", u"tmpl", u"styleLink", u"numStyleLink", u"name"}) {
        root->removeChildren(kW, QStringView(name));
    }
    XmlElement *lvl = nullptr;
    for (Ooxml::XmlChild &c : root->children) {
        if (c.element && c.element->is(kW, u"lvl") && wAttr(c.element.get(), u"ilvl").toInt() == ilvl) {
            lvl = c.element.get();
        }
    }
    if (lvl == nullptr) {
        lvl = &root->appendElement(levelElement(ilvl, defaultNumber(ilvl)));
    }
    const auto has = [&](const char *key) { return spec.contains(QString::fromLatin1(key)); };
    const auto value = [&](const char *key) { return spec.value(QString::fromLatin1(key)); };
    if (has("start")) {
        setVal(*lvl, QStringLiteral("start"), QString::number(qMax(0, value("start").toInt())));
    }
    if (has("format")) {
        setVal(*lvl, QStringLiteral("numFmt"), value("format").toString());
    }
    if (has("text")) {
        setVal(*lvl, QStringLiteral("lvlText"), value("text").toString());
    }
    if (has("justification")) {
        setVal(*lvl, QStringLiteral("lvlJc"), value("justification").toString());
    }
    if (has("suffix")) {
        lvl->removeChildren(kW, u"suff");
        if (value("suffix").toString() != u"tab") {
            setVal(*lvl, QStringLiteral("suff"), value("suffix").toString());
        }
    }
    if (has("restartAfter")) {
        lvl->removeChildren(kW, u"lvlRestart");
        const int after = value("restartAfter").toInt();
        if (after != -2) {
            setVal(*lvl, QStringLiteral("lvlRestart"), QString::number(after < 0 ? 0 : after + 1));
        }
    }
    if (has("legal")) {
        lvl->removeChildren(kW, u"isLgl");
        if (value("legal").toBool()) {
            lvl->appendElement(kW, QStringLiteral("isLgl"));
        }
    }
    if (has("numberPosition") || has("textIndent")) {
        const NumberingLevel now = parseNumberingLevel(lvl);
        const int text = has("textIndent") ? value("textIndent").toInt() : levelLeft(now);
        const int number = has("numberPosition") ? value("numberPosition").toInt() : levelLeft(now) - levelHanging(now);
        setIndent(*lvl, text, text - number);
    }
    if (has("tab")) {
        XmlElement *pPr = lvl->child(kW, u"pPr");
        if (pPr != nullptr) {
            pPr->removeChildren(kW, u"tabs");
        }
        if (const int tab = value("tab").toInt(); tab >= 0) {
            if (pPr == nullptr) {
                pPr = &lvl->appendElement(kW, QStringLiteral("pPr"));
            }
            XmlElement &stop = pPr->insertElementBefore(std::make_shared<XmlElement>(kW, QStringLiteral("tabs")),
                                                        {u"spacing", u"ind", u"contextualSpacing", u"jc"})
                                   .appendElement(kW, QStringLiteral("tab"));
            stop.setAttribute(kW, QStringLiteral("val"), QStringLiteral("num"));
            stop.setAttribute(kW, QStringLiteral("pos"), QString::number(tab));
        }
    }
    if (has("font") || has("color")) {
        XmlElement *rPr = lvl->child(kW, u"rPr");
        if (rPr == nullptr) {
            rPr = &lvl->appendElement(kW, QStringLiteral("rPr"));
        }
        if (has("font")) {
            setFonts(*rPr, value("font").toString());
        }
        if (has("color")) {
            rPr->removeChildren(kW, u"color");
            if (const QString color = value("color").toString().remove(QLatin1Char('#')).toUpper(); !color.isEmpty()) {
                setVal(*rPr, QStringLiteral("color"), color);
            }
        }
        if (rPr->children.isEmpty()) {
            lvl->removeChildren(kW, u"rPr");
        }
    }
    sortLevel(*lvl);
    return Ooxml::serializeFragment(*root);
}

} // namespace QindaOffice::Docx
