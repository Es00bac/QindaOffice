// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_host.h"

#include "mail_service.h"

#include <QFileInfo>
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
    static const QStringList views{QStringLiteral("mail"), QStringLiteral("calendar"), QStringLiteral("tasks")};
    if (!views.contains(view)) return;
    const QString id = view == QLatin1String("mail") ? QStringLiteral("mailViewMailAction")
        : view == QLatin1String("calendar")          ? QStringLiteral("mailViewCalendarAction")
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
    const QString folder = m_service != nullptr ? m_service->currentFolderName() : QString();
    return folder.isEmpty() ? tr("Mail") : folder;
}

void MailHost::compose(const QVariantMap &draft)
{
    if (!draft.isEmpty()) emit composeRequested(draft);
}

QString MailHost::filterString() const
{
    return tr("Mail and calendar files (*.eml *.ics);;Messages (*.eml);;Calendars (*.ics);;All files (*)");
}

bool MailHost::openWithoutDocument(const QString &path)
{
    if (m_service == nullptr) return false;
    const QString lower = path.trimmed().toLower();
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
    if (suffix == QLatin1String("eml")) {
        const bool ok = !m_service->importMessageFile(local).isEmpty();
        if (ok) setView(QStringLiteral("mail"));
        return ok;
    }
    return false;
}

} // namespace QindaOffice::Apps::Mail
