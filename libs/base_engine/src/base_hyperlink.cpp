// SPDX-License-Identifier: GPL-3.0-or-later
// Hyperlink fields: Access's "display#address#subaddress#screentip" text.
// See base_hyperlink.h for the contract.
#include "base_hyperlink.h"

#include <QStringList>

namespace QindaOffice::Base {

Hyperlink parseHyperlink(const QString &stored)
{
    Hyperlink link;
    const QString text = stored.trimmed();
    if (text.count(QLatin1Char('#')) < 2) {
        link.address = text;
        return link;
    }
    const QStringList parts = text.split(QLatin1Char('#'));
    link.display = parts.value(0).trimmed();
    link.address = parts.value(1).trimmed();
    link.subAddress = parts.value(2).trimmed();
    // A screen tip may itself hold '#': everything after the third one.
    link.screenTip = parts.mid(3).join(QLatin1Char('#')).trimmed();
    return link;
}

QString Hyperlink::target() const
{
    if (address.isEmpty()) {
        return subAddress.isEmpty() ? QString() : QLatin1Char('#') + subAddress;
    }
    return subAddress.isEmpty() ? address : address + QLatin1Char('#') + subAddress;
}

QString Hyperlink::text() const
{
    if (display.isEmpty() && subAddress.isEmpty() && screenTip.isEmpty()) {
        return address;
    }
    return accessText();
}

QString Hyperlink::accessText() const
{
    QString base = address;
    QString sub = subAddress;
    if (sub.isEmpty() && base.contains(QLatin1Char('#'))) {
        sub = base.mid(base.indexOf(QLatin1Char('#')) + 1);
        base = base.left(base.indexOf(QLatin1Char('#')));
    }
    QString out = display + QLatin1Char('#') + base + QLatin1Char('#');
    if (!sub.isEmpty() || !screenTip.isEmpty()) {
        out += sub;
    }
    if (!screenTip.isEmpty()) {
        out += QLatin1Char('#') + screenTip;
    }
    return out;
}

} // namespace QindaOffice::Base
