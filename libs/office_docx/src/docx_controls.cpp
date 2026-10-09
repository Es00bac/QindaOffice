// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_controls.h"

#include "docx_properties.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"

#include <QDate>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kW14 = QString::fromUtf16(Ooxml::Ns::W14);

QChar symbolOf(const XmlElement *state, QChar fallback, QString *font)
{
    if (state == nullptr) {
        return fallback;
    }
    *font = state->attribute(kW14, u"font");
    bool ok = false;
    const uint code = state->attribute(kW14, u"val").toUInt(&ok, 16);
    return ok && code > 0 && code <= 0xFFFF ? QChar(char16_t(code)) : fallback;
}

void readItems(const XmlElement &list, ContentControl &control)
{
    for (const XmlElement *item : list.elements(kW, u"listItem")) {
        const QString value = wAttr(item, u"value");
        const QString shown = wAttr(item, u"displayText", value);
        control.itemTexts.append(shown);
        control.itemValues.append(value.isEmpty() ? shown : value);
    }
}

// What a fragment contributes to a control's text.
QString contentText(const QString &text)
{
    QString out = text;
    out.remove(QChar(0x200B));
    out.remove(QChar(0xFFFC));
    return out;
}

// The block-level controls around `block`: its own and those of every
// table it sits in, outermost first.
QStringList blockKeys(const QTextBlock &block)
{
    QStringList keys = block.blockFormat().property(kBlockControls).toStringList();
    QTextFrame *frame = QTextCursor(block).currentFrame();
    while (frame != nullptr) {
        if (const auto *table = qobject_cast<QTextTable *>(frame)) {
            keys = table->format().property(kBlockControls).toStringList() + keys;
        }
        frame = frame->parentFrame();
    }
    return keys;
}

} // namespace

ContentControl parseControl(const XmlElement &sdt, bool block)
{
    ContentControl control;
    control.block = block;
    if (const XmlElement *end = sdt.child(kW, u"sdtEndPr")) {
        control.endPropertiesXml = Ooxml::serializeFragment(*end);
    }
    const XmlElement *pr = sdt.child(kW, u"sdtPr");
    if (pr == nullptr) {
        return control;
    }
    control.propertiesXml = Ooxml::serializeFragment(*pr);
    control.showingPlaceholder = onOff(pr, u"showingPlcHdr");
    control.title = childVal(pr, u"alias");
    control.tag = childVal(pr, u"tag");
    using Kind = ContentControl::Kind;
    if (const XmlElement *box = pr->child(kW14, u"checkbox")) {
        control.kind = Kind::CheckBox;
        control.checkedSymbol = symbolOf(box->child(kW14, u"checkedState"), QChar(0x2612), &control.checkedFont);
        control.uncheckedSymbol =
            symbolOf(box->child(kW14, u"uncheckedState"), QChar(0x2610), &control.uncheckedFont);
    } else if (const XmlElement *list = pr->child(kW, u"dropDownList")) {
        control.kind = Kind::DropDown;
        readItems(*list, control);
    } else if (const XmlElement *combo = pr->child(kW, u"comboBox")) {
        control.kind = Kind::ComboBox;
        readItems(*combo, control);
    } else if (const XmlElement *date = pr->child(kW, u"date")) {
        control.kind = Kind::Date;
        control.dateFormat = childVal(date, u"dateFormat", QStringLiteral("M/d/yyyy"));
    } else if (pr->child(kW, u"text") != nullptr) {
        control.kind = Kind::PlainText;
    } else if (pr->child(kW, u"picture") != nullptr) {
        control.kind = Kind::Picture;
    } else if (pr->child(kW, u"docPartObj") != nullptr || pr->child(kW, u"docPartList") != nullptr) {
        control.kind = Kind::Gallery;
    } else if (pr->child(kW, u"group") != nullptr) {
        control.kind = Kind::Group;
    } else if (pr->child(kW, u"citation") != nullptr || pr->child(kW, u"bibliography") != nullptr
               || pr->child(kW, u"equation") != nullptr) {
        control.kind = Kind::Other;
    }
    return control;
}

QHash<QString, QString> controlTexts(const QTextDocument &document)
{
    QHash<QString, QString> texts;
    QHash<QString, bool> started; // block-level: a '\n' goes between paragraphs
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QStringList keys = fragment.charFormat().property(kContentControls).toStringList();
            if (!keys.isEmpty()) {
                const QString text = contentText(fragment.text());
                for (const QString &key : keys) {
                    texts[key] += text;
                }
            }
        }
        for (const QString &key : blockKeys(block)) {
            QString &text = texts[key];
            if (started.value(key)) {
                text += QLatin1Char('\n');
            }
            started.insert(key, true);
            text += contentText(block.text());
        }
    }
    return texts;
}

