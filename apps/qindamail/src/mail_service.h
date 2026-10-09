// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaMail's process-wide engine (D-042): ONE per process, shared by every
// main window (MailHost.service in QML). It owns the local store
// ($XDG_DATA_HOME/qindaoffice/mail/store.json), the attachment store, the
// account list and keyring, one AccountRuntime per configured account and
// the built-in "On this computer" account (id `local`, never in
// accounts.json), and exposes folders, the message list, the selection,
// drafts and sending, calendars and tasks to QML.
//
// AGENT-CONTRACT (the app lead's QML, docs/mail.md#qindamail-app): the
// property, invokable, signal and role names below are the QML API; rename
// none. Asynchronous calls return a request id and answer ONCE through
// operationFinished / sendFinished / eventChangeFinished /
// accountTestFinished / calendarsDiscovered with a state from the provider
// contract (accepted | offline | rejected | conflict).
// AGENT-GUARD: no property, result, status text or log line carries a
// password or token (AccountRuntime is the only code that handles them).
//
// Files: mail_service.cpp (lifecycle, accounts, status), _folders (tree,
// list, selection), _messages (actions, attachments), _sync (sync, mailbox
// lists, timers), _compose (drafts, send, mailto), _accounts, _calendar,
// _tasks; M64 (D-072): _source (RFC 5322 sources, content, attachment
// bytes), _files (.eml/.msg/.mbox in, .eml out), _contacts (address book,
// suggestions, pictures, links), _meetings (iMIP). M81 (D-090): rules,
// categories, flags, search folders, automatic replies — mail_organizer.h.

#include "account_runtime.h"
#include "account_store.h"
#include "autoconfig.h"
#include "contact_store.h"
#include "folder_model.h"
#include "mail_secrets.h"
#include "message_list_model.h"

#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

class QTimer;

namespace QindaTK {
class IcsCalendarProvider;
class LocalCalendarProjectionModel;
class RecurrenceService;
}

namespace QindaOffice::Core {
class JsonSettings;
}

namespace QindaOffice::Mail {
class ContactStore;
class SecretBackend;
struct MessageContent;
struct RenderPolicy;
}

class QNetworkAccessManager;

namespace QindaOffice::Apps::Mail {

class MailOrganizer;
class ProviderCalls;
class PstImporter;

Q_MOC_INCLUDE("pst_import.h")
Q_MOC_INCLUDE("mail_organizer.h")

inline constexpr const char kLocalAccountId[] = "local";
inline constexpr const char kLocalCalendarId[] = "local";

class MailService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The application owns the MailService (MailHost.service).")
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QindaOffice::Mail::AccountStore *accountStore READ accountStore CONSTANT)
    Q_PROPERTY(QindaOffice::Mail::AutoConfig *autoConfig READ autoConfig CONSTANT)
    Q_PROPERTY(QindaOffice::Mail::MailSecrets *secrets READ secrets CONSTANT)
    Q_PROPERTY(QindaTK::LocalProductivityStore *store READ store CONSTANT)
    Q_PROPERTY(QindaTK::LocalAttachmentStore *attachmentStore READ attachmentStore CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool autoSync READ autoSync WRITE setAutoSync NOTIFY autoSyncChanged)
    Q_PROPERTY(QString defaultAccountId READ defaultAccountId NOTIFY accountsChanged)
    Q_PROPERTY(QStringList ownAddresses READ ownAddresses NOTIFY accountsChanged)
    Q_PROPERTY(QVariantList sendableAccounts READ sendableAccounts NOTIFY accountsChanged)
    // ---- folders and messages (mail_service_folders.cpp) ----
    Q_PROPERTY(QindaOffice::Apps::Mail::FolderModel *folderModel READ folderModel CONSTANT)
    Q_PROPERTY(QString currentFolderKey READ currentFolderKey WRITE setCurrentFolderKey NOTIFY currentFolderKeyChanged)
    Q_PROPERTY(QString currentFolderName READ currentFolderName NOTIFY currentFolderNameChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadTotalChanged)
    Q_PROPERTY(QindaOffice::Apps::Mail::MessageListModel *messageModel READ messageModel CONSTANT)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool threaded READ threaded WRITE setThreaded NOTIFY threadedChanged)
    Q_PROPERTY(QString selectedMessageId READ selectedMessageId WRITE setSelectedMessageId NOTIFY selectedMessageIdChanged)
    Q_PROPERTY(QVariantMap selectedMessage READ selectedMessage NOTIFY selectedMessageChanged)
    Q_PROPERTY(int markReadDelayMs READ markReadDelayMs WRITE setMarkReadDelayMs NOTIFY markReadDelayMsChanged)
    // ---- calendar and tasks ----
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)
    Q_PROPERTY(QVariantList writableCalendars READ writableCalendars NOTIFY calendarsChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QString eventsError READ eventsError NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList taskColumns READ taskColumns NOTIFY taskColumnsChanged)
    // ---- address book (M64, mail_service_contacts.cpp) ----
    Q_PROPERTY(QindaOffice::Mail::ContactStore *contacts READ contacts CONSTANT)
    // ---- Outlook data files (M76, pst_import.h) ----
    Q_PROPERTY(QindaOffice::Apps::Mail::PstImporter *outlookImport READ outlookImport CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Mail::MailOrganizer *organizer READ organizer CONSTANT)   // M81

