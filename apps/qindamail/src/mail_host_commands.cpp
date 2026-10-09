// SPDX-License-Identifier: GPL-3.0-or-later
// MailHost: the window's commands, its menus and their enabled state
// (mail_host.h lists the ids).
#include "mail_host.h"

#include "mail_organizer.h"
#include "mail_service.h"
#include "pst_import.h"
#include "standard_actions.h"

#include <QKeySequence>

namespace QindaOffice::Apps::Mail {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *shortcut;
    const char *icon;
};

// AGENT-GUARD: shortcuts must stay unique across the window's model (the
// standard set included: Ctrl+N is New Message here, so New Window moved to
// Ctrl+Shift+N; Ctrl+F stays Find). tst_qindamail_host checks it. Alt+Left/
// Right and Delete belong to Tk.CalendarGridView while it has focus, so the
// calendar commands avoid them and the message commands are disabled
// outside the mail view (a disabled QShortcut lets the key through).
constexpr Command kCommands[] = {
    {"mailNewMessageAction", QT_TR_NOOP("New &Message"), "Ctrl+N", "mail-plus"},
    {"mailNewEventAction", QT_TR_NOOP("New &Event…"), "Ctrl+Shift+E", "calendar-plus"},
    {"taskNewAction", QT_TR_NOOP("New &Task"), "Ctrl+Shift+K", "list-plus"},
    {"mailReplyAction", QT_TR_NOOP("&Reply"), "Ctrl+R", "reply"},
    {"mailReplyAllAction", QT_TR_NOOP("Reply &All"), "Ctrl+Shift+R", "reply-all"},
    {"mailForwardAction", QT_TR_NOOP("&Forward"), "Ctrl+L", "arrow-right"},
    {"mailDeleteAction", QT_TR_NOOP("&Delete"), "Delete", "trash-2"},
    {"mailArchiveAction", QT_TR_NOOP("Arc&hive"), "Backspace", "archive"},
    {"mailToggleReadAction", QT_TR_NOOP("Mark as &Read"), "Ctrl+U", "mail-open"},
    {"mailToggleFlagAction", QT_TR_NOOP("F&lag"), "Insert", "flag"},
    {"mailMoveAction", QT_TR_NOOP("&Move to…"), "Ctrl+Shift+V", "folder"},
    {"mailSyncAction", QT_TR_NOOP("&Sync All"), "F5", "refresh-cw"},
    {"mailSearchAction", QT_TR_NOOP("&Search Mail"), "Ctrl+E", "search"},
    {"mailAddAccountAction", QT_TR_NOOP("&Add Account…"), "", "user-plus"},
    {"mailAccountSettingsAction", QT_TR_NOOP("Account &Settings…"), "", "settings"},
    {"mailViewMailAction", QT_TR_NOOP("&Mail"), "Ctrl+1", "mail"},
    {"mailViewCalendarAction", QT_TR_NOOP("&Calendar"), "Ctrl+2", "calendar"},
    {"mailViewTasksAction", QT_TR_NOOP("&Tasks"), "Ctrl+3", "list-todo"},
    {"mailReadingPaneAction", QT_TR_NOOP("&Reading Pane"), "", "panel-right"},
    {"mailConversationsAction", QT_TR_NOOP("Show as &Conversations"), "", "mails"},
    {"calTodayAction", QT_TR_NOOP("&Today"), "Ctrl+T", "calendar-check"},
    {"calPreviousAction", QT_TR_NOOP("&Previous"), "Ctrl+PgUp", "chevron-left"},
    {"calNextAction", QT_TR_NOOP("&Next"), "Ctrl+PgDown", "chevron-right"},
    {"calDayAction", QT_TR_NOOP("&Day"), "Ctrl+Alt+1", "calendar-clock"},
    {"calWorkWeekAction", QT_TR_NOOP("&Work Week"), "Ctrl+Alt+2", "calendar-range"},
    {"calWeekAction", QT_TR_NOOP("W&eek"), "Ctrl+Alt+3", "columns-3"},
    {"calMonthAction", QT_TR_NOOP("&Month"), "Ctrl+Alt+4", "calendar-days"},
    {"calImportAction", QT_TR_NOOP("&Import Calendar…"), "", "import"},
    {"calExportAction", QT_TR_NOOP("E&xport Calendar…"), "", "file-output"},
    {"calSubscribeAction", QT_TR_NOOP("Su&bscribe to Calendar…"), "", "link-2"},
    // M64 (D-072): People, message files, pictures, PDF. Ctrl+Shift+C and
    // F12 are Outlook's New Contact and Save As.
    {"mailViewPeopleAction", QT_TR_NOOP("&People"), "Ctrl+4", "users"},
    {"contactNewAction", QT_TR_NOOP("New &Contact"), "Ctrl+Shift+C", "user-plus"},
    {"contactDeleteAction", QT_TR_NOOP("&Delete Contact"), "", "trash-2"},
    {"contactImportAction", QT_TR_NOOP("&Import Contacts…"), "", "import"},
    {"contactExportAction", QT_TR_NOOP("E&xport Contacts…"), "", "file-output"},
    {"mailAddSenderAction", QT_TR_NOOP("Add Sender to Co&ntacts"), "", "book-user"},
    {"mailShowPicturesAction", QT_TR_NOOP("Show &Pictures"), "", "image"},
    {"mailSaveMessageAction", QT_TR_NOOP("Save Message &As…"), "F12", "save"},
    {"mailSaveAttachmentsAction", QT_TR_NOOP("Save All Attac&hments…"), "", "paperclip"},
    {"mailImportMboxAction", QT_TR_NOOP("Import Mail&box…"), "", "mailbox"},
    {"mailExportPdfAction", QT_TR_NOOP("Export as P&DF…"), "", "file-down"},
    // M76 (D-085): Outlook data files, contact groups (Ctrl+Shift+L is
    // Outlook's New Contact Group), CardDAV address books.
    {"mailImportPstAction", QT_TR_NOOP("Outlook &Data File (.pst)…"), "", "archive-restore"},
    {"contactNewGroupAction", QT_TR_NOOP("New Contact &Group"), "Ctrl+Shift+L", "users-round"},
    {"contactAddressBookAction", QT_TR_NOOP("Connect &Address Book…"), "", "cloud"},
};

