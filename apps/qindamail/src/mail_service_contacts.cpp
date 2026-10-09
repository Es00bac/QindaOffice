// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: the address book's place in mail (M64, D-072) — "Add sender
// to contacts", recipient suggestions (contacts plus addresses seen in
// mail), remote pictures shown on request, and opening links.
//
// AGENT-GUARD (privacy): nothing here touches the network unless the user
// asked to see the pictures of THIS message (showPictures) or always for its
// sender; the fetch is http(s) only, bounded in size, count and time, sends
// no cookies and keeps nothing on disk.
#include "mail_service.h"

#include "contact.h"
#include "contact_store.h"
#include "message_content.h"
#include "message_render.h"

#include <qindatk/local_productivity_store.h>
#include <qindatk/mail_mime.h>

#include <QDesktopServices>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace QindaOffice::Apps::Mail {

namespace {

constexpr qint64 kMaximumPictureBytes = 8 * 1024 * 1024;
constexpr int kMaximumPicturesPerMessage = 64;
constexpr int kRemoteCacheMessages = 8;
constexpr int kMaximumTrustedSenders = 2000;
constexpr int kMaximumSeenAddresses = 5000;

bool matchesWords(const QString &haystack, const QStringList &words)
{
    // Every word must start a word of the haystack (name parts, address).
    for (const QString &word : words) {
        bool found = false;
        qsizetype at = 0;
        while (!found && (at = haystack.indexOf(word, at, Qt::CaseInsensitive)) >= 0) {
            found = at == 0 || !haystack.at(at - 1).isLetterOrNumber();
            ++at;
        }
        if (!found) return false;
    }
    return true;
}

} // namespace

QVariantMap MailService::addSenderToContacts(const QString &messageId)
{
    const QVariantMap message = messageById(messageId);
    const QString address = message.value(QStringLiteral("senderAddress")).toString().trimmed();
    if (address.isEmpty()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("This message has no sender address")}};
    }
    QString name = message.value(QStringLiteral("sender")).toString().trimmed();
    if (name.compare(address, Qt::CaseInsensitive) == 0) name.clear();
    const QVariantMap result = m_contacts->addSender(name, address);
    if (result.value(QStringLiteral("ok")).toBool()) {
        emit notify(result.value(QStringLiteral("existed")).toBool() ? tr("Already in your contacts")
                                                                       : tr("Added to your contacts"),
                    name.isEmpty() ? address : name, QStringLiteral("success"));
    } else {
        emit notify(tr("Could not add the contact"), result.value(QStringLiteral("error")).toString(),
                    QStringLiteral("danger"));
    }
    m_seenDirty = true;
    return result;
}

void MailService::rebuildSeenAddresses() const
{
    // Senders and recipients of stored mail and drafts, by frequency then
    // recency: what Outlook's autocomplete list learns from.
    QHash<QString, SeenAddress> seen;
    const auto note = [&seen](const QString &name, const QString &address, const QString &date) {
        const QString key = address.trimmed().toLower();
        if (key.isEmpty() || !QindaTK::Mime::validAddress(key)) return;
        SeenAddress &entry = seen[key];
        entry.address = address.trimmed();
        if (!name.trimmed().isEmpty() && name.trimmed() != address.trimmed()) entry.name = name.trimmed();
        ++entry.count;
        entry.lastDate = std::max(entry.lastDate, date);
    };
    const auto noteList = [&note](const QString &list, const QString &date) {
        QString error;
        for (const QindaTK::Mime::Address &address : QindaTK::Mime::parseAddressList(list, &error))
            note(address.name, address.address, date);
    };
    for (const QVariantList &records : {m_store->messages(), m_store->drafts()}) {
        for (const QVariant &value : records) {
            const QVariantMap record = value.toMap();
            const QString date = record.value(QStringLiteral("date")).toString();
            note(record.value(QStringLiteral("sender")).toString(), record.value(QStringLiteral("senderAddress")).toString(), date);
            for (const char *field : {"to", "cc", "bcc"}) noteList(record.value(QString::fromLatin1(field)).toString(), date);
        }
    }
    const QStringList own = ownAddresses();
    m_seen.clear();
    for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
        if (!own.contains(it.key(), Qt::CaseInsensitive)) m_seen.append(it.value());
    }
    std::sort(m_seen.begin(), m_seen.end(), [](const SeenAddress &a, const SeenAddress &b) {
        return a.count != b.count ? a.count > b.count : a.lastDate > b.lastDate;
    });
    if (m_seen.size() > kMaximumSeenAddresses) m_seen.resize(kMaximumSeenAddresses);
    m_seenDirty = false;
}

QVariantList MailService::recipientSuggestions(const QString &query, int limit) const
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty() || limit <= 0) return {};
    QVariantList out;
    QSet<QString> used;
    for (const QVariant &value : m_contacts->complete(trimmed, limit)) {
        QVariantMap candidate = value.toMap();
        const QString address = candidate.value(QStringLiteral("value")).toString();
        if (address.isEmpty() || used.contains(address.toLower())) continue;
        used.insert(address.toLower());
        candidate.insert(QStringLiteral("valid"), true);
        out.append(candidate);
    }
    if (m_seenDirty) rebuildSeenAddresses();
    const QStringList words = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const SeenAddress &entry : std::as_const(m_seen)) {
        if (out.size() >= limit) break;
        if (used.contains(entry.address.toLower())) continue;
        if (!matchesWords(entry.name + QLatin1Char(' ') + entry.address, words)) continue;
        used.insert(entry.address.toLower());
        out.append(QVariantMap{{QStringLiteral("label"), QindaOffice::Mail::formatMailbox(entry.name, entry.address)},
                               {QStringLiteral("value"), entry.address}, {QStringLiteral("name"), entry.name},
                               {QStringLiteral("valid"), true}});
    }
    return out;
}