public:
    struct Options {
        // Empty: $XDG_DATA_HOME/qindaoffice/mail (store.json, attachments/,
        // folders.json) and the AccountStore default path.
        QString dataDir;
        QString accountsPath;
        QString settingsPath;
        // Empty: <dataDir>/contacts.json (M64).
        QString contactsPath;
        bool autoSync = true;
        // Follow QNetworkInformation for `online` (tests keep it true).
        bool watchNetwork = true;
        std::shared_ptr<QindaOffice::Mail::SecretBackend> secretBackend;
    };

    explicit MailService(QObject *parent = nullptr);
    explicit MailService(const Options &options, QObject *parent = nullptr);
    ~MailService() override;

    // The process's service, created on first use (parented to the
    // application so it outlives every window).
    [[nodiscard]] static MailService &shared();

    // ---- accounts (mail_service.cpp / _accounts) ----
    [[nodiscard]] QVariantList accounts() const;
    [[nodiscard]] QindaOffice::Mail::AccountStore *accountStore() const { return m_accountStore; }
    [[nodiscard]] QindaOffice::Mail::AutoConfig *autoConfig() const { return m_autoConfig; }
    [[nodiscard]] QindaOffice::Mail::MailSecrets *secrets() const { return m_secrets; }
    [[nodiscard]] QindaTK::LocalProductivityStore *store() const { return m_store; }
    [[nodiscard]] QindaTK::LocalAttachmentStore *attachmentStore() const { return m_attachments; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] bool online() const { return m_online; }
    [[nodiscard]] bool autoSync() const { return m_autoSync; }
    void setAutoSync(bool enabled);
    [[nodiscard]] QString defaultAccountId() const;
    [[nodiscard]] QStringList ownAddresses() const;
    [[nodiscard]] QVariantList sendableAccounts() const;
    // C++ (tests, the host): the runtime of a configured account.
    [[nodiscard]] AccountRuntime *runtime(const QString &accountId) const;
    [[nodiscard]] ProviderCalls *calls() const { return m_calls; }
    // C++ (tests): how attachments and links are opened (default QDesktopServices).
    void setUrlOpener(std::function<bool(const QUrl &)> opener) { m_openUrl = std::move(opener); }
    // C++ (tests): how remote pictures are downloaded once the user allowed
    // them (default: a bounded QNetworkAccessManager GET, mail_service_contacts.cpp).
    using FetchDone = std::function<void(const QByteArray &bytes, const QString &error)>;
    using RemoteFetcher = std::function<void(const QUrl &url, FetchDone done)>;
    void setRemoteFetcher(RemoteFetcher fetcher) { m_fetchRemote = std::move(fetcher); }

    Q_INVOKABLE QVariantMap addAccount(const QVariantMap &account, const QString &password = {});
    Q_INVOKABLE QVariantMap updateAccount(const QVariantMap &account, const QString &password = {});
    Q_INVOKABLE QVariantMap removeAccount(const QString &accountId);
    Q_INVOKABLE QString signIn(const QString &accountId);
    Q_INVOKABLE void testAccount(const QString &requestId, const QVariantMap &account, const QString &password);
    Q_INVOKABLE void discoverCalendars(const QString &requestId, const QString &url, const QString &userName,
                                       const QString &password);
    // M76: CardDAV address books at `url` → addressBooksDiscovered(requestId, result).
    Q_INVOKABLE void discoverAddressBooks(const QString &requestId, const QString &url, const QString &userName,
                                          const QString &password);
    Q_INVOKABLE QVariantMap accountById(const QString &accountId) const;

    // ---- folders and messages (_folders) ----
    [[nodiscard]] FolderModel *folderModel() const { return m_folderModel; }
    [[nodiscard]] QString currentFolderKey() const { return m_currentFolderKey; }
    void setCurrentFolderKey(const QString &key);
    [[nodiscard]] QString currentFolderName() const;
    [[nodiscard]] int unreadTotal() const { return m_unreadTotal; }
    [[nodiscard]] MessageListModel *messageModel() const { return m_messageModel; }
    [[nodiscard]] QString searchText() const;
    void setSearchText(const QString &text);
    [[nodiscard]] QString filter() const;
    void setFilter(const QString &filter);
    [[nodiscard]] bool threaded() const;
    void setThreaded(bool threaded);
    [[nodiscard]] QString selectedMessageId() const { return m_selectedMessageId; }
    void setSelectedMessageId(const QString &id);
    [[nodiscard]] QVariantMap selectedMessage() const { return m_selectedMessage; }
    [[nodiscard]] int markReadDelayMs() const { return m_markReadDelayMs; }
    void setMarkReadDelayMs(int ms);

    Q_INVOKABLE void toggleFolderExpanded(const QString &key);
    // Selectable folders of one account (all accounts when empty), for a
    // Move menu: [{key, name, depth, iconName, specialUse}].
    Q_INVOKABLE QVariantList folderTargets(const QString &accountId) const;
    // Every message of a conversation, oldest first (full records).
    Q_INVOKABLE QVariantList conversation(const QString &threadId) const;
    Q_INVOKABLE QVariantMap messageById(const QString &messageId) const;
    // The folder key a store record lives in (`acct:<account>:<mailbox>`).
    [[nodiscard]] QString folderKeyOf(const QVariantMap &record) const;

    // ---- message actions (_messages): request id → operationFinished ----
    Q_INVOKABLE QString markRead(const QStringList &ids, bool read);
    Q_INVOKABLE QString setFlagged(const QStringList &ids, bool flagged);
    Q_INVOKABLE QString archive(const QStringList &ids);
    Q_INVOKABLE QString deleteMessages(const QStringList &ids);
    Q_INVOKABLE QString moveTo(const QStringList &ids, const QString &folderKey);
    Q_INVOKABLE QString fetchAttachment(const QString &messageId, const QString &attachmentId);
    Q_INVOKABLE QString openAttachment(const QString &messageId, const QString &attachmentId);
    Q_INVOKABLE QString saveAttachmentAs(const QString &messageId, const QString &attachmentId, const QUrl &url);
    // A .eml or Outlook .msg file (path or file: URL) into the local Inbox;
    // returns its id ("" on failure, with a notification). An mbox file is
    // handed to importMailbox.
    Q_INVOKABLE QString importMessageFile(const QString &path);
    // M76 (_outlook): an Outlook .msg that is a contact, a contact group, an
    // appointment or a task goes to People, the calendar or the task board:
    // {handled, ok, view, id, error}; handled false for mail.
    Q_INVOKABLE QVariantMap importOutlookItemFile(const QString &path);

    // ---- message sources and files (M64: _source, _files) ----
    // The RFC 5322 source when this computer has it (never the network).
    [[nodiscard]] QByteArray storedSource(const QString &messageId) const;
    // The source, first downloaded whole from the IMAP server when needed
    // (ImapSourceFetcher) and kept; done(source, error) runs on a later turn.
    // Empty source: show the record.
    void loadSource(const QString &messageId, std::function<void(const QByteArray &, const QString &)> done);
    // What the reading pane and the printer show: the parsed source when this
    // computer has it, else a message built from the stored record.
    [[nodiscard]] QindaOffice::Mail::MessageContent contentOf(const QString &messageId) const;
    // Remote pictures for this message: allowed (shown once, or always for
    // its sender) and the bytes fetched so far.
    [[nodiscard]] QindaOffice::Mail::RenderPolicy renderPolicy(const QString &messageId) const;
    // Downloads the remote pictures of a message whose pictures are allowed
    // and not fetched yet (no-op otherwise); picturesChanged as they arrive.
    void fetchRemoteImages(const QString &messageId, const QStringList &urls = {});
    // An mbox file into a new local folder named after it, a few messages
    // per event-loop turn; operationFinished(requestId, …) at the end.
    Q_INVOKABLE QString importMailbox(const QUrl &url);
    // The message as an .eml file (its original source when known).
    Q_INVOKABLE QString saveMessageAs(const QString &messageId, const QUrl &url);
    // Every listed attachment into `folder` (existing names get " (2)" …).
    Q_INVOKABLE QString saveAllAttachments(const QString &messageId, const QUrl &folder);
    // http(s) links open in the desktop's browser; anything else is refused.
    Q_INVOKABLE bool openLink(const QString &url);

    // ---- contacts and pictures (M64: _contacts) ----
    [[nodiscard]] QindaOffice::Mail::ContactStore *contacts() const { return m_contacts; }
    [[nodiscard]] PstImporter *outlookImport() const { return m_outlookImport; }
    [[nodiscard]] MailOrganizer *organizer() const { return m_organizer; }
    // "Add sender to contacts": {ok, id, existed, error}.
    Q_INVOKABLE QVariantMap addSenderToContacts(const QString &messageId);
    // Recipient autocomplete: contacts first, then addresses seen in mail,
    // [{label "Name <addr>", value addr, valid true}].
    Q_INVOKABLE QVariantList recipientSuggestions(const QString &query, int limit = 8) const;
    // "Show pictures" for one message (this session) and "always for this sender" (kept).
    Q_INVOKABLE void showPictures(const QString &messageId);
    Q_INVOKABLE bool picturesShown(const QString &messageId) const;
    Q_INVOKABLE void setSenderPicturesAllowed(const QString &address, bool allowed);
    Q_INVOKABLE bool senderPicturesAllowed(const QString &address) const;

    // ---- meeting requests (M64 P3, iMIP: _meetings) ----
    // The meeting a message's text/calendar part proposes: the event fields,
    // method, uid, organizer(Name), attendees [{address, name, status}],
    // myAddress, myStatus, eventId (when in the calendar), canReply. {} when none.
    Q_INVOKABLE QVariantMap meetingOf(const QString &messageId) const;
    // response: accepted | tentative | declined — the local calendar follows,
    // and a METHOD:REPLY goes to the organizer through the receiving account;
    // for a CANCEL, "removed" takes the meeting out of the calendar only.
    Q_INVOKABLE QString respondToMeeting(const QString &messageId, const QString &response);
    // The event as a METHOD:REQUEST to `invitees` ("Name <addr>" each) through
    // a sending account (_invitations); the event keeps uid, invitees, SEQUENCE.
    Q_INVOKABLE QString sendInvitations(const QString &eventId, const QStringList &invitees);

    // ---- sync (_sync) ----
    Q_INVOKABLE QString syncAll();
    Q_INVOKABLE QString syncAccount(const QString &accountId);
    Q_INVOKABLE QString syncFolder(const QString &folderKey);

    // ---- drafts and sending (_compose) ----
    Q_INVOKABLE QVariantMap newDraft(const QString &accountId = {}) const;
    Q_INVOKABLE QVariantMap replyDraft(const QString &messageId, bool replyAll);
    Q_INVOKABLE QVariantMap forwardDraft(const QString &messageId);
    Q_INVOKABLE QVariantMap mailtoDraft(const QString &url) const;
    // `draft` with local files imported into its attachment store (M48,
    // `qindamail --attach`); files that cannot be read are named in
    // `missingAttachments`, the composer says so.
    Q_INVOKABLE QVariantMap attachFiles(const QVariantMap &draft, const QStringList &paths);
    // Stores the draft (new id when it has none): {ok, id, error}.
    Q_INVOKABLE QVariantMap saveDraft(const QVariantMap &draft);
    Q_INVOKABLE bool discardDraft(const QString &draftId);
    // Saves, then delivers; sendFinished(requestId, result) follows.
    Q_INVOKABLE QString send(const QVariantMap &draft);

    // ---- calendar (_calendar) ----
    [[nodiscard]] QVariantList calendars() const;
    [[nodiscard]] QVariantList writableCalendars() const;
    [[nodiscard]] QVariantList events() const { return m_events; }
    [[nodiscard]] QString eventsError() const { return m_eventsError; }
    Q_INVOKABLE void setCalendarVisible(const QString &calendarId, bool visible);
    // Local dates, half-open [start, end); re-projects on store changes.
    Q_INVOKABLE bool projectRange(const QString &startDate, const QString &endDate);
    Q_INVOKABLE QString createEvent(const QVariantMap &event);
    Q_INVOKABLE QString updateEvent(const QVariantMap &event);
    // scope: series | occurrence | following.
    Q_INVOKABLE QString deleteEvent(const QString &eventId, const QString &occurrenceId, const QString &scope);
    // A CalendarGridView/CalendarView proposal: {kind shift-local | resize-local,
    // minutes, scope, occurrenceId}.
    Q_INVOKABLE void applyChange(const QString &requestId, const QString &eventId, const QVariantMap &proposal);
    Q_INVOKABLE QVariantMap eventById(const QString &eventId) const;
    Q_INVOKABLE QString importIcs(const QUrl &url, const QString &calendarId = {});
    // The file's VTODOs as tasks on the board; returns how many (M56).
    int importIcsTasks(const QString &path);
    Q_INVOKABLE QString exportIcs(const QUrl &url, const QString &calendarId = {});
    Q_INVOKABLE QString subscribeFeed(const QString &url, const QString &name = {}, const QString &color = {});
    Q_INVOKABLE QVariantList recurrencePreview(const QString &eventId, const QVariantMap &rule);

    // ---- tasks (_tasks) ----
    [[nodiscard]] QVariantList taskColumns() const { return m_taskColumns; }
    // Returns the new task's id ("" when the title is empty).
    Q_INVOKABLE QString addTask(const QString &title);
    Q_INVOKABLE void moveTask(const QString &requestId, const QString &taskId, const QString &toStatus, int index);
    Q_INVOKABLE QString updateTask(const QVariantMap &task);
    Q_INVOKABLE QString deleteTask(const QString &taskId);

