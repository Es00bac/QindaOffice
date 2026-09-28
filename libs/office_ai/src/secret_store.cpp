// SPDX-License-Identifier: GPL-3.0-or-later
#include "secret_store.h"

#include <qt6keychain/keychain.h>

namespace QindaOffice::Ai {

SecretStore::SecretStore(QObject *parent)
    : QObject(parent)
{
}

void SecretStore::store(const QString &key, const QString &value,
                        const std::function<void(bool)> &done)
{
    auto *job = new QKeychain::WritePasswordJob(QStringLiteral("qindaoffice"), this);
    job->setKey(key);
    job->setTextData(value);
    if (done) {
        QObject::connect(job, &QKeychain::Job::finished, this,
                         [done](QKeychain::Job *finished) { done(finished->error() == QKeychain::NoError); });
    }
    job->start();
}

void SecretStore::retrieve(const QString &key, const std::function<void(const QString &)> &cb)
{
    auto *job = new QKeychain::ReadPasswordJob(QStringLiteral("qindaoffice"), this);
    job->setKey(key);
    QObject::connect(job, &QKeychain::ReadPasswordJob::finished, this,
                     [job, cb](QKeychain::Job *) {
                         if (job->error() == QKeychain::NoError) {
                             cb(job->textData());
                         } else {
                             cb(QString());
                         }
                         job->deleteLater();
                     });
    job->start();
}

void SecretStore::remove(const QString &key)
{
    auto *job = new QKeychain::DeletePasswordJob(QStringLiteral("qindaoffice"), this);
    job->setKey(key);
    job->start();
}

} // namespace QindaOffice::Ai
