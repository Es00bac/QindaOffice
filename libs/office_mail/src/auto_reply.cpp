// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic replies: the settings value (auto_reply.h).
#include "auto_reply.h"

#include <QCoreApplication>

namespace QindaOffice::Mail::AutoReplies {

using namespace Qt::StringLiterals;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::AutoReplies", text);
}

} // namespace

AutoReply fromVariantMap(const QVariantMap &map)
{
    AutoReply reply;
    reply.enabled = map.value(u"enabled"_s).toBool();
    reply.scheduled = map.value(u"scheduled"_s).toBool();
    reply.start = QDateTime::fromString(map.value(u"start"_s).toString(), Qt::ISODate).toUTC();
    reply.end = QDateTime::fromString(map.value(u"end"_s).toString(), Qt::ISODate).toUTC();
    reply.internalMessage = map.value(u"internalMessage"_s).toString();
    reply.externalMessage = map.value(u"externalMessage"_s).toString();
    const QString audience = map.value(u"externalAudience"_s, u"all"_s).toString();
    reply.externalAudience = audience == "none"_L1 || audience == "contacts"_L1 ? audience : u"all"_s;
    reply.subject = map.value(u"subject"_s).toString().simplified();
    reply.days = qBound(1, map.value(u"days"_s, 1).toInt(), 365);
    return reply;
}

QVariantMap toVariantMap(const AutoReply &reply)
{
    return {{u"enabled"_s, reply.enabled},
            {u"scheduled"_s, reply.scheduled},
            {u"start"_s, reply.start.isValid() ? reply.start.toUTC().toString(Qt::ISODate) : QString()},
            {u"end"_s, reply.end.isValid() ? reply.end.toUTC().toString(Qt::ISODate) : QString()},
            {u"internalMessage"_s, reply.internalMessage},
            {u"externalMessage"_s, reply.externalMessage},
            {u"externalAudience"_s, reply.externalAudience},
            {u"subject"_s, reply.subject},
            {u"days"_s, reply.days}};
}

QStringList validate(const AutoReply &reply)
{
    QStringList problems;
    if (reply.scheduled) {
        if (!reply.start.isValid()) problems.append(u"start: "_s + tr("choose when the replies start"));
        if (!reply.end.isValid()) problems.append(u"end: "_s + tr("choose when the replies end"));
        if (reply.start.isValid() && reply.end.isValid() && reply.end <= reply.start)
            problems.append(u"end: "_s + tr("the end is before the start"));
    }
    if (reply.enabled && reply.internalMessage.trimmed().isEmpty() && reply.externalMessage.trimmed().isEmpty())
        problems.append(u"internalMessage: "_s + tr("write the reply"));
    if (reply.internalMessage.size() > kMaxMessageLength || reply.externalMessage.size() > kMaxMessageLength)
        problems.append(u"internalMessage: "_s + tr("at most 16 384 characters"));
    if (reply.subject.size() > 256) problems.append(u"subject: "_s + tr("at most 256 characters"));
    return problems;
}

} // namespace QindaOffice::Mail::AutoReplies
