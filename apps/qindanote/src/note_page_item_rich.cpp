// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageItem's formatted text (M84, D-093): the open text block's
// QindaTK rich-text document, which the window's Tk.RichTextEditor edits.
//
// AGENT-CONTRACT (one history, note_page_history.h): the document runs
// with externalUndo — every transaction it commits (a typing run, a format
// command, a paste, a table edit) is written back into the block
// (RichBlock::store) and pushed as ONE RichEditCommand on the page's
// QUndoStack, which replays it on the live document or restores the block
// snapshot. The document's own undo stays empty: Ctrl+Z inside the editor
// reaches Edit ▸ Undo (the area leaves Ctrl chords to the window), and the
// area's own Undo and Redo (its commands, QindaTK D-321) ask the page's
// history through undoRequested/redoRequested, so ink, blocks and typing
// undo in the order they happened.

#include "note_page_item.h"

#include "note_clipboard.h"
#include "note_page_history.h"
#include "note_rich_render.h"
#include "note_rich_style.h"
#include "note_rich_text.h"

#include <qindatk/rich_text_area.h>
#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_format_state.h>
#include <qindatk/rich_text_spans.h>

#include <QJsonArray>
#include <QUndoStack>

namespace QindaOffice::Apps::Notes {

using namespace QindaTK::RichText;

namespace {

// Consecutive typing or deleting is one step, as the old plain editor's was.
bool continuesTyping(const QString &label)
{
    return label == QindaTK::RichTextArea::tr("Typing") || label == QindaTK::RichTextArea::tr("Delete")
        || label == NotePageItem::tr("Typing");
}

} // namespace

QindaTK::RichTextDocument *NotePageItem::richDocument() const
{
    return m_richDocument;
}

QindaTK::RichTextArea *NotePageItem::richArea() const
{
    return m_richArea;
}

void NotePageItem::attachRichArea(QQuickItem *item)
{
    auto *area = qobject_cast<QindaTK::RichTextArea *>(item);
    if (area == m_richArea) {
        return;
    }
    if (m_richArea != nullptr) {
        disconnect(m_richArea, nullptr, this, nullptr);
        disconnect(m_richArea->formatState(), nullptr, this, nullptr);
    }
    m_richArea = area;
    if (area == nullptr) {
        return;
    }
    // Paste keeps formatting from the web, Word and Write — sanitised first.
    if (m_clipboard == nullptr) {
        m_clipboard = new NoteClipboard(this);
    }
    area->setClipboardAdapter(m_clipboard);
    // (M89: the area claims its typing keys' ShortcutOverride itself since
    // QindaTK D-320, so the ink tools' one-letter shortcuts no longer steal
    // them; QindaNote's own filter for that is gone.)
    connect(area->formatState(), &QindaTK::RichTextFormatState::changed, this, &NotePageItem::richStateChanged);
    connect(area, &QindaTK::RichTextArea::caretChanged, this, &NotePageItem::richStateChanged);
    connect(area, &QindaTK::RichTextArea::linkActivated, this, &NotePageItem::linkActivated);
}

void NotePageItem::openRichDocument(const QJsonObject &block)
{
    closeRichDocument();
    std::unique_ptr<QindaTK::RichTextDocument> document = RichBlock::forBlock(block);
    m_richDocument = document.release();
    m_richDocument->setParent(this);
    m_richDocument->setExternalUndo(true);
    m_richParagraphs = RichBlock::paragraphIds(*m_richDocument);
    connect(m_richDocument, &QindaTK::RichTextDocument::committed, this, &NotePageItem::onRichCommitted);
    // M89 (QindaTK D-321): the area's Undo/Redo step the page's history.
    connect(m_richDocument, &QindaTK::RichTextDocument::undoRequested, this, &NotePageItem::undo);
    connect(m_richDocument, &QindaTK::RichTextDocument::redoRequested, this, &NotePageItem::redo);
    emit richDocumentChanged();
}

void NotePageItem::closeRichDocument()
{
    m_richMacro = 0;
    if (m_richDocument == nullptr) {
        return;
    }
    QindaTK::RichTextDocument *document = m_richDocument;
    m_richDocument = nullptr;
    disconnect(document, nullptr, this, nullptr);
    // The editor lets go of it first (its binding follows the signal);
    // history commands hold it weakly and fall back to their snapshots.
    emit richDocumentChanged();
    document->deleteLater();
}

void NotePageItem::placeRichCaret()
{
    if (m_richArea == nullptr || m_richDocument == nullptr || m_richArea->document() != m_richDocument) {
        return;
    }
    const auto [paragraph, offset] = m_pendingCaret;
    m_pendingCaret = {QString(), -1};
    const QUuid id(paragraph);
    if (offset >= 0 && m_richDocument->paragraph(id) != nullptr) {
        const Position at = m_richDocument->clampPosition({id, offset});
        m_richArea->setSelection({at, at});
        return;
    }
    const Position end = m_richDocument->endPosition();
    m_richArea->setSelection({end, end});
}

Selection NotePageItem::richSelection() const
{
    if (m_richArea != nullptr && m_richArea->document() == m_richDocument) {
        return m_richArea->selection();
    }
    // No editor on screen (a test, a headless agent): the whole text.
    return {m_richDocument->startPosition(), m_richDocument->endPosition()};
}

void NotePageItem::onRichCommitted(const QString &transaction, const QString &label)
{
    if (m_page == nullptr || m_editingBlock < 0 || m_richDocument == nullptr || m_replayingRich) {
        return;
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (m_editingBlock >= blocks.size()) {
        return;
    }
    QJsonObject block = blocks.at(m_editingBlock).toObject();
    const QStringList before = m_richParagraphs;
    m_richParagraphs = RichBlock::paragraphIds(*m_richDocument);
    if (label == QindaTK::RichTextArea::tr("New paragraph")) {
        carryTodoAcrossSplit(&block, before);
    }
    RichBlock::store(&block, *m_richDocument);
    if (!block.contains(QStringLiteral("rect"))) {
        // A stacked (M21) block gains its rect where it sits today.
        for (const PageBlock &laid : std::as_const(m_blocks)) {
            if (laid.index == m_editingBlock) {
                setBlockRect(&block, laid.rect);
            }
        }
    }
    blocks.replace(m_editingBlock, block);
    const QByteArray beforeJson = m_page->envelope.blocksJson;
    const QByteArray afterJson = withBlocksArray(beforeJson, blocks);
    m_page->envelope.blocksJson = afterJson;
    m_editingText = block.value(QStringLiteral("text")).toString();
    m_history->push(new RichEditCommand(label, beforeJson, afterJson, this, m_richDocument, m_editingBlock,
                                        transaction, m_richMacro == 0 && continuesTyping(label), m_richMacro));
    relayoutBlocks();
    updatePageMetrics();
    update();
    emit pageEdited();
    emit editingChanged();
    emit richStateChanged();
}

void NotePageItem::carryTodoAcrossSplit(QJsonObject *block, const QStringList &before)
{
    // OneNote: Enter at the end of a tagged line starts the next line with
    // the same tags (a To Do list keeps going, unticked).
    QStringList added;
    for (const QString &id : std::as_const(m_richParagraphs)) {
        if (!before.contains(id)) {
            added.append(id);
        }
    }
    if (added.size() != 1) {
        return;
    }
    const int at = int(m_richParagraphs.indexOf(added.first()));
    if (at <= 0) {
        return;
    }
    const QString previous = m_richParagraphs.at(at - 1);
    QJsonArray tags = block->value(QStringLiteral("tags")).toArray();
    const QJsonArray existing = tags;
    for (const QJsonValue &value : existing) {
        QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("p")).toString() != previous) {
            continue;
        }
        entry.insert(QStringLiteral("p"), added.first());
        if (entry.contains(QStringLiteral("done"))) {
            entry.insert(QStringLiteral("done"), false);
        }
        tags.append(entry);
    }
    if (tags.size() != existing.size()) {
        block->insert(QStringLiteral("tags"), tags);
    }
}