const QString kViewGroup = QStringLiteral("mailView");
const QString kModeGroup = QStringLiteral("calendarMode");

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

} // namespace

QStringList MailHost::commandIds()
{
    QStringList ids;
    for (const Command &command : kCommands) ids.append(QString::fromLatin1(command.id));
    return ids + organizerCommandIds();
}

void MailHost::createCommands()
{
    for (const Command &command : kCommands) {
        actions()->add(QString::fromLatin1(command.id), tr(command.text),
                       QKeySequence(QString::fromLatin1(command.shortcut)), QString::fromLatin1(command.icon));
    }
    // The standard New opens another main window over the same service.
    ActionEntry *newWindow = action(QString::fromLatin1(ActionId::kFileNew));
    newWindow->setText(tr("New &Window"));
    newWindow->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    newWindow->setIconName(QStringLiteral("panels-top-left"));

    for (const char *id : {"mailViewMailAction", "mailViewCalendarAction", "mailViewTasksAction",
                           "mailViewPeopleAction"}) {
        ActionEntry *entry = action(QString::fromLatin1(id));
        entry->setCheckable(true);
        entry->setRadioGroup(kViewGroup);
    }
    for (const char *id : {"calDayAction", "calWorkWeekAction", "calWeekAction", "calMonthAction"}) {
        ActionEntry *entry = action(QString::fromLatin1(id));
        entry->setCheckable(true);
        entry->setRadioGroup(kModeGroup);
    }
    action(QStringLiteral("mailViewMailAction"))->setChecked(true);
    action(QStringLiteral("calWeekAction"))->setChecked(true);
    ActionEntry *pane = action(QStringLiteral("mailReadingPaneAction"));
    pane->setCheckable(true);
    pane->setChecked(true);
    ActionEntry *threads = action(QStringLiteral("mailConversationsAction"));
    threads->setCheckable(true);
    threads->setChecked(m_service->threaded());

    const auto on = [this](const char *id, auto &&slot) {
        connect(action(QString::fromLatin1(id)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    const auto selected = [this] { return m_service->selectedMessageId(); };
    on("mailNewMessageAction", [this] { compose(m_service->newDraft(currentAccountId())); });
    on("mailReplyAction", [this, selected] { compose(m_service->replyDraft(selected(), false)); });
    on("mailReplyAllAction", [this, selected] { compose(m_service->replyDraft(selected(), true)); });
    on("mailForwardAction", [this, selected] { compose(m_service->forwardDraft(selected())); });
    on("mailDeleteAction", [this] { static_cast<void>(m_service->deleteMessages(selectedIds())); });
    on("mailArchiveAction", [this] { static_cast<void>(m_service->archive(selectedIds())); });
    on("mailToggleReadAction", [this] { toggleSelectedRead(); });
    on("mailToggleFlagAction", [this] { toggleSelectedFlag(); });
    on("mailMoveAction", [this] { emit moveRequested(); });
    on("mailSyncAction", [this] { static_cast<void>(m_service->syncAll()); });
    on("mailSearchAction", [this] {
        setView(QStringLiteral("mail"));
        emit searchRequested();
    });
    on("mailAddAccountAction", [this] { emit accountDialogRequested(QString()); });
    on("mailAccountSettingsAction", [this] { emit accountDialogRequested(currentAccountId()); });
    on("mailNewEventAction", [this] { emit eventDialogRequested(); });
    on("taskNewAction", [this] {
        setView(QStringLiteral("tasks"));
        emit taskNewRequested();
    });
    on("calImportAction", [this] { emit calendarImportRequested(); });
    on("calExportAction", [this] { emit calendarExportRequested(); });
    on("calSubscribeAction", [this] { emit calendarSubscribeRequested(); });
    on("calTodayAction", [this] { setView(QStringLiteral("calendar")); });
    on("contactNewAction", [this] {
        setView(QStringLiteral("people"));
        emit contactNewRequested();
    });
    on("contactDeleteAction", [this] {
        if (!m_selectedContactId.isEmpty()) emit contactDeleteRequested(m_selectedContactId);
    });
    on("contactImportAction", [this] { emit contactsImportRequested(); });
    on("contactExportAction", [this] { emit contactsExportRequested(); });
    on("mailAddSenderAction", [this, selected] { static_cast<void>(m_service->addSenderToContacts(selected())); });
    on("mailShowPicturesAction", [this, selected] { m_service->showPictures(selected()); });
    on("mailSaveMessageAction", [this] { emit messageSaveRequested(); });
    on("mailSaveAttachmentsAction", [this] { emit attachmentsSaveRequested(); });
    on("mailImportMboxAction", [this] { emit mailboxImportRequested(); });
    on("mailExportPdfAction", [this] { emit pdfExportRequested(); });
    on("mailImportPstAction", [this] { emit outlookImportRequested(); });
    on("contactNewGroupAction", [this] {
        setView(QStringLiteral("people"));
        emit contactGroupNewRequested();
    });
    on("contactAddressBookAction", [this] { emit addressBookRequested(); });
    createOrganizerCommands();   // M81
    connect(action(QString::fromLatin1(ActionId::kEditFind)), &ActionEntry::triggered, this,
            [this] { emit searchRequested(); });
    actions()->setEnabled(QString::fromLatin1(ActionId::kEditFind), true);

    const QList<QPair<const char *, QString>> views{{"mailViewMailAction", QStringLiteral("mail")},
                                                    {"mailViewCalendarAction", QStringLiteral("calendar")},
                                                    {"mailViewTasksAction", QStringLiteral("tasks")},
                                                    {"mailViewPeopleAction", QStringLiteral("people")}};
    for (const auto &[id, view] : views) {
        connect(action(QString::fromLatin1(id)), &ActionEntry::toggled, this, [this, view](bool checked) {
            if (checked) setView(view);
        });
    }
    const QList<QPair<const char *, QString>> modes{{"calDayAction", QStringLiteral("day")},
                                                    {"calWorkWeekAction", QStringLiteral("workweek")},
                                                    {"calWeekAction", QStringLiteral("week")},
                                                    {"calMonthAction", QStringLiteral("month")}};
    for (const auto &[id, mode] : modes) {
        connect(action(QString::fromLatin1(id)), &ActionEntry::toggled, this, [this, mode](bool checked) {
            if (!checked) return;
            setCalendarMode(mode);
            setView(QStringLiteral("calendar"));
        });
    }
    connect(pane, &ActionEntry::toggled, this, &MailHost::readingPaneVisibleChanged);
    connect(threads, &ActionEntry::toggled, this, [this](bool checked) { m_service->setThreaded(checked); });
    connect(m_service, &MailService::threadedChanged, this, [this] {
        action(QStringLiteral("mailConversationsAction"))->setChecked(m_service->threaded());
    });
}

void MailHost::insertMenus()
{
    Quick::MenuTree *tree = menuTree();
    const QString file = QStringLiteral("fileMenu");
    const QString newWindow = QString::fromLatin1(ActionId::kFileNew);
    for (const char *id : {"mailNewMessageAction", "mailNewEventAction", "taskNewAction", "contactNewAction",
                           "contactNewGroupAction"})
        tree->insertInto(file, newWindow, a(id));
    const QString close = QString::fromLatin1(ActionId::kFileCloseWindow);
    // AGENT-GUARD: an id appears ONCE in the menu tree (the panel export's
    // validator rejects duplicates); the contact files live in People.
    for (const char *id : {"mailSaveMessageAction", "mailSaveAttachmentsAction", "mailExportPdfAction"})
        tree->insertInto(file, close, a(id));
    // M76: File ▸ Import ▸ Outlook Data File / Mailbox.
    tree->insertInto(file, close, MenuNode::submenu(QStringLiteral("mailImportMenu"), tr("&Import"),
                                                    {a("mailImportPstAction"), a("mailImportMboxAction")}));
    tree->insertInto(file, close, MenuNode::separator());
    for (const char *id : {"calImportAction", "calExportAction", "calSubscribeAction"})
        tree->insertInto(file, close, a(id));
    tree->insertInto(file, close, MenuNode::separator());

    tree->insertInto(QStringLiteral("editMenu"), QString(), MenuNode::separator());
    tree->insertInto(QStringLiteral("editMenu"), QString(), a("mailSearchAction"));

    const QString zoomIn = QString::fromLatin1(ActionId::kViewZoomIn);
    for (const char *id : {"mailViewMailAction", "mailViewCalendarAction", "mailViewTasksAction",
                           "mailViewPeopleAction"})
        tree->insertInto(QStringLiteral("viewMenu"), zoomIn, a(id));
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, MenuNode::separator());
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, a("mailReadingPaneAction"));
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, a("mailConversationsAction"));
    tree->insertInto(QStringLiteral("viewMenu"), zoomIn, MenuNode::separator());

    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("mailMessageMenu"), tr("&Message"),
                                             {a("mailReplyAction"), a("mailReplyAllAction"), a("mailForwardAction"),
                                              MenuNode::separator(), a("mailToggleReadAction"),
                                              a("mailToggleFlagAction"), MenuNode::separator(),
                                              a("mailArchiveAction"), a("mailMoveAction"), a("mailDeleteAction"),
                                              MenuNode::separator(), a("mailAddSenderAction"),
                                              a("mailShowPicturesAction")}));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("mailPeopleMenu"), tr("&People"),
                                             {a("contactDeleteAction"), MenuNode::separator(),
                                              a("contactImportAction"), a("contactExportAction"),
                                              MenuNode::separator(), a("contactAddressBookAction")}));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("mailCalendarMenu"), tr("&Calendar"),
                                             {a("calTodayAction"), a("calPreviousAction"), a("calNextAction"),
                                              MenuNode::separator(), a("calDayAction"), a("calWorkWeekAction"),
                                              a("calWeekAction"), a("calMonthAction")}));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("mailAccountsMenu"), tr("A&ccounts"),
                                             {a("mailAddAccountAction"), a("mailAccountSettingsAction"),
                                              a("mailAutoRepliesAction"), MenuNode::separator(), a("mailSyncAction")}));
    insertOrganizerMenus();   // M81: Message ▸ Categorize / Follow Up / Rules, the Folder menu
}

