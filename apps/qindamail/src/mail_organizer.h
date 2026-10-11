// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's everyday mail handling for QindaMail (M81, D-090;
// docs/mail.md#rules-and-alerts): client-side rules (on arrival and Run
// Rules Now), color categories, follow-up flags (and flagged mail on the
// task board), search folders and automatic replies. One per MailService
// (MailService.organizer), sharing its store and account runtimes.
//
// AGENT-CONTRACT (the QML dialogs, MailHost, docs/mail.md#qml-api-m81):
// - state lives in organizer.json beside view.json (schema
//   qindaoffice.mail.organizer/1): rules, categories {listKey → [Category]},
//   searchFolders, arrival baselines, keyword support, reply settings;
//   unknown keys are kept;
// - message records gain `categories` [names], `followUp` (follow_up.h),
//   `importance`, `tagsPending` (a change not yet on the server);
// - every asynchronous call returns a request id answered ONCE by
//   rulesRunFinished / tagsFinished / autoRepliesFinished on a later turn.
// AGENT-GUARD: rules never run on a mailbox's first download nor after a
// UIDVALIDITY reset (`reset`): that would forward or delete old mail.
//
// Files: mail_organizer.cpp (state, facts, category lists),
// _rules.cpp (rules, arrival, Run Rules Now), _actions.cpp (carrying out a
// plan), _tags.cpp (categories and flags on items, server mapping, the task
// board), _search.cpp (search folders), _replies.cpp (automatic replies,
// server rules).

#include "follow_up.h"
#include "mail_categories.h"
#include "mail_rules.h"
#include "search_folders.h"

#include "folder_model.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace QindaOffice::Core {
class JsonSettings;
}

