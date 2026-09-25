// SPDX-License-Identifier: GPL-3.0-or-later
#include "connect_settings.h"

#include <QJsonArray>

namespace QindaOffice::Books::Connect {

void SyncReport::merge(const SyncReport &other)
{
    ok = ok && other.ok;
    imported += other.imported;
    applied += other.applied;
    matched += other.matched;
    posted += other.posted;
    errors += other.errors;
}

MercurySettings MercurySettings::fromJson(const QJsonObject &json)
{
    MercurySettings s;
    for (const QJsonValue &v : json.value(QStringLiteral("accounts")).toArray()) {
        const QJsonObject o = v.toObject();
        MercuryAccount a;
        a.id = o.value(QStringLiteral("id")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.kind = o.value(QStringLiteral("kind")).toString();
        a.lastFour = o.value(QStringLiteral("lastFour")).toString();
        if (!a.id.isEmpty()) {
            s.accounts << a;
        }
    }
    const QJsonObject links = json.value(QStringLiteral("links")).toObject();
    for (auto it = links.begin(); it != links.end(); ++it) {
        if (it.value().toInteger() > 0) {
            s.links.insert(it.key(), it.value().toInteger());
        }
    }
    s.startDate = QDate::fromString(json.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
    return s;
}

QJsonObject MercurySettings::toJson() const
{
    QJsonArray list;
    for (const MercuryAccount &a : accounts) {
        list.append(QJsonObject{{QStringLiteral("id"), a.id},
                                {QStringLiteral("name"), a.name},
                                {QStringLiteral("kind"), a.kind},
                                {QStringLiteral("lastFour"), a.lastFour}});
    }
    QJsonObject linkMap;
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        linkMap.insert(it.key(), it.value());
    }
    return {{QStringLiteral("accounts"), list},
            {QStringLiteral("links"), linkMap},
            {QStringLiteral("startDate"), startDate.toString(Qt::ISODate)}};
}

StripeSettings StripeSettings::fromJson(const QJsonObject &json)
{
    StripeSettings s;
    s.clearingAccountId = json.value(QStringLiteral("clearingAccountId")).toInteger();
    s.feeAccountId = json.value(QStringLiteral("feeAccountId")).toInteger();
    s.payoutAccountId = json.value(QStringLiteral("payoutAccountId")).toInteger();
    s.currency = json.value(QStringLiteral("currency")).toString();
    s.autoPostFees = json.value(QStringLiteral("autoPostFees")).toBool(true);
    s.autoPostPayouts = json.value(QStringLiteral("autoPostPayouts")).toBool(true);
    s.autoApplyInvoices = json.value(QStringLiteral("autoApplyInvoices")).toBool(true);
    s.accountName = json.value(QStringLiteral("accountName")).toString();
    s.livemode = json.value(QStringLiteral("livemode")).toBool();
    s.startDate = QDate::fromString(json.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
    return s;
}

QJsonObject StripeSettings::toJson() const
{
    return {{QStringLiteral("clearingAccountId"), clearingAccountId},
            {QStringLiteral("feeAccountId"), feeAccountId},
            {QStringLiteral("payoutAccountId"), payoutAccountId},
            {QStringLiteral("currency"), currency},
            {QStringLiteral("autoPostFees"), autoPostFees},
            {QStringLiteral("autoPostPayouts"), autoPostPayouts},
            {QStringLiteral("autoApplyInvoices"), autoApplyInvoices},
            {QStringLiteral("accountName"), accountName},
            {QStringLiteral("livemode"), livemode},
            {QStringLiteral("startDate"), startDate.toString(Qt::ISODate)}};
}

} // namespace QindaOffice::Books::Connect
