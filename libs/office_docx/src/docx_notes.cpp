// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_notes.h"

#include "docx_styles.h"
#include "ooxml_namespaces.h"

#include <functional>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

QString roman(int value)
{
    static const struct {
        int value;
        const char *text;
    } kSteps[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
                  {40, "xl"},  {10, "x"},   {9, "ix"},  {5, "v"},    {4, "iv"},  {1, "i"}};
    QString out;
    int rest = qBound(1, value, 3999);
    for (const auto &step : kSteps) {
        while (rest >= step.value) {
            out += QLatin1String(step.text);
            rest -= step.value;
        }
    }
    return out;
}

// Word's letter numbering: a … z, then aa … zz, aaa … (the letter repeats).
QString letters(int value)
{
    const int n = qMax(1, value);
    const QChar letter(char16_t(u'a' + (n - 1) % 26));
    return QString((n - 1) / 26 + 1, letter);
}

} // namespace

QString noteFormat(const NoteOptions &options, bool endnote)
{
    if (!options.numberFormat.isEmpty()) {
        return options.numberFormat;
    }
    return endnote ? QStringLiteral("lowerRoman") : QStringLiteral("decimal");
}

QString noteNumber(int value, const QString &format)
{
    if (format == u"lowerRoman") {
        return roman(value);
    }
    if (format == u"upperRoman") {
        return roman(value).toUpper();
    }
    if (format == u"lowerLetter") {
        return letters(value);
    }
    if (format == u"upperLetter") {
        return letters(value).toUpper();
    }
    if (format == u"chicago") {
        static const char16_t kSymbols[] = {u'*', 0x2020, 0x2021, 0x00A7};
        const int n = qMax(1, value);
        return QString((n - 1) / 4 + 1, QChar(kSymbols[(n - 1) % 4]));
    }
    return QString::number(value);
}

NoteOptions readNoteOptions(const XmlElement *notePr, NoteOptions base)
{
    if (notePr == nullptr) {
        return base;
    }
    if (const XmlElement *e = notePr->child(kW, u"numFmt")) {
        base.numberFormat = wAttr(e, u"val");
    }
    if (const XmlElement *e = notePr->child(kW, u"numStart")) {
        bool ok = false;
        const int start = wAttr(e, u"val").toInt(&ok);
        base.start = ok && start > 0 ? start : 1;
    }
    if (const XmlElement *e = notePr->child(kW, u"numRestart")) {
        const QString restart = wAttr(e, u"val");
        base.restart = restart == u"continuous" ? QString() : restart;
    }
    if (const XmlElement *e = notePr->child(kW, u"pos")) {
        const QString position = wAttr(e, u"val");
        base.position = position == u"pageBottom" || position == u"docEnd" ? QString() : position;
    }
    return base;
}

void writeNoteOptions(XmlElement &notePr, const NoteOptions &options)
{
    for (const char16_t *name : {u"pos", u"numFmt", u"numStart", u"numRestart"}) {
        notePr.removeChildren(kW, QStringView(name));
    }
    QList<Ooxml::XmlChild> head;
    auto add = [&head](const QString &name, const QString &value) {
        auto e = std::make_shared<XmlElement>(kW, name);
        e->setAttribute(kW, QStringLiteral("val"), value);
        head.append({e, QString()});
    };
    if (!options.position.isEmpty()) {
        add(QStringLiteral("pos"), options.position);
    }
    if (!options.numberFormat.isEmpty()) {
        add(QStringLiteral("numFmt"), options.numberFormat);
    }
    if (options.start != 1) {
        add(QStringLiteral("numStart"), QString::number(options.start));
    }
    if (!options.restart.isEmpty()) {
        add(QStringLiteral("numRestart"), options.restart);
    }
    notePr.children = head + notePr.children;
}

QString noteText(const XmlElement &note)
{
    QStringList paragraphs;
    for (const XmlElement *p : note.elements(kW, u"p")) {
        QString line;
        std::function<void(const XmlElement &)> walk = [&](const XmlElement &e) {
            for (const XmlElement *c : e.elements()) {
                if (c->ns != kW) {
                    continue;
                }
                if (c->name == u"t") {
                    line += c->text();
                } else if (c->name == u"tab") {
                    line += QLatin1Char(' ');
                } else if (c->name != u"instrText" && c->name != u"rPr" && c->name != u"pPr") {
                    walk(*c);
                }
            }
        };
        walk(*p);
        paragraphs.append(line);
    }
    return paragraphs.join(QLatin1Char(' ')).trimmed();
}

} // namespace QindaOffice::Docx
