// SPDX-License-Identifier: GPL-3.0-or-later
// IMAP keywords, header blocks and UID COPY (imap_extras.h).
#include "imap_extras.h"

#include <qindatk/imap_session.h>
#include <qindatk/protocol_client.h>

#include <QMetaObject>
#include <QRegularExpression>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
using QindaTK::Protocol::Failure;

struct ImapExtras::Job {
    enum Kind { Keywords, Store, Headers, Copy };
    Kind kind = Keywords;
    QString mailbox;
    qint64 uidValidity = 0;
    QList<qint64> uids;
    QStringList add, remove;
    QString target;
};

namespace {

constexpr int kChunk = 500;
constexpr int kMaxUids = 5000;

// An RFC 3501 flag keyword: printable ASCII without the atom-specials, so
// nothing a caller passes can break out of the command line.
bool atom(const QString &keyword)
{
    if (keyword.isEmpty() || keyword.size() > 512 || keyword.startsWith(u'\\')) return false;
    for (const QChar c : keyword) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e || QStringView(u"(){%*\"\\]").contains(c)) return false;
    }
    return true;
}

QByteArray uidSet(const QList<qint64> &uids)
{
    QByteArray out;
    for (const qint64 uid : uids) out += (out.isEmpty() ? QByteArray() : QByteArrayLiteral(",")) + QByteArray::number(uid);
    return out;
}

QStringList flagsOf(const QindaTK::Imap::Value *flags)
{
    QStringList out;
    if (flags == nullptr || flags->kind != QindaTK::Imap::Value::List) return out;
    for (const QindaTK::Imap::Value &flag : flags->items) out.append(QString::fromUtf8(flag.text));
    return out;
}

// FLAGS of `uids` into `keywords` (uid → keyword atoms, system flags left out).
bool fetchFlags(QindaTK::Imap::Session &session, const QList<qint64> &uids, QVariantMap *keywords)
{
    QList<QList<qint64>> chunks;
    if (uids.isEmpty()) chunks.append(QList<qint64>());
    for (qsizetype i = 0; i < uids.size(); i += kChunk) chunks.append(uids.mid(i, kChunk));
    for (const QList<qint64> &chunk : std::as_const(chunks)) {
        QList<QByteArray> untagged;
        const QByteArray set = chunk.isEmpty() ? QByteArray("1:*") : uidSet(chunk);
        if (!session.command("UID FETCH " + set + " (UID FLAGS)", &untagged)) return false;
        for (const QindaTK::Imap::FetchItem &item : QindaTK::Imap::fetchItems(untagged)) {
            const QindaTK::Imap::Value *uid = item.find("UID");
            if (uid == nullptr) continue;
            QStringList words;
            for (const QString &flag : flagsOf(item.find("FLAGS")))
                if (!flag.startsWith(u'\\')) words.append(flag);
            keywords->insert(QString::fromLatin1(uid->text), words);
        }
    }
    return true;
}

QVariantMap answer(bool ok, const QString &state, const QString &error, bool retryable = false)
{
    return {{u"ok"_s, ok}, {u"state"_s, state}, {u"error"_s, error}, {u"retryable"_s, retryable}};
}

QVariantMap run(const QindaTK::Protocol::Endpoint &endpoint, const QindaTK::Protocol::Credentials &credentials,
                const ImapExtras::Server &, const QString &mailbox, qint64 uidValidity, int kind,
                const QList<qint64> &uids, const QStringList &add, const QStringList &remove, const QString &target)
{
    QindaTK::Protocol::Channel channel;
    channel.addSecret(credentials.secret);
    QindaTK::Imap::Session session(&channel);
    QVariantMap out;
    QString problem;
    QList<QByteArray> selected;
    if (session.open(endpoint, credentials)
        && session.command("SELECT " + QindaTK::Imap::mailboxArgument(mailbox), &selected)) {
        qint64 validity = 0;
        QStringList permanent;
        static const QRegularExpression validityRe(u"\\[UIDVALIDITY (\\d+)\\]"_s, QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression permanentRe(u"\\[PERMANENTFLAGS \\(([^)]*)\\)\\]"_s, QRegularExpression::CaseInsensitiveOption);
        for (const QByteArray &line : std::as_const(selected)) {
            const QString text = QString::fromUtf8(line);
            if (const auto m = validityRe.match(text); m.hasMatch()) validity = m.captured(1).toLongLong();
            if (const auto m = permanentRe.match(text); m.hasMatch()) permanent = m.captured(1).split(u' ', Qt::SkipEmptyParts);
        }
        out.insert(u"permanentFlags"_s, permanent);
        out.insert(u"allowsKeywords"_s, permanent.contains(u"\\*"_s));
        if (uidValidity > 0 && validity != uidValidity) {
            problem = u"conflict"_s;
        } else if (kind == ImapExtras::Job::Keywords) {
            QVariantMap keywords;
            if (fetchFlags(session, uids, &keywords)) out.insert(u"keywords"_s, keywords);
        } else if (kind == ImapExtras::Job::Store) {
            const QByteArray uid = QByteArray::number(uids.value(0));
            bool ok = true;
            if (!add.isEmpty()) ok = session.command("UID STORE " + uid + " +FLAGS.SILENT (" + add.join(u' ').toLatin1() + ')');
            if (ok && !remove.isEmpty())
                ok = session.command("UID STORE " + uid + " -FLAGS.SILENT (" + remove.join(u' ').toLatin1() + ')');
            QVariantMap keywords;
            if (ok && fetchFlags(session, {uids.value(0)}, &keywords)) out.insert(u"keywords"_s, keywords);
        } else if (kind == ImapExtras::Job::Headers) {
            QVariantMap headers;
            for (qsizetype i = 0; i < uids.size() && !session.failed(); i += kChunk) {
                QList<QByteArray> untagged;
                if (!session.command("UID FETCH " + uidSet(uids.mid(i, kChunk)) + " (UID BODY.PEEK[HEADER])", &untagged)) break;
                for (const QindaTK::Imap::FetchItem &item : QindaTK::Imap::fetchItems(untagged)) {
                    const QindaTK::Imap::Value *uid = item.find("UID");
                    // AGENT-NOTE: FetchItem::find matches a prefix only when the name ends in '['.
                    const QindaTK::Imap::Value *header = item.find("BODY[");
                    if (uid != nullptr && header != nullptr) headers.insert(QString::fromLatin1(uid->text), QString::fromUtf8(header->text));
                }
            }
            out.insert(u"headers"_s, headers);
        } else {
            session.command("UID COPY " + QByteArray::number(uids.value(0)) + ' ' + QindaTK::Imap::mailboxArgument(target));
        }
    }
    const Failure failure = channel.failure();
    if (failure == Failure::None) session.logout();
    const bool ok = failure == Failure::None && problem.isEmpty();
    if (problem == "conflict"_L1) {
        out.insert(answer(false, u"conflict"_s, u"The mailbox was rebuilt on the server; sync it first"_s));
    } else {
        out.insert(answer(ok, ok ? u"accepted"_s : QindaTK::Protocol::failureState(failure == Failure::None ? Failure::Rejected : failure),
                          ok ? QString() : QindaTK::Protocol::redact(channel.error(), {credentials.secret}),
                          failure == Failure::Offline));
    }
    return out;
}

} // namespace

