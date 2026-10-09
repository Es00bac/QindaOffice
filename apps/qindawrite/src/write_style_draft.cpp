// SPDX-License-Identifier: GPL-3.0-or-later
// StyleDraft (write_style_draft.h): the New / Modify Style dialog's style
// as a one-paragraph sample the M70 dialogs format.

#include "write_style_draft.h"

#include "docx_format.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "docx_style_edit.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"
#include "write_borders.h"
#include "write_document.h"
#include "write_font.h"
#include "write_paragraph.h"

#include <QTextBlock>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

QString markupOrEmpty(const Ooxml::XmlElementPtr &element, const QString &name)
{
    if (element) {
        return Ooxml::serializeFragment(*element);
    }
    return Ooxml::serializeFragment(Ooxml::XmlElement(kW, name));
}

QString alignmentName(Qt::Alignment a)
{
    a &= Qt::AlignHorizontal_Mask;
    return a == Qt::AlignHCenter ? QStringLiteral("center")
        : a == Qt::AlignRight    ? QStringLiteral("right")
        : a == Qt::AlignJustify  ? QStringLiteral("justify")
                                 : QStringLiteral("left");
}

} // namespace

StyleDraft::StyleDraft(WriteDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    m_sample.setUndoRedoEnabled(false);
}

QTextCursor StyleDraft::sample() const
{
    QTextCursor cursor(const_cast<QTextDocument *>(&m_sample));
    cursor.select(QTextCursor::Document);
    return cursor;
}

void StyleDraft::setSample(const QTextBlockFormat &block, const QTextCharFormat &run)
{
    m_sample.clear();
    m_sample.setDefaultFont(m_document->textDocument()->defaultFont());
    QTextCursor cursor(&m_sample);
    cursor.setBlockFormat(block);
    cursor.setBlockCharFormat(run);
    cursor.insertText(tr("Sample Text"), run);
}

void StyleDraft::listStyles(const Docx::StyleSheet &sheet)
{
    m_paragraphStyles.clear();
    m_characterStyles.clear();
    for (const Docx::StyleEntry &entry : Docx::styleEntries(*m_document->stylesRoot(), *m_document->textDocument())) {
        if (!entry.inDocument || entry.id == m_id || sheet.chainOf(entry.id).contains(m_id)) {
            continue; // a style cannot be based on itself or on one based on it
        }
        const QVariantMap row{{QStringLiteral("id"), entry.id}, {QStringLiteral("name"), entry.name}};
        if (entry.type == u"character") {
            m_characterStyles.append(row);
        } else if (entry.type == u"paragraph" || entry.type == u"linked") {
            m_paragraphStyles.append(row);
        }
    }
}

bool StyleDraft::loadStyle(const QString &id)
{
    const Ooxml::XmlElementPtr root = m_document->stylesRoot();
    const Docx::StyleSheet sheet = Docx::StyleSheet::parse(root.get());
    const Docx::Style *style = sheet.style(id);
    const Ooxml::XmlElementPtr element = Docx::styleElement(*root, id);
    if (style == nullptr || element == nullptr || (style->type != u"paragraph" && style->type != u"character")) {
        return false;
    }
    const Ooxml::Theme theme = Docx::DocumentStyles(m_document->parts()).theme();
    m_mode = QStringLiteral("modify");
    m_id = id;
    m_sourceName = style->name;
    m_name = Docx::displayStyleName(style->name);
    const QString link = Docx::childVal(element.get(), u"link");
    const bool linked = style->type == u"paragraph" && sheet.style(link) != nullptr && sheet.style(link)->type == u"character";
    m_type = linked ? QStringLiteral("linked") : style->type;
    m_basedOn = style->basedOn;
    m_next = style->next;
    m_quick = element->child(kW, u"qFormat") != nullptr;
    m_numbering.clear();
    listStyles(sheet);
    const bool character = style->type == u"character";
    const QString paragraphId = character ? sheet.defaultParagraphStyle() : id;
    QTextBlockFormat block = Docx::blockFormatFromPPr(sheet.paragraphProperties(paragraphId).get(), theme);
    block.setProperty(Docx::kParagraphXml, markupOrEmpty(character ? nullptr : style->pPr, QStringLiteral("pPr")));
    block.setProperty(Docx::kParagraphSnapshot, Docx::blockSnapshot(block));
    const Ooxml::XmlElementPtr runs = character ? Docx::mergeProperties(sheet.paragraphRunProperties(paragraphId).get(),
                                                                        sheet.characterRunProperties(id).get())
                                                : sheet.paragraphRunProperties(id);
    QTextCharFormat run = Docx::charFormatFromRPr(runs.get(), theme);
    run.setProperty(Docx::kRunXml, markupOrEmpty(style->rPr, QStringLiteral("rPr")));
    run.setProperty(Docx::kRunSnapshot, Docx::charSnapshot(run));
    setSample(block, run);
    emit changed();
    return true;
}

