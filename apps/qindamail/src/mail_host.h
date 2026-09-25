// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One QindaMail main window's host (D-042): a DocumentHost WITHOUT a
// Core::Document (Save stays disabled, D-011) over the process's shared
// MailService. It owns the window's commands (ActionEntries with the stable
// app-scoped ids below), the Message / Calendar / Accounts menus and the
// View additions, keeps the commands' enabled state honest (a reply needs a
// selected message) and turns commands into requests the QML answers:
// composeRequested(draft) opens a composer window, the *Requested() signals
// open dialogs.
//
// AGENT-CONTRACT (MailWindow.qml and the lead's ribbon layout; AGENTS.md
// rule 5): the command ids are
//   mailNewMessageAction mailNewEventAction mailReplyAction
//   mailReplyAllAction mailForwardAction mailDeleteAction mailArchiveAction
//   mailToggleReadAction mailToggleFlagAction mailMoveAction mailSyncAction
//   mailSearchAction mailAddAccountAction mailAccountSettingsAction
//   mailViewMailAction mailViewCalendarAction mailViewTasksAction (radio
//   group "mailView") mailReadingPaneAction mailConversationsAction
//   calTodayAction calPreviousAction calNextAction calDayAction
//   calWorkWeekAction calWeekAction calMonthAction (radio group
//   "calendarMode") calImportAction calExportAction calSubscribeAction
//   taskNewAction
// plus the standard fileNewAction re-labelled "New Window" (Ctrl+Shift+N).
// Never rename or reuse one. calToday/calPrevious/calNext are answered by
// the QML (their `triggered`); the view and mode groups are read from the
// entries' `checked`.
//
// Files: mail_host.cpp (view state, title, opened paths), mail_host_commands.cpp
// (entries, menus, enabled state).

#include "document_host.h"
#include "mail_service.h"

#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Mail {

class MailService;

// AGENT-NOTE: not `final` — QML_ELEMENT derives a creation helper from it.
class MailHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QindaOffice::Apps::Mail::MailService *service READ service CONSTANT)
    // Settable twin of DocumentHost::preferencesPage (MailPreferencesPage.qml).
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    // mail | calendar | tasks — the checked member of the "mailView" group.
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    // day | workweek | week | month — the checked member of "calendarMode".
    Q_PROPERTY(QString calendarMode READ calendarMode WRITE setCalendarMode NOTIFY calendarModeChanged)
    Q_PROPERTY(bool readingPaneVisible READ readingPaneVisible NOTIFY readingPaneVisibleChanged)
public:
    explicit MailHost(MailService *service, QObject *parent = nullptr);
    ~MailHost() override;

    [[nodiscard]] MailService *service() const { return m_service; }
    [[nodiscard]] QString view() const { return m_view; }
    void setView(const QString &view);
    [[nodiscard]] QString calendarMode() const { return m_calendarMode; }
    void setCalendarMode(const QString &mode);
    [[nodiscard]] bool readingPaneVisible() const;
    [[nodiscard]] QString title() const override;

    // Opens a composer for `draft` (the same route every command takes).
    Q_INVOKABLE void compose(const QVariantMap &draft);

    // Every id this host adds (tests; the ribbon layout is written against it).
    [[nodiscard]] static QStringList commandIds();

signals:
    void composeRequested(const QVariantMap &draft);
    // Empty id: Add Account; otherwise that account's settings.
    void accountDialogRequested(const QString &accountId);
    void eventDialogRequested();
    void calendarImportRequested();
    void calendarExportRequested();
    void calendarSubscribeRequested();
    void searchRequested();
    void moveRequested();
    void taskNewRequested();
    void viewChanged();
    void calendarModeChanged();
    void readingPaneVisibleChanged();

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] bool openWithoutDocument(const QString &path) override;
    [[nodiscard]] bool reusableWithoutDocument() const override { return true; }

private:
    // mail_host_commands.cpp
    void createCommands();
    void insertMenus();
    void syncCommandState();
    void toggleSelectedRead();
    void toggleSelectedFlag();
    [[nodiscard]] QString currentAccountId() const;
    [[nodiscard]] QStringList selectedIds() const;

    QPointer<MailService> m_service;
    QString m_view = QStringLiteral("mail");
    QString m_calendarMode = QStringLiteral("week");
};

} // namespace QindaOffice::Apps::Mail
