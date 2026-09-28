// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's live notes (D-057, note_live_session.h): the commands, the
// AI providers dialog request, and the page side of a session — the page
// and markdown block the notes go into, reading them back (user edits
// win), writing each revision as one undo step, and the transcript stored
// on the block ("transcript", not painted) when the session ends.
//
// AGENT-GUARD: every method re-resolves the LiveTarget by ids; the page
// may have been renamed, moved, deleted or navigated away from since the
// session started.

#include "note_host.h"

#include "navigator_model.h"
#include "note_blocks.h"
#include "note_live_session.h"
#include "note_page_item.h"

#include "model_catalog.h"
#include "paths.h"
#include "provider_settings.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// What the block says before the first revision; liveNotesText() reports
// it as "no notes yet" so the model never treats it as the user's text.
QString placeholder()
{
    return QObject::tr("# Live notes\n\n*Listening — the notes appear here and keep improving as "
                       "the session goes on.*");
}

int blockIndex(const QJsonArray &blocks, const QString &uid)
{
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        if (blocks.at(i).toObject().value(QStringLiteral("uid")).toString() == uid) {
            return int(i);
        }
    }
    return -1;
}

} // namespace

void NoteHost::createLiveNotes()
{
    const QString configRoot = Core::configDirFor(Core::appLeaf());
    m_live = new LiveNotesSession(this, m_catalog, configRoot + QStringLiteral("/live-notes.json"),
                                  this);
    m_providerSettings = new Ai::ProviderSettings(m_catalog, this);

    Quick::ActionModel *model = actions();
    ActionEntry *panel = model->add(QStringLiteral("noteLiveNotesAction"), tr("&Live Notes"),
                                    QKeySequence(QStringLiteral("Ctrl+Shift+L")),
                                    QStringLiteral("audio-lines"));
    panel->setCheckable(true);
    connect(panel, &ActionEntry::toggled, this,
            [this](bool visible) { emit liveNotesToggleRequested(visible); });
    connect(model->add(QStringLiteral("noteLiveStartAction"), tr("&Start Live Notes"), {},
                       QStringLiteral("mic")),
            &ActionEntry::triggered, this, [this] {
                emit liveNotesToggleRequested(true);
                if (m_live->state() == QLatin1String("paused")) {
                    m_live->resume();
                } else {
                    m_live->start();
                }
            });
    connect(model->add(QStringLiteral("noteLivePauseAction"), tr("&Pause Live Notes"), {},
                       QStringLiteral("pause")),
            &ActionEntry::triggered, m_live, &LiveNotesSession::pause);
    connect(model->add(QStringLiteral("noteLiveStopAction"), tr("S&top Live Notes"), {},
                       QStringLiteral("square")),
            &ActionEntry::triggered, m_live, &LiveNotesSession::stop);
    connect(model->add(QStringLiteral("aiProvidersAction"), tr("AI &Providers…"), {},
                       QStringLiteral("key-round")),
            &ActionEntry::triggered, this, [this] { emit providersRequested(); });
    connect(m_live, &LiveNotesSession::stateChanged, this, &NoteHost::syncLiveActions);
    syncLiveActions();

    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    for (const char *id : {"noteLiveNotesAction", "noteLiveStartAction", "noteLivePauseAction",
                           "noteLiveStopAction"}) {
        menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QString::fromLatin1(id)));
    }
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QStringLiteral("aiProvidersAction")));
}

void NoteHost::syncLiveActions()
{
    const QString state = m_live->state();
    const bool idle = state == QLatin1String("idle");
    actions()->setEnabled(QStringLiteral("noteLiveStartAction"),
                          idle || state == QLatin1String("paused"));
    actions()->setEnabled(QStringLiteral("noteLivePauseAction"), state == QLatin1String("listening"));
    actions()->setEnabled(QStringLiteral("noteLiveStopAction"),
                          !idle && state != QLatin1String("finishing"));
}