signals:
    void accountsChanged();
    void busyChanged();
    void statusTextChanged();
    void onlineChanged();
    void autoSyncChanged();
    void currentFolderKeyChanged();
    void currentFolderNameChanged();
    void unreadTotalChanged();
    void searchTextChanged();
    void filterChanged();
    void threadedChanged();
    void selectedMessageIdChanged();
    void selectedMessageChanged();
    void markReadDelayMsChanged();
    void calendarsChanged();
    void eventsChanged();
    void taskColumnsChanged();
    void operationFinished(const QString &requestId, const QString &state, const QString &message);
    void sendFinished(const QString &requestId, const QVariantMap &result);
    void eventChangeFinished(const QString &requestId, const QString &state, const QString &message);
    void accountTestFinished(const QString &requestId, const QVariantMap &result);
    void calendarsDiscovered(const QString &requestId, const QVariantMap &result);
    void addressBooksDiscovered(const QString &requestId, const QVariantMap &result);
    // variant: info | success | warning | danger (Tk.Toast).
    void notify(const QString &title, const QString &text, const QString &variant);
    // M64: a message's source arrived, or its remote pictures changed.
    void sourceLoaded(const QString &messageId);
    void picturesChanged(const QString &messageId);
    void meetingChanged(const QString &messageId);

