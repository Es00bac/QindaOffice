// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore lookups: display order, search, recipient autocomplete (M64).

#include "contact_store.h"

#include <QCollator>
#include <QLocale>

#include <algorithm>
#include <numeric>
#include <vector>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

QVariantMap mapWithPhoto(const Contact &contact, const QString &photoDirectory)
{
    QVariantMap map = toVariantMap(contact);
    map.insert(u"photoUrl"_s, contact.photoFile.isEmpty()
                                  ? QString()
                                  : QUrl::fromLocalFile(photoDirectory + u'/' + contact.photoFile).toString());
    return map;
}

} // namespace

void ContactStore::invalidate()
{
    m_indexValid = false;
    m_cacheValid = false;
    m_cache.clear();
}

void ContactStore::ensureIndex() const
{
    ensureLoaded();
    if (m_indexValid) return;
    m_index.clear();
    m_byAddress.clear();
    m_byUid.clear();
    m_index.reserve(m_contacts.size());
    // AGENT-NOTE: the C/POSIX locale has no collation (Qt then compares
    // code units: case-sensitive, "émile" after "Zoë"); an address book
    // wants human order, so a C locale sorts as English.
    const QLocale locale = QLocale().language() == QLocale::C ? QLocale(QLocale::English, QLocale::UnitedStates)
                                                              : QLocale();
    QCollator collator(locale);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::vector<QCollatorSortKey> sortKeys;
    sortKeys.reserve(static_cast<size_t>(m_contacts.size()));
    for (qsizetype i = 0; i < m_contacts.size(); ++i) {
        const Contact &c = m_contacts.at(i);
        IndexEntry entry;
        entry.key = contactSearchKey(c);
        const QString display = c.effectiveDisplayName();
        entry.display = foldForSearch(display);
        for (const QString &name : {c.givenName, c.familyName, c.nickname}) {
            if (!name.isEmpty()) entry.names.append(foldForSearch(name));
        }
        for (const ContactValue &email : c.emails) {
            entry.emails.append(foldForSearch(email.value));
            const QString key = email.value.toLower();
            if (!m_byAddress.contains(key)) m_byAddress.insert(key, i);
        }
        for (const QString &uid : {c.uid, c.id}) {
            QString key = uid.trimmed().toLower();
            if (key.startsWith("urn:uuid:"_L1)) key = key.mid(9);
            if (!key.isEmpty() && !m_byUid.contains(key)) m_byUid.insert(key, i);
        }
        m_index.append(entry);
        sortKeys.push_back(collator.sortKey(display));
    }
    m_order.resize(m_contacts.size());
    std::iota(m_order.begin(), m_order.end(), qsizetype(0));
    std::stable_sort(m_order.begin(), m_order.end(), [&sortKeys](qsizetype a, qsizetype b) {
        return sortKeys[static_cast<size_t>(a)].compare(sortKeys[static_cast<size_t>(b)]) < 0;
    });
    m_indexValid = true;
}

QVariantMap ContactStore::mapFor(const Contact &contact) const
{
    QVariantMap map = mapWithPhoto(contact, photoDirectory());
    if (contact.isGroup()) map.insert(u"members"_s, resolvedMembers(contact));
    return map;
}

QVariantList ContactStore::contacts() const
{
    ensureIndex();
    if (!m_cacheValid) {
        const QString directory = photoDirectory();
        m_cache.clear();
        m_cache.reserve(m_order.size());
        for (qsizetype i : std::as_const(m_order)) {
            QVariantMap map = mapWithPhoto(m_contacts.at(i), directory);
            if (m_contacts.at(i).isGroup()) map.insert(u"members"_s, resolvedMembers(m_contacts.at(i)));
            m_cache.append(map);
        }
        m_cacheValid = true;
    }
    return m_cache;
}

QVariantMap ContactStore::contactById(const QString &id) const
{
    const qsizetype index = indexOf(id);
    return index >= 0 ? mapFor(m_contacts.at(index)) : QVariantMap();
}

qsizetype ContactStore::indexForAddress(const QString &address) const
{
    ensureIndex();
    QString addr = address.trimmed();
    const qsizetype open = addr.lastIndexOf(u'<');
    if (open >= 0 && addr.endsWith(u'>')) addr = addr.mid(open + 1, addr.size() - open - 2).trimmed();
    if (addr.startsWith("mailto:"_L1, Qt::CaseInsensitive)) addr = addr.mid(7).trimmed();
    if (addr.isEmpty()) return -1;
    return m_byAddress.value(addr.toLower(), -1);
}

