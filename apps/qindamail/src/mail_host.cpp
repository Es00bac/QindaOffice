// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_host.h"

#include "contact_store.h"
#include "launch_arguments.h"
#include "mail_service.h"
#include "message_print.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Apps::Mail {

MailHost::MailHost(MailService *service, QObject *parent)
    : Quick::DocumentHost(parent)
    , m_service(service)
{
    createCommands();
    insertMenus();
    connect(service, &MailService::selectedMessageIdChanged, this, &MailHost::syncCommandState);
    connect(service, &MailService::selectedMessageChanged, this, &MailHost::syncCommandState);
    connect(service, &MailService::accountsChanged, this, &MailHost::syncCommandState);
    connect(service, &MailService::currentFolderNameChanged, this, &MailHost::titleChanged);
    connect(service, &MailService::statusTextChanged, this, [this] { setStatusInfo(m_service->statusText()); });
    setStatusInfo(service->statusText());
    syncCommandState();
    finalizeHost();
}

MailHost::~MailHost() = default;

void MailHost::setView(const QString &view)
{
    static const QStringList views{QStringLiteral("mail"), QStringLiteral("calendar"), QStringLiteral("tasks"),
                                   QStringLiteral("people")};
    if (!views.contains(view)) return;
    const QString id = view == QLatin1String("mail") ? QStringLiteral("mailViewMailAction")
        : view == QLatin1String("calendar")          ? QStringLiteral("mailViewCalendarAction")
        : view == QLatin1String("people")            ? QStringLiteral("mailViewPeopleAction")
                                                     : QStringLiteral("mailViewTasksAction");
    // The radio group is the source of truth; its toggled handler lands here.
    actions()->setChecked(id, true);
    if (view == m_view) return;
    m_view = view;
    emit viewChanged();
    emit titleChanged();
    syncCommandState();
}

void MailHost::setCalendarMode(const QString &mode)
{
    static const QStringList modes{QStringLiteral("day"), QStringLiteral("workweek"), QStringLiteral("week"),
                                   QStringLiteral("month")};
    if (!modes.contains(mode)) return;
    const QString id = mode == QLatin1String("day") ? QStringLiteral("calDayAction")
        : mode == QLatin1String("workweek")         ? QStringLiteral("calWorkWeekAction")
        : mode == QLatin1String("week")             ? QStringLiteral("calWeekAction")
                                                    : QStringLiteral("calMonthAction");
    actions()->setChecked(id, true);
    if (mode == m_calendarMode) return;
    m_calendarMode = mode;
    emit calendarModeChanged();
}

bool MailHost::readingPaneVisible() const
{
    const Quick::ActionEntry *entry = action(QStringLiteral("mailReadingPaneAction"));
    return entry != nullptr && entry->isChecked();
}

QString MailHost::title() const
{
    if (m_view == QLatin1String("calendar")) return tr("Calendar");
    if (m_view == QLatin1String("tasks")) return tr("Tasks");
    if (m_view == QLatin1String("people")) return tr("People");
    const QString folder = m_service != nullptr ? m_service->currentFolderName() : QString();
    return folder.isEmpty() ? tr("Mail") : folder;
}

void MailHost::compose(const QVariantMap &draft)
{
    if (!draft.isEmpty()) emit composeRequested(draft);
}

void MailHost::setSelectedContactId(const QString &id)
{
    if (id == m_selectedContactId) return;
    m_selectedContactId = id;
    emit selectedContactIdChanged();
    syncCommandState();
}

QString MailHost::filterString() const
{
    return tr("Mail, contact and calendar files (*.eml *.msg *.mbox *.vcf *.ics);;Messages (*.eml *.msg);;"
              "Outlook messages (*.msg);;Mailboxes (*.mbox);;Contacts (*.vcf *.vcard);;Calendars (*.ics);;"
              "All files (*)");
}

Quick::PrintRenderCallback MailHost::printRenderCallback() const
{
    if (m_service == nullptr || m_view != QLatin1String("mail")) return {};
    return MessagePrinter::callback(m_service, m_service->selectedMessageId());
}

bool MailHost::exportMessagePdf(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    return !path.isEmpty() && exportPdf(path);
}