bool NotePageItem::replayRichEdit(QindaTK::RichTextDocument *document, int block, const QStringList &transactions,
                                  bool undo, const QByteArray &expected, const QByteArray &target)
{
    if (m_page == nullptr || document == nullptr || document != m_richDocument || block != m_editingBlock
        || m_page->envelope.blocksJson != expected) {
        return false;
    }
    m_replayingRich = true;
    bool ok = true;
    for (qsizetype n = 0; ok && n < transactions.size(); ++n) {
        const QString &transaction = transactions.at(undo ? transactions.size() - 1 - n : n);
        ok = document->applyHistoryChanges(transaction, undo, document->revisionString());
    }
    m_replayingRich = false;
    if (!ok) {
        // A half-replayed document must not stay on screen: the caller
        // restores the snapshot, which closes the editor over a fresh copy.
        closeRichDocument();
        m_editingBlock = -1;
        m_editingText.clear();
        emit editingChanged();
        return false;
    }
    m_page->envelope.blocksJson = target;
    m_richParagraphs = RichBlock::paragraphIds(*document);
    m_editingText = blocksArray(target).at(block).toObject().value(QStringLiteral("text")).toString();
    relayoutBlocks();
    updatePageMetrics();
    update();
    emit pageEdited();
    emit editingChanged();
    emit richStateChanged();
    return true;
}

bool NotePageItem::richCommand(const QString &id, const QVariantMap &args)
{
    if (m_richDocument == nullptr || m_richArea == nullptr || m_richArea->document() != m_richDocument) {
        return false;
    }
    return m_richArea->invokeCommand(id, args);
}

QVariantMap NotePageItem::richCommandState(const QString &id) const
{
    if (m_richDocument == nullptr || m_richArea == nullptr || m_richArea->document() != m_richDocument) {
        return {};
    }
    return m_richArea->commandState(id);
}