namespace QindaOffice::Apps::Mail {

class AccountRuntime;
class MailService;

class MailOrganizer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MailService owns the organizer (MailService.organizer).")
    // Bumped on every change of rules, categories or search folders.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY changed)
    Q_PROPERTY(QVariantList searchFolders READ searchFolders NOTIFY changed)
    // [{accountId, accountName, name, enabled, summary}] (read-only, M81).
    Q_PROPERTY(QVariantList serverRules READ serverRules NOTIFY serverRulesChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    using Done = std::function<void(const QVariantMap &result)>;

    MailOrganizer(MailService *service, const QString &path, QObject *parent = nullptr);
    ~MailOrganizer() override;

    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QVariantList rules() const;
    [[nodiscard]] QVariantList searchFolders() const;
    [[nodiscard]] QVariantList serverRules() const { return m_serverRules; }
    [[nodiscard]] bool running() const { return m_running || !m_queue.isEmpty(); }

    // ---- rules (_rules.cpp) ----
    Q_INVOKABLE QVariantMap newRule() const;
    // Create Rule from a message: {rule, suggestions {from, subject, sentTo, account}}.
    Q_INVOKABLE QVariantMap ruleFromMessage(const QString &messageId) const;
    Q_INVOKABLE QStringList validateRule(const QVariantMap &rule) const;
    Q_INVOKABLE QString describeRule(const QVariantMap &rule) const;
    // {ok, id, errors}
    Q_INVOKABLE QVariantMap saveRule(const QVariantMap &rule);
    Q_INVOKABLE bool deleteRule(const QString &id);
    Q_INVOKABLE QString copyRule(const QString &id);
    Q_INVOKABLE bool moveRule(const QString &id, int index);
    Q_INVOKABLE bool setRuleEnabled(const QString &id, bool enabled);
    // {ok, count, error}; `ids` empty = every rule.
    Q_INVOKABLE QVariantMap exportRules(const QUrl &url, const QStringList &ids = {}) const;
    // {ok, added, problems, error}: imported rules go after the others.
    Q_INVOKABLE QVariantMap importRules(const QUrl &url);
    // which: all | read | unread → rulesRunFinished(requestId, {state, processed, matched, error}).
    Q_INVOKABLE QString runRulesNow(const QStringList &ruleIds, const QString &folderKey, bool includeSubfolders,
                                    const QString &which);
    // "Projects (Work)" for a folder key; "" when unknown.
    Q_INVOKABLE QString folderLabel(const QString &folderKey) const;

    // ---- categories (mail_organizer.cpp, _tags.cpp) ----
    // The master list an account's items use ("local" or the account id
    // for Exchange / Microsoft 365): [{name, color, preset}].
    Q_INVOKABLE QVariantList categoriesFor(const QString &accountId) const;
    Q_INVOKABLE QString listKeyFor(const QString &accountId) const;
    Q_INVOKABLE QString categoryColor(const QString &accountId, const QString &name) const;
    // Outlook's 25 colours: [{name, color, preset}].
    Q_INVOKABLE QVariantList categoryPalette() const;
    Q_INVOKABLE QVariantMap addCategory(const QString &accountId, const QString &name, const QString &color);
    Q_INVOKABLE QVariantMap updateCategory(const QString &accountId, const QString &name, const QVariantMap &changes);
    Q_INVOKABLE bool deleteCategory(const QString &accountId, const QString &name);
    // kind: message | event | task | contact.
    Q_INVOKABLE QStringList categoriesOf(const QString &kind, const QString &id) const;
    Q_INVOKABLE QString accountOf(const QString &kind, const QString &id) const;
    Q_INVOKABLE QString setCategories(const QString &kind, const QStringList &ids, const QStringList &names);
    Q_INVOKABLE QString toggleCategory(const QString &kind, const QStringList &ids, const QString &name, bool on);
    // ---- follow-up flags (_tags.cpp) ----
    // preset: today | tomorrow | thisWeek | nextWeek | noDate; "complete"; "clear".
    Q_INVOKABLE QString followUp(const QStringList &messageIds, const QString &preset);
    Q_INVOKABLE QString followUpCustom(const QStringList &messageIds, const QString &start, const QString &due);
    // {status, preset, start, due, completed, text, dueLabel}
    Q_INVOKABLE QVariantMap followUpOf(const QString &messageId) const;

    // ---- search folders (_search.cpp) ----
    // [{preset, name, group, argument: "" | people | words | size | days | categories}]
    Q_INVOKABLE QVariantList searchFolderPresets() const;
    Q_INVOKABLE QVariantMap newSearchFolder(const QString &preset, const QVariant &argument = {}) const;
    Q_INVOKABLE QVariantMap searchFolderByKey(const QString &key) const;
    Q_INVOKABLE QVariantMap saveSearchFolder(const QVariantMap &folder);
    Q_INVOKABLE bool deleteSearchFolder(const QString &idOrKey);

    // ---- conversations (_conversations.cpp) ----
    // Clean Up: read, unflagged, uncategorised messages whose text a later
    // message of the conversation quotes go to Trash; the answer is
    // MailService.operationFinished (with a notice saying how many).
    Q_INVOKABLE QString cleanUpConversation(const QString &messageId);
    // Ignore: the conversation goes to Trash, and so does what arrives in it
    // later; asked again for an ignored conversation, it stops ignoring.
    Q_INVOKABLE QString ignoreConversation(const QString &messageId);
    Q_INVOKABLE bool isIgnored(const QString &messageId) const;

    // ---- automatic replies and the server's rules (_replies.cpp) ----
    // [{id, name, method graph | ews | sieve | none, reason}]
    Q_INVOKABLE QVariantList autoReplyAccounts() const;
    Q_INVOKABLE QString readAutoReplies(const QString &accountId);
    Q_INVOKABLE QString writeAutoReplies(const QString &accountId, const QVariantMap &settings);
    Q_INVOKABLE void refreshServerRules();

    // Shared explicit copy primitive for rules and Quick Steps. The callback
    // runs on this object's thread; local copies may complete synchronously.
    void copyMessage(const QString &messageId, const QString &folderKey, Done done);

    // ---- C++ (MailService) ----
    // A sync of `mailbox` answered: rules run on its new messages, and the
    // server's tags come back into the records.
    void noteSyncResult(AccountRuntime *runtime, const QString &mailbox, const QVariantMap &result);
    // A calendar sync of the account finished: event categories set here
    // while the event had no server identity (or was offline) go up now.
    void noteCalendarSync(AccountRuntime *runtime);
    // Search folder rows (`search:<id>`, after a header) and their records.
    [[nodiscard]] QVector<FolderRow> searchRows(bool expanded) const;
    [[nodiscard]] bool isSearchKey(const QString &key) const;
    [[nodiscard]] QVariantList searchRecords(const QString &key) const;
    // The display fields of a list row: categoryChips [{name, color}],
    // followUpStatus, followUpDue, importance.
    void decorate(QVariantMap *record) const;
    // Flagged mail as task-board cards per status, and a card's move.
    [[nodiscard]] QVariantList flagCards(const QString &status) const;
    [[nodiscard]] static bool isFlagCard(const QString &taskId);
    void moveFlagCard(const QString &requestId, const QString &taskId, const QString &toStatus);
    // A rule's redirect waiting in the Outbox (MailService::deliver hands
    // drafts with `redirectOf` here); answers through sendFinished.
    void deliverQueuedRedirect(const QString &requestId, const QVariantMap &draft);
    // Tests: "now" for presets and rule dates.
    void setToday(const QDate &today) { m_today = today; }

signals:
    void changed();
    void serverRulesChanged();
    void runningChanged();
    void rulesRunFinished(const QString &requestId, const QVariantMap &result);
    void tagsFinished(const QString &requestId, const QVariantMap &result);
    void autoRepliesFinished(const QString &requestId, const QVariantMap &result);

private:
    friend class MailOrganizerTestAccess;
    struct Pending {
        QString messageId;
        QString requestId;          // "" on arrival
        QStringList onlyRules;
        bool includeDisabled = false;
    };

    // mail_organizer.cpp
    void load();
    void save();
    void bump();
    [[nodiscard]] QDate today() const;
    [[nodiscard]] QindaOffice::Mail::CriteriaContext context() const;
    [[nodiscard]] QindaOffice::Mail::MessageFacts factsOf(const QVariantMap &record, const QString &headers = {}) const;
    [[nodiscard]] QVariantMap record(const QString &messageId) const;
    [[nodiscard]] QList<QindaOffice::Mail::Category> &listFor(const QString &accountId);
    [[nodiscard]] const QList<QindaOffice::Mail::Category> listFor(const QString &accountId) const;
    bool editRecord(const QString &collection, const QString &id, const QVariantMap &changes, QString *error = nullptr);
    void answerLater(const char *signal, const QString &requestId, const QVariantMap &result);
    void expect(const QString &requestId, Done done);
    void syncCategoryList(AccountRuntime *runtime);
    // _search.cpp
    [[nodiscard]] QHash<QString, QStringList> skippedMailboxes() const;
    [[nodiscard]] bool searchable(const QVariantMap &record, const QHash<QString, QStringList> &skipped) const;
    // _rules.cpp
    void enqueue(const QList<Pending> &items);
    void processNext();
    void finishRun(const QString &requestId);
    // _actions.cpp
    void carryOut(const QString &messageId, const QindaOffice::Mail::RulePlan &plan, Done done);
    void forward(const QString &messageId, const QString &recipients);
    void redirect(const QString &messageId, const QString &recipients, Done done);
    void dispose(const QString &messageId, const QindaOffice::Mail::RulePlan &plan, Done done);
    // _tags.cpp
    void pushMessageTags(const QString &messageId, bool categories, bool flag, Done done);
    void pushEventCategories(const QString &eventId, Done done);
    void pullTags(AccountRuntime *runtime, const QString &mailbox, const QVariantMap &syncResult);
    void applyServerTags(const QString &accountId, const QVariantList &items);
    QString applyFollowUp(const QStringList &ids, const std::function<QindaOffice::Mail::FollowUp(const QVariantMap &)> &make);
    QString applyCategories(const QString &kind, const QStringList &ids,
                            const std::function<QStringList(const QStringList &)> &change);
    // Shows `text` once per session and account (a server that keeps no keywords …).
    void tellOnce(const QString &key, const QString &title, const QString &text);

    QPointer<MailService> m_service;
    std::unique_ptr<QindaOffice::Core::JsonSettings> m_file;
    QList<QindaOffice::Mail::MailRule> m_rules;
    QHash<QString, QList<QindaOffice::Mail::Category>> m_categories;   // list key → master list
    QList<QindaOffice::Mail::SearchFolder> m_searchFolders;
    QSet<QString> m_baselines;            // "account\nmailbox" past its first download
    QStringList m_ignored;                // ignored conversations' thread ids, oldest first (≤ 1000)
    QHash<QString, bool> m_keywordSupport;  // account → the server stores new keywords
    QSet<QString> m_mailboxBlocked;       // accounts whose Graph/EWS mailbox refused this session (consent)
    QSet<QString> m_told;
    QHash<QString, Done> m_waiting;       // MailService request id → continuation
    QHash<QString, QVariantMap> m_runs;   // Run Rules Now request → {processed, matched, pending}
    QList<Pending> m_queue;
    QVariantList m_serverRules;
    QDate m_today;
    int m_revision = 0;
    bool m_running = false;
};

} // namespace QindaOffice::Apps::Mail
