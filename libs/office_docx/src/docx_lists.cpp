// SPDX-License-Identifier: GPL-3.0-or-later
// Word's list definitions (docx_lists.h): parsing with a cache, a level's
// indents, Word's number formats, a level's marker text and the list
// formats. The library and the dialogs' level specs: docx_list_library.cpp.

#include "docx_lists.h"

#include "docx_properties.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QHash>
#include <QMutex>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

QString romanText(int value)
{
    static constexpr std::pair<int, const char *> kRoman[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
        {50, "L"},   {40, "XL"},  {10, "X"},  {9, "IX"},   {5, "V"},   {4, "IV"}, {1, "I"}};
    QString out;
    int n = qBound(1, value, 3999);
    for (const auto &[v, s] : kRoman) {
        while (n >= v) {
            out += QLatin1String(s);
            n -= v;
        }
    }
    return out;
}

QString cardinalText(int n)
{
    static const char *kOnes[] = {"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten",
                                  "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen", "Sixteen", "Seventeen",
                                  "Eighteen", "Nineteen"};
    static const char *kTens[] = {"", "", "Twenty", "Thirty", "Forty", "Fifty", "Sixty", "Seventy", "Eighty", "Ninety"};
    if (n < 0 || n >= 1000) {
        return QString::number(n);
    }
    if (n < 20) {
        return QLatin1String(kOnes[n]);
    }
    if (n < 100) {
        return QLatin1String(kTens[n / 10]) + (n % 10 != 0 ? QLatin1Char('-') + QString::fromLatin1(kOnes[n % 10]) : QString());
    }
    return QLatin1String(kOnes[n / 100]) + QStringLiteral(" Hundred")
        + (n % 100 != 0 ? QLatin1Char(' ') + cardinalText(n % 100) : QString());
}

QString ordinalText(int n)
{
    static const QHash<QString, QString> kIrregular{
        {QStringLiteral("One"), QStringLiteral("First")},   {QStringLiteral("Two"), QStringLiteral("Second")},
        {QStringLiteral("Three"), QStringLiteral("Third")}, {QStringLiteral("Five"), QStringLiteral("Fifth")},
        {QStringLiteral("Eight"), QStringLiteral("Eighth")}, {QStringLiteral("Nine"), QStringLiteral("Ninth")},
        {QStringLiteral("Twelve"), QStringLiteral("Twelfth")}};
    const QString text = cardinalText(n);
    const qsizetype cut = qMax(text.lastIndexOf(QLatin1Char(' ')), text.lastIndexOf(QLatin1Char('-'))) + 1;
    const QString last = text.mid(cut);
    if (const auto it = kIrregular.constFind(last); it != kIrregular.constEnd()) {
        return text.left(cut) + it.value();
    }
    if (last.endsWith(QLatin1Char('y'))) {
        return text.left(text.size() - 1) + QStringLiteral("ieth");
    }
    return text + QStringLiteral("th");
}

} // namespace

std::shared_ptr<const ListDefinition> listDefinition(const QString &abstractXml)
{
    // AGENT-NOTE: the labels are recomputed after every edit and each list
    // format holds its definition as XML; parse each distinct one once.
    static QMutex mutex;
    static QHash<QString, std::shared_ptr<const ListDefinition>> cache;
    QMutexLocker lock(&mutex);
    if (const auto it = cache.constFind(abstractXml); it != cache.constEnd()) {
        return it.value();
    }
    auto definition = std::make_shared<ListDefinition>();
    if (const XmlElementPtr root = Ooxml::parseFragment(abstractXml)) {
        for (const XmlElement *lvl : root->elements(kW, u"lvl")) {
            const int ilvl = wAttr(lvl, u"ilvl").toInt();
            if (ilvl >= 0 && ilvl < 9) {
                definition->levels[size_t(ilvl)] = parseNumberingLevel(lvl);
                definition->defined[size_t(ilvl)] = true;
            }
        }
    }
    if (cache.size() > 512) {
        cache.clear();
    }
    cache.insert(abstractXml, definition);
    return definition;
}

int levelLeft(const NumberingLevel &level)
{
    const XmlElement *ind = level.pPr ? level.pPr->child(kW, u"ind") : nullptr;
    const QString left = wAttr(ind, u"left", wAttr(ind, u"start"));
    return left.isEmpty() ? 0 : qRound(Ooxml::Units::parseTwips(left));
}

int levelHanging(const NumberingLevel &level)
{
    const XmlElement *ind = level.pPr ? level.pPr->child(kW, u"ind") : nullptr;
    if (const QString hanging = wAttr(ind, u"hanging"); !hanging.isEmpty()) {
        return qRound(Ooxml::Units::parseTwips(hanging));
    }
    const QString first = wAttr(ind, u"firstLine");
    return first.isEmpty() ? 0 : -qRound(Ooxml::Units::parseTwips(first));
}

int levelTab(const NumberingLevel &level)
{
    const XmlElement *tabs = level.pPr ? level.pPr->child(kW, u"tabs") : nullptr;
    for (const XmlElement *tab : tabs != nullptr ? tabs->elements(kW, u"tab") : QList<const XmlElement *>()) {
        if (wAttr(tab, u"val") == u"num" || wAttr(tab, u"val") == u"left") {
            return qRound(Ooxml::Units::parseTwips(wAttr(tab, u"pos")));
        }
    }
    return -1;
}

