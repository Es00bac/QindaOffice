// SPDX-License-Identifier: GPL-3.0-or-later
// MailHost: the M81 commands (D-090) — rules, categories, follow-up flags,
// search folders, automatic replies and conversations — their menus and
// enabled state (mail_host.h lists the ids).
#include "mail_host.h"

#include "mail_organizer.h"
#include "mail_service.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace QindaOffice::Apps::Mail {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *shortcut;
    const char *icon;
};

// AGENT-GUARD: ids are the panel's activation identity — append only, never
// rename or reuse (AGENTS.md rule 5). Ctrl+Shift+G (Custom flag) and
// Ctrl+Delete (Ignore) are Outlook's; Outlook's Ctrl+Shift+P (New Search
// Folder) is the suite's Print Preview, so that one has none here.
// tst_qindamail_host checks every shortcut is unique.
constexpr Command kOrganizerCommands[] = {
    {"mailQuickStepsAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Manage Quick &Steps…"), "", "zap"},
    {"mailRunQuickStepAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Run Quick S&tep…"), "", "play"},
    {"mailRulesAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Manage &Rules && Alerts…"), "", "wand"},
    {"mailCreateRuleAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Create R&ule…"), "", "wand-sparkles"},
    {"mailRunRulesAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Run Rules &Now…"), "", "play"},
    {"mailCategorizeAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Categori&ze"), "", "tag"},
    {"mailCategoriesAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "All &Categories…"), "", "sliders-horizontal"},
    {"mailClearCategoriesAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Clear &All Categories"), "", "eraser"},
    {"mailFlagTodayAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "&Today"), "", "flag"},
    {"mailFlagTomorrowAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "T&omorrow"), "", "calendar-days"},
    {"mailFlagThisWeekAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "This &Week"), "", "calendar-range"},
    {"mailFlagNextWeekAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "&Next Week"), "", "calendar-plus"},
    {"mailFlagNoDateAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "No &Date"), "", "flag"},
    {"mailFlagCustomAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "&Custom…"), "Ctrl+Shift+G", "calendar-clock"},
    {"mailFlagCompleteAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "&Mark Complete"), "", "check-check"},
    {"mailFlagClearAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "C&lear Flag"), "", "circle-x"},
    {"mailNewSearchFolderAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "New Search &Folder…"), "", "folder-plus"},
    {"mailEditSearchFolderAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Customi&ze This Search Folder…"), "", "settings-2"},
    {"mailDeleteSearchFolderAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "&Delete Search Folder"), "", "trash"},
    {"mailAutoRepliesAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Automatic Re&plies…"), "", "message-square"},
    {"mailCleanUpAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "Clean &Up Conversation"), "", "sparkles"},
    {"mailIgnoreAction", QT_TRANSLATE_NOOP("QindaOffice::Apps::Mail::MailHost", "&Ignore Conversation"), "Ctrl+Delete", "mail-x"},
};

const QList<QPair<const char *, const char *>> &flagPresets()
{
    static const QList<QPair<const char *, const char *>> presets{
        {"mailFlagTodayAction", "today"},       {"mailFlagTomorrowAction", "tomorrow"},
        {"mailFlagThisWeekAction", "thisWeek"}, {"mailFlagNextWeekAction", "nextWeek"},
        {"mailFlagNoDateAction", "noDate"},     {"mailFlagCompleteAction", "complete"},
        {"mailFlagClearAction", "clear"}};
    return presets;
}

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

} // namespace

QStringList MailHost::organizerCommandIds()
{
    QStringList ids;
    for (const Command &command : kOrganizerCommands) ids.append(QString::fromLatin1(command.id));
    return ids;
}

QVariantMap MailHost::categorizeTarget() const
{
    if (m_view == QLatin1String("people") && !m_selectedContactId.isEmpty())
        return {{QStringLiteral("kind"), QStringLiteral("contact")}, {QStringLiteral("ids"), QStringList{m_selectedContactId}},
                {QStringLiteral("accountId"), QString::fromLatin1(kLocalAccountId)}};
    const QVariantMap message = m_service->selectedMessage();
    if (m_view != QLatin1String("mail") || message.isEmpty() || message.value(QStringLiteral("isDraft")).toBool()) return {};
    return {{QStringLiteral("kind"), QStringLiteral("message")}, {QStringLiteral("ids"), selectedIds()},
            {QStringLiteral("accountId"), message.value(QStringLiteral("accountId"))}};
}

void MailHost::createOrganizerCommands()
{
    for (const Command &command : kOrganizerCommands) {
        actions()->add(QString::fromLatin1(command.id),
                       QCoreApplication::translate("QindaOffice::Apps::Mail::MailHost", command.text),
                       QKeySequence(QString::fromLatin1(command.shortcut)), QString::fromLatin1(command.icon));
    }
    const auto on = [this](const char *id, auto &&slot) {
        connect(action(QString::fromLatin1(id)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    MailOrganizer *organizer = m_service->organizer();
    on("mailQuickStepsAction", [this] { emit quickStepsRequested(currentAccountId(),selectedIds(),false); });
    on("mailRunQuickStepAction", [this] { emit quickStepsRequested(currentAccountId(),selectedIds(),true); });
    on("mailRulesAction", [this] { emit rulesRequested(); });
    on("mailCreateRuleAction", [this, organizer] {
        const QVariantMap made = organizer->ruleFromMessage(m_service->selectedMessageId());
        emit ruleEditRequested(made.value(QStringLiteral("rule")).toMap(), made.value(QStringLiteral("suggestions")).toMap());
    });
    on("mailRunRulesAction", [this] { emit runRulesRequested(); });
    on("mailCategorizeAction", [this] {
        const QVariantMap target = categorizeTarget();
        if (!target.isEmpty())
            emit categorizeRequested(target.value(QStringLiteral("kind")).toString(), target.value(QStringLiteral("ids")).toStringList(),
                                     target.value(QStringLiteral("accountId")).toString());
    });
    on("mailCategoriesAction", [this] {
        const QVariantMap target = categorizeTarget();
        emit categoriesRequested(target.value(QStringLiteral("kind")).toString(), target.value(QStringLiteral("ids")).toStringList(),
                                 target.value(QStringLiteral("accountId")).toString());
    });
    on("mailClearCategoriesAction", [this, organizer] {
        const QVariantMap target = categorizeTarget();
        if (!target.isEmpty())
            static_cast<void>(organizer->setCategories(target.value(QStringLiteral("kind")).toString(),
                                                       target.value(QStringLiteral("ids")).toStringList(), {}));
    });
    for (const auto &[id, preset] : flagPresets()) {
        const QString value = QString::fromLatin1(preset);
        on(id, [this, organizer, value] { static_cast<void>(organizer->followUp(selectedIds(), value)); });
    }
    on("mailFlagCustomAction", [this] { emit followUpCustomRequested(selectedIds()); });
    on("mailNewSearchFolderAction", [this] {
        setView(QStringLiteral("mail"));
        emit searchFolderRequested(QString());
    });
    on("mailEditSearchFolderAction", [this] { emit searchFolderRequested(m_service->currentFolderKey()); });
    on("mailDeleteSearchFolderAction", [this, organizer] { static_cast<void>(organizer->deleteSearchFolder(m_service->currentFolderKey())); });
    on("mailAutoRepliesAction", [this] { emit autoRepliesRequested(); });
    on("mailCleanUpAction", [this, organizer] { static_cast<void>(organizer->cleanUpConversation(m_service->selectedMessageId())); });
    on("mailIgnoreAction", [this, organizer] { static_cast<void>(organizer->ignoreConversation(m_service->selectedMessageId())); });
    connect(organizer, &MailOrganizer::changed, this, &MailHost::syncCommandState);
    connect(m_service, &MailService::currentFolderKeyChanged, this, &MailHost::syncCommandState);
}

void MailHost::insertOrganizerMenus()
{
    Quick::MenuTree *tree = menuTree();
    // Message: Categorize, Follow Up and Rules after the read/flag pair.
    const QString message = QStringLiteral("mailMessageMenu");
    const QString archive = QStringLiteral("mailArchiveAction");
    tree->insertInto(message, archive,
                     MenuNode::submenu(QStringLiteral("mailCategorizeMenu"), tr("Ca&tegorize"),
                                       {a("mailCategorizeAction"), a("mailCategoriesAction"), a("mailClearCategoriesAction")}));
    tree->insertInto(message, archive,
                     MenuNode::submenu(QStringLiteral("mailFollowUpMenu"), tr("Follow &Up"),
                                       {a("mailFlagTodayAction"), a("mailFlagTomorrowAction"), a("mailFlagThisWeekAction"),
                                        a("mailFlagNextWeekAction"), a("mailFlagNoDateAction"), a("mailFlagCustomAction"),
                                        MenuNode::separator(), a("mailFlagCompleteAction"), a("mailFlagClearAction")}));
    tree->insertInto(message, archive,
                     MenuNode::submenu(QStringLiteral("mailRulesMenu"), tr("R&ules"),
                                       {a("mailCreateRuleAction"), a("mailRulesAction"), a("mailRunRulesAction")}));
    tree->insertInto(message, archive,
                     MenuNode::submenu(QStringLiteral("mailQuickStepsMenu"), tr("Quick &Steps"),
                                       {a("mailRunQuickStepAction"),a("mailQuickStepsAction")}));
    tree->insertInto(message, archive, MenuNode::separator());
    tree->insertInto(message, archive, a("mailCleanUpAction"));
    tree->insertInto(message, archive, a("mailIgnoreAction"));
    tree->insertInto(message, archive, MenuNode::separator());
    // Folder, as Outlook's Folder tab: the search folders.
    tree->insertMenuBefore(QStringLiteral("mailPeopleMenu"),
                           MenuNode::submenu(QStringLiteral("mailFolderMenu"), tr("Fol&der"),
                                             {a("mailNewSearchFolderAction"), a("mailEditSearchFolderAction"),
                                              a("mailDeleteSearchFolderAction")}));
}

void MailHost::syncOrganizerState()
{
    const QVariantMap message = m_service->selectedMessage();
    const bool mail = m_view == QLatin1String("mail");
    const bool real = mail && !message.isEmpty() && !message.value(QStringLiteral("isDraft")).toBool();
    QStringList onMessage{QStringLiteral("mailRunQuickStepAction"),QStringLiteral("mailCreateRuleAction"), QStringLiteral("mailFlagCustomAction"),
                          QStringLiteral("mailCleanUpAction"), QStringLiteral("mailIgnoreAction")};
    for (const auto &[id, preset] : flagPresets()) onMessage.append(QString::fromLatin1(id));
    actions()->setEnabled(onMessage, real);
    const bool categorize = !categorizeTarget().isEmpty();
    actions()->setEnabled({QStringLiteral("mailCategorizeAction"), QStringLiteral("mailClearCategoriesAction")}, categorize);
    const bool search = m_service->organizer()->isSearchKey(m_service->currentFolderKey());
    actions()->setEnabled({QStringLiteral("mailEditSearchFolderAction"), QStringLiteral("mailDeleteSearchFolderAction")}, mail && search);
    action(QStringLiteral("mailIgnoreAction"))
        ->setText(real && m_service->organizer()->isIgnored(m_service->selectedMessageId()) ? tr("Stop &Ignoring Conversation")
                                                                                             : tr("&Ignore Conversation"));
}

} // namespace QindaOffice::Apps::Mail
