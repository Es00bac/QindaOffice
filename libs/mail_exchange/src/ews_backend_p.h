// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers shared by ews_backend.cpp (reads, CreateItem) and ews_writes.cpp
// (UpdateItem, DeleteItem, occurrences, folders). Internal to mail_exchange.

#include "calendar_sync.h"
#include "ews_soap.h"

#include <QDateTime>

namespace QindaOffice::Mail::EwsDetail {

inline constexpr int kBatch = 50;

// ResponseCodes that mean "the item changed since that ChangeKey".
inline bool isConflictCode(const QString &code)
{
    return code == QLatin1String("ErrorIrresolvableConflict") || code == QLatin1String("ErrorChangeKeyRequiredForWriteOperations")
        || code == QLatin1String("ErrorStaleObject");
}

inline QString utc(const QDateTime &time) { return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")); }

inline CalendarSync::WriteOutcome outcome(CalendarSync::WriteOutcome::Kind kind, const QString &text = {},
                                          const QString &id = {}, const QString &version = {})
{
    CalendarSync::WriteOutcome out;
    out.kind = kind;
    out.error = text;
    out.remoteId = id;
    out.version = version;
    return out;
}

inline void writeItemId(QXmlStreamWriter &w, const QString &id, const QString &changeKey = {})
{
    Ews::startT(w, QStringLiteral("ItemId"));
    w.writeAttribute(QStringLiteral("Id"), id);
    if (!changeKey.isEmpty()) w.writeAttribute(QStringLiteral("ChangeKey"), changeKey);
    w.writeEndElement();
}

// The first item of a *ResponseMessage's <m:Items>, or null.
inline const XmlNode *firstItem(const Ews::ResponseMessage &message)
{
    const XmlNode *items = message.node ? message.node->child(u"Items") : nullptr;
    return items && !items->children.isEmpty() ? &items->children.first() : nullptr;
}

} // namespace QindaOffice::Mail::EwsDetail