ImapExtras::ImapExtras(const Server &server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

void ImapExtras::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism.isEmpty() ? u"auto"_s : mechanism;
}

void ImapExtras::clearSecret()
{
    m_secret.clear();
}

void ImapExtras::refuse(const QString &requestId, const QString &error)
{
    QVariantMap result = answer(false, u"rejected"_s, error);
    result.insert(u"requestId"_s, requestId);
    QMetaObject::invokeMethod(this, [this, requestId, result] { emit extrasFinished(requestId, result); },
                              Qt::QueuedConnection);
}

bool ImapExtras::keywords(const QString &requestId, const QString &mailbox, qint64 uidValidity, const QList<qint64> &uids)
{
    Job job;
    job.kind = Job::Keywords;
    job.mailbox = mailbox;
    job.uidValidity = uidValidity;
    job.uids = uids.mid(0, kMaxUids);
    return start(requestId, job);
}

bool ImapExtras::storeKeywords(const QString &requestId, const QString &mailbox, qint64 uidValidity, qint64 uid,
                               const QStringList &add, const QStringList &remove)
{
    for (const QString &keyword : add + remove) {
        if (!atom(keyword)) {
            refuse(requestId, tr("“%1” cannot be stored as an IMAP keyword").arg(keyword));
            return false;
        }
    }
    Job job;
    job.kind = Job::Store;
    job.mailbox = mailbox;
    job.uidValidity = uidValidity;
    job.uids = {uid};
    job.add = add;
    job.remove = remove;
    return start(requestId, job);
}

bool ImapExtras::headers(const QString &requestId, const QString &mailbox, qint64 uidValidity, const QList<qint64> &uids)
{
    Job job;
    job.kind = Job::Headers;
    job.mailbox = mailbox;
    job.uidValidity = uidValidity;
    job.uids = uids.mid(0, kMaxUids);
    return start(requestId, job);
}

bool ImapExtras::copy(const QString &requestId, const QString &mailbox, qint64 uidValidity, qint64 uid,
                      const QString &targetMailbox)
{
    Job job;
    job.kind = Job::Copy;
    job.mailbox = mailbox;
    job.uidValidity = uidValidity;
    job.uids = {uid};
    job.target = targetMailbox;
    return start(requestId, job);
}

bool ImapExtras::start(const QString &requestId, const Job &job)
{
    if (m_server.host.isEmpty() || job.mailbox.isEmpty()) {
        refuse(requestId, tr("The message is not stored from an IMAP mailbox"));
        return false;
    }
    if ((job.kind == Job::Store || job.kind == Job::Copy) && job.uids.value(0) <= 0) {
        refuse(requestId, tr("The message has no IMAP UID"));
        return false;
    }
    if (job.kind == Job::Copy && job.target.trimmed().isEmpty()) {
        refuse(requestId, tr("Choose a folder to copy to"));
        return false;
    }
    if (m_secret.isEmpty()) {
        refuse(requestId, tr("The account has no credentials"));
        return false;
    }
    const QindaTK::Protocol::Endpoint endpoint{m_server.host, static_cast<quint16>(m_server.port), m_server.security,
                                               m_server.timeoutMs, false};
    const QindaTK::Protocol::Credentials credentials{m_server.userName, m_secret, m_mechanism};
    const Server server = m_server;
    QindaTK::Protocol::startWorker(
        this,
        [endpoint, credentials, server, job] {
            return run(endpoint, credentials, server, job.mailbox, job.uidValidity, job.kind, job.uids, job.add,
                       job.remove, job.target);
        },
        [this, requestId](const QVariantMap &done) {
            QVariantMap result = done;
            result.insert(u"requestId"_s, requestId);
            emit extrasFinished(requestId, result);
        });
    return true;
}

} // namespace QindaOffice::Apps::Mail
