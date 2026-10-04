// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "secret_backend.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QObject>
#include <QTimer>

namespace QindaOffice::Mail::ImportedPassword {

// One asynchronous operation owned by the backend. Destruction cancels its
// callback, dismisses only its own prompt and closes only its own session.
// The connection and all callbacks belong to the creating event-loop thread.
class Reader final : public QObject {
    Q_OBJECT
public:
    Reader(QDBusConnection bus, QString key, bool presenceOnly,
           SecretBackend::Callback done, QObject *parent);
    ~Reader() override;
    void start();
    void cancel();

private slots:
    void promptCompleted(bool dismissed, const QDBusVariant &result);

private:
    using Reply = std::function<void(const QDBusMessage &)>;
    void call(const QString &destination, const QString &path, const QString &interface,
              const QString &method, const QVariantList &arguments, const QString &signature, Reply done);
    void pinProvider(std::function<void()> done);
    void search();
    void unlock();
    void checkItem();
    void openSession();
    void readSecret();
    void finish(SecretOutcome outcome);
    void fail(SecretOutcome::Status status, const QString &error);
    void cleanup();

    QDBusConnection m_bus;
    QString m_key;
    QString m_owner;
    QString m_item;
    QString m_prompt;
    QString m_session;
    SecretBackend::Callback m_done;
    QTimer m_deadline;
    bool m_presenceOnly;
    bool m_finished = false;
    int m_pendingCalls = 0;
};
} // namespace QindaOffice::Mail::ImportedPassword
