// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Recipient autocomplete for the composer's To / Cc / Bcc (M64, D-072):
// the `completionProvider` of QindaTK's Tk.TokenField, answering from the
// address book first and then from addresses seen in mail
// (MailService::recipientSuggestions).
//
// AGENT-CONTRACT (QindaTK TokenField.qml): request(query) returns a request
// id and LATER emits finished(id, {candidates: [{label, value, valid}]}) —
// never inside request(), because the field stores the id only after the
// call returns and ignores answers to other ids.

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Mail {

class MailService;

// The property type must be complete for moc (Qt 6).
Q_MOC_INCLUDE("mail_service.h")

class RecipientCompleter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Apps::Mail::MailService *service READ service WRITE setService NOTIFY serviceChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
public:
    explicit RecipientCompleter(QObject *parent = nullptr);

    [[nodiscard]] MailService *service() const { return m_service; }
    void setService(MailService *service);
    [[nodiscard]] int limit() const { return m_limit; }
    void setLimit(int limit);

    Q_INVOKABLE QString request(const QString &query);

signals:
    void serviceChanged();
    void limitChanged();
    void finished(const QString &requestId, const QVariantMap &result);

private:
    QPointer<MailService> m_service;
    int m_limit = 8;
    quint64 m_serial = 0;
};

} // namespace QindaOffice::Apps::Mail