void MailService::showPictures(const QString &messageId)
{
    if (messageId.isEmpty()) return;
    m_picturesShown.insert(messageId);
    emit picturesChanged(messageId);
    fetchRemoteImages(messageId);
}

bool MailService::picturesShown(const QString &messageId) const
{
    return m_picturesShown.contains(messageId);
}

void MailService::setSenderPicturesAllowed(const QString &address, bool allowed)
{
    const QString key = address.trimmed().toLower();
    if (key.isEmpty() || allowed == m_picturesSenders.contains(key)) return;
    if (allowed && m_picturesSenders.size() >= kMaximumTrustedSenders) {
        emit notify(tr("Too many trusted senders"),
                    tr("Pictures always load for at most %1 senders.").arg(kMaximumTrustedSenders),
                    QStringLiteral("warning"));
        return;
    }
    if (allowed) m_picturesSenders.insert(key);
    else m_picturesSenders.remove(key);
    QStringList list(m_picturesSenders.cbegin(), m_picturesSenders.cend());
    list.sort();
    saveSetting(QStringLiteral("picturesSenders"), list);
    // Every message of that sender changes; the views re-render the one they show.
    emit picturesChanged(QString());
    if (allowed && !m_selectedMessageId.isEmpty()) fetchRemoteImages(m_selectedMessageId);
}

bool MailService::senderPicturesAllowed(const QString &address) const
{
    return m_picturesSenders.contains(address.trimmed().toLower());
}

QindaOffice::Mail::RenderPolicy MailService::renderPolicy(const QString &messageId) const
{
    QindaOffice::Mail::RenderPolicy policy;
    const QString sender = messageById(messageId).value(QStringLiteral("senderAddress")).toString();
    policy.allowRemote = m_picturesShown.contains(messageId) || senderPicturesAllowed(sender);
    policy.remoteImages = m_remoteImages.value(messageId);
    return policy;
}

void MailService::fetchRemoteImages(const QString &messageId, const QStringList &urls)
{
    QStringList wanted = urls;
    if (wanted.isEmpty()) {
        QindaOffice::Mail::RenderPolicy policy = renderPolicy(messageId);
        if (!policy.allowRemote) return;
        policy.attachedImagesBelow = false;
        wanted = QindaOffice::Mail::renderBody(contentOf(messageId), policy).remoteImages;
    }
    if (!m_fetchRemote) {
        m_fetchRemote = [this](const QUrl &url, FetchDone done) {
            if (m_network == nullptr) {
                m_network = new QNetworkAccessManager(this);
                m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
            }
            QNetworkRequest request(url);
            request.setTransferTimeout(15000);
            request.setMaximumRedirectsAllowed(5);
            request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
            request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 QindaMail"));
            QNetworkReply *reply = m_network->get(request);
            connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
                if (received > kMaximumPictureBytes) reply->abort();
            });
            connect(reply, &QNetworkReply::finished, this, [reply, done] {
                reply->deleteLater();
                const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
                done(bytes.size() > kMaximumPictureBytes ? QByteArray() : bytes, reply->errorString());
            });
        };
    }
    // The bound is per message across calls: fetched, failed and in flight.
    int known = int(m_remoteImages.value(messageId).size());
    for (const QString &key : std::as_const(m_fetching)) known += key.startsWith(messageId + QLatin1Char('\n'));
    for (const QString &value : std::as_const(wanted)) {
        const QUrl url(value);
        const QString key = messageId + QLatin1Char('\n') + value;
        if ((url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))
            || m_remoteImages.value(messageId).contains(value) || m_fetching.contains(key)
            || known >= kMaximumPicturesPerMessage) {
            continue;
        }
        ++known;
        m_fetching.insert(key);
        m_fetchRemote(url, [this, messageId, value, key](const QByteArray &bytes, const QString &) {
            m_fetching.remove(key);
            // An empty answer is remembered too: no retry loop on a dead link.
            m_remoteImages[messageId].insert(value, bytes);
            m_remoteOrder.removeAll(messageId);
            m_remoteOrder.append(messageId);
            while (m_remoteOrder.size() > kRemoteCacheMessages) m_remoteImages.remove(m_remoteOrder.takeFirst());
            emit picturesChanged(messageId);
        });
    }
}

bool MailService::openLink(const QString &link)
{
    const QUrl url(link.trimmed());
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("https") && scheme != QLatin1String("http")) || url.host().isEmpty()) {
        emit notify(tr("This link was not opened"), tr("Only web links (http and https) open from a message."),
                    QStringLiteral("warning"));
        return false;
    }
    const bool opened = m_openUrl ? m_openUrl(url) : QDesktopServices::openUrl(url);
    if (!opened) emit notify(tr("No browser opened the link"), url.toDisplayString(), QStringLiteral("warning"));
    return opened;
}

} // namespace QindaOffice::Apps::Mail
