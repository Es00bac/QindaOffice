// SPDX-License-Identifier: GPL-3.0-or-later
#include "recipient_completer.h"

#include "contact_store.h"
#include "mail_service.h"

#include <QTimer>

#include <algorithm>

namespace QindaOffice::Apps::Mail {

RecipientCompleter::RecipientCompleter(QObject *parent)
    : QObject(parent)
{
}

void RecipientCompleter::setService(MailService *service)
{
    if (service == m_service) return;
    m_service = service;
    emit serviceChanged();
}

void RecipientCompleter::setLimit(int limit)
{
    limit = std::clamp(limit, 1, 50);
    if (limit == m_limit) return;
    m_limit = limit;
    emit limitChanged();
}

QString RecipientCompleter::request(const QString &query)
{
    const QString id = QStringLiteral("complete-%1").arg(++m_serial);
    QTimer::singleShot(0, this, [this, id, query] {
        QVariantList candidates = m_service != nullptr ? m_service->recipientSuggestions(query, m_limit) : QVariantList();
        for (QVariant &value : candidates) {
            QVariantMap candidate = value.toMap();
            if (!candidate.value(QStringLiteral("group")).toBool()) continue;
            const QString label = tr("%1 (group)").arg(candidate.value(QStringLiteral("name")).toString());
            if (m_groups.size() > 512) m_groups.clear();
            m_groups.insert(label.simplified(), candidate.value(QStringLiteral("contactId")).toString());
            candidate.insert(QStringLiteral("label"), label);
            value = candidate;
        }
        emit finished(id, {{QStringLiteral("candidates"), candidates}});
    });
    return id;
}

QString RecipientCompleter::groupIdFor(const QString &text) const
{
    if (m_service == nullptr) return {};
    const QString wanted = text.simplified();
    const QString id = m_groups.value(wanted);
    if (!id.isEmpty()) return id;
    for (const QVariant &value : m_service->contacts()->search(wanted, 50)) {
        const QVariantMap contact = value.toMap();
        if (contact.value(QStringLiteral("isGroup")).toBool()
            && contact.value(QStringLiteral("displayName")).toString().compare(wanted, Qt::CaseInsensitive) == 0)
            return contact.value(QStringLiteral("id")).toString();
    }
    return {};
}

QVariantList RecipientCompleter::expand(const QString &text)
{
    const QString id = groupIdFor(text);
    if (id.isEmpty()) return {};
    QVariantList out;
    for (const QString &recipient : m_service->contacts()->expandGroup(id).value(QStringLiteral("recipients")).toStringList())
        out.append(QVariantMap{{QStringLiteral("label"), recipient}, {QStringLiteral("valid"), true}});
    return out;
}

} // namespace QindaOffice::Apps::Mail
