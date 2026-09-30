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
#include "agent_provider.h"

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
#include <tuple>

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
                } else if (m_live->askBeforeStart() && !m_live->active()) {
                    emit liveNotesSetupRequested(); // the dialog's Start calls start()
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
    // M56 (D-061): the flag hotkey, catch-up, the Session panel and its
    // page commands.
    connect(model->add(QStringLiteral("noteLiveFlagAction"), tr("&This Matters"),
                       QKeySequence(Qt::Key_F9), QStringLiteral("star")),
            &ActionEntry::triggered, m_live, &LiveNotesSession::flag);
    connect(model->add(QStringLiteral("noteLiveCatchUpAction"), tr("&Catch Me Up"), {},
                       QStringLiteral("history")),
            &ActionEntry::triggered, this, [this] {
                emit liveNotesToggleRequested(true);
                m_live->catchUp(5);
            });
    ActionEntry *sessionPanel = model->add(QStringLiteral("noteSessionPanelAction"), tr("Session &Review"),
                                           {}, QStringLiteral("clipboard-list"));
    sessionPanel->setCheckable(true);
    connect(sessionPanel, &ActionEntry::toggled, this,
            [this](bool visible) { emit sessionPanelToggleRequested(visible); });
    connect(model->add(QStringLiteral("noteStudyPageAction"), tr("Make a &Study Page"), {},
                       QStringLiteral("book-open")),
            &ActionEntry::triggered, m_live, &LiveNotesSession::makeStudyPage);
    connect(model->add(QStringLiteral("noteActionItemsToMailAction"), tr("Review Action Items for &QindaMail"),
                       {}, QStringLiteral("list-todo")),
            &ActionEntry::triggered, m_live, &LiveNotesSession::sendActionItemsToMail);
    for (const auto &[id, kind, title] : QList<std::tuple<QString, QString, QString>>{
        {QStringLiteral("noteMeetingRecapAction"), QStringLiteral("recap"), tr("Draft Meeting Recap")},
        {QStringLiteral("noteProjectPlanAction"), QStringLiteral("plan"), tr("Draft Project Next Steps")},
        {QStringLiteral("noteTroubleshootingAction"), QStringLiteral("troubleshooting"), tr("Draft Troubleshooting Brief")},
        {QStringLiteral("noteFollowUpQuestionsAction"), QStringLiteral("questions"), tr("Draft Questions for Next Time")}}) {
        connect(model->add(id, title, {}, QStringLiteral("sparkles")), &ActionEntry::triggered,
            m_live, [this, kind] { m_live->makeDraft(kind); });
        menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(id));
    }
    connect(model->add(QStringLiteral("noteAcceptDraftAction"), tr("Accept Reviewed Draft"), {}, QStringLiteral("check")),
        &ActionEntry::triggered, m_live, [this] { static_cast<void>(m_live->acceptDraft()); });
    connect(model->add(QStringLiteral("noteDiscardDraftAction"), tr("Discard Follow-Up Draft"), {}, QStringLiteral("x")),
        &ActionEntry::triggered, m_live, &LiveNotesSession::clearDraft);
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QStringLiteral("noteAcceptDraftAction")));
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QStringLiteral("noteDiscardDraftAction")));
    const auto syncDraftActions = [this] {
        const bool ready = !m_live->working() && !m_live->active();
        actions()->setEnabled({QStringLiteral("noteMeetingRecapAction"), QStringLiteral("noteProjectPlanAction"),
            QStringLiteral("noteTroubleshootingAction"), QStringLiteral("noteFollowUpQuestionsAction"),
            QStringLiteral("noteStudyPageAction"), QStringLiteral("noteActionItemsToMailAction")}, ready);
        actions()->setEnabled(QStringLiteral("noteAcceptDraftAction"), ready && !m_live->draftText().isEmpty());
        actions()->setEnabled(QStringLiteral("noteDiscardDraftAction"), m_live->working() || !m_live->draftText().isEmpty());
    };
    connect(m_live, &LiveNotesSession::draftChanged, this, syncDraftActions);
    connect(m_live, &LiveNotesSession::workingChanged, this, syncDraftActions);
    connect(m_live, &LiveNotesSession::stateChanged, this, syncDraftActions);
    syncDraftActions();
    connect(this, &NoteHost::currentPageChanged, m_live, &LiveNotesSession::clearDraft);
    if (agentProvider()) {
        connect(m_live, &LiveNotesSession::stateChanged, agentProvider(), &QindaTK::AgentProvider::changed);
        connect(m_live, &LiveNotesSession::transcriptChanged, agentProvider(), &QindaTK::AgentProvider::changed);
        connect(m_live, &LiveNotesSession::draftChanged, agentProvider(), &QindaTK::AgentProvider::changed);
        connect(m_live, &LiveNotesSession::workingChanged, agentProvider(), &QindaTK::AgentProvider::changed);
    }
    connect(model->add(QStringLiteral("noteSplitByTopicAction"), tr("Split into Pages by &Topic"), {},
                       QStringLiteral("split")),
            &ActionEntry::triggered, this, &NoteHost::splitCurrentPage);
    connect(model->add(QStringLiteral("noteExportMarkdownAction"), tr("Export &Markdown…"), {},
                       QStringLiteral("file-text")),
            &ActionEntry::triggered, this, [this] {
                emit exportMarkdownRequested(QStringLiteral("%1.md").arg(currentPageTitle()));
            });
    connect(model->add(QStringLiteral("noteOpenInWriteAction"), tr("Open in Qinda&Write"), {},
                       QStringLiteral("square-pen")),
            &ActionEntry::triggered, this, &NoteHost::openInWrite);
    connect(model->add(QStringLiteral("noteExportAnkiAction"), tr("Export Flashcards for &Anki…"), {},
                       QStringLiteral("layers")),
            &ActionEntry::triggered, this, [this] {
                emit exportAnkiRequested(QStringLiteral("%1.txt").arg(currentPageTitle()));
            });
    // The Session panel re-reads the page after edits, coalesced.
    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setSingleShot(true);
    m_sessionTimer->setInterval(300);
    connect(m_sessionTimer, &QTimer::timeout, this, &NoteHost::sessionChanged);
    connect(this, &NoteHost::currentPageChanged, m_sessionTimer, qOverload<>(&QTimer::start));
    connect(m_document, &Core::Document::contentChanged, m_sessionTimer, qOverload<>(&QTimer::start));
    connect(model->add(QStringLiteral("aiProvidersAction"), tr("AI &Providers…"), {},
                       QStringLiteral("key-round")),
            &ActionEntry::triggered, this, [this] { emit providersRequested(); });
    connect(m_live, &LiveNotesSession::stateChanged, this, &NoteHost::syncLiveActions);
    syncLiveActions();

    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    for (const char *id : {"noteLiveNotesAction", "noteLiveStartAction", "noteLivePauseAction",
                           "noteLiveStopAction", "noteLiveFlagAction", "noteLiveCatchUpAction"}) {
        menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QString::fromLatin1(id)));
    }
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    for (const char *id : {"noteSessionPanelAction", "noteStudyPageAction", "noteActionItemsToMailAction",
                           "noteSplitByTopicAction"}) {
        menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QString::fromLatin1(id)));
    }
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    menuTree()->appendTo(QStringLiteral("aiMenu"), MenuNode::action(QStringLiteral("aiProvidersAction")));
    // Export sits with Export PDF in the Page menu.
    for (const char *id : {"noteExportMarkdownAction", "noteOpenInWriteAction", "noteExportAnkiAction"}) {
        menuTree()->insertInto(QStringLiteral("notePageMenu"), QString(), MenuNode::action(QString::fromLatin1(id)));
    }
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
    actions()->setEnabled(QStringLiteral("noteLiveFlagAction"), state == QLatin1String("listening"));
    actions()->setEnabled(QStringLiteral("noteLiveCatchUpAction"), !idle);
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
    writeLiveBlock(target, QJsonObject{
        {QStringLiteral("session_started_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("session_scenario"), m_live->setup().value(QStringLiteral("scenario")).toString()}}, false);
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
