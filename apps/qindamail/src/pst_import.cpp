// SPDX-License-Identifier: GPL-3.0-or-later
// The Outlook data file import (pst_import.h).

#include "pst_import.h"

#include "mail_service.h"
#include "outlook_items.h"
#include "outlook_records.h"
#include "provider_calls.h"
#include "pst_reader.h"
#include "rfc822_writer.h"

#include <qindatk/local_productivity_store.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimer>

namespace QindaOffice::Apps::Mail {

using namespace QindaOffice::Mail;

namespace {

constexpr int kMaxFolderDepth = 64;
constexpr int kMaxFolders = 100000;
constexpr int kMaxWarnings = 50;

// A folder name that keeps the local folder keys and hierarchy intact.
QString cleanName(const QString &name, const QString &fallback)
{
    QString out = name;
    out.replace(QLatin1Char('/'), QLatin1Char('-'));
    out.replace(QLatin1Char(':'), QLatin1Char('-'));
    out = out.simplified().left(120);
    return out.isEmpty() ? fallback : out;
}

} // namespace

struct PstImporter::Job {
    struct PendingFolder {
        quint32 nid = 0;
        QString parentPath;
        int depth = 0;
    };
    struct Item {
        quint32 nid = 0;
        QString folder;      // the local folder its mail goes to
    };
    QString requestId;
    QString path;
    Pst::PstFile file;
    QString rootFolder;
    QList<PendingFolder> scan;
    QSet<quint32> seenFolders;
    QSet<QString> usedPaths;
    QList<Item> items;
    qsizetype next = 0;
    QList<Contact> contacts;
    QHash<QString, int> taskOrder;         // existing task id → its order
    QHash<QString, int> nextOrder;         // status → next free order
    // M89 (D-097): this slice's records, written as ONE store change per
    // collection (LocalProductivityStore::upsertMany, QindaTK D-324).
    QVariantList pendingMessages, pendingEvents, pendingTasks;
    int folders = 0, messages = 0, people = 0, groups = 0, events = 0, tasks = 0, skipped = 0;
    QStringList warnings;
    bool cancelled = false;

