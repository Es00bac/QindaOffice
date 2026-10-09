// SPDX-License-Identifier: GPL-3.0-or-later
#include "imap_source_fetcher.h"

#include <qindatk/imap_session.h>
#include <qindatk/protocol_client.h>

#include <QMetaObject>

namespace QindaOffice::Apps::Mail {

namespace {

QVariantMap answer(bool ok, const QString &state, const QString &error, bool retryable = false)
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("state"), state}, {QStringLiteral("error"), error},
            {QStringLiteral("retryable"), retryable}};
}

} // namespace

ImapSourceFetcher::ImapSourceFetcher(const Server &server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

void ImapSourceFetcher::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism.isEmpty() ? QStringLiteral("auto") : mechanism;
}

void ImapSourceFetcher::clearSecret()
{
    m_secret.clear();
}

bool ImapSourceFetcher::fetch(const QString &requestId, const QString &mailbox, qint64 uidValidity, qint64 uid,
                              qint64 maximumBytes)
{
    const auto refuse = [this, requestId](const QString &error) {
        QVariantMap result = answer(false, QStringLiteral("rejected"), error);
        result.insert(QStringLiteral("requestId"), requestId);
        QMetaObject::invokeMethod(this, [this, requestId, result] { emit sourceFinished(requestId, result); },
                                  Qt::QueuedConnection);
        return false;
    };
    if (m_server.host.isEmpty() || mailbox.isEmpty() || uid <= 0 || uidValidity <= 0)
        return refuse(tr("The message is not stored from an IMAP mailbox"));
    if (m_secret.isEmpty()) return refuse(tr("The account has no credentials"));
    const QindaTK::Protocol::Endpoint endpoint{m_server.host, static_cast<quint16>(m_server.port), m_server.security,
                                               m_server.timeoutMs, false};
    const QindaTK::Protocol::Credentials credentials{m_server.userName, m_secret, m_mechanism};
    QindaTK::Protocol::startWorker(this, [endpoint, credentials, mailbox, uidValidity, uid, maximumBytes] {
        QindaTK::Protocol::Channel channel;
        channel.addSecret(credentials.secret);
        QindaTK::Imap::Session session(&channel);
        QVariantMap result;
        QString problem;
        qint64 validity = 0;
        qint64 exists = 0;
        if (session.open(endpoint, credentials) && session.select(mailbox, &validity, &exists)) {
            if (validity != uidValidity) {
                problem = QStringLiteral("conflict");
            } else {
                // The size first: a message above the bound is never downloaded.
                QList<QByteArray> sized;
                qint64 size = -1;
                if (session.command("UID FETCH " + QByteArray::number(uid) + " (UID RFC822.SIZE)", &sized)) {
                    for (const QindaTK::Imap::FetchItem &item : QindaTK::Imap::fetchItems(sized)) {
                        if (const QindaTK::Imap::Value *value = item.find("RFC822.SIZE")) size = value->text.toLongLong();
                    }
                }
                if (size < 0 && !session.failed()) {
                    problem = QStringLiteral("gone");
                } else if (size > maximumBytes) {
                    problem = QStringLiteral("large");
                } else if (!session.failed()) {
                    QList<QByteArray> untagged;
                    if (session.command("UID FETCH " + QByteArray::number(uid) + " (UID BODY.PEEK[])", &untagged)) {
                        for (const QindaTK::Imap::FetchItem &item : QindaTK::Imap::fetchItems(untagged)) {
                            const QindaTK::Imap::Value *body = item.find("BODY[");
                            if (body != nullptr && body->kind != QindaTK::Imap::Value::Nil)
                                result.insert(QStringLiteral("source"), body->text);
                        }
                        if (!result.contains(QStringLiteral("source"))) problem = QStringLiteral("gone");
                    }
                }
            }
        }
        const QindaTK::Protocol::Failure failure = channel.failure();
        if (failure == QindaTK::Protocol::Failure::None) session.logout();
        const bool ok = failure == QindaTK::Protocol::Failure::None && problem.isEmpty()
            && result.contains(QStringLiteral("source"));
        QString error;
        QString state = QStringLiteral("accepted");
        if (!ok) {
            state = problem == QLatin1String("conflict") ? QStringLiteral("conflict")
                : QindaTK::Protocol::failureState(failure == QindaTK::Protocol::Failure::None
                                                      ? QindaTK::Protocol::Failure::Rejected : failure);
            error = problem == QLatin1String("conflict") ? QStringLiteral("The mailbox was rebuilt on the server; sync it first")
                : problem == QLatin1String("gone")       ? QStringLiteral("The server no longer has this message")
                : problem == QLatin1String("large")      ? QStringLiteral("The message is too large to download whole")
                                                         : QindaTK::Protocol::redact(channel.error(), {credentials.secret});
        }
        result.insert(answer(ok, state, error, failure == QindaTK::Protocol::Failure::Offline));
        return result;
    }, [this, requestId](const QVariantMap &done) {
        QVariantMap result = done;
        result.insert(QStringLiteral("requestId"), requestId);
        emit sourceFinished(requestId, result);
    });
    return true;
}

} // namespace QindaOffice::Apps::Mail
