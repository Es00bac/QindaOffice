// SPDX-License-Identifier: GPL-3.0-or-later
#include "connect_secrets.h"

#include <QTimer>

#include <qt6keychain/keychain.h>

namespace QindaOffice::Books::Connect {

namespace {

SecretOutcome outcomeOf(const QKeychain::Job &job)
{
    SecretOutcome outcome;
    switch (job.error()) {
    case QKeychain::NoError:
        outcome.status = SecretOutcome::Status::Ok;
        return outcome;
    case QKeychain::EntryNotFound:
        outcome.status = SecretOutcome::Status::NotFound;
        outcome.error = QStringLiteral("No token is stored for this connection.");
        return outcome;
    case QKeychain::AccessDeniedByUser:
    case QKeychain::AccessDenied:
        outcome.status = SecretOutcome::Status::Denied;
        break;
    case QKeychain::NoBackendAvailable:
    case QKeychain::NotImplemented:
        outcome.status = SecretOutcome::Status::Unavailable;
        break;
    case QKeychain::CouldNotDeleteEntry:
    case QKeychain::OtherError:
        outcome.status = SecretOutcome::Status::Failed;
        break;
    }
    // QtKeychain's text names the backend, never the secret.
    outcome.error = job.errorString();
    if (outcome.status == SecretOutcome::Status::Unavailable) {
        outcome.error = QStringLiteral("No desktop keyring is available (%1). Bank and payment tokens are kept "
                                       "only in the keyring; start KWallet or GNOME Keyring and try again.")
                            .arg(job.errorString());
    }
    return outcome;
}

template<typename JobT>
JobT *makeJob(const QString &service, const QString &key)
{
    auto *job = new JobT(service);
    job->setKey(key);
    job->setAutoDelete(true);
    job->setInsecureFallback(false); // AGENT-GUARD: see the header
    return job;
}

void later(const SecretBackend::Callback &done, const SecretOutcome &outcome)
{
    QTimer::singleShot(0, [done, outcome] { done(outcome); });
}

SecretOutcome unavailable()
{
    return SecretOutcome{SecretOutcome::Status::Unavailable, {},
                         QStringLiteral("No desktop keyring is available (simulated).")};
}

} // namespace

KeychainSecretBackend::KeychainSecretBackend(QString service)
    : m_service(std::move(service))
{
}

void KeychainSecretBackend::read(const QString &key, Callback done)
{
    auto *job = makeJob<QKeychain::ReadPasswordJob>(m_service, key);
    QObject::connect(job, &QKeychain::Job::finished, job, [job, done](QKeychain::Job *) {
        SecretOutcome outcome = outcomeOf(*job);
        if (outcome.ok()) {
            outcome.value = job->textData();
        }
        done(outcome);
    });
    job->start();
}

void KeychainSecretBackend::write(const QString &key, const QString &value, Callback done)
{
    auto *job = makeJob<QKeychain::WritePasswordJob>(m_service, key);
    job->setTextData(value);
    QObject::connect(job, &QKeychain::Job::finished, job, [job, done](QKeychain::Job *) { done(outcomeOf(*job)); });
    job->start();
}

void KeychainSecretBackend::remove(const QString &key, Callback done)
{
    auto *job = makeJob<QKeychain::DeletePasswordJob>(m_service, key);
    QObject::connect(job, &QKeychain::Job::finished, job, [job, done](QKeychain::Job *) {
        SecretOutcome outcome = outcomeOf(*job);
        if (outcome.status == SecretOutcome::Status::NotFound) {
            outcome = SecretOutcome{SecretOutcome::Status::Ok, {}, {}};
        }
        done(outcome);
    });
    job->start();
}

void InMemorySecretBackend::read(const QString &key, Callback done)
{
    if (m_unavailable) {
        return later(done, unavailable());
    }
    if (m_values.contains(key)) {
        return later(done, SecretOutcome{SecretOutcome::Status::Ok, m_values.value(key), {}});
    }
    later(done, SecretOutcome{SecretOutcome::Status::NotFound, {},
                              QStringLiteral("No token is stored for this connection.")});
}

void InMemorySecretBackend::write(const QString &key, const QString &value, Callback done)
{
    if (m_unavailable) {
        return later(done, unavailable());
    }
    m_values.insert(key, value);
    later(done, SecretOutcome{SecretOutcome::Status::Ok, {}, {}});
}

void InMemorySecretBackend::remove(const QString &key, Callback done)
{
    if (m_unavailable) {
        return later(done, unavailable());
    }
    m_values.remove(key);
    later(done, SecretOutcome{SecretOutcome::Status::Ok, {}, {}});
}

std::shared_ptr<SecretBackend> defaultSecretBackend()
{
    if (qEnvironmentVariable("QQO_BOOKS_SECRET_BACKEND") == QLatin1String("memory")) {
        static const std::shared_ptr<SecretBackend> shared = std::make_shared<InMemorySecretBackend>();
        return shared;
    }
    return std::make_shared<KeychainSecretBackend>();
}

QString secretKey(const QString &companyUuid, const QString &provider)
{
    return companyUuid + QLatin1Char('/') + provider;
}

} // namespace QindaOffice::Books::Connect
