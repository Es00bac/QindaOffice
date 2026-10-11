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
//
// M76 (D-085): contact groups are offered too, labelled "Name (group)", with
// `group: true` and the group's `contactId`. M89 (D-097): the composer's
// fields use QindaTK's two-argument validator (D-314), which receives the
// chosen suggestion itself, so a picked group becomes ONE token whose value
// is "group:<contactId>"; ComposerWindow.qml replaces it with
// expandGroup(contactId)'s members — the group chosen, never another one
// whose label reads the same. A group's plain name typed and committed still
// expands through expand(text), as in Outlook.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
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
    // The members of the group with this contact id as candidates
    // [{label, valid}]; [] for anything else.
    Q_INVOKABLE QVariantList expandGroup(const QString &contactId) const;
    // The members of the group a typed name names (its display name, case
    // blind); [] otherwise.
    Q_INVOKABLE QVariantList expand(const QString &text) const;
    // The id of the group a typed name names (as expand() resolves it), or "".
    Q_INVOKABLE QString groupIdFor(const QString &text) const;

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
