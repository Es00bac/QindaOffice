// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>
namespace QindaOffice::Core { class JsonSettings; }
namespace QindaOffice::Apps::Mail {
class MailService;
// One process-owned store beside view.json. Mailbox means account identity
// (or "local"), not a folder. All callbacks and mutations use the GUI thread.
// Execution is ordered, stops on first failed action, and never sends a draft.
class MailQuickSteps : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MailService owns Quick Steps")
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY runChanged)
    Q_PROPERTY(bool cancelRequested READ cancelRequested NOTIFY runChanged)
    Q_PROPERTY(QVariantMap outcome READ outcome NOTIFY runChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    MailQuickSteps(MailService *service, const QString &path, QObject *parent = nullptr);
    ~MailQuickSteps() override;
    int revision() const { return m_revision; }
    bool busy() const { return !m_requestId.isEmpty(); }
    bool cancelRequested() const { return m_cancel; }
    QVariantMap outcome() const { return m_outcome; }
    QString lastError() const { return m_error; }
    Q_INVOKABLE QVariantList mailboxes() const;
    Q_INVOKABLE QVariantList forMailbox(const QString &mailboxId) const;
    Q_INVOKABLE QVariantMap newStep(const QString &mailboxId) const;
    Q_INVOKABLE QVariantMap saveStep(const QVariantMap &step);
    Q_INVOKABLE bool removeStep(const QString &id);
    Q_INVOKABLE bool moveStep(const QString &id, int index);
    Q_INVOKABLE QStringList validate(const QVariantMap &step) const;
    // Exactly one finished signal, always later. Cancellation stops after the
    // current provider operation acknowledges; completed operations stay done.
    Q_INVOKABLE QString execute(const QString &id, const QStringList &messageIds);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QVariantMap draft(const QString &id) const;
signals:
    void changed();
    void runChanged();
    void finished(const QString &requestId, const QVariantMap &result);
private:
    using Done = std::function<void(const QVariantMap &)>;
    bool persist(const QVariantList &steps);
    void next();
    void complete(const QString &state, const QString &error = {});
    void perform(const QString &messageId, const QVariantMap &action, Done done);
    void waitFor(const QString &requestId, Done done);
    void answered(const QString &requestId, const QVariantMap &result);
    QPointer<MailService> m_service;
    std::unique_ptr<QindaOffice::Core::JsonSettings> m_file;
    QVariantList m_steps, m_actions, m_results;
    QVariantMap m_outcome;
    QHash<QString, Done> m_waiting;
    QStringList m_messages;
    QString m_requestId, m_error;
    int m_revision = 0, m_messageIndex = 0, m_actionIndex = 0;
    bool m_readOnly = false, m_cancel = false;
};
}
