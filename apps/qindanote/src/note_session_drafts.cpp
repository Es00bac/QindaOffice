// SPDX-License-Identifier: GPL-3.0-or-later
// Reviewable follow-ups from the current page (M59, D-065). No draft
// changes notes or starts a handoff until the human accepts it. The source
// revision includes page identity and all content changes, but not focus.
#include "note_live_session.h"
#include "note_host.h"
#include "note_evidence.h"
#include "note_blocks.h"
#include "live_notes_followup.h"
#include "model_catalog.h"
#include "paths.h"
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
namespace QindaOffice::Apps::Notes {
namespace {
QDate sourceDate(const NoteHost *host)
{
    QDate date;
    if (const auto *page = host->currentPage())
        for (const auto &entry : blocksArray(page->envelope.blocksJson)) {
            const auto start = QDateTime::fromString(
                entry.toObject().value(QStringLiteral("session_started_at")).toString(), Qt::ISODate);
            if (!start.isValid()) continue;
            const auto next = start.toLocalTime().date();
            if (date.isValid() && date != next) return {}; // no single date anchor
            date = next;
        }
    return date;
}
QString taskPreview(const QList<Ai::LiveNotesExtras::ActionItem> &items)
{
    QString out = QObject::tr("## Review tasks\n");
    for (const auto &item : items) {
        out += QStringLiteral("\n- [ ] %1\n  - %2: %3\n  - %4: %5\n")
            .arg(item.title, QObject::tr("Owner"), item.owner.isEmpty() ? QObject::tr("Unassigned") : item.owner,
                QObject::tr("Due"), item.due.isValid() ? item.due.toString(Qt::ISODate) : QObject::tr("No date recorded"));
        if (!item.notes.isEmpty()) out += QStringLiteral("  - %1\n").arg(item.notes);
    }
    return out;
}
}
QVariantList LiveNotesSession::followUpKinds() const { return Ai::LiveNotesFollowUp::kinds(); }
void LiveNotesSession::setWorking(bool on)
{
    if (m_working != on) { m_working = on; emit workingChanged(); }
}
void LiveNotesSession::withModel(const std::function<void(Ai::ChatProvider *)> &use)
{
    const auto config = m_catalog ? m_catalog->configFor(m_modelId) : std::nullopt;
    if (!config) {
        setStatus({}, tr("Choose the notes model first (Live Notes ▸ Settings)."));
        use(nullptr); // AGENT-GUARD: every failure must release the busy state
        return;
    }
    QPointer<LiveNotesSession> self(this);
    const QString modelId = m_modelId;
    Ai::ApiKeyResolver::resolve(Ai::ModelCatalog::keySources(*config), this,
        [self, modelId, use](const QString &key, const QString &) {
        if (!self) return;
        auto *provider = self->m_catalog->createProvider(modelId, key, self);
        if (!provider) self->setStatus({}, tr("The notes model is not available."));
        use(provider);
    });
}
void LiveNotesSession::clearDraft()
{
    const bool hadDraft = m_working || !m_draftText.isEmpty();
    ++m_draftGeneration;
    const auto run = m_draftRun;
    m_draftRun = nullptr;
    if (run) { run->cancel(); run->deleteLater(); }
    m_draftKind.clear(); m_draftTitle.clear(); m_draftText.clear(); m_draftRevision.clear();
    m_draftTasks.clear();
    setWorking(false);
    emit draftChanged();
    if (hadDraft) setStatus(tr("Draft discarded."));
}
void LiveNotesSession::makeStudyPage() { makeDraft(QStringLiteral("study")); }
void LiveNotesSession::sendActionItemsToMail() { makeDraft(QStringLiteral("tasks")); }
void LiveNotesSession::makeDraft(const QString &kind)
{
    if (m_working) return;
    if (!Ai::LiveNotesFollowUp::known(kind)) { setStatus({}, tr("Unknown follow-up kind.")); return; }
    if (active()) { setStatus({}, tr("Stop live notes before preparing a follow-up draft.")); return; }
    if (m_host->currentPageNotes().trimmed().isEmpty()) {
        setStatus({}, tr("This page has no typed notes to use.")); return;
    }
    if (m_host->pageItem() && (m_host->pageItem()->editingBlock() >= 0 || m_host->pageItem()->hasActiveGesture())) {
        setStatus({}, tr("Finish the current edit before preparing a draft.")); return;
    }
    clearDraft();
    m_host->actions()->setChecked(QStringLiteral("noteSessionPanelAction"), true);
    const auto *tools = m_host->assistant()->documentTools();
    m_draftRevision = tools->undoRevisionToken();
    m_draftKind = kind;
    m_draftTitle = Ai::LiveNotesFollowUp::title(kind, m_host->currentPageTitle());
    const QString evidence = noteEvidence(m_host, 60000);
    const Ai::ChatRequest request = kind == QLatin1String("tasks")
        ? Ai::LiveNotesExtras::actionItemsRequest(evidence, sourceDate(m_host))
        : Ai::LiveNotesFollowUp::request(kind, m_host->currentPageTitle(), evidence);
    const int generation = m_draftGeneration;
    setWorking(true);
    setStatus(tr("Preparing a draft for you to review…"));
    emit draftChanged();
    QPointer<LiveNotesSession> self(this);
    withModel([self, generation, request](Ai::ChatProvider *provider) {
        if (!self || generation != self->m_draftGeneration) { if (provider) provider->deleteLater(); return; }
        if (!provider) { self->setWorking(false); return; }
        auto *run = provider->start(request);
        if (!run) { provider->deleteLater(); self->setWorking(false);
            self->setStatus({}, tr("The model could not start the request.")); return; }
        run->setParent(provider);
        self->m_draftRun = run;
        auto complete = [self, provider, generation](const QString &text, const QString &error) {
            provider->deleteLater();
            if (!self || generation != self->m_draftGeneration) return;
            self->m_draftRun = nullptr;
            self->setWorking(false);
            if (self->m_draftRevision != self->m_host->assistant()->documentTools()->undoRevisionToken()) {
                self->clearDraft(); self->setStatus({}, tr("The source notes changed; generate a fresh draft.")); return;
            }
            if (!error.isEmpty()) { self->setStatus({}, tr("Could not prepare the draft: %1").arg(error)); return; }
            if (text.size() > 65536) {
                self->setStatus({}, tr("The model returned an oversized draft.")); return;
            }
            if (self->m_draftKind == QLatin1String("tasks")) {
                self->m_draftTasks = Ai::LiveNotesExtras::parseActionItems(text);
                if (self->m_draftTasks.isEmpty()) { self->setStatus({}, tr("No valid action items were found.")); return; }
                self->m_draftText = taskPreview(self->m_draftTasks);
            } else {
                self->m_draftText = Ai::LiveNotesPrompt::sanitizeReply(text);
                if (self->m_draftText.size() > 65536) {
                    self->m_draftText.clear(); self->setStatus({}, tr("The model returned an oversized draft.")); return;
                }
            }
            if (self->m_draftText.trimmed().isEmpty()) { self->setStatus({}, tr("The model returned an empty draft.")); return; }
            self->setStatus(tr("Review the draft, then add it or discard it. Your source notes are unchanged."));
            emit self->draftChanged();
        };
        connect(run, &Ai::ChatRun::finished, self, [complete](const Ai::ChatResult &result) { complete(result.text, {}); });
        connect(run, &Ai::ChatRun::failed, self, [complete](const Ai::ChatError &error) { complete({}, error.message); });
        // One bounded request; a stalled model must not hold the panel busy forever.
        QTimer::singleShot(300000, run, [run] { run->cancel(); });
    });
}
bool LiveNotesSession::acceptDraft()
{
    if (m_working || m_draftText.isEmpty()) return false;
    if (m_draftRevision != m_host->assistant()->documentTools()->undoRevisionToken()) {
        clearDraft(); setStatus({}, tr("The source notes changed; generate a fresh draft.")); return false;
    }
    const QString title = m_draftTitle, text = m_draftText;
    if (m_draftKind != QLatin1String("tasks")) {
        const QString pageId = m_host->addNotesPage(title, text);
        if (pageId.isEmpty()) { setStatus({}, tr("The draft could not be added.")); return false; }
        clearDraft();
        setStatus(tr("Added “%1” after the source page.").arg(title));
        emit m_host->sessionChanged();
        return true;
    }
    const QString dir = Core::stateDirFor(Core::appLeaf()) + QStringLiteral("/handoff");
    if (!QDir().mkpath(dir)) { setStatus({}, tr("Could not create the task handoff folder.")); return false; }
    const QString path = dir + QStringLiteral("/action-items-%1.ics")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSaveFile file(path);
    const QByteArray data = Ai::LiveNotesExtras::actionItemsIcs(m_draftTasks, m_host->currentPageTitle());
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setStatus({}, tr("Could not save the reviewed tasks: %1").arg(file.errorString())); return false;
    }
    // The same headless handoff guard as NoteHost::openInWrite. Only the
    // human's Accept command may reach this process-launching boundary.
    const QString mail = qEnvironmentVariableIsSet("QQO_NO_HANDOFF") ? QString()
        : QStandardPaths::findExecutable(QStringLiteral("qindamail"));
    clearDraft(); // consumes it even if QindaMail is absent; no duplicate Accept
    if (mail.isEmpty() || !QProcess::startDetached(mail, {path})) {
        setStatus(tr("Reviewed tasks saved to %1; QindaMail was not opened.").arg(path));
    } else setStatus(tr("Reviewed tasks opened in QindaMail."));
    return true;
}
}