    void warn(const QString &text)
    {
        if (warnings.size() < kMaxWarnings) warnings.append(text);
    }
};

PstImporter::PstImporter(MailService *service)
    : QObject(service)
    , m_service(service)
{
}

PstImporter::~PstImporter() = default;

bool PstImporter::running() const
{
    return m_job != nullptr;
}

double PstImporter::progress() const
{
    if (m_job == nullptr) return m_phase == QLatin1String("done") ? 1.0 : 0.0;
    return m_total > 0 ? double(m_processed) / double(m_total) : 0.0;
}

QVariantMap PstImporter::counts() const
{
    if (m_job == nullptr) return m_summary;
    return {{QStringLiteral("folders"), m_job->folders}, {QStringLiteral("messages"), m_job->messages},
            {QStringLiteral("contacts"), m_job->people}, {QStringLiteral("groups"), m_job->groups},
            {QStringLiteral("events"), m_job->events}, {QStringLiteral("tasks"), m_job->tasks},
            {QStringLiteral("skipped"), m_job->skipped}};
}

void PstImporter::setPhase(const QString &phase)
{
    if (phase == m_phase) return;
    m_phase = phase;
    emit stateChanged();
}

QString PstImporter::rootFolderFor(const QByteArray &recordKey, const QString &baseName) const
{
    const QVariantMap imports = m_service->setting(QStringLiteral("outlookImports")).toMap();
    const QString key = QString::fromLatin1(recordKey.toHex());
    if (!key.isEmpty() && imports.contains(key)) return imports.value(key).toString();
    const QString base = cleanName(baseName, tr("Outlook"));
    QStringList taken = m_service->localMailboxes();
    for (const QVariant &folder : imports) taken.append(folder.toString());
    QString candidate = base;
    for (int n = 2; taken.contains(candidate, Qt::CaseInsensitive); ++n) candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
    return candidate;
}

QString PstImporter::start(const QUrl &file)
{
    if (m_service == nullptr) return {};
    if (running()) {
        m_lastError = tr("An Outlook import is already running");
        emit stateChanged();
        return {};
    }
    auto job = std::make_unique<Job>();
    job->requestId = ProviderCalls::newRequestId(QStringLiteral("pst"));
    job->path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    m_fileName = QFileInfo(job->path).fileName();
    m_total = 0;
    m_processed = 0;
    m_currentFolder.clear();
    m_summary.clear();
    m_lastError.clear();
    m_job = std::move(job);
    m_phase = QStringLiteral("scanning");
    emit stateChanged();
    emit progressChanged();
    const QString requestId = m_job->requestId;
    QString error;
    if (!m_job->file.open(m_job->path, &error)) {
        QTimer::singleShot(0, this, [this, error] { finish(QStringLiteral("rejected"), error); });
        return requestId;
    }
    m_job->rootFolder = rootFolderFor(m_job->file.recordKey(), QFileInfo(m_job->path).completeBaseName());
    m_service->m_bulkSources = true;
    m_job->scan.append({m_job->file.ipmSubtree(), QString(), 0});
    for (const QVariant &value : m_service->store()->tasks()) {
        const QVariantMap task = value.toMap();
        const int order = task.value(QStringLiteral("order")).toInt();
        m_job->taskOrder.insert(task.value(QStringLiteral("id")).toString(), order);
        int &next = m_job->nextOrder[task.value(QStringLiteral("status")).toString()];
        next = qMax(next, order + 1);
    }
    QTimer::singleShot(0, this, [this] { step(); });
    return requestId;
}

void PstImporter::cancel()
{
    if (m_job != nullptr) m_job->cancelled = true;
}

void PstImporter::step()
{
    if (m_job == nullptr) return;
    if (m_job->cancelled) {
        finish(QStringLiteral("cancelled"), tr("The import was cancelled"));
        return;
    }
    if (m_phase == QLatin1String("scanning")) scanStep();
    else importStep();
}

void PstImporter::next()
{
    // AGENT-NOTE: the views refresh once at the end (the mbox importer's
    // rule): every store write restarts the service's refresh timer, and
    // rebuilding folders and lists after every slice made the import
    // quadratic (a third of a 10 000-message import before this).
    if (m_service->m_refreshTimer != nullptr) m_service->m_refreshTimer->stop();
    QTimer::singleShot(0, this, [this] { step(); });
}

void PstImporter::scanStep()
{
    Job &job = *m_job;
    QElapsedTimer slice;
    slice.start();
    // At least one folder per turn, then as many as the slice allows.
    bool first = true;
    while (!job.scan.isEmpty() && (first || slice.elapsed() < m_sliceMs)) {
        first = false;
        const Job::PendingFolder pending = job.scan.takeFirst();
        if (job.seenFolders.contains(pending.nid) || pending.depth > kMaxFolderDepth || job.seenFolders.size() >= kMaxFolders)
            continue;
        job.seenFolders.insert(pending.nid);
        Pst::Folder folder;
        QString error;
        if (!job.file.readFolder(pending.nid, &folder, &error)) {
            ++job.skipped;
            job.warn(tr("A folder could not be read: %1").arg(error));
            continue;
        }
        QString path = job.rootFolder;
        if (pending.depth > 0) {
            ++job.folders;
            const QString base = pending.parentPath + QLatin1Char('/') + cleanName(folder.name, tr("Folder"));
            path = base;
            for (int n = 2; job.usedPaths.contains(path.toLower()); ++n) path = QStringLiteral("%1 (%2)").arg(base).arg(n);
        }
        job.usedPaths.insert(path.toLower());
        m_currentFolder = folder.name;
        for (const quint32 nid : std::as_const(folder.items)) job.items.append({nid, path});
        for (const quint32 child : std::as_const(folder.subfolders)) job.scan.append({child, path, pending.depth + 1});
    }
    m_total = int(job.items.size());
    emit progressChanged();
    if (job.scan.isEmpty()) setPhase(QStringLiteral("importing"));
    next();
}

void PstImporter::importStep()
{
    Job &job = *m_job;
    QElapsedTimer slice;
    slice.start();
    // At least one item per turn (a slice of 0 ms imports one item a turn).
    do {
        if (job.next >= job.items.size()) break;
        importItem(quint32(job.next));
        ++job.next;
        ++m_processed;
    } while (slice.elapsed() < m_sliceMs);
    flush();
    if (job.next < job.items.size()) m_currentFolder = job.items.at(job.next).folder.section(QLatin1Char('/'), -1);
    emit progressChanged();
    if (job.next >= job.items.size()) {
        finish(QStringLiteral("accepted"), QString());
        return;
    }
    next();
}

void PstImporter::importItem(quint32 index)
{
    Job &job = *m_job;
    const Job::Item work = job.items.at(index);
    QString error;
    OutlookItem item;
    std::unique_ptr<Mapi::Message> message = job.file.openMessage(work.nid, &error);
    if (!message || !readOutlookItem(*message, &item, &error)) {
        ++job.skipped;
        job.warn(tr("An item in “%1” could not be read: %2").arg(work.folder.section(QLatin1Char('/'), -1), error));
        return;
    }
    const QString key = OutlookRecords::itemKey(job.file.recordKey(), work.nid);
    switch (item.kind) {
    case OutlookItemKind::Mail: {
        const QVariantMap extra{{QStringLiteral("unread"), item.unread}, {QStringLiteral("flagged"), item.flagged},
                                {QStringLiteral("importedFrom"), QStringLiteral("outlook")}};
        const QVariantMap record = m_service->rawMessageRecord(writeRfc822(item.mail), work.folder, QString(), &error, extra);
        if (record.isEmpty()) {
            ++job.skipped;
            job.warn(tr("A message could not be kept: %1").arg(error));
        } else {
            job.pendingMessages.append(record); // counted when the slice is written (flush)
        }
        break;
    }
    case OutlookItemKind::Contact:
        item.contact.uid = OutlookRecords::uidFor(key);
        job.contacts.append(item.contact);
        ++job.people;
        break;
    case OutlookItemKind::DistList:
        item.contact.uid = OutlookRecords::uidFor(key);
        item.contact.members = OutlookRecords::groupMembers(item.members, job.file.recordKey(), m_service->contacts());
        job.contacts.append(item.contact);
        ++job.groups;
        break;
    case OutlookItemKind::Appointment: {
        QVariantMap record;
        if (!OutlookRecords::eventRecord(item.appointment, key, &record)) {
            ++job.skipped;
            job.warn(tr("“%1” has no date and was not imported").arg(item.appointment.subject));
        } else {
            job.pendingEvents.append(record);
        }
        break;
    }
    case OutlookItemKind::Task: {
        const QString id = QStringLiteral("task:") + key;
        const QString status = OutlookRecords::taskStatus(item.task);
        const int order = job.taskOrder.contains(id) ? job.taskOrder.value(id) : job.nextOrder[status]++;
        job.pendingTasks.append(OutlookRecords::taskRecord(item.task, id, order));
        break;
    }
    case OutlookItemKind::Other:
        ++job.skipped;
        if (!job.warnings.contains(tr("Notes and journal entries are not imported.")))
            job.warn(tr("Notes and journal entries are not imported."));
        break;
    }
}

void PstImporter::flush()
{
    if (m_job == nullptr) return;
    Job &job = *m_job;
    // One store change per collection and slice: the toolkit store indexes
    // the ids once and emits one dataChanged, where a write per record
    // scanned the whole list and re-ran every binding each time (D-085).
    QString error;
    const int messages = m_service->storeMessages(job.pendingMessages, &error);
    job.messages += messages;
    if (messages < job.pendingMessages.size()) {
        job.skipped += int(job.pendingMessages.size()) - messages;
        job.warn(tr("Messages could not be kept: %1").arg(error));
    }
    const auto write = [this, &job](const QString &collection, const QVariantList &records, int *count) {
        if (records.isEmpty()) return;
        if (m_service->store()->upsertMany(collection, records).value(QStringLiteral("ok")).toBool()) *count += int(records.size());
        else job.skipped += int(records.size());
    };
    write(QStringLiteral("events"), job.pendingEvents, &job.events);
    write(QStringLiteral("tasks"), job.pendingTasks, &job.tasks);
    job.pendingMessages.clear();
    job.pendingEvents.clear();
    job.pendingTasks.clear();
}

void PstImporter::finish(const QString &state, const QString &error)
{
    if (m_job == nullptr) return;
    flush(); // what a cancel or a failure left of the slice
    Job &job = *m_job;
    QString finalState = state;
    QString finalError = error;
    if (!job.contacts.isEmpty()) {
        const QVariantMap merged = m_service->contacts()->importContacts(job.contacts);
        if (!merged.value(QStringLiteral("ok")).toBool()) job.warn(merged.value(QStringLiteral("error")).toString());
    }
    m_service->m_bulkSources = false;
    m_service->syncSources();   // before the store (the commit point) is saved
    if (state != QLatin1String("rejected")) {
        QVariantMap imports = m_service->setting(QStringLiteral("outlookImports")).toMap();
        const QString key = QString::fromLatin1(job.file.recordKey().toHex());
        if (!key.isEmpty()) imports.insert(key, job.rootFolder);
        m_service->saveSetting(QStringLiteral("outlookImports"), imports);
        QString saveError;
        if (!m_service->saveStore(&saveError)) {
            finalState = QStringLiteral("rejected");
            finalError = saveError;
        }
    }
    m_service->refreshDerived();
    m_summary = {{QStringLiteral("state"), finalState},
                 {QStringLiteral("file"), m_fileName},
                 {QStringLiteral("folder"), job.rootFolder},
                 {QStringLiteral("folderKey"), QStringLiteral("acct:local:") + job.rootFolder},
                 {QStringLiteral("folders"), job.folders},
                 {QStringLiteral("messages"), job.messages},
                 {QStringLiteral("contacts"), job.people},
                 {QStringLiteral("groups"), job.groups},
                 {QStringLiteral("events"), job.events},
                 {QStringLiteral("tasks"), job.tasks},
                 {QStringLiteral("skipped"), job.skipped},
                 {QStringLiteral("total"), m_total},
                 {QStringLiteral("error"), finalError},
                 {QStringLiteral("warnings"), job.warnings}};
    const QString requestId = job.requestId;
    const bool clean = finalState == QLatin1String("accepted") && job.skipped == 0;
    m_lastError = finalError;
    m_job.reset();   // `job` dangles from here on
    m_phase = finalState == QLatin1String("accepted") ? QStringLiteral("done")
        : finalState == QLatin1String("cancelled") ? QStringLiteral("cancelled") : QStringLiteral("failed");
    emit stateChanged();
    emit progressChanged();
    emit finished(requestId, m_summary);
    if (finalState == QLatin1String("rejected")) {
        emit m_service->notify(tr("Could not import the Outlook data file"), finalError, QStringLiteral("danger"));
    } else {
        emit m_service->notify(finalState == QLatin1String("cancelled") ? tr("Outlook import cancelled") : tr("Outlook data imported"),
                               tr("%1 messages, %2 contacts, %3 events, %4 tasks")
                                   .arg(m_summary.value(QStringLiteral("messages")).toInt())
                                   .arg(m_summary.value(QStringLiteral("contacts")).toInt()
                                        + m_summary.value(QStringLiteral("groups")).toInt())
                                   .arg(m_summary.value(QStringLiteral("events")).toInt())
                                   .arg(m_summary.value(QStringLiteral("tasks")).toInt()),
                               clean ? QStringLiteral("success") : QStringLiteral("warning"));
    }
}

} // namespace QindaOffice::Apps::Mail
