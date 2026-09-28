// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageItem's content blocks (D-027): typed text, checklist items and
// images are PAINTED (note_page_render.cpp) so ink goes over them; at most
// one block is live at a time — the window places a Tk.TextArea /
// Tk.TextField at `editorRect` while `editingBlock` >= 0 and writes back
// through updateEdit()/commitEdit(). Every mutation is a BlocksCommand on
// the one page history; typing on one block merges into one step.
// blocksJson in the envelope stays the source of truth; PageBlock is the
// derived layout (note_blocks.h).

#include "note_page_item.h"

#include "note_page_history.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QUndoStack>
#include <QUrl>

namespace QindaOffice::Apps::Notes {

namespace {

constexpr qreal kNewTextBlockWidth = 360;
constexpr qreal kPageMargin = 40;

} // namespace

void NotePageItem::relayoutBlocks()
{
    m_blocks = m_page != nullptr ? layoutBlocks(m_page->envelope.blocksJson, m_pageWidth)
                                 : QList<PageBlock>{};
}

void NotePageItem::writeBlocks(const QJsonArray &blocks, const QString &undoText, int mergeKey)
{
    if (m_page == nullptr) {
        return;
    }
    const QByteArray before = m_page->envelope.blocksJson;
    const QByteArray after = withBlocksArray(before, blocks);
    if (before == after) {
        return;
    }
    m_page->envelope.blocksJson = after;
    m_history->push(new BlocksCommand(undoText, before, after, this, mergeKey));
    relayoutBlocks();
    updatePageMetrics();
    update();
    emit pageEdited();
}

void NotePageItem::applyHistoryBlocks(const QByteArray &blocksJson)
{
    if (m_page == nullptr) {
        return;
    }
    if (m_editingBlock >= 0) {
        // The editor's block may no longer exist after the undo.
        m_editingBlock = -1;
        m_editingText.clear();
        emit editingChanged();
    }
    m_page->envelope.blocksJson = blocksJson;
    relayoutBlocks();
    updatePageMetrics();
    update();
    emit pageEdited();
}

void NotePageItem::appendBlock(const QJsonObject &block, const QString &undoText)
{
    if (m_page == nullptr) {
        return;
    }
    if (m_editingBlock >= 0) {
        commitEdit();
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    blocks.append(block);
    writeBlocks(blocks, undoText);
}

void NotePageItem::addTextBlock()
{
    // "Add text" appends a full-width block under the lowest block (the
    // discoverable path); the Text tool's tap places one anywhere.
    qreal y = kBlockTop;
    for (const PageBlock &block : m_blocks) {
        y = qMax(y, block.rect.bottom() + kBlockGap);
    }
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("text")},
                      {QStringLiteral("text"), QString()}};
    setBlockRect(&block, QRectF(kPageMargin, y, m_pageWidth - 2 * kPageMargin, 0));
    appendBlock(block, tr("Add Text"));
    beginEdit(int(m_blocks.size()) - 1);
}

void NotePageItem::addCheckItem()
{
    qreal y = kBlockTop;
    for (const PageBlock &block : m_blocks) {
        y = qMax(y, block.rect.bottom() + kBlockGap);
    }
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("check")},
                      {QStringLiteral("text"), QString()},
                      {QStringLiteral("checked"), false},
                      {QStringLiteral("level"), 0}};
    setBlockRect(&block, QRectF(kPageMargin, y, m_pageWidth - 2 * kPageMargin, 0));
    appendBlock(block, tr("Add Checklist Item"));
    beginEdit(int(m_blocks.size()) - 1);
}

void NotePageItem::addImageBlock(const QByteArray &imageData)
{
    if (imageData.isEmpty()) {
        return;
    }
    // Base64 in the page envelope: a notebook page is one self-contained
    // JSON file, so an image referenced by path would break the moment the
    // notebook moved.
    appendBlock(QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                            {QStringLiteral("data"),
                             QString::fromLatin1(imageData.toBase64())}},
                tr("Add Image"));
}

bool NotePageItem::addImageFile(const QUrl &url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    addImageBlock(file.readAll());
    return true;
}

int NotePageItem::blockAt(const QPointF &pos) const
{
    // Topmost first: later blocks were added later.
    for (qsizetype i = m_blocks.size() - 1; i >= 0; --i) {
        if (m_blocks.at(i).rect.contains(pos)) {
            return m_blocks.at(i).index;
        }
    }
    return -1;
}

