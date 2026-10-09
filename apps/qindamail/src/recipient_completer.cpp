// SPDX-License-Identifier: GPL-3.0-or-later
#include "recipient_completer.h"

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
        const QVariantList candidates = m_service != nullptr ? m_service->recipientSuggestions(query, m_limit)
                                                             : QVariantList();
        emit finished(id, {{QStringLiteral("candidates"), candidates}});
    });
    return id;
}

} // namespace QindaOffice::Apps::Mail