void StyleDraft::loadSelection(const QTextCursor &selection)
{
    const Ooxml::XmlElementPtr root = m_document->stylesRoot();
    const Docx::StyleSheet sheet = Docx::StyleSheet::parse(root.get());
    m_mode = QStringLiteral("new");
    m_id.clear();
    m_sourceName.clear();
    QSet<QString> names;
    for (const QString &other : sheet.styleIds()) {
        names.insert(Docx::displayStyleName(sheet.style(other)->name).toLower());
    }
    int n = 1;
    while (names.contains(QStringLiteral("style%1").arg(n))) {
        ++n;
    }
    m_name = QStringLiteral("Style%1").arg(n);
    m_type = QStringLiteral("paragraph");
    m_basedOn = Docx::DocumentStyles(m_document->parts()).paragraphStyleOf(selection.block());
    if (sheet.style(m_basedOn) == nullptr) {
        m_basedOn = sheet.defaultParagraphStyle();
    }
    m_next.clear(); // itself
    m_quick = true;
    m_numbering.clear();
    listStyles(sheet);
    // The selection's look: its paragraph's and first run's formatting, as
    // the writer would write them (their own markup and what changed).
    QTextBlockFormat block = selection.block().blockFormat();
    for (const int key : {int(QTextFormat::ObjectIndex), int(QTextFormat::ListStyle), Docx::kListHanging,
                          Docx::kSectionXml, Docx::kPageBreakRun, Docx::kBlockControls, Docx::kSyntheticBreak}) {
        block.clearProperty(key);
    }
    QTextCursor first(selection.document());
    first.setPosition(selection.hasSelection() ? selection.selectionStart() + 1 : selection.position());
    QTextCharFormat run = first.charFormat();
    for (const int key : {int(QTextFormat::IsAnchor), int(QTextFormat::AnchorHref), int(QTextFormat::AnchorName),
                          Docx::kRunStyleId, Docx::kCommentIds, Docx::kContentControls, Docx::kFootnoteIdProperty,
                          Docx::kRevisionInsertProperty, Docx::kRevisionDeleteProperty, Docx::kFieldInstruction}) {
        run.clearProperty(key);
    }
    setSample(block, run);
    emit changed();
}

void StyleDraft::setName(const QString &name)
{
    if (name != m_name) {
        m_name = name;
        emit changed();
    }
}

void StyleDraft::setType(const QString &type)
{
    if (type != m_type && m_mode == u"new") {
        m_type = type;
        emit changed();
    }
}

void StyleDraft::setBasedOn(const QString &id)
{
    if (id != m_basedOn) {
        m_basedOn = id;
        emit changed();
    }
}

void StyleDraft::setNext(const QString &id)
{
    if (id != m_next) {
        m_next = id;
        emit changed();
    }
}

void StyleDraft::setQuick(bool quick)
{
    if (quick != m_quick) {
        m_quick = quick;
        emit changed();
    }
}

void StyleDraft::setNumbering(const QString &numbering)
{
    if (numbering != m_numbering) {
        m_numbering = numbering;
        emit changed();
    }
}

QString StyleDraft::wordName() const
{
    return !m_sourceName.isEmpty() && Docx::displayStyleName(m_sourceName) == m_name ? m_sourceName : m_name;
}

QVariantMap StyleDraft::look() const
{
    const QTextBlock block = m_sample.begin();
    const QVariantMap font = fontInfo();
    const QVariantMap paragraph = paragraphInfoAt(sample());
    return {{QStringLiteral("family"), font.value(QStringLiteral("family"))},
            {QStringLiteral("size"), font.value(QStringLiteral("size"))},
            {QStringLiteral("bold"), font.value(QStringLiteral("bold"))},
            {QStringLiteral("italic"), font.value(QStringLiteral("italic"))},
            {QStringLiteral("underline"), font.value(QStringLiteral("underline")).toString() != u"none"},
            {QStringLiteral("color"), font.value(QStringLiteral("color"))},
            {QStringLiteral("alignment"), alignmentName(block.blockFormat().alignment())},
            {QStringLiteral("before"), paragraph.value(QStringLiteral("before"))},
            {QStringLiteral("after"), paragraph.value(QStringLiteral("after"))},
            {QStringLiteral("lineRule"), paragraph.value(QStringLiteral("lineRule"))}};
}