void NotePageItem::tapAt(const QPointF &pos)
{
    if (m_page == nullptr || !pageRect().contains(pos)) {
        return;
    }
    const int index = blockAt(pos);
    if (index >= 0) {
        if (index == m_editingBlock) {
            return;
        }
        for (const PageBlock &block : m_blocks) {
            if (block.index != index) {
                continue;
            }
            if (block.kind == PageBlock::Kind::Check) {
                // The box toggles; the label opens.
                const qreal boxRight = block.rect.left() + block.level * kCheckIndent + 20;
                if (pos.x() <= boxRight) {
                    if (m_editingBlock >= 0) {
                        commitEdit();
                    }
                    toggleCheck(index);
                    return;
                }
            }
            if (block.kind == PageBlock::Kind::Image) {
                return; // images have no editor (M24 selects them)
            }
            break;
        }
        beginEdit(index);
        return;
    }
    // Empty page under the tap: a new text block starts here.
    if (m_editingBlock >= 0) {
        commitEdit();
    }
    const qreal x = qMax(kPageMargin / 2, pos.x() - kTextPadding);
    const qreal width = qMax<qreal>(120, qMin(kNewTextBlockWidth, m_pageWidth - x - kPageMargin / 2));
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("text")},
                      {QStringLiteral("text"), QString()}};
    setBlockRect(&block, QRectF(x, qMax(kPageMargin / 2, pos.y() - kTextPadding), width, 0));
    appendBlock(block, tr("Add Text"));
    beginEdit(int(m_blocks.size()) - 1);
}

void NotePageItem::beginEdit(int index)
{
    if (m_page == nullptr || index < 0) {
        return;
    }
    if (m_editingBlock >= 0 && m_editingBlock != index) {
        commitEdit();
    }
    const PageBlock *target = nullptr;
    for (const PageBlock &block : m_blocks) {
        if (block.index == index) {
            target = &block;
        }
    }
    if (target == nullptr || target->kind == PageBlock::Kind::Image) {
        return;
    }
    m_editingBlock = index;
    m_editingText = target->text;
    m_editBefore = m_page->envelope.blocksJson;
    update(target->rect.toAlignedRect());
    emit editingChanged();
    emit editorRequested();
}

void NotePageItem::updateEdit(const QString &text)
{
    if (m_page == nullptr || m_editingBlock < 0 || m_editingText == text) {
        return;
    }
    m_editingText = text;
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (m_editingBlock >= blocks.size()) {
        return;
    }
    QJsonObject block = blocks.at(m_editingBlock).toObject();
    block.insert(QStringLiteral("text"), text);
    if (!block.contains(QStringLiteral("rect"))) {
        // A stacked (M21) block gains its rect on the first edit — where it
        // sits today — so a taller neighbour above cannot shove it later.
        for (const PageBlock &laid : m_blocks) {
            if (laid.index == m_editingBlock) {
                setBlockRect(&block, laid.rect);
            }
        }
    }
    blocks.replace(m_editingBlock, block);
    // Consecutive typing on this block merges into one undo step.
    writeBlocks(blocks, tr("Typing"), m_editingBlock);
    emit editingChanged();
}

void NotePageItem::commitEdit()
{
    if (m_page == nullptr || m_editingBlock < 0) {
        return;
    }
    const int index = m_editingBlock;
    const QString text = m_editingText;
    m_editingBlock = -1;
    m_editingText.clear();
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index < blocks.size()) {
        const QJsonObject block = blocks.at(index).toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("text")
            && text.trimmed().isEmpty()) {
            // An empty text block is litter: remove it (one undo step with
            // the typing that emptied it, if any).
            blocks.removeAt(index);
            writeBlocks(blocks, tr("Remove Empty Text"), index);
        }
    }
    update();
    emit editingChanged();
}

// Typing is live (updateEdit), so "cancel" cannot un-type; it closes the
// editor and, like commit, drops the litter of an empty text block (the
// Text tool's tap that was never typed into).
void NotePageItem::cancelEdit()
{
    commitEdit();
}

void NotePageItem::deleteBlock(int index)
{
    if (m_page == nullptr || index < 0) {
        return;
    }
    if (m_editingBlock == index) {
        m_editingBlock = -1;
        m_editingText.clear();
        emit editingChanged();
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index >= blocks.size()) {
        return;
    }
    blocks.removeAt(index);
    writeBlocks(blocks, tr("Delete Block"));
}

