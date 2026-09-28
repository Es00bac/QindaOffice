// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_tools.h"

#include "note_blocks.h"
#include "note_host.h"
#include "note_page_item.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace QindaOffice::Apps::Notes {

namespace {

constexpr qreal kMargin = 40;

bool isTyped(const QJsonObject &block)
{
    const QString type = block.value(QStringLiteral("type")).toString();
    return type == QLatin1String("text") || type == QLatin1String("check")
        || type == QLatin1String("markdown");
}

QString describe(const QJsonObject &block)
{
    const QString type = block.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("check")) {
        return block.value(QStringLiteral("checked")).toBool() ? QStringLiteral("checklist item, done")
                                                              : QStringLiteral("checklist item");
    }
    if (type == QLatin1String("markdown")) {
        return QStringLiteral("markdown");
    }
    return type;
}

// A working copy of the page: each entry remembers the snapshot index it
// came from (-1 for a block the edits created).
struct Entry {
    int origin = -1;
    QJsonObject block;
};

int findEntry(const QList<Entry> &entries, int origin)
{
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (entries.at(i).origin == origin) {
            return int(i);
        }
    }
    return -1;
}

QJsonObject newBlock(const QJsonObject &edit)
{
    QString type = edit.value(QStringLiteral("type")).toString(QStringLiteral("markdown"));
    if (type != QLatin1String("text") && type != QLatin1String("check")) {
        type = QStringLiteral("markdown");
    }
    QJsonObject block{{QStringLiteral("type"), type},
                      {QStringLiteral("text"), edit.value(QStringLiteral("text")).toString()}};
    if (type == QLatin1String("check")) {
        block.insert(QStringLiteral("checked"), edit.value(QStringLiteral("checked")).toBool());
        block.insert(QStringLiteral("level"), 0);
    }
    return block;
}

} // namespace

QString NoteTools::snapshot(int budget)
{
    const NotePage *page = m_host->currentPage();
    if (page == nullptr) {
        m_snapshotPage.clear();
        m_snapshot = {};
        return QStringLiteral("(no page is open)");
    }
    m_snapshotPage = page->id;
    m_snapshot = blocksArray(page->envelope.blocksJson);
    QString out = QStringLiteral("Page title: %1\n").arg(page->title);
    if (!page->envelope.ink.strokes().isEmpty()) {
        out += QStringLiteral("(The page also has %1 handwritten ink strokes; they cannot be "
                              "edited.)\n")
                   .arg(page->envelope.ink.strokes().size());
    }
    if (m_snapshot.isEmpty()) {
        return out + QStringLiteral("(the page has no typed content)\n");
    }
    QString transcripts;
    for (qsizetype i = 0; i < m_snapshot.size(); ++i) {
        const QJsonObject block = m_snapshot.at(i).toObject();
        // M56 (#4): a live-notes block carries its session's transcript —
        // the assistant answers "what did she say about X?" from it.
        const QString transcript = block.value(QStringLiteral("transcript")).toString();
        if (!transcript.isEmpty()) {
            transcripts += QStringLiteral("\n\nTRANSCRIPT of the live session behind block [%1] (answer "
                                          "questions about what was said from it, citing [mm:ss]):\n%2")
                               .arg(i)
                               .arg(transcript.right(qMax(4000, budget / 2)));
        }
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("image")) {
            out += QStringLiteral("\n[%1] image (not editable)\n").arg(i);
            continue;
        }
        if (!isTyped(block)) {
            continue;
        }
        out += QStringLiteral("\n[%1] %2:\n%3\n")
                   .arg(i)
                   .arg(describe(block), block.value(QStringLiteral("text")).toString());
        if (out.size() > budget) {
            out += QStringLiteral("\n(… the page continues; later blocks omitted)\n");
            break;
        }
    }
    return out + transcripts;
}

QString NoteTools::operationGuide() const
{
    return QStringLiteral(
        "Blocks are the [n] entries of the snapshot. Markdown blocks render headings, lists, "
        "**bold**, ==highlight==, > quotes and - [ ] tasks; text blocks are plain text.\n"
        "- {\"op\":\"replace_block\",\"block\":2,\"text\":\"…\"} — new text for block 2 "
        "(markdown for a markdown block).\n"
        "- {\"op\":\"delete_block\",\"block\":3}\n"
        "- {\"op\":\"insert_block\",\"after\":2,\"type\":\"markdown\",\"text\":\"…\"} — type "
        "markdown (default), text or check (with \"checked\": true/false); \"after\": -1 puts it "
        "at the top.\n"
        "- {\"op\":\"replace_page\",\"markdown\":\"…\"} — replace ALL the page's typed blocks "
        "with one markdown block (images and ink stay). Best for rewriting or condensing a "
        "whole page.\n"
        "- {\"op\":\"rename_page\",\"title\":\"…\"}\n"
        "Block numbers always refer to the snapshot, even after earlier edits in the same "
        "block.");
}