QString wordNumberText(int value, const QString &numFmt)
{
    if (numFmt == u"bullet" || numFmt == u"none") {
        return {};
    }
    if (numFmt == u"upperRoman" || numFmt == u"lowerRoman") {
        const QString out = romanText(value);
        return numFmt == u"lowerRoman" ? out.toLower() : out;
    }
    if (numFmt == u"upperLetter" || numFmt == u"lowerLetter") {
        const int n = qMax(1, value);
        const QChar letter(char16_t(u'A' + (n - 1) % 26));
        const QString out(1 + (n - 1) / 26, letter);
        return numFmt == u"lowerLetter" ? out.toLower() : out;
    }
    if (numFmt == u"decimalZero") {
        return value >= 0 && value < 10 ? QStringLiteral("0") + QString::number(value) : QString::number(value);
    }
    if (numFmt == u"ordinal") {
        const int mod100 = value % 100;
        const int mod10 = value % 10;
        const char *suffix = (mod100 >= 11 && mod100 <= 13) ? "th" : mod10 == 1 ? "st" : mod10 == 2 ? "nd" : mod10 == 3 ? "rd" : "th";
        return QString::number(value) + QLatin1String(suffix);
    }
    if (numFmt == u"cardinalText") {
        return cardinalText(value);
    }
    if (numFmt == u"ordinalText") {
        return ordinalText(value);
    }
    return QString::number(value);
}

QTextListFormat::Style qtStyleFor(const NumberingLevel &level)
{
    if (level.isBullet()) {
        const QString t = level.text;
        if (t == u"o" || t == QString(QChar(0x25CB)) || t == QString(QChar(0xF06F))) {
            return QTextListFormat::ListCircle;
        }
        if (t == QString(QChar(0xF0A7)) || t == QString(QChar(0x25AA)) || t == QString(QChar(0x25A0))) {
            return QTextListFormat::ListSquare;
        }
        return QTextListFormat::ListDisc;
    }
    if (level.format == u"upperRoman") {
        return QTextListFormat::ListUpperRoman;
    }
    if (level.format == u"lowerRoman") {
        return QTextListFormat::ListLowerRoman;
    }
    if (level.format == u"upperLetter") {
        return QTextListFormat::ListUpperAlpha;
    }
    if (level.format == u"lowerLetter") {
        return QTextListFormat::ListLowerAlpha;
    }
    return QTextListFormat::ListDecimal;
}

QString levelLabel(const ListDefinition &definition, int ilvl, const std::array<int, 9> &counters)
{
    const NumberingLevel &level = definition.levels[size_t(qBound(0, ilvl, 8))];
    if (level.isBullet()) {
        return level.text;
    }
    QString out;
    const QString &text = level.text;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'%' && i + 1 < text.size() && text.at(i + 1) >= u'1' && text.at(i + 1) <= u'9') {
            const int k = text.at(i + 1).unicode() - u'1';
            const QString format = level.legal && k < ilvl ? QStringLiteral("decimal") : definition.levels[size_t(k)].format;
            out += wordNumberText(counters[size_t(k)], format == u"bullet" ? QStringLiteral("decimal") : format);
            ++i;
        } else {
            out += text.at(i);
        }
    }
    return out;
}

// ---- list formats --------------------------------------------------------------------

bool isWordList(const QTextListFormat &format)
{
    return format.hasProperty(kListDefinition) && format.intProperty(kListNumId) > 0;
}

QList<int> listStarts(const QTextListFormat &format)
{
    QList<int> out(9, -1);
    const QVariantList starts = format.property(kListStarts).toList();
    for (int k = 0; k < 9 && k < starts.size(); ++k) {
        out[k] = starts.at(k).toInt();
    }
    return out;
}

QTextListFormat wordListFormat(const QString &definition, const QString &abstractKey, int numId, int level,
                               const QList<int> &starts)
{
    const std::shared_ptr<const ListDefinition> parsed = listDefinition(definition);
    const NumberingLevel &l = parsed->levels[size_t(qBound(0, level, 8))];
    QTextListFormat lf;
    lf.setIndent(0);
    lf.setStyle(qtStyleFor(l));
    if (!l.isBullet()) {
        // HTML and Markdown see this level's own number between its text.
        const QString own = QStringLiteral("%%1").arg(level + 1);
        const qsizetype at = l.text.indexOf(own);
        if (at >= 0) {
            lf.setNumberPrefix(l.text.left(at));
            lf.setNumberSuffix(l.text.mid(at + own.size()));
        }
        const int start = starts.value(level, -1);
        lf.setStart(start >= 0 ? start : l.start);
    }
    lf.setProperty(kListDefinition, definition);
    lf.setProperty(kListAbstractKey, abstractKey);
    lf.setProperty(kListNumId, numId);
    lf.setProperty(kListLevel, level);
    QVariantList list;
    for (int k = 0; k < 9; ++k) {
        list.append(starts.value(k, -1));
    }
    lf.setProperty(kListStarts, list);
    return lf;
}

} // namespace QindaOffice::Docx