bool NotePageItem::applyRichStyle(RichStyle::Style style)
{
    if (m_richDocument == nullptr) {
        return false;
    }
    return RichStyle::apply(*m_richDocument, richSelection(), style);
}

int NotePageItem::richStyleAtCaret() const
{
    if (m_richDocument == nullptr) {
        return -1;
    }
    return int(RichStyle::at(*m_richDocument, richSelection().focus));
}

bool NotePageItem::insertRichLink(const QString &href, const QString &label)
{
    if (m_richDocument == nullptr || href.trimmed().isEmpty()) {
        return false;
    }
    const std::optional<Position> caret =
        RichStyle::insertLink(*m_richDocument, richSelection(), href.trimmed(), label);
    if (caret && m_richArea != nullptr && m_richArea->document() == m_richDocument) {
        m_richArea->setSelection({*caret, *caret});
    }
    return caret.has_value();
}

bool NotePageItem::insertRichTable(int rows, int columns)
{
    if (m_page == nullptr) {
        return false;
    }
    if (m_richDocument == nullptr) {
        // OneNote: a table with nothing open starts a new note holding it.
        addTextBlock();
        if (m_richDocument == nullptr) {
            return false;
        }
    }
    const Selection selection = richSelection();
    if (!m_richDocument->beginTransaction(tr("Insert Table"), {}, selection)) {
        return false;
    }
    const std::optional<Position> caret =
        m_richDocument->insertTable(selection.focus, qBound(1, rows, 100), qBound(1, columns, 32), 0);
    if (!caret) {
        m_richDocument->cancelTransaction();
        return false;
    }
    m_richDocument->commitTransaction({*caret, *caret});
    if (m_richArea != nullptr && m_richArea->document() == m_richDocument) {
        m_richArea->setSelection({*caret, *caret});
    }
    return true;
}

bool NotePageItem::editRichTable(RichStyle::TableEdit edit)
{
    return m_richDocument != nullptr && RichStyle::editTable(*m_richDocument, richSelection().focus, edit);
}

bool NotePageItem::richCaretInTable() const
{
    return m_richDocument != nullptr && RichStyle::inTable(*m_richDocument, richSelection().focus);
}

QString NotePageItem::richSelectedText() const
{
    return m_richArea != nullptr && m_richArea->document() == m_richDocument ? m_richArea->selectedText()
                                                                              : QString();
}

QColor NotePageItem::richHighlight() const
{
    // M89 (QindaTK D-322): the area's format state reports the highlight
    // (invalid for none or a mixed selection).
    if (m_richDocument == nullptr || m_richArea == nullptr || m_richArea->document() != m_richDocument) {
        return {};
    }
    return m_richArea->formatState()->highlight();
}

bool NotePageItem::indentRich(int delta)
{
    if (m_richDocument == nullptr) {
        return false;
    }
    const Selection selection = richSelection();
    const Paragraph *p = m_richDocument->paragraph(selection.focus.paragraph);
    if (p == nullptr) {
        return false;
    }
    if (!p->format.listId.isEmpty()) {
        if (!m_richDocument->beginTransaction(delta > 0 ? tr("Increase Indent") : tr("Decrease Indent"), {},
                                              selection)) {
            return false;
        }
        if (!m_richDocument->changeListLevel(selection.anchor, selection.focus, delta)) {
            m_richDocument->cancelTransaction();
            return false;
        }
        return m_richDocument->commitTransaction(selection);
    }
    ParagraphFormatPatch patch;
    patch.indentStartPt = qMax(0.0, p->format.indentStartPt + delta * 18.0);
    if (!m_richDocument->beginTransaction(delta > 0 ? tr("Increase Indent") : tr("Decrease Indent"), {},
                                          selection)) {
        return false;
    }
    if (!m_richDocument->setParagraphFormat(selection.anchor, selection.focus, patch)) {
        m_richDocument->cancelTransaction();
        return false;
    }
    return m_richDocument->commitTransaction(selection);
}

void NotePageItem::beginRichMacro()
{
    m_richMacro = ++m_richMacroSerial;
}

void NotePageItem::endRichMacro()
{
    m_richMacro = 0;
}

void NotePageItem::updateRichText(const QString &text)
{
    // updateEdit()'s rich half: the whole text replaced as one typing
    // transaction (the assistant's and the tests' plain-text path).
    QindaTK::RichTextDocument *doc = m_richDocument;
    if (!doc->beginTransaction(tr("Typing"), QStringLiteral("typing"))) {
        return;
    }
    std::optional<Position> at = doc->deleteRange(doc->startPosition(), doc->endPosition());
    if (at && !text.isEmpty()) {
        at = doc->insertText(*at, text);
    }
    if (!at) {
        doc->cancelTransaction();
        return;
    }
    doc->commitTransaction({*at, *at});
}

} // namespace QindaOffice::Apps::Notes
