// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic replies through ManageSieve (managesieve_provider.h).
#include "managesieve_provider.h"

#include "managesieve_session.h"
#include "sieve_script.h"

#include <qindatk/protocol_client.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimeZone>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;
using QindaTK::Protocol::Failure;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::ManageSieveProvider", text);
}

struct Job {
    QindaTK::Protocol::Endpoint endpoint;
    QindaTK::Protocol::Credentials credentials;
    bool write = false;
    AutoReply reply;
    QStringList addresses;
    QTimeZone zone;
};

// The active script with QindaMail's change applied; `problem` set when the
// server refuses it. Runs inside the open session.
void writeScript(ManageSieve::Session &session, const Job &job, const QString &activeName, const QString &script,
                 bool *changed, QString *problem)
{
    const QString name = activeName.isEmpty() ? QString::fromLatin1(ManageSieveProvider::kScriptName) : activeName;
    if (!job.reply.enabled) {
        if (activeName.isEmpty() || !Sieve::hasManagedVacation(script)) return;
        const QString cleaned = Sieve::withoutVacation(script);
        if (activeName == QLatin1String(ManageSieveProvider::kScriptName) && cleaned.trimmed().isEmpty()) {
            // Only QindaMail's own script held the reply: switch it off and drop it.
            if (!session.setActive(QString()) || !session.deleteScript(name))
                *problem = tr("The server refused to remove the reply: %1").arg(session.lastStatus());
            *changed = problem->isEmpty();
            return;
        }
        if (session.supportsCheckScript() && !session.checkScript(cleaned)) {
            *problem = tr("The server refused the script: %1").arg(session.lastStatus());
        } else if (!session.putScript(name, cleaned)) {
            *problem = tr("The server refused the script: %1").arg(session.lastStatus());
        } else {
            *changed = true;
        }
        return;
    }
    const QStringList extensions = session.sieveExtensions();
    if (job.reply.scheduled && !(extensions.contains(u"date"_s) && extensions.contains(u"relational"_s))) {
        *problem = tr("This server cannot limit automatic replies to a time range (its Sieve lacks the date and "
                      "relational extensions); turn the time range off.");
        return;
    }
    const QString updated = Sieve::withVacation(script, job.reply, job.addresses, job.zone);
    if (updated == script) return;
    if (session.supportsCheckScript() && !session.checkScript(updated)) {
        *problem = tr("The server refused the script: %1").arg(session.lastStatus());
    } else if (!session.putScript(name, updated)) {
        *problem = tr("The server refused the script: %1").arg(session.lastStatus());
    } else if (activeName.isEmpty() && !session.setActive(name)) {
        *problem = tr("The server did not turn the script on: %1").arg(session.lastStatus());
    } else {
        *changed = true;
    }
}

