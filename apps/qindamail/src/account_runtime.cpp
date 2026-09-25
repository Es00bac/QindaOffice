// SPDX-License-Identifier: GPL-3.0-or-later
#include "account_runtime.h"

#include "ews_calendar_provider.h"
#include "graph_calendar_provider.h"
#include "oauth2_session.h"
#include "provider_calls.h"

#include <qindatk/caldav_provider.h>
#include <qindatk/ics_calendar_provider.h>
#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/pop3_mail_provider.h>
#include <qindatk/smtp_mail_provider.h>

#include <QTimeZone>
#include <QUrl>

namespace QindaOffice::Apps::Mail {

using QindaOffice::Mail::CalendarEntry;
using QindaOffice::Mail::MailAccount;

namespace {

QString systemZone()
{
    return QString::fromLatin1(QTimeZone::systemTimeZoneId());
}

bool isDefaultEntry(const QString &id)
{
    return id.isEmpty() || id == QLatin1String("default") || id == QLatin1String("primary")
        || id == QLatin1String("calendar");
}

} // namespace

AccountRuntime::AccountRuntime(const MailAccount &account, const RuntimeContext &context, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_context(context)
{
    build();
}

AccountRuntime::~AccountRuntime() = default;

bool AccountRuntime::isImap() const
{
    return m_account.hasMail() && m_account.incoming.protocol == QLatin1String("imap");
}

bool AccountRuntime::isPop3() const
{
    return m_account.hasMail() && m_account.incoming.protocol == QLatin1String("pop3");
}

void AccountRuntime::build()
{
    const auto &in = m_account.incoming;
    const auto &out = m_account.outgoing;
    const QString incomingUser = in.userName.isEmpty() ? m_account.emailAddress : in.userName;
    if (isPop3()) {
        m_pop3 = new QindaTK::Pop3MailProvider(this);
        m_pop3->setProperty("host", in.host);
        m_pop3->setProperty("port", in.port);
        m_pop3->setProperty("security", in.security);
        m_pop3->setProperty("userName", incomingUser);
        m_pop3->setProperty("accountId", m_account.id);
        m_pop3->setProperty("leaveOnServer", in.leaveOnServer);
        m_pop3->setProperty("deleteAfterDays", in.deleteAfterDays);
        m_pop3->setStore(m_context.store);
        m_pop3->setAttachmentStore(m_context.attachments);
        m_context.calls->watch(m_pop3);
    }
    if (m_account.hasMail() && !out.isEmpty()) {
        m_smtp = new QindaTK::SmtpMailProvider(this);
        m_smtp->setProperty("host", out.host);
        m_smtp->setProperty("port", out.port);
        m_smtp->setProperty("security", out.security);
        const bool shared = out.sameCredentialsAsIncoming && !in.isEmpty();
        m_smtp->setProperty("userName", shared ? incomingUser
                                               : (out.userName.isEmpty() ? m_account.emailAddress : out.userName));
        m_smtp->setProperty("fromAddress", m_account.emailAddress);
        m_smtp->setProperty("fromName", m_account.fullName);
        m_smtp->setAttachmentStore(m_context.attachments);
        m_context.calls->watch(m_smtp);
    }
    buildCalendars();

    const bool wantsOAuth = !m_account.oauth.provider.isEmpty()
        && (usesOAuth(QStringLiteral("imap")) || usesOAuth(QStringLiteral("pop3")) || usesOAuth(QStringLiteral("smtp"))
            || usesOAuth(m_account.calendar.protocol));
    if (wantsOAuth) {
        m_oauth = new QindaOffice::Mail::OAuth2Session(this);
        m_oauth->setSecrets(m_context.secrets);
        const QVariantMap configured = m_oauth->configure(m_account.toVariantMap());
        if (!configured.value(QStringLiteral("ok")).toBool()) {
            m_oauthError = configured.value(QStringLiteral("error")).toString();
        }
        m_context.calls->watch(m_oauth);
    }
}

void AccountRuntime::buildCalendars()
{
    const auto &settings = m_account.calendar;
    const QString protocol = settings.protocol;
    if (protocol == QLatin1String("none") || protocol.isEmpty()) {
        return;
    }
    QList<CalendarEntry> entries;
    for (const CalendarEntry &entry : settings.calendars) {
        if (entry.enabled) {
            entries.append(entry);
        }
    }
    if (entries.isEmpty() && settings.calendars.isEmpty()) {
        CalendarEntry entry;
        entry.id = QStringLiteral("default");
        entries.append(entry);
    }
    const QString user = settings.userName.isEmpty() ? m_account.emailAddress : settings.userName;
    for (const CalendarEntry &entry : std::as_const(entries)) {
        CalendarBinding binding;
        binding.entryId = entry.id;
        binding.id = m_account.id + QLatin1Char(':') + entry.id;
        binding.name = entry.name.isEmpty() ? m_account.displayName : entry.name;
        binding.color = entry.color;
        binding.service = protocol;
        binding.readOnly = entry.extra.value(QStringLiteral("readOnly")).toBool();
        QString url = entry.url.isEmpty() ? settings.url : entry.url;
        if (url.startsWith(QLatin1Char('/'))) {
            url = QUrl(settings.url).resolved(QUrl(url)).toString();
        }
        QObject *provider = nullptr;
        if (protocol == QLatin1String("caldav")) {
            auto *caldav = new QindaTK::CalDavProvider(this);
            caldav->setProperty("collectionUrl", url);
            caldav->setProperty("userName", user);
            caldav->setStore(m_context.store);
            provider = caldav;
        } else if (protocol == QLatin1String("ics")) {
            auto *ics = new QindaTK::IcsCalendarProvider(this);
            // A file: URL is an import source (MailService imports it once
            // into this calendar); only network feeds are synced.
            if (!QUrl(url).isLocalFile()) {
                ics->setProperty("feedUrl", url);
            }
            ics->setProperty("userName", settings.userName);
            ics->setStore(m_context.store);
            binding.readOnly = !QUrl(url).isLocalFile();
            provider = ics;
        } else if (protocol == QLatin1String("ews")) {
            auto *ews = new QindaOffice::Mail::EwsCalendarProvider(this);
            ews->setProperty("url", settings.url);
            ews->setProperty("userName", user);
            ews->setProperty("folderId", isDefaultEntry(entry.id) ? QString() : entry.id);
            ews->setStore(m_context.store);
            provider = ews;
        } else if (protocol == QLatin1String("graph")) {
            auto *graph = new QindaOffice::Mail::GraphCalendarProvider(this);
            graph->setProperty("graphCalendarId", isDefaultEntry(entry.id) ? QString() : entry.id);
            graph->setStore(m_context.store);
            provider = graph;
        }
        if (provider == nullptr) {
            continue;
        }
        provider->setProperty("accountId", m_account.id);
        provider->setProperty("calendarId", binding.id);
        provider->setProperty("defaultZone", systemZone());
        m_context.calls->watch(provider);
        binding.provider = provider;
        m_calendars.append(binding);
    }
}

QindaTK::ImapMailProvider *AccountRuntime::imap(const QString &mailbox)
{
    if (!isImap()) {
        return nullptr;
    }
    const QString name = mailbox.isEmpty() ? QStringLiteral("INBOX") : mailbox;
    if (QindaTK::ImapMailProvider *existing = m_imap.value(name)) {
        return existing;
    }
    const auto &in = m_account.incoming;
    auto *provider = new QindaTK::ImapMailProvider(this);
    provider->setProperty("host", in.host);
    provider->setProperty("port", in.port);
    provider->setProperty("security", in.security);
    provider->setProperty("userName", in.userName.isEmpty() ? m_account.emailAddress : in.userName);
    provider->setProperty("accountId", m_account.id);
    provider->setProperty("mailbox", name);
    const QString archive = specialMailbox(QStringLiteral("archive"));
    provider->setProperty("archiveMailbox", archive.isEmpty() ? QStringLiteral("Archive") : archive);
    provider->setStore(m_context.store);
    provider->setAttachmentStore(m_context.attachments);
    m_context.calls->watch(provider);
    m_imap.insert(name, provider);
    return provider;
}

const CalendarBinding *AccountRuntime::calendar(const QString &calendarId) const
{
    for (const CalendarBinding &binding : m_calendars) {
        if (binding.id == calendarId) {
            return &binding;
        }
    }
    return nullptr;
}

void AccountRuntime::setStatus(const QString &status, const QString &error)
{
    if (status == m_status && error == m_lastError) {
        return;
    }
    m_status = status;
    m_lastError = error;
    emit statusChanged();
}

void AccountRuntime::markSynced()
{
    m_lastSync = QDateTime::currentDateTimeUtc();
    emit statusChanged();
}

void AccountRuntime::noteResult(const QVariantMap &result)
{
    const QString state = result.value(QStringLiteral("state")).toString();
    const QString error = result.value(QStringLiteral("error")).toString();
    if (state == QLatin1String("accepted")) {
        setStatus(QStringLiteral("idle"));
    } else if (state == QLatin1String("offline")) {
        setStatus(QStringLiteral("offline"), error);
    } else if (isAuthRejection(result)) {
        setStatus(QStringLiteral("needsSignIn"), error);
    } else {
        setStatus(QStringLiteral("error"), error);
    }
}

void AccountRuntime::adjustPending(int delta)
{
    m_pending = qMax(0, m_pending + delta);
    emit pendingChanged();
}

void AccountRuntime::setMailboxes(const QVariantList &mailboxes)
{
    m_mailboxes = mailboxes;
    // The archive role may have been learned from SPECIAL-USE just now.
    const QString archive = specialMailbox(QStringLiteral("archive"));
    for (QindaTK::ImapMailProvider *provider : std::as_const(m_imap)) {
        provider->setProperty("archiveMailbox", archive.isEmpty() ? QStringLiteral("Archive") : archive);
    }
}

QString AccountRuntime::specialMailbox(const QString &role) const
{
    if (role == QLatin1String("inbox")) {
        return QStringLiteral("INBOX");
    }
    for (const QVariant &value : m_mailboxes) {
        const QVariantMap mailbox = value.toMap();
        if (mailbox.value(QStringLiteral("specialUse")).toString() == role
            && mailbox.value(QStringLiteral("selectable"), true).toBool()) {
            return mailbox.value(QStringLiteral("name")).toString();
        }
    }
    const auto &folders = m_account.specialFolders;
    if (role == QLatin1String("sent")) return folders.sent;
    if (role == QLatin1String("drafts")) return folders.drafts;
    if (role == QLatin1String("trash")) return folders.trash;
    if (role == QLatin1String("archive")) return folders.archive;
    if (role == QLatin1String("junk")) return folders.junk;
    return {};
}

bool AccountRuntime::serverFilesSentMail() const
{
    return m_account.kind == QLatin1String(QindaOffice::Mail::kKindGmail)
        || m_account.kind == QLatin1String(QindaOffice::Mail::kKindMicrosoft365);
}

} // namespace QindaOffice::Apps::Mail