QString MailHost::currentAccountId() const
{
    const QString key = m_service->currentFolderKey();
    const QString account = key.startsWith(QStringLiteral("acct:")) ? key.section(QLatin1Char(':'), 1, 1) : QString();
    if (!account.isEmpty() && account != QLatin1String(kLocalAccountId)) return account;
    const QVariantMap selected = m_service->selectedMessage();
    const QString owner = selected.value(QStringLiteral("accountId")).toString();
    if (!owner.isEmpty() && owner != QLatin1String(kLocalAccountId)) return owner;
    for (const QVariant &value : m_service->accounts()) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (id != QLatin1String(kLocalAccountId)) return id;
    }
    return {};
}

QStringList MailHost::selectedIds() const
{
    const QString id = m_service->selectedMessageId();
    return id.isEmpty() ? QStringList() : QStringList{id};
}

void MailHost::toggleSelectedRead()
{
    const QVariantMap message = m_service->selectedMessage();
    static_cast<void>(m_service->markRead(selectedIds(), message.value(QStringLiteral("unread")).toBool()));
}

void MailHost::toggleSelectedFlag()
{
    // Outlook's Quick Click: a flag due today, or off again (M81).
    const QVariantMap message = m_service->selectedMessage();
    const bool flagged = message.value(QStringLiteral("flagged")).toBool()
        || message.value(QStringLiteral("followUpStatus")).toString() == QLatin1String("flagged");
    static_cast<void>(m_service->organizer()->followUp(selectedIds(), flagged ? QStringLiteral("clear") : QStringLiteral("today")));
}

