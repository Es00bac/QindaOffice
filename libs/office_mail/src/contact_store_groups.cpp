// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore and contact groups (M76, D-085): a member's uri resolved to
// a contact, a group's members for the People card, and a group expanded
// into recipients for the composer. See contact_store.h.

#include "contact_store.h"

#include <QSet>

#include <functional>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

qsizetype ContactStore::indexForMember(const QString &uri) const
{
    ensureIndex();
    QString key = uri.trimmed().toLower();
    if (key.startsWith("urn:uuid:"_L1)) key = key.mid(9);
    return key.isEmpty() ? -1 : m_byUid.value(key, -1);
}

QVariantMap ContactStore::contactForMember(const QString &uri) const
{
    const qsizetype index = indexForMember(uri);
    return index >= 0 ? mapFor(m_contacts.at(index)) : QVariantMap();
}

QVariantList ContactStore::resolvedMembers(const Contact &group) const
{
    QVariantList out;
    for (const ContactMember &m : group.members) {
        QVariantMap row{{u"uri"_s, m.uri}, {u"name"_s, m.name}, {u"address"_s, m.address()}};
        if (m.isAddress()) {
            row.insert(u"kind"_s, u"address"_s);
            row.insert(u"displayName"_s, m.name.isEmpty() ? m.address() : m.name);
            row.insert(u"contactId"_s, QString());
        } else if (const qsizetype index = indexForMember(m.uri); index >= 0) {
            const Contact &c = m_contacts.at(index);
            row.insert(u"kind"_s, c.isGroup() ? u"group"_s : u"contact"_s);
            row.insert(u"displayName"_s, c.effectiveDisplayName());
            row.insert(u"contactId"_s, c.id);
            row.insert(u"address"_s, c.primaryEmail());
        } else {
            // A card this address book does not hold (yet): an import or a
            // sync may bring it later, so the member is kept, shown as missing.
            row.insert(u"kind"_s, u"missing"_s);
            row.insert(u"displayName"_s, m.name.isEmpty() ? m.uri : m.name);
            row.insert(u"contactId"_s, QString());
        }
        out.append(row);
    }
    return out;
}

QVariantMap ContactStore::withMemberUris(const QVariantMap &map) const
{
    if (!map.contains(u"members"_s)) return map;
    QVariantList members;
    for (const QVariant &value : map.value(u"members"_s).toList()) {
        QVariantMap member = value.toMap();
        const QString contactId = member.value(u"contactId"_s).toString();
        if (!contactId.isEmpty()) {
            const qsizetype index = indexOf(contactId);
            if (index >= 0) {
                const Contact &c = m_contacts.at(index);
                member.insert(u"uri"_s, ContactMember::forCard(c.uid.isEmpty() ? c.id : c.uid).uri);
                member.remove(u"name"_s);
            }
        }
        members.append(member.isEmpty() ? value : QVariant(member));
    }
    QVariantMap out = map;
    out.insert(u"members"_s, members);
    return out;
}

QVariantMap ContactStore::expandGroup(const QString &id) const
{
    ensureIndex();
    const qsizetype start = indexOf(id);
    if (start < 0 || !m_contacts.at(start).isGroup())
        return {{u"ok"_s, false}, {u"error"_s, u"There is no group with id “%1”."_s.arg(id)}};
    QStringList recipients;
    QSet<QString> addresses;
    QSet<qsizetype> visited;
    int missing = 0;
    const auto add = [&](const QString &name, const QString &address) {
        if (!isPlausibleEmail(address)) {
            ++missing;
            return;
        }
        if (addresses.contains(address.toLower())) return;
        addresses.insert(address.toLower());
        recipients.append(formatMailbox(name.compare(address, Qt::CaseInsensitive) == 0 ? QString() : name, address));
    };
    // AGENT-GUARD: groups may contain each other; each is walked once and
    // nesting stops at kMaxGroupDepth, so a cycle cannot recurse forever.
    std::function<void(qsizetype, int)> walk = [&](qsizetype group, int depth) {
        if (visited.contains(group) || depth > ContactLimits::kMaxGroupDepth) return;
        visited.insert(group);
        for (const ContactMember &m : m_contacts.at(group).members) {
            if (m.isAddress()) {
                add(m.name, m.address());
                continue;
            }
            const qsizetype target = indexForMember(m.uri);
            if (target < 0) {
                ++missing;
            } else if (m_contacts.at(target).isGroup()) {
                walk(target, depth + 1);
            } else {
                const Contact &c = m_contacts.at(target);
                add(c.effectiveDisplayName(), c.primaryEmail());
            }
        }
    };
    walk(start, 0);
    return {{u"ok"_s, true}, {u"name"_s, m_contacts.at(start).effectiveDisplayName()}, {u"recipients"_s, recipients},
            {u"count"_s, int(recipients.size())}, {u"missing"_s, missing}, {u"error"_s, QString()}};
}

} // namespace QindaOffice::Mail