XmlElementPtr controlProperties(const ContentControl &control, const QString &text, bool repeat)
{
    XmlElementPtr pr = Ooxml::parseFragment(control.propertiesXml);
    if (!pr) {
        pr = std::make_shared<XmlElement>(kW, QStringLiteral("sdtPr"));
    }
    if (repeat) {
        pr->removeChildren(kW, u"id");
    }
    if (text == control.sourceText) {
        return pr;
    }
    // AGENT-GUARD: an edited control is no longer its placeholder, and a
    // bound one would be refilled from its XML by Word on open.
    pr->removeChildren(kW, u"showingPlcHdr");
    pr->removeChildren(kW, u"dataBinding");
    using Kind = ContentControl::Kind;
    switch (control.kind) {
    case Kind::CheckBox:
        if (XmlElement *box = pr->child(kW14, u"checkbox")) {
            XmlElement *checked = box->child(kW14, u"checked");
            if (checked == nullptr) {
                checked = &box->insertElementBefore(std::make_shared<XmlElement>(kW14, QStringLiteral("checked")),
                                                    {u"checkedState", u"uncheckedState"});
            }
            checked->setAttribute(kW14, QStringLiteral("val"),
                                  text.contains(control.checkedSymbol) ? QStringLiteral("1") : QStringLiteral("0"));
        }
        break;
    case Kind::DropDown:
    case Kind::ComboBox:
        if (XmlElement *list = pr->child(kW, control.kind == Kind::DropDown ? u"dropDownList" : u"comboBox")) {
            const qsizetype chosen = control.itemTexts.indexOf(text);
            if (chosen >= 0) {
                list->setAttribute(kW, QStringLiteral("lastValue"), control.itemValues.at(chosen));
            } else {
                list->removeAttribute(kW, u"lastValue");
            }
        }
        break;
    case Kind::Date:
        if (XmlElement *date = pr->child(kW, u"date")) {
            const QDate day = QDate::fromString(text.trimmed(), qtDateFormat(control.dateFormat));
            if (day.isValid()) {
                date->setAttribute(kW, QStringLiteral("fullDate"),
                                   day.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral("T00:00:00Z"));
            } else {
                date->removeAttribute(kW, u"fullDate");
            }
        }
        break;
    default:
        break;
    }
    return pr;
}

ControlSpan controlAt(const QTextDocument &document, int position)
{
    const QTextBlock block = document.findBlock(position);
    if (!block.isValid()) {
        return {};
    }
    // Inline: the fragment holding `position`, or the one ending there.
    QList<QTextFragment> fragments;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        fragments.append(it.fragment());
    }
    auto keysOf = [](const QTextFragment &f) { return f.charFormat().property(kContentControls).toStringList(); };
    qsizetype hit = -1;
    for (qsizetype i = 0; i < fragments.size(); ++i) {
        const QTextFragment &f = fragments.at(i);
        if (position >= f.position() && position < f.position() + f.length() && !keysOf(f).isEmpty()) {
            hit = i;
            break;
        }
        if (position == f.position() + f.length() && !keysOf(f).isEmpty()) {
            hit = i; // just after a control: a click on the right half of a symbol
        }
    }
    if (hit >= 0) {
        const QString key = keysOf(fragments.at(hit)).constLast();
        qsizetype first = hit;
        qsizetype last = hit;
        while (first > 0 && keysOf(fragments.at(first - 1)).contains(key)) {
            --first;
        }
        while (last + 1 < fragments.size() && keysOf(fragments.at(last + 1)).contains(key)) {
            ++last;
        }
        return {key, fragments.at(first).position(), fragments.at(last).position() + fragments.at(last).length()};
    }
    const QStringList keys = block.blockFormat().property(kBlockControls).toStringList();
    if (keys.isEmpty()) {
        return {};
    }
    const QString key = keys.constLast();
    QTextBlock first = block;
    QTextBlock last = block;
    while (first.previous().isValid()
           && first.previous().blockFormat().property(kBlockControls).toStringList().contains(key)) {
        first = first.previous();
    }
    while (last.next().isValid() && last.next().blockFormat().property(kBlockControls).toStringList().contains(key)) {
        last = last.next();
    }
    return {key, first.position(), last.position() + last.length() - 1};
}

QString qtDateFormat(const QString &wordFormat)
{
    QString format = wordFormat;
    format.replace(QStringLiteral("AM/PM"), QStringLiteral("AP"));
    format.replace(QStringLiteral("am/pm"), QStringLiteral("ap"));
    return format.isEmpty() ? QStringLiteral("M/d/yyyy") : format;
}

} // namespace QindaOffice::Docx