QVariantMap ContactStore::contactForAddress(const QString &email) const
{
    const qsizetype index = indexForAddress(email);
    return index >= 0 ? mapFor(m_contacts.at(index)) : QVariantMap();
}

QVariantList ContactStore::search(const QString &query, int limit) const
{
    ensureIndex();
    const QStringList words = searchWords(query);
    const qsizetype max = limit > 0 ? limit : 200;
    QVariantList out;
    for (qsizetype i : std::as_const(m_order)) {
        if (out.size() >= max) break;
        if (searchKeyMatches(m_index.at(i).key, words)) out.append(mapFor(m_contacts.at(i)));
    }
    return out;
}

QVariantList ContactStore::complete(const QString &query, int limit) const
{
    ensureIndex();
    const QString q = foldForSearch(query.trimmed());
    if (q.isEmpty()) return {};
    const QStringList words = searchWords(query);
    struct Candidate {
        int score;
        qsizetype rank;     // display order, for ties
        qsizetype contact;
        qsizetype email;
    };
    QList<Candidate> candidates;
    for (qsizetype rank = 0; rank < m_order.size(); ++rank) {
        const qsizetype i = m_order.at(rank);
        const IndexEntry &entry = m_index.at(i);
        if (entry.emails.isEmpty() && !m_contacts.at(i).isGroup()) continue;
        // AGENT-NOTE: tiers ×10 so that the preferred (+5) and first (+1)
        // address bonuses order a contact's own addresses without ever
        // lifting a weaker match above a stronger one.
        int nameScore = 0;
        if (entry.display == q || entry.names.contains(q)) nameScore = 100;
        else if (entry.display.startsWith(q)) nameScore = 90;
        else if (std::any_of(entry.names.cbegin(), entry.names.cend(), [&q](const QString &n) { return n.startsWith(q); })) nameScore = 80;
        else if (searchKeyMatches(entry.key, words)) nameScore = 50;
        if (m_contacts.at(i).isGroup()) {
            // M76: a group is offered by its name; the composer expands it.
            if (nameScore > 0) candidates.append({nameScore * 10, rank, i, -1});
            continue;
        }
        for (qsizetype e = 0; e < entry.emails.size(); ++e) {
            const QString &address = entry.emails.at(e);
            const QString local = address.section(u'@', 0, 0);
            int score = nameScore;
            if (address == q || local == q) score = qMax(score, 95);
            else if (address.startsWith(q) || local.startsWith(q)) score = qMax(score, 85);
            else if (address.contains(q)) score = qMax(score, 20);
            if (score == 0) continue;
            const bool preferred = m_contacts.at(i).emails.at(e).preferred;
            candidates.append({score * 10 + (preferred ? 5 : 0) + (e == 0 ? 1 : 0), rank, i, e});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.email < b.email;
    });
    const qsizetype max = limit > 0 ? qMin(limit, 200) : 8;
    QVariantList out;
    QSet<QString> seen;
    for (const Candidate &candidate : std::as_const(candidates)) {
        if (out.size() >= max) break;
        const Contact &c = m_contacts.at(candidate.contact);
        if (candidate.email < 0) {
            out.append(QVariantMap{{u"label"_s, c.effectiveDisplayName()}, {u"value"_s, QString()},
                                   {u"name"_s, c.effectiveDisplayName()}, {u"contactId"_s, c.id}, {u"type"_s, u"group"_s},
                                   {u"group"_s, true}, {u"memberCount"_s, int(c.members.size())}});
            continue;
        }
        const ContactValue &email = c.emails.at(candidate.email);
        if (seen.contains(email.value.toLower())) continue;
        seen.insert(email.value.toLower());
        const QString display = c.effectiveDisplayName();
        const QString name = display.compare(email.value, Qt::CaseInsensitive) == 0 ? QString() : display;
        out.append(QVariantMap{{u"label"_s, formatMailbox(name, email.value)}, {u"value"_s, email.value},
                               {u"name"_s, name}, {u"contactId"_s, c.id}, {u"type"_s, email.type}});
    }
    return out;
}

} // namespace QindaOffice::Mail