LiveTarget NoteHost::beginLiveNotes(const QString &topic)
{
    Notebook *notebook = m_document->notebook();
    NotePage *page = currentPage();
    const bool reuse = page != nullptr && blocksArray(page->envelope.blocksJson).isEmpty()
        && page->envelope.ink.strokes().isEmpty();
    if (!reuse) {
        newPage(); // opens it
        page = currentPage();
    }
    if (page == nullptr) {
        return {};
    }
    const QString title = topic.trimmed().isEmpty()
        ? tr("Live notes %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
        : topic.trimmed();
    notebook->renamePage(m_currentSectionId, m_currentPageId, title);
    m_navigator->rebuild();
    emit currentPageChanged();

    const LiveTarget target{m_currentSectionId, m_currentPageId, Notebook::newId()};
    if (m_pageItem != nullptr && m_pageItem->page() == page) {
        m_pageItem->appendMarkdownBlock(target.blockUid, placeholder());
    } else {
        QJsonArray blocks = blocksArray(page->envelope.blocksJson);
        blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")},
                                  {QStringLiteral("uid"), target.blockUid},
                                  {QStringLiteral("text"), placeholder()}});
        page->envelope.blocksJson = withBlocksArray(page->envelope.blocksJson, blocks);
        m_document->setDirty(true);
    }
    return target;
}

QString NoteHost::liveBlockField(const LiveTarget &target, const QString &field) const
{
    const NotePage *page = m_document->notebook()->pageById(target.sectionId, target.pageId);
    if (page == nullptr) {
        return {};
    }
    const QJsonArray blocks = blocksArray(page->envelope.blocksJson);
    const int index = blockIndex(blocks, target.blockUid);
    return index < 0 ? QString() : blocks.at(index).toObject().value(field).toString();
}

QString NoteHost::liveNotesText(const LiveTarget &target) const
{
    const QString text = liveBlockField(target, QStringLiteral("text"));
    return text == placeholder() ? QString() : text;
}

QString NoteHost::liveTranscript(const LiveTarget &target) const
{
    return liveBlockField(target, QStringLiteral("transcript"));
}

bool NoteHost::writeLiveBlock(const LiveTarget &target, const QJsonObject &fields, bool undoable)
{
    Notebook *notebook = m_document->notebook();
    NotePage *page = notebook->pageById(target.sectionId, target.pageId);
    if (page == nullptr) {
        return true; // the page was deleted: nothing to write, nothing to retry
    }
    const bool onScreen = m_pageItem != nullptr && m_pageItem->page() == page;
    if (onScreen && undoable) {
        int index = m_pageItem->blockIndexForUid(target.blockUid);
        if (index < 0) {
            // The user deleted the notes block mid-session: bring it back.
            m_pageItem->appendMarkdownBlock(target.blockUid, QString());
            index = m_pageItem->blockIndexForUid(target.blockUid);
        }
        return m_pageItem->setBlockFields(index, fields, tr("Live Notes Update"));
    }
    QJsonArray blocks = blocksArray(page->envelope.blocksJson);
    int index = blockIndex(blocks, target.blockUid);
    if (index < 0) {
        blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")},
                                  {QStringLiteral("uid"), target.blockUid}});
        index = int(blocks.size()) - 1;
    }
    QJsonObject block = blocks.at(index).toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        block.insert(it.key(), it.value());
    }
    blocks.replace(index, block);
    page->envelope.blocksJson = withBlocksArray(page->envelope.blocksJson, blocks);
    m_document->setDirty(true);
    m_document->notifyContentChanged();
    if (onScreen) {
        m_persistTimer->start();
    } else if (NoteSection *section = notebook->sectionById(target.sectionId);
               section != nullptr && notebook->isOpen()) {
        QString error;
        if (!notebook->persistPage(*section, *page, &error)) {
            showStatusMessage(tr("Save failed: %1").arg(error), 8000);
        }
    }
    return true;
}

bool NoteHost::applyLiveNotes(const LiveTarget &target, const QString &notes)
{
    return writeLiveBlock(target, QJsonObject{{QStringLiteral("text"), notes}}, true);
}

void NoteHost::storeLiveTranscript(const LiveTarget &target, const QString &transcript)
{
    // Not an undo step: the transcript is a record of the session, not an
    // edit the user made.
    writeLiveBlock(target, QJsonObject{{QStringLiteral("transcript"), transcript}}, false);
}

void NoteHost::endLiveNotes(const LiveTarget &target, bool wroteNotes)
{
    if (!target.isValid() || wroteNotes
        || liveBlockField(target, QStringLiteral("text")) != placeholder()) {
        return;
    }
    applyLiveNotes(target, tr("# Live notes\n\n*No notes were written: nothing substantive was "
                              "heard, or the session stopped before the first update.*"));
}

} // namespace QindaOffice::Apps::Notes
