// SPDX-License-Identifier: GPL-3.0-or-later
// Content controls in the .docx writer (docx_controls.h): a story's blocks
// and tables written inside the block-level w:sdt elements they carry, and
// the w:sdt wrapper itself (inline ones are grouped in writeRuns()).

#include "docx_controls.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"

#include <QTextFrame>

#include <utility>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

} // namespace

bool endsWithParagraph(const XmlElement &element)
{
    const QList<const XmlElement *> children = element.elements();
    if (children.isEmpty()) {
        return false;
    }
    const XmlElement *last = children.last();
    if (last->ns == kW && last->name == u"p") {
        return true;
    }
    if (last->ns == kW && last->name == u"sdt") {
        const XmlElement *content = last->child(kW, u"sdtContent");
        return content != nullptr && endsWithParagraph(*content);
    }
    return false;
}

QStringList BodyWriter::knownControls(const QVariant &keys) const
{
    QStringList known;
    if (m_env.parts == nullptr || !keys.isValid()) {
        return known;
    }
    for (const QString &key : keys.toStringList()) {
        if (m_env.parts->contentControls.contains(key)) {
            known.append(key);
        }
    }
    return known;
}

XmlElement &BodyWriter::openControl(XmlElement &parent, const QString &key)
{
    const ContentControl control = m_env.parts->contentControls.value(key);
    XmlElement &sdt = parent.appendElement(kW, QStringLiteral("sdt"));
    // AGENT-GUARD: a control written twice (its content copied, or split by
    // an edit) keeps one w:id; Word gives the copy a new one.
    const bool repeat = m_env.writtenControls.contains(key);
    m_env.writtenControls.insert(key);
    sdt.appendElement(controlProperties(control, m_controlTexts.value(key), repeat));
    if (!control.endPropertiesXml.isEmpty()) {
        if (XmlElementPtr end = Ooxml::parseFragment(control.endPropertiesXml)) {
            sdt.appendElement(end);
        }
    }
    return sdt.appendElement(kW, QStringLiteral("sdtContent"));
}

void BodyWriter::writeItems(const QList<QTextFrame::iterator> &items, XmlElement &container)
{
    // The block-level controls open at this point, outermost first, each
    // with the w:sdtContent the next block goes into.
    QList<std::pair<QString, XmlElement *>> open;
    auto target = [&]() -> XmlElement & { return open.isEmpty() ? container : *open.constLast().second; };
    auto enter = [&](const QStringList &keys) {
        qsizetype common = 0;
        while (common < open.size() && common < keys.size() && open.at(common).first == keys.at(common)) {
            ++common;
        }
        open.resize(common);
        for (qsizetype k = common; k < keys.size(); ++k) {
            open.append({keys.at(k), &openControl(target(), keys.at(k))});
        }
    };
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (QTextFrame *child = items.at(i).currentFrame()) {
            if (const auto *table = qobject_cast<const QTextTable *>(child)) {
                enter(knownControls(table->format().property(kBlockControls)));
                writeTable(table, target());
            } else {
                enter({});
                writeFrame(child, target());
            }
            continue;
        }
        const QTextBlock block = items.at(i).currentBlock();
        const bool last = i == items.size() - 1;
        if (skipsBlock(block, last)) {
            continue;
        }
        enter(knownControls(block.blockFormat().property(kBlockControls)));
        writeBlock(block, target(), last);
    }
}

} // namespace QindaOffice::Docx::detail