QString MailHost::suggestedFileName(const QString &suffix) const
{
    QString name = m_service != nullptr ? m_service->selectedMessage().value(QStringLiteral("subject")).toString() : QString();
    for (const QChar c : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'), QLatin1Char('?'),
                          QLatin1Char('"'), QLatin1Char('<'), QLatin1Char('>'), QLatin1Char('|')}) {
        name.replace(c, QLatin1Char('_'));
    }
    name = name.simplified().left(120);
    if (name.isEmpty()) name = tr("Message");
    return name + QLatin1Char('.') + suffix;
}

QUrl MailHost::suggestedSaveUrl(const QString &fileName) const
{
    QString name = QFileInfo(fileName).fileName();
    if (name.isEmpty()) name = tr("Attachment");
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                               + QLatin1Char('/') + name);
}

bool MailHost::openWithoutDocument(const QString &path)
{
    if (m_service == nullptr) return false;
    const QString lower = path.trimmed().toLower();
    if (lower.startsWith(QString::fromLatin1(Launch::kComposeScheme))) {
        // `qindamail --attach <file> mailto:…` (launch_arguments.h, M48).
        const Launch::ComposeRequest request = Launch::parseComposeToken(path.trimmed());
        const QVariantMap base =
            request.mailto.isEmpty() ? m_service->newDraft() : m_service->mailtoDraft(request.mailto);
        const QVariantMap draft = m_service->attachFiles(base, request.attachments);
        QTimer::singleShot(0, this, [this, draft] { emit composeRequested(draft); });
        return request.ok;
    }
    if (lower.startsWith(QStringLiteral("mailto:"))) {
        // Queued: the window's QML is connected by the time it runs, also
        // for the first window a `qindamail mailto:…` launch creates.
        const QVariantMap draft = m_service->mailtoDraft(path.trimmed());
        QTimer::singleShot(0, this, [this, draft] { emit composeRequested(draft); });
        return true;
    }
    if (lower.startsWith(QStringLiteral("webcal:")) || lower.startsWith(QStringLiteral("webcals:"))
        || ((lower.startsWith(QStringLiteral("https:")) || lower.startsWith(QStringLiteral("http:")))
            && QUrl(path).path().endsWith(QStringLiteral(".ics"), Qt::CaseInsensitive))) {
        static_cast<void>(m_service->subscribeFeed(path.trimmed()));
        setView(QStringLiteral("calendar"));
        return true;
    }
    const QString local = lower.startsWith(QStringLiteral("file:")) ? QUrl(path).toLocalFile() : path;
    const QString suffix = QFileInfo(local).suffix().toLower();
    if (!QFileInfo(local).isFile()) return false;
    if (suffix == QLatin1String("ics") || suffix == QLatin1String("ical") || suffix == QLatin1String("ifb")) {
        static_cast<void>(m_service->importIcs(QUrl::fromLocalFile(local), QString()));
        setView(QStringLiteral("calendar"));
        return true;
    }
    if (suffix == QLatin1String("vcf") || suffix == QLatin1String("vcard")) {
        // A vCard from the file manager lands in the address book (M64).
        const QVariantMap result = m_service->contacts()->importFile(QUrl::fromLocalFile(local));
        const bool ok = result.value(QStringLiteral("ok")).toBool();
        emit m_service->notify(ok ? tr("Contacts imported") : tr("Could not import the contacts"),
                               ok ? tr("%1 added, %2 updated").arg(result.value(QStringLiteral("added")).toInt())
                                        .arg(result.value(QStringLiteral("updated")).toInt())
                                  : result.value(QStringLiteral("error")).toString(),
                               ok ? QStringLiteral("success") : QStringLiteral("danger"));
        if (ok) setView(QStringLiteral("people"));
        return ok;
    }
    if (suffix == QLatin1String("mbox") || suffix == QLatin1String("mbx")) {
        static_cast<void>(m_service->importMailbox(QUrl::fromLocalFile(local)));
        setView(QStringLiteral("mail"));
        return true;
    }
    if (suffix == QLatin1String("eml") || suffix == QLatin1String("msg")) {
        // importMessageFile also recognizes an mbox by its content.
        const bool ok = !m_service->importMessageFile(local).isEmpty();
        if (ok) setView(QStringLiteral("mail"));
        return ok;
    }
    return false;
}

} // namespace QindaOffice::Apps::Mail