void MailHost::syncCommandState()
{
    const QVariantMap message = m_service->selectedMessage();
    const bool mail = m_view == QLatin1String("mail");
    const bool any = mail && !message.isEmpty();
    const bool draft = message.value(QStringLiteral("isDraft")).toBool();
    const bool real = any && !draft;
    const QStringList respond{QStringLiteral("mailReplyAction"), QStringLiteral("mailReplyAllAction"),
                              QStringLiteral("mailForwardAction")};
    actions()->setEnabled(respond, real);
    actions()->setEnabled({QStringLiteral("mailToggleReadAction"), QStringLiteral("mailToggleFlagAction"),
                           QStringLiteral("mailArchiveAction"), QStringLiteral("mailMoveAction")},
                          real);
    actions()->setEnabled(QStringLiteral("mailDeleteAction"), any);
    // M64: what acts on the open message, and print (a message is the
    // "document" of a mail window; nothing selected, nothing to print).
    actions()->setEnabled({QStringLiteral("mailAddSenderAction"), QStringLiteral("mailShowPicturesAction"),
                           QStringLiteral("mailSaveMessageAction"), QStringLiteral("mailExportPdfAction"),
                           QString::fromLatin1(ActionId::kFilePrint), QString::fromLatin1(ActionId::kFilePrintPreview)},
                          real);
    actions()->setEnabled(QStringLiteral("mailSaveAttachmentsAction"),
                          real && !message.value(QStringLiteral("attachments")).toList().isEmpty());
    actions()->setEnabled(QStringLiteral("contactDeleteAction"),
                          m_view == QLatin1String("people") && !m_selectedContactId.isEmpty());
    action(QStringLiteral("mailToggleReadAction"))
        ->setText(message.value(QStringLiteral("unread")).toBool() ? tr("Mark as &Read") : tr("Mark as &Unread"));
    action(QStringLiteral("mailToggleFlagAction"))
        ->setText(message.value(QStringLiteral("flagged")).toBool() ? tr("C&lear Flag") : tr("F&lag"));
    // M76: one Outlook import at a time per process.
    actions()->setEnabled(QStringLiteral("mailImportPstAction"), !m_service->outlookImport()->running());
    syncOrganizerState();   // M81
    const bool calendar = m_view == QLatin1String("calendar");
    actions()->setEnabled({QStringLiteral("calPreviousAction"), QStringLiteral("calNextAction")}, calendar);
    actions()->setEnabled(QStringLiteral("mailReadingPaneAction"), mail);
    actions()->setEnabled(QStringLiteral("mailConversationsAction"), mail);
}

} // namespace QindaOffice::Apps::Mail