QString StyleDraft::description() const
{
    const QVariantMap l = look();
    QStringList font{l.value(QStringLiteral("family")).toString(),
                     tr("%1 pt").arg(l.value(QStringLiteral("size")).toReal())};
    if (l.value(QStringLiteral("bold")).toBool()) {
        font << tr("Bold");
    }
    if (l.value(QStringLiteral("italic")).toBool()) {
        font << tr("Italic");
    }
    if (const QString color = l.value(QStringLiteral("color")).toString(); !color.isEmpty()) {
        font << tr("Font colour %1").arg(color);
    }
    QStringList parts{tr("Font: %1").arg(font.join(QStringLiteral(", ")))};
    if (m_type != u"character") {
        parts << tr("%1, space before %2 pt, after %3 pt")
                     .arg(l.value(QStringLiteral("alignment")).toString())
                     .arg(l.value(QStringLiteral("before")).toReal())
                     .arg(l.value(QStringLiteral("after")).toReal());
    }
    const auto nameOf = [this](const QString &id) {
        for (const QVariantList *list : {&m_paragraphStyles, &m_characterStyles}) {
            for (const QVariant &v : *list) {
                if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                    return v.toMap().value(QStringLiteral("name")).toString();
                }
            }
        }
        return id;
    };
    if (!m_basedOn.isEmpty()) {
        parts << tr("Based on: %1").arg(nameOf(m_basedOn));
    }
    if (m_type != u"character") {
        parts << tr("Following style: %1").arg(m_next.isEmpty() || m_next == m_id ? m_name : nameOf(m_next));
    }
    return parts.join(QStringLiteral("; "));
}

QVariantMap StyleDraft::paragraphInfo() const
{
    return paragraphInfoAt(sample());
}

bool StyleDraft::applyParagraph(const QVariantMap &answer)
{
    const bool applied = applyParagraphAt(sample(), answer);
    emit changed();
    return applied;
}

QVariantMap StyleDraft::tabsInfo() const
{
    return {{QStringLiteral("stops"), tabStopsAt(sample())},
            {QStringLiteral("defaultStop"), m_document->parts().defaultTabStop}};
}

bool StyleDraft::applyTabs(const QVariantMap &answer)
{
    applyTabStopsAt(sample(), answer.value(QStringLiteral("stops")).toList());
    emit changed();
    return true;
}

QVariantMap StyleDraft::fontInfo() const
{
    QTextCursor first(const_cast<QTextDocument *>(&m_sample));
    if (m_sample.characterCount() > 1) {
        first.setPosition(1);
    }
    return fontInfoOf(first.charFormat(), m_sample.defaultFont());
}

bool StyleDraft::applyFont(const QVariantMap &answer)
{
    const bool applied = applyFontAt(sample(), answer);
    // What is typed after the sample, and the paragraph mark, look the same.
    if (m_sample.characterCount() > 1) {
        QTextCursor first(&m_sample);
        first.setPosition(1);
        QTextCursor(&m_sample).setBlockCharFormat(first.charFormat());
    }
    emit changed();
    return applied;
}

QVariantMap StyleDraft::dialogInfo() const
{
    return paragraphBordersInfoAt(sample());
}

bool StyleDraft::applyDialog(const QVariantMap &answer)
{
    applyParagraphBordersAt(sample(), answer);
    emit changed();
    return true;
}

Ooxml::XmlElementPtr StyleDraft::paragraphMarkup() const
{
    if (m_type == u"character") {
        return nullptr;
    }
    return Docx::paragraphFormatting(m_sample.begin(), m_document->parts(), m_mode == u"modify");
}

Ooxml::XmlElementPtr StyleDraft::runMarkup() const
{
    QTextCursor first(const_cast<QTextDocument *>(&m_sample));
    if (m_sample.characterCount() > 1) {
        first.setPosition(1);
    }
    return Docx::runFormatting(first.charFormat(), m_sample, m_document->parts());
}

} // namespace QindaOffice::Apps::Write
