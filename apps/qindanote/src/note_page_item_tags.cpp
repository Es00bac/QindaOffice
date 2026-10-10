// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageItem's tags, links and page colour (M84, D-093). Tags live in the
// block JSON (note_tags.h), so applying one is a BlocksCommand like any
// block edit; the open formatted-text editor stays open across it
// (applyHistoryBlocks keeps an editor whose text did not change).

#include "note_page_item.h"

#include "note_page_history.h"
#include "note_page_render.h"
#include "note_rich_render.h"
#include "note_rich_text.h"
#include "note_tags.h"

#include <qindatk/rich_text_area.h>
#include <qindatk/rich_text_document.h>

#include <QJsonArray>
#include <QUndoStack>

namespace QindaOffice::Apps::Notes {

namespace {

// The paragraphs a tag command applies to: the editor's selection, or every
// paragraph when no editor is on screen (a test, a headless agent).
QStringList targetParagraphs(QindaTK::RichTextDocument *document, QindaTK::RichTextArea *area)
{
    if (area != nullptr && area->document() == document) {
        QStringList ids;
        for (const QString &id : area->selectedParagraphIds()) {
            ids.append(QUuid(id).toString(QUuid::WithoutBraces));
        }
        if (ids.isEmpty()) {
            ids.append(QUuid(area->caretParagraphId()).toString(QUuid::WithoutBraces));
        }
        return ids;
    }
    return RichBlock::paragraphIds(*document);
}

} // namespace

bool NotePageItem::toggleTag(const Tags::TagDef &def)
{
    if (m_page == nullptr || !def.isValid()) {
        return false;
    }
    if (m_editingBlock < 0) {
        // OneNote: a tag with nothing selected starts a new tagged note.
        addTextBlock();
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (m_editingBlock < 0 || m_editingBlock >= blocks.size()) {
        return false;
    }
    QJsonObject block = blocks.at(m_editingBlock).toObject();
    const QStringList targets = m_richDocument != nullptr ? targetParagraphs(m_richDocument, m_richArea)
                                                          : QStringList{QString()};
    bool added = false;
    block.insert(QStringLiteral("tags"), Tags::toggled(block.value(QStringLiteral("tags")).toArray(), targets,
                                                        def, &added));
    if (m_richDocument != nullptr) {
        // Paragraph tags need the paragraphs' ids on disk: `rich` is kept.
        RichBlock::store(&block, *m_richDocument);
    } else if (block.value(QStringLiteral("tags")).toArray().isEmpty()) {
        block.remove(QStringLiteral("tags"));
    }
    blocks.replace(m_editingBlock, block);
    writeBlocks(blocks, added ? tr("Tag: %1").arg(def.name) : tr("Remove Tag: %1").arg(def.name));
    emit editingChanged(); // the block may have changed height
    emit richStateChanged();
    return true;
}

bool NotePageItem::removeTags()
{
    if (m_page == nullptr || m_editingBlock < 0) {
        return false;
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (m_editingBlock >= blocks.size()) {
        return false;
    }
    QJsonObject block = blocks.at(m_editingBlock).toObject();
    const QStringList targets = m_richDocument != nullptr ? targetParagraphs(m_richDocument, m_richArea)
                                                          : QStringList{QString()};
    const QJsonArray tags = Tags::withoutTargets(block.value(QStringLiteral("tags")).toArray(), targets);
    if (tags.isEmpty()) {
        block.remove(QStringLiteral("tags"));
    } else {
        block.insert(QStringLiteral("tags"), tags);
    }
    if (m_richDocument != nullptr) {
        RichBlock::store(&block, *m_richDocument);
    }
    blocks.replace(m_editingBlock, block);
    writeBlocks(blocks, tr("Remove Tag"));
    emit editingChanged();
    emit richStateChanged();
    return true;
}

bool NotePageItem::setTagDone(int index, int entry, bool done)
{
    if (m_page == nullptr) {
        return false;
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index < 0 || index >= blocks.size()) {
        return false;
    }
    QJsonObject block = blocks.at(index).toObject();
    const QJsonArray tags = block.value(QStringLiteral("tags")).toArray();
    if (entry < 0 || entry >= tags.size()) {
        return false;
    }
    block.insert(QStringLiteral("tags"), Tags::withDone(tags, entry, done));
    blocks.replace(index, block);
    writeBlocks(blocks, done ? tr("Check To Do") : tr("Uncheck To Do"));
    return true;
}

std::pair<int, int> NotePageItem::todoBoxAt(const QPointF &pos) const
{
    for (const PageBlock &block : m_blocks) {
        for (const Tags::Placed &placed : Tags::place(block)) {
            if (placed.def.todo && !placed.band && placed.rect.adjusted(-3, -3, 3, 3).contains(pos)) {
                return {block.index, placed.entry};
            }
        }
    }
    return {-1, -1};
}

bool NotePageItem::toggleTodoBoxAt(const QPointF &pos)
{
    const auto [index, entry] = todoBoxAt(pos);
    if (index < 0) {
        return false;
    }
    for (const PageBlock &block : std::as_const(m_blocks)) {
        if (block.index == index) {
            return setTagDone(index, entry,
                              !block.tags.at(entry).toObject().value(QStringLiteral("done")).toBool());
        }
    }
    return false;
}

QString NotePageItem::linkAt(const QPointF &pos) const
{
    for (const PageBlock &block : m_blocks) {
        if (block.index != m_editingBlock && !block.rich.isEmpty() && block.rect.contains(pos)) {
            return RichRender::linkAt(block.rich, block.rect.width(), pos - block.rect.topLeft());
        }
    }
    return {};
}

QColor NotePageItem::paperColor() const
{
    return m_page != nullptr ? notePaper(m_page->envelope.blocksJson) : QColor(Qt::white);
}

void NotePageItem::setPageColor(const QColor &color)
{
    if (m_page == nullptr) {
        return;
    }
    const QByteArray before = m_page->envelope.blocksJson;
    const QByteArray after = withPageColor(before, color);
    if (before == after) {
        return;
    }
    m_page->envelope.blocksJson = after;
    m_history->push(new BlocksCommand(tr("Page Color"), before, after, this));
    update();
    emit pageChanged();
    emit pageEdited();
}

void NotePageItem::revealBlock(int index, const QString &paragraph)
{
    for (const PageBlock &block : std::as_const(m_blocks)) {
        if (block.index != index) {
            continue;
        }
        if (block.kind == PageBlock::Kind::Image) {
            emit revealRequested(block.rect);
            return;
        }
        if (m_editingBlock == index && m_richDocument != nullptr) {
            m_pendingCaret = {paragraph, 0};
            emit editorRequested();
            return;
        }
        if (!paragraph.isEmpty()) {
            m_pendingCaret = {paragraph, 0};
        }
        beginEdit(index);
        return;
    }
}

} // namespace QindaOffice::Apps::Notes