void NotePageItem::toggleCheck(int index)
{
    if (m_page == nullptr) {
        return;
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index < 0 || index >= blocks.size()) {
        return;
    }
    QJsonObject block = blocks.at(index).toObject();
    if (block.value(QStringLiteral("type")).toString() != QLatin1String("check")) {
        return;
    }
    block.insert(QStringLiteral("checked"), !block.value(QStringLiteral("checked")).toBool());
    blocks.replace(index, block);
    writeBlocks(blocks, tr("Toggle Checklist Item"));
}

void NotePageItem::insertCheckItemAfter(int index)
{
    if (m_page == nullptr) {
        return;
    }
    if (m_editingBlock >= 0) {
        commitEdit();
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index < 0 || index >= blocks.size()) {
        return;
    }
    const QJsonObject above = blocks.at(index).toObject();
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("check")},
                      {QStringLiteral("text"), QString()},
                      {QStringLiteral("checked"), false},
                      {QStringLiteral("level"), above.value(QStringLiteral("level")).toInt(0)}};
    for (const PageBlock &laid : m_blocks) {
        if (laid.index == index) {
            setBlockRect(&block, QRectF(laid.rect.x(), laid.rect.bottom() + 2, laid.rect.width(), 0));
        }
    }
    blocks.insert(index + 1, block);
    writeBlocks(blocks, tr("Add Checklist Item"));
    beginEdit(index + 1);
}

void NotePageItem::indentCheckItem(int index, int delta)
{
    if (m_page == nullptr) {
        return;
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index < 0 || index >= blocks.size()) {
        return;
    }
    QJsonObject block = blocks.at(index).toObject();
    if (block.value(QStringLiteral("type")).toString() != QLatin1String("check")) {
        return;
    }
    const int level = qBound(0, block.value(QStringLiteral("level")).toInt(0) + delta, 3);
    block.insert(QStringLiteral("level"), level);
    blocks.replace(index, block);
    writeBlocks(blocks, tr("Indent Checklist Item"));
}

int NotePageItem::blockIndexForUid(const QString &uid) const
{
    if (m_page == nullptr || uid.isEmpty()) {
        return -1;
    }
    const QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        if (blocks.at(i).toObject().value(QStringLiteral("uid")).toString() == uid) {
            return int(i);
        }
    }
    return -1;
}

bool NotePageItem::setBlockFields(int index, const QJsonObject &fields, const QString &undoText)
{
    if (m_page == nullptr || index < 0 || index == m_editingBlock) {
        return false; // never pull text out from under the live editor
    }
    QJsonArray blocks = blocksArray(m_page->envelope.blocksJson);
    if (index >= blocks.size()) {
        return false;
    }
    QJsonObject block = blocks.at(index).toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        block.insert(it.key(), it.value());
    }
    blocks.replace(index, block);
    writeBlocks(blocks, undoText);
    return true;
}

void NotePageItem::appendMarkdownBlock(const QString &uid, const QString &text)
{
    qreal y = kBlockTop;
    for (const PageBlock &block : m_blocks) {
        y = qMax(y, block.rect.bottom() + kBlockGap);
    }
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("markdown")},
                      {QStringLiteral("uid"), uid},
                      {QStringLiteral("text"), text}};
    setBlockRect(&block, QRectF(kPageMargin, y, m_pageWidth - 2 * kPageMargin, 0));
    appendBlock(block, tr("Start Live Notes"));
}

QString NotePageItem::editingKind() const
{
    for (const PageBlock &block : m_blocks) {
        if (block.index == m_editingBlock) {
            return block.kind == PageBlock::Kind::Check      ? QStringLiteral("check")
                : block.kind == PageBlock::Kind::Markdown ? QStringLiteral("markdown")
                                                          : QStringLiteral("text");
        }
    }
    return {};
}

bool NotePageItem::editingChecked() const
{
    for (const PageBlock &block : m_blocks) {
        if (block.index == m_editingBlock) {
            return block.checked;
        }
    }
    return false;
}

QRectF NotePageItem::editorRect() const
{
    for (const PageBlock &block : m_blocks) {
        if (block.index == m_editingBlock) {
            if (block.kind == PageBlock::Kind::Check) {
                const qreal indent = block.level * kCheckIndent;
                return block.rect.adjusted(indent + 24, 0, 0, 0);
            }
            return block.rect;
        }
    }
    return {};
}

} // namespace QindaOffice::Apps::Notes
