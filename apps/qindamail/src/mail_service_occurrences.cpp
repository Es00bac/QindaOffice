// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_service.h"
#include <qindatk/local_calendar_projection_model.h>
#include <qindatk/recurrence_service.h>
#include <QDateTime>
#include <QTimeZone>
namespace QindaOffice::Apps::Mail {
using namespace Qt::StringLiterals;
QVariantMap MailService::eventOccurrence(const QString &eventId, const QString &occurrenceId) const
{
    QVariantMap event=eventById(eventId);
    if (event.isEmpty() || occurrenceId.isEmpty()) return event;
    if (!occurrenceId.startsWith(eventId+u'/')) return {};
    const QString local=QindaTK::RecurrenceService::canonicalLocal(occurrenceId.mid(eventId.size()+1));
    if (local.isEmpty()) return {};
    // Projected rows carry any modified times/texts. Keep the master's rule
    // and metadata so opening an occurrence never replaces a custom series.
    for (const auto &value:m_projection->occurrences()) {
        const auto row=value.toMap();
        if (row.value(u"occurrenceId"_s).toString()!=occurrenceId) continue;
        event.insert(row); event.insert(u"id"_s,eventId); return event;
    }
    const QString zone=event.value(u"zone"_s).toString();
    const qint64 span=QindaTK::RecurrenceService::spanSeconds(event.value(u"startLocal"_s).toString(),event.value(u"endLocal"_s).toString(),zone);
    if (span<=0 || span>INT_MAX) return {};
    const QVariantMap occurrence=m_recurrence->occurrenceAt(eventId,event.value(u"startLocal"_s).toString(),int(span),zone,
                                                           event.value(u"recurrenceRule"_s).toMap(),local);
    QVariantMap override;
    for (const auto &value:event.value(u"overrides"_s).toList())
        if (QindaTK::RecurrenceService::canonicalLocal(value.toMap().value(u"recurrenceId"_s).toString())==local) override=value.toMap();
    if (occurrence.isEmpty() && override.isEmpty()) return {};
    QDate date; QTime time; bool allDay=false;
    if (!QindaTK::RecurrenceService::parseLocal(local,&date,&time,&allDay)) return {};
    const QDateTime end=QDateTime(date,time,QTimeZone(zone.toUtf8())).addSecs(span);
    event.insert(u"startLocal"_s,local);
    event.insert(u"endLocal"_s,allDay?date.addDays(span/86400).toString(Qt::ISODate):end.toString(u"yyyy-MM-dd'T'HH:mm:ss"_s));
    for (const char *field:{"startLocal","endLocal","title","description","location"})
        if (override.contains(QLatin1String(field))) event.insert(QLatin1String(field),override.value(QLatin1String(field)));
    event.insert(u"occurrenceId"_s,occurrenceId); return event;
}
}