QVariantMap run(const Job &job)
{
    QindaTK::Protocol::Channel channel;
    channel.addSecret(job.credentials.secret);
    ManageSieve::Session session(&channel);
    QVariantMap out;
    bool available = true;
    bool changed = false;
    QString problem;
    if (session.open(job.endpoint, job.credentials)) {
        out.insert(u"extensions"_s, session.sieveExtensions());
        out.insert(u"implementation"_s, session.implementation());
        if (!session.sieveExtensions().contains(u"vacation"_s)) {
            available = false;
            problem = tr("The server's Sieve filters have no vacation extension.");
        } else {
            QList<ManageSieve::Script> scripts;
            QString activeName;
            QString script;
            if (session.listScripts(&scripts)) {
                for (const ManageSieve::Script &entry : std::as_const(scripts)) {
                    if (entry.active) activeName = entry.name;
                }
                if (!activeName.isEmpty()) session.getScript(activeName, &script);
            }
            out.insert(u"activeScript"_s, activeName);
            if (!session.failed()) {
                if (job.write) {
                    writeScript(session, job, activeName, script, &changed, &problem);
                } else {
                    bool found = false;
                    AutoReply reply = Sieve::readManaged(script, &found);
                    if (!found) reply.enabled = false;
                    out.insert(u"settings"_s, AutoReplies::toVariantMap(reply));
                    out.insert(u"foreignVacation"_s, Sieve::hasForeignVacation(script));
                }
            }
        }
    }
    const Failure failure = channel.failure();
    if (failure == Failure::None) session.logout();
    const bool ok = failure == Failure::None && problem.isEmpty();
    const QStringList secrets{job.credentials.secret};
    out.insert(u"ok"_s, ok);
    out.insert(u"state"_s, ok ? u"accepted"_s
                              : QindaTK::Protocol::failureState(failure == Failure::None ? Failure::Rejected : failure));
    out.insert(u"error"_s, ok ? QString()
                              : QindaTK::Protocol::redact(problem.isEmpty() ? channel.error() : problem, secrets));
    out.insert(u"retryable"_s, failure == Failure::Offline);
    out.insert(u"available"_s, available);
    out.insert(u"changed"_s, changed);
    QStringList transcript;
    for (const QString &line : channel.transcript()) transcript.append(QindaTK::Protocol::redact(line, secrets));
    out.insert(u"transcript"_s, transcript);
    return out;
}

} // namespace

ManageSieveProvider::ManageSieveProvider(const Server &server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

ManageSieveProvider::~ManageSieveProvider()
{
    m_secret.fill(u'\0');
}

void ManageSieveProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret.fill(u'\0');
    m_secret = secret;
    m_mechanism = mechanism.isEmpty() ? u"auto"_s : mechanism;
}

void ManageSieveProvider::clearSecret()
{
    m_secret.fill(u'\0');
    m_secret.clear();
}

void ManageSieveProvider::refuse(const QString &requestId, const QString &error)
{
    const QVariantMap result{{u"requestId"_s, requestId}, {u"ok"_s, false}, {u"state"_s, u"rejected"_s},
                             {u"error"_s, error}, {u"retryable"_s, false}, {u"available"_s, true}};
    QMetaObject::invokeMethod(this, [this, requestId, result] { emit vacationFinished(requestId, result); },
                              Qt::QueuedConnection);
}

bool ManageSieveProvider::readVacation(const QString &requestId)
{
    return start(requestId, false, {}, {});
}

bool ManageSieveProvider::writeVacation(const QString &requestId, const QVariantMap &settings,
                                        const QStringList &addresses)
{
    const AutoReply reply = AutoReplies::fromVariantMap(settings);
    const QStringList problems = AutoReplies::validate(reply);
    if (!problems.isEmpty()) {
        refuse(requestId, problems.first().section(u": "_s, 1));
        return false;
    }
    return start(requestId, true, reply, addresses);
}

bool ManageSieveProvider::start(const QString &requestId, bool write, const AutoReply &reply,
                                const QStringList &addresses)
{
    if (m_server.host.trimmed().isEmpty() || m_server.port <= 0 || m_server.port > 65535) {
        refuse(requestId, tr("No ManageSieve server is configured for this account"));
        return false;
    }
    if (m_secret.isEmpty()) {
        refuse(requestId, tr("The account has no credentials"));
        return false;
    }
    Job job;
    job.endpoint = {m_server.host, static_cast<quint16>(m_server.port), m_server.security, m_server.timeoutMs,
                    m_server.allowInsecureAuth};
    job.credentials = {m_server.userName, m_secret, m_mechanism};
    job.write = write;
    job.reply = reply;
    job.addresses = addresses;
    job.zone = QTimeZone::systemTimeZone();
    QindaTK::Protocol::startWorker(this, [job] { return run(job); }, [this, requestId](const QVariantMap &done) {
        QVariantMap result = done;
        result.insert(u"requestId"_s, requestId);
        emit vacationFinished(requestId, result);
    });
    return true;
}

} // namespace QindaOffice::Mail