Ai::EditOutcome NoteTools::apply(const QJsonArray &edits)
{
    Ai::EditOutcome outcome;
    NotePageItem *item = m_host->pageItem();
    const NotePage *page = m_host->currentPage();
    if (item == nullptr || page == nullptr || page->id != m_snapshotPage) {
        outcome.errors.append(QObject::tr("The page changed since the assistant read it; ask "
                                          "again."));
        return outcome;
    }
    const QJsonArray current = blocksArray(page->envelope.blocksJson);
    QList<Entry> entries;
    for (qsizetype i = 0; i < current.size(); ++i) {
        // A block keeps its snapshot identity only while it is unchanged
        // (live notes may have rewritten it meanwhile).
        const bool same = i < m_snapshot.size() && m_snapshot.at(i) == current.at(i);
        entries.append({same ? int(i) : -2, current.at(i).toObject()});
    }
    QString newTitle;
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QString op = edit.value(QStringLiteral("op")).toString();
        const int block = edit.value(QStringLiteral("block")).toInt(-1);
        if (op == QLatin1String("replace_block") || op == QLatin1String("delete_block")) {
            const int at = findEntry(entries, block);
            if (at < 0 || !isTyped(entries.at(at).block)) {
                outcome.errors.append(QObject::tr("%1: block %2 is not a text block on the page "
                                                  "as it is now").arg(op).arg(block));
                continue;
            }
            if (op == QLatin1String("delete_block")) {
                entries.removeAt(at);
                outcome.changes.append(QObject::tr("deleted block %1").arg(block));
            } else {
                entries[at].block.insert(QStringLiteral("text"),
                                         edit.value(QStringLiteral("text")).toString());
                outcome.changes.append(QObject::tr("rewrote block %1").arg(block));
            }
        } else if (op == QLatin1String("insert_block")) {
            const int after = edit.value(QStringLiteral("after")).toInt(-1);
            const int at = after < 0 ? -1 : findEntry(entries, after);
            if (after >= 0 && at < 0) {
                outcome.errors.append(QObject::tr("insert_block: block %1 is gone").arg(after));
                continue;
            }
            QJsonObject created = newBlock(edit);
            // Provisional place right under its anchor; reflow() settles it.
            const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, item->pageWidth());
            qreal y = kBlockTop - 1;
            for (const PageBlock &b : laid) {
                if (after >= 0 && b.index == after) {
                    y = b.rect.bottom() + 0.5;
                }
            }
            setBlockRect(&created, QRectF(kMargin, y, item->pageWidth() - 2 * kMargin, 0));
            entries.insert(at + 1, {-1, created});
            outcome.changes.append(QObject::tr("added a %1 block")
                                       .arg(created.value(QStringLiteral("type")).toString()));
        } else if (op == QLatin1String("replace_page")) {
            qreal top = -1;
            const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, item->pageWidth());
            for (const PageBlock &b : laid) {
                if (b.kind != PageBlock::Kind::Image && (top < 0 || b.rect.top() < top)) {
                    top = b.rect.top();
                }
            }
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [](const Entry &e) { return isTyped(e.block); }),
                          entries.end());
            QJsonObject created{{QStringLiteral("type"), QStringLiteral("markdown")},
                                {QStringLiteral("text"),
                                 edit.value(QStringLiteral("markdown")).toString()}};
            setBlockRect(&created, QRectF(kMargin, top < 0 ? kBlockTop : top,
                                          item->pageWidth() - 2 * kMargin, 0));
            entries.prepend({-1, created});
            outcome.changes.append(QObject::tr("replaced the page's text"));
        } else if (op == QLatin1String("rename_page")) {
            newTitle = edit.value(QStringLiteral("title")).toString().trimmed();
            if (!newTitle.isEmpty()) {
                outcome.changes.append(QObject::tr("renamed the page “%1”").arg(newTitle));
            }
        } else {
            outcome.errors.append(QObject::tr("unknown operation “%1”").arg(op));
        }
    }
    const int textChanges = int(outcome.changes.size()) - (newTitle.isEmpty() ? 0 : 1);
    if (textChanges > 0) {
        QJsonArray blocks;
        for (const Entry &entry : entries) {
            blocks.append(entry.block);
        }
        reflow(&blocks, item->pageWidth());
        if (!item->replaceBlocks(blocks, QObject::tr("Assistant Edit"))) {
            outcome.errors.append(QObject::tr("The page could not be changed."));
            return outcome;
        }
    }
    if (!newTitle.isEmpty()) {
        m_host->renamePage(m_host->currentSectionId(), m_host->currentPageId(), newTitle);
    }
    outcome.applied = int(outcome.changes.size());
    m_snapshotPage.clear(); // a second apply needs a fresh snapshot
    return outcome;
}

void NoteTools::undo()
{
    if (NotePageItem *item = m_host->pageItem()) {
        item->undo();
    }
}

void NoteTools::reflow(QJsonArray *blocks, qreal pageWidth)
{
    const QByteArray json = withBlocksArray(QByteArray("{}"), *blocks);
    QList<PageBlock> laid = layoutBlocks(json, pageWidth);
    std::stable_sort(laid.begin(), laid.end(), [](const PageBlock &a, const PageBlock &b) {
        return a.rect.top() < b.rect.top();
    });
    QList<QRectF> placed;
    for (const PageBlock &block : laid) {
        QRectF rect = block.rect;
        bool moved = true;
        while (moved) {
            moved = false;
            for (const QRectF &above : placed) {
                const bool sideBySide = rect.right() <= above.left() || above.right() <= rect.left();
                if (!sideBySide && rect.top() < above.bottom() + kBlockGap
                    && rect.bottom() > above.top()) {
                    rect.moveTop(above.bottom() + kBlockGap);
                    moved = true;
                }
            }
        }
        placed.append(rect);
        QJsonObject object = blocks->at(block.index).toObject();
        if (rect != block.rect || !object.contains(QStringLiteral("rect"))) {
            setBlockRect(&object, rect);
            blocks->replace(block.index, object);
        }
    }
}

} // namespace QindaOffice::Apps::Notes