private:
    friend class MailServiceTestAccess;
    friend class PstImporter;   // M76: imports through importRawMessage and the store
    friend class MailOrganizer; // M81: rule actions through the message actions and the store
    struct Batch {
        int pending = 0;
        QString state = QStringLiteral("accepted");
        QString message;
    };

    // mail_service.cpp
    void initialize(const Options &options);
    void startRuntime(const QindaOffice::Mail::MailAccount &account);
    void stopRuntime(const QString &accountId);
    void refreshBusy();
    void setStatusText(const QString &text);
    void setOnline(bool online);
    [[nodiscard]] bool saveStore(QString *error = nullptr);
    [[nodiscard]] QString accountName(const QString &accountId) const;
    [[nodiscard]] QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    void saveSetting(const QString &key, const QVariant &value);
    // One request id over several operations; the worst state wins.
    QString beginBatch(const QString &prefix, int count);
    void batchDone(const QString &requestId, const QVariantMap &result);
    void finishNow(const QString &requestId, const QString &state, const QString &message);
    void scheduleRefresh();
    void refreshDerived();
    // _folders
    void rebuildFolders();
    void rebuildMessages();
    void refreshSelectedMessage();
    [[nodiscard]] QVariantList folderRecords(const QString &key) const;
    [[nodiscard]] QVariantList draftRows(bool outbox) const;
    // Every folder of one account (depth from the hierarchy, not collapsed),
    // counts from `counts` (key → {unread, total}).
    [[nodiscard]] QVector<FolderRow> accountFolderRows(const QString &accountId,
                                                       const QHash<QString, QPair<int, int>> &counts) const;
    [[nodiscard]] QHash<QString, QPair<int, int>> folderCounts() const;
    [[nodiscard]] QString inboxKey(const QString &accountId) const;
    [[nodiscard]] QString ownerAccountOf(const QVariantMap &record) const;
    [[nodiscard]] QVariantMap draftAsRow(const QVariantMap &draft) const;
    // _messages
    void runMessageAction(const QString &batchId, const QString &messageId, const QString &action,
                          const QString &target = {});
    void editLocalMessage(const QString &batchId, const QString &messageId, const QVariantMap &changes);
    void removeRecordAttachments(const QVariantMap &record);
    // Reads an attachment's bytes, downloading them first when they are
    // still on the server; `done` gets the attachment store's read result
    // or a failure result (ok false, state, error).
    void withAttachment(const QString &messageId, const QString &attachmentId,
                        std::function<void(const QVariantMap &)> done);
    [[nodiscard]] QVariantMap rawMessage(const QString &messageId) const;
    // _source: attachment bytes from the store, else from the stored source
    // ({ok, name, mimeType, bytes} or {ok false, error}); no network.
    [[nodiscard]] QVariantMap attachmentBytes(const QVariantMap &record, const QString &attachmentId) const;
    [[nodiscard]] QString sourcePath(const QString &messageId) const;
    [[nodiscard]] bool writeSource(const QString &messageId, const QByteArray &raw, QString *error);
    // M76: bulk imports write sources without a sync each, then syncSources() once.
    void syncSources();
    void removeSource(const QVariantMap &record);
    void fetchImapSource(const QVariantMap &record, AccountRuntime *runtime);
    void finishSource(const QString &messageId, const QByteArray &source, const QString &error);
    // Source files whose record is gone (an IMAP expunge); run once at start.
    void pruneSources();
    [[nodiscard]] QindaOffice::Mail::MessageContent recordContent(const QVariantMap &record) const;
    // _files: one RFC 5322 message into a local folder; the new record id.
    // `extra` fields go onto the record last (M76: unread, flagged, importedFrom).
    QString importRawMessage(const QByteArray &raw, const QString &mailbox, const QString &origin, QString *error,
                             const QVariantMap &extra = {});

    [[nodiscard]] QStringList localMailboxes() const;
    void importMailboxStep(const QString &requestId);
    // _contacts
    void rebuildSeenAddresses() const;
    // _sync
    void syncRuntime(AccountRuntime *runtime, const QString &batchId);
    void loadFolderCache();
    void saveFolderCache();
    void restartTimer(AccountRuntime *runtime);
    void flushOutbox();
    // _compose
    void deliver(const QString &requestId, const QVariantMap &draft);
    void fileSentCopy(AccountRuntime *runtime, const QVariantMap &draft, const QVariantMap &sendResult,
                      const QString &requestId);
    void storeLocalSentCopy(const QVariantMap &draft, const QString &fromAddress, const QString &fromName);
    void finishSend(const QString &requestId, QVariantMap result);
    // _calendar
    void rebuildEvents();
    void writeThrough(const QString &eventOrTaskCollection, const QVariantMap &record, const QString &requestId,
                      bool eventSignal);
    [[nodiscard]] AccountRuntime *runtimeForCalendar(const QString &calendarId, const CalendarBinding **binding) const;
    [[nodiscard]] bool calendarReadOnly(const QString &calendarId) const;
    void finishEventChange(const QString &requestId, const QString &state, const QString &message);
    // _tasks
    void rebuildTasks();

    ProviderCalls *m_calls = nullptr;
    QindaTK::LocalProductivityStore *m_store = nullptr;
    QindaTK::LocalAttachmentStore *m_attachments = nullptr;
    QindaTK::RecurrenceService *m_recurrence = nullptr;
    QindaTK::LocalCalendarProjectionModel *m_projection = nullptr;
    QindaTK::IcsCalendarProvider *m_ics = nullptr;
    QindaOffice::Mail::AccountStore *m_accountStore = nullptr;
    QindaOffice::Mail::AutoConfig *m_autoConfig = nullptr;
    QindaOffice::Mail::MailSecrets *m_secrets = nullptr;
    FolderModel *m_folderModel = nullptr;
    MessageListModel *m_messageModel = nullptr;
    std::unique_ptr<QindaOffice::Core::JsonSettings> m_settings;
    QList<AccountRuntime *> m_runtimes;
    QHash<QString, QTimer *> m_timers;
    QHash<QString, Batch> m_batches;
    QHash<QString, QVariantList> m_folderCache;   // accountId → IMAP LIST result
    QSet<QString> m_collapsed;
    QSet<QString> m_hiddenCalendars;
    QSet<QString> m_sending;                       // draft ids in flight
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_markReadTimer = nullptr;
    QString m_dataDir;
    QString m_currentFolderKey;
    QString m_currentFolderName;
    QString m_selectedMessageId;
    QVariantMap m_selectedMessage;
    QString m_statusText;
    QString m_rangeStart;
    QString m_rangeEnd;
    QString m_eventsError;
    QVariantList m_events;
    QVariantList m_taskColumns;
    std::function<bool(const QUrl &)> m_openUrl;
    // ---- M64 ----
    struct SeenAddress {
        QString name;
        QString address;
        int count = 0;
        QString lastDate;
    };
    struct MboxJob;
    QindaOffice::Mail::ContactStore *m_contacts = nullptr;
    PstImporter *m_outlookImport = nullptr;
    MailOrganizer *m_organizer = nullptr;
    bool m_bulkSources = false;
    QHash<QString, QList<std::function<void(const QByteArray &, const QString &)>>> m_sourceWaiters;
    mutable QHash<QString, std::shared_ptr<const QindaOffice::Mail::MessageContent>> m_contentCache;
    mutable QStringList m_contentOrder;
    QSet<QString> m_picturesShown;
    QHash<QString, QHash<QString, QByteArray>> m_remoteImages;   // messageId → url → bytes
    QStringList m_remoteOrder;
    QSet<QString> m_fetching;                                     // "messageId\nurl" in flight
    QSet<QString> m_picturesSenders;
    RemoteFetcher m_fetchRemote;
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, std::shared_ptr<MboxJob>> m_mboxJobs;
    mutable QList<SeenAddress> m_seen;
    mutable bool m_seenDirty = true;
    int m_unreadTotal = 0;
    int m_markReadDelayMs = 500;
    bool m_busy = false;
    bool m_online = true;
    bool m_autoSync = true;
};

} // namespace QindaOffice::Apps::Mail
