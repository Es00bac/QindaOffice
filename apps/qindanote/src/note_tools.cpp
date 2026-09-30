// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_tools.h"

#include "note_blocks.h"
#include "note_host.h"
#include "note_page_item.h"
#include "note_evidence.h"
#include "note_agent.h"
#include "agent_schema.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

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
                      {QStringLiteral("uid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                      {QStringLiteral("text"), edit.value(QStringLiteral("text")).toString()}};
    if (type == QLatin1String("check")) {
        block.insert(QStringLiteral("checked"), edit.value(QStringLiteral("checked")).toBool());
        block.insert(QStringLiteral("level"), 0);
    }
    return block;
}

void preserveSessions(const QList<Entry> &entries, QJsonObject *created)
{
    QJsonArray sources;
    for (const auto &entry : entries) {
        const auto existing = entry.block.value(QStringLiteral("session_sources")).toArray();
        if (!existing.isEmpty()) {
            for (const auto &source : existing) sources.append(source);
        } else if (entry.block.contains(QStringLiteral("transcript"))
                   || entry.block.contains(QStringLiteral("audio"))) {
            QJsonObject source;
            for (const auto *key : {"uid", "transcript", "audio", "session_started_at", "session_scenario"}) {
                const auto name = QString::fromLatin1(key);
                if (entry.block.contains(name)) source.insert(name, entry.block.value(name));
            }
            sources.append(source);
        }
    }
    if (sources.isEmpty()) return;
    created->insert(QStringLiteral("session_sources"), sources);
    if (sources.size() == 1) {
        const auto source = sources.first().toObject();
        for (auto it = source.begin(); it != source.end(); ++it) created->insert(it.key(), it.value());
    } else {
        QStringList transcripts;
        for (const auto &source : sources)
            transcripts.append(QStringLiteral("Session %1:\n%2").arg(
                source.toObject().value(QStringLiteral("uid")).toString(),
                source.toObject().value(QStringLiteral("transcript")).toString()));
        created->insert(QStringLiteral("transcript"), transcripts.join(QStringLiteral("\n\n")));
    }
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
    m_snapshotRevision = revisionToken();
    return noteEvidence(m_host, budget);
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
        "- {\"op\":\"set_checked\",\"block\":1,\"checked\":true} — mark a checklist item done.\n"
        "Block numbers always refer to the snapshot, even after earlier edits in the same "
        "batch. The whole batch is validated before any change. At most 256 operations and "
        "500 resulting blocks; texts at most 65536 characters. Session transcripts and "
        "recordings survive replace_page. Do not mark a proposal as a decision or invent facts.");
}

Ai::EditOutcome NoteTools::apply(const QJsonArray &edits)
{
    Ai::EditOutcome outcome;
    const QString schemaError = QindaTK::validateAgentJson(
        QJsonObject{{QStringLiteral("edits"), edits}}, noteEditsSchema());
    const QString blocked = m_guard ? m_guard() : QString();
    if (!schemaError.isEmpty() || !blocked.isEmpty()
        || QJsonDocument(edits).toJson(QJsonDocument::Compact).size() > 262144) {
        outcome.errors.append(!schemaError.isEmpty() ? schemaError : !blocked.isEmpty() ? blocked
            : QObject::tr("The edit batch is too large (256 KiB maximum)."));
        return outcome;
    }
    NotePageItem *item = m_host->pageItem();
    const NotePage *page = m_host->currentPage();
    if (item == nullptr || page == nullptr || page->id != m_snapshotPage) {
        outcome.errors.append(QObject::tr("The page changed since the assistant read it; ask "
                                          "again."));
        return outcome;
    }
    const QJsonArray current = blocksArray(page->envelope.blocksJson);
    if (current != m_snapshot || (!m_snapshotRevision.isEmpty() && m_snapshotRevision != revisionToken())) {
        outcome.errors.append(QObject::tr("The page changed since the assistant read it; ask again."));
        return outcome;
    }
    QList<Entry> entries;
    for (qsizetype i = 0; i < current.size(); ++i) {
        // A block keeps its snapshot identity only while it is unchanged
        // (live notes may have rewritten it meanwhile).
        const bool same = i < m_snapshot.size() && m_snapshot.at(i) == current.at(i);
        entries.append({same ? int(i) : -2, current.at(i).toObject()});
    }
    QString newTitle = page->title;
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QString op = edit.value(QStringLiteral("op")).toString();
        const int block = edit.value(QStringLiteral("block")).toInt(-1);
        if (op == QLatin1String("replace_block") || op == QLatin1String("delete_block")
            || op == QLatin1String("set_checked")) {
            const int at = findEntry(entries, block);
            if (at < 0 || !isTyped(entries.at(at).block)) {
                outcome.errors.append(QObject::tr("%1: block %2 is not a text block on the page "
                                                  "as it is now").arg(op).arg(block));
                continue;
            }
            if (op == QLatin1String("delete_block")) {
                if (entries.at(at).block.contains(QStringLiteral("transcript"))
                    || entries.at(at).block.contains(QStringLiteral("audio"))) {
                    outcome.errors.append(QObject::tr("Keep session evidence: rewrite this block instead of deleting it."));
                    continue;
                }
                entries.removeAt(at);
                outcome.changes.append(QObject::tr("deleted block %1").arg(block));
            } else if (op == QLatin1String("set_checked")) {
                if (entries.at(at).block.value(QStringLiteral("type")).toString() != QLatin1String("check")) {
                    outcome.errors.append(QObject::tr("set_checked needs a checklist item."));
                    continue;
                }
                entries[at].block.insert(QStringLiteral("checked"), edit.value(QStringLiteral("checked")));
                outcome.changes.append(QObject::tr("updated checklist item %1").arg(block));
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
            QJsonObject created{{QStringLiteral("type"), QStringLiteral("markdown")},
                                {QStringLiteral("uid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                {QStringLiteral("text"),
                                 edit.value(QStringLiteral("markdown")).toString()}};
            preserveSessions(entries, &created);
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [](const Entry &e) { return isTyped(e.block); }),
                          entries.end());
            setBlockRect(&created, QRectF(kMargin, top < 0 ? kBlockTop : top,
                                          item->pageWidth() - 2 * kMargin, 0));
            entries.prepend({-1, created});
            outcome.changes.append(QObject::tr("replaced the page's text"));
        } else if (op == QLatin1String("rename_page")) {
            newTitle = edit.value(QStringLiteral("title")).toString().trimmed();
            if (newTitle.isEmpty()) {
                outcome.errors.append(QObject::tr("A page title cannot be blank."));
            } else {
                outcome.changes.append(QObject::tr("renamed the page “%1”").arg(newTitle));
            }
        } else {
            outcome.errors.append(QObject::tr("unknown operation “%1”").arg(op));
        }
    }
    if (entries.size() > 500) outcome.errors.append(QObject::tr("At most 500 blocks after an AI edit."));
    // AGENT-GUARD: validation is atomic. In particular, a valid first op
    // followed by an invalid target never changes the page or its title.
    if (!outcome.errors.isEmpty()) { outcome.changes.clear(); return outcome; }
    if (!outcome.changes.isEmpty()) {
        QJsonArray blocks;
        for (const Entry &entry : entries) {
            blocks.append(entry.block);
        }
        reflow(&blocks, item->pageWidth());
        if (!item->replaceContent(blocks, newTitle, QObject::tr("Assistant Edit"))) {
            outcome.changes.clear();
            outcome.errors.append(QObject::tr("No content changed, or the page is being edited."));
            return outcome;
        }
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
