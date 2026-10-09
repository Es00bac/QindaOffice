// SPDX-License-Identifier: GPL-3.0-or-later
// vCard 3.0 (RFC 2426) / 4.0 (RFC 6350) writer (M64). See vcard.h.

#include "vcard.h"

#include "vcard_p.h"

#include <QSet>

namespace QindaOffice::Mail::VCard {

using namespace Qt::StringLiterals;
using namespace Detail;

namespace {

using Params = QList<QPair<QString, QString>>;

// AGENT-GUARD: names come from the store (hand-editable JSON) and QML; one
// with a ':' or a line break would corrupt the whole card, so it is skipped.
bool validName(const QString &name)
{
    if (name.isEmpty()) return false;
    for (QChar ch : name) {
        const bool ok = (ch >= u'A' && ch <= u'Z') || (ch >= u'a' && ch <= u'z') || (ch >= u'0' && ch <= u'9')
            || ch == u'-' || ch == u'_';
        if (!ok) return false;
    }
    return true;
}

QString singleLine(QString text)
{
    text.replace("\r\n"_L1, " "_L1);
    text.replace(u'\n', u' ');
    text.replace(u'\r', u' ');
    return text;
}

class Writer {
public:
    explicit Writer(Version version) : m_v40(version == Version::V40) {}
    QByteArray card(const Contact &c);

private:
    void line(const QString &group, const QString &name, const Params &params, const QString &value);
    [[nodiscard]] QString paramText(const QString &value) const;
    [[nodiscard]] QString date(const QString &iso) const;
    QString typed(const QString &name, const QString &type, bool preferred, const QString &implicitToken,
                  const QString &value, const Params &more = {});
    void customLabel(const QString &group, const QString &type);
    void photo(const Contact &c);
    void extra(const VCardProperty &p);

    bool m_v40;
    QByteArray m_out;
    QSet<QString> m_takenGroups; // groups the extras already use (upper-case)
    int m_nextGroup = 1;
};

void Writer::line(const QString &group, const QString &name, const Params &params, const QString &value)
{
    QString text = group.isEmpty() ? name : group + u'.' + name;
    for (const auto &[paramName, paramValue] : params) {
        text += u';';
        text += paramName;
        text += u'=';
        text += paramValue;
    }
    text += u':';
    text += value;
    m_out += foldLine(text.toUtf8());
}

// A parameter value: quoted when it holds ; : or , (RFC 6350 §3.3); 4.0
// carries " and newlines through RFC 6868 carets, 3.0 cannot carry them.
QString Writer::paramText(const QString &value) const
{
    QString v = value;
    if (m_v40) {
        v = encodeCaret(v);
    } else {
        v.replace(u'"', u'\'');
        v = singleLine(v);
    }
    for (QChar ch : std::as_const(v)) {
        if (ch == u';' || ch == u':' || ch == u',') return u"\"%1\""_s.arg(v);
    }
    return v;
}

// 3.0: "1975-04-15" / "--04-15"; 4.0 (RFC 6350 §4.3.1 basic format):
// "19750415" / "--0415".
QString Writer::date(const QString &iso) const
{
    if (!m_v40) return iso;
    if (iso.startsWith("--"_L1)) return "--"_L1 + QString(iso.mid(2)).remove(u'-');
    return QString(iso).remove(u'-');
}

// One typed value (EMAIL, TEL, ADR, URL). A custom label cannot be a TYPE
// token, so it goes through Apple's grouping (itemN.X-ABLabel), which the
// reader maps back; returns that group ("" for a plain token type).
QString Writer::typed(const QString &name, const QString &type, bool preferred, const QString &implicitToken,
                      const QString &value, const Params &more)
{
    QString group;
    QStringList tokens;
    if (!implicitToken.isEmpty() && !m_v40) tokens.append(implicitToken);
    if (isTypeTokenList(type)) {
        tokens += type.split(u',');
    } else if (!type.isEmpty()) {
        do {
            group = u"item%1"_s.arg(m_nextGroup++);
        } while (m_takenGroups.contains(group.toUpper()));
    }
    if (preferred && !m_v40) tokens.append(u"pref"_s);
    Params params;
    if (!tokens.isEmpty()) params.append({u"TYPE"_s, tokens.join(u',')});
    if (preferred && m_v40) params.append({u"PREF"_s, u"1"_s});
    params += more;
    line(group, name, params, value);
    return group;
}

void Writer::customLabel(const QString &group, const QString &type)
{
    if (!group.isEmpty()) line(group, u"X-ABLabel"_s, {}, escapeText(type));
}

void Writer::photo(const Contact &c)
{
    if (!c.photo.isEmpty()) {
        const QString mime = !c.photoMimeType.isEmpty() ? c.photoMimeType : sniffImageMimeType(c.photo);
        const QString base64 = QString::fromLatin1(c.photo.toBase64());
        if (m_v40) {
            line({}, u"PHOTO"_s, {},
                 u"data:%1;base64,%2"_s.arg(mime.isEmpty() ? u"application/octet-stream"_s : mime, base64));
        } else {
            Params params{{u"ENCODING"_s, u"b"_s}};
            if (const QString type = typeParamForMimeType(mime); !type.isEmpty()) params.append({u"TYPE"_s, type});
            line({}, u"PHOTO"_s, params, base64);
        }
    }
    if (!c.photoUrl.isEmpty()) {
        const Params params = m_v40 ? Params() : Params{{u"VALUE"_s, u"uri"_s}};
        line({}, u"PHOTO"_s, params, singleLine(c.photoUrl));
    }
}

// An unmapped property, written back as read (vcard.h: its value already
// carries the vCard escapes).
void Writer::extra(const VCardProperty &p)
{
    if (!validName(p.name) || (!p.group.isEmpty() && !validName(p.group))) return;
    Params params;
    for (const auto &[name, value] : p.params) {
        if (!validName(name) || name.compare("CHARSET"_L1, Qt::CaseInsensitive) == 0) continue;
        if (name.compare("ENCODING"_L1, Qt::CaseInsensitive) == 0) {
            // QUOTED-PRINTABLE / 8BIT described bytes that are decoded now.
            const QString encoding = value.toUpper();
            if (encoding == "B"_L1 || encoding == "BASE64"_L1) params.append({u"ENCODING"_s, u"b"_s});
            continue;
        }
        const bool tokenList = name.compare("TYPE"_L1, Qt::CaseInsensitive) == 0 && isTypeTokenList(value.toLower());
        params.append({name, tokenList ? value : paramText(value)});
    }
    QString value = p.value;
    value.replace("\r\n"_L1, "\\n"_L1);
    value.replace(u'\n', "\\n"_L1);
    value.replace(u'\r', "\\n"_L1);
    line(p.group, p.name, params, value);
}

QByteArray Writer::card(const Contact &c)
{
    m_out.clear();
    m_takenGroups.clear();
    m_nextGroup = 1;
    for (const VCardProperty &p : c.extra) {
        if (!p.group.isEmpty()) m_takenGroups.insert(p.group.toUpper());
    }
    line({}, u"BEGIN"_s, {}, u"VCARD"_s);
    line({}, u"VERSION"_s, {}, m_v40 ? u"4.0"_s : u"3.0"_s);
    line({}, u"PRODID"_s, {}, u"-//QindaQt//QindaMail//EN"_s);
    // FN is required by both versions; N by 3.0.
    line({}, u"FN"_s, {}, escapeText(c.effectiveDisplayName()));
    const QStringList name = {c.familyName, c.givenName, c.middleName, c.prefix, c.suffix};
    if (!m_v40 || !name.join(QString()).isEmpty()) {
        QStringList parts;
        for (const QString &part : name) parts.append(escapeText(part));
        line({}, u"N"_s, {}, parts.join(u';'));
    }
    if (!c.nickname.isEmpty()) line({}, u"NICKNAME"_s, {}, escapeText(c.nickname));
    if (!c.organization.isEmpty() || !c.department.isEmpty()) {
        QString org = escapeText(c.organization);
        if (!c.department.isEmpty()) {
            org += u';';
            org += escapeText(c.department);
        }
        line({}, u"ORG"_s, {}, org);
    }
    if (!c.title.isEmpty()) line({}, u"TITLE"_s, {}, escapeText(c.title));
    for (const ContactValue &e : c.emails) {
        customLabel(typed(u"EMAIL"_s, e.type, e.preferred, u"internet"_s, escapeText(e.value)), e.type);
    }
    for (const ContactValue &t : c.phones) {
        customLabel(typed(u"TEL"_s, t.type, t.preferred, {}, escapeText(t.value)), t.type);
    }
    for (const ContactAddress &a : c.addresses) {
        Params more;
        if (m_v40 && !a.label.isEmpty()) more.append({u"LABEL"_s, paramText(a.label)});
        QStringList parts;
        for (const QString &part : {a.poBox, a.extended, a.street, a.locality, a.region, a.postalCode, a.country}) {
            parts.append(escapeText(part));
        }
        const QString group = typed(u"ADR"_s, a.type, a.preferred, {}, parts.join(u';'), more);
        // AGENT-CONTRACT (vcard_reader.cpp addLabel): the 3.0 LABEL follows
        // its ADR directly, so the reader pairs them even when several
        // addresses share a type.
        if (!m_v40 && !a.label.isEmpty()) {
            Params labelParams;
            if (isTypeTokenList(a.type)) labelParams.append({u"TYPE"_s, a.type});
            line({}, u"LABEL"_s, labelParams, escapeText(a.label));
        }
        customLabel(group, a.type);
    }
    for (const ContactValue &u : c.urls) {
        // A URI value: not text-escaped (RFC 6350 §6.7.8).
        customLabel(typed(u"URL"_s, u.type, u.preferred, {}, singleLine(u.value)), u.type);
    }
    if (!c.birthday.isEmpty()) line({}, u"BDAY"_s, {}, date(c.birthday));
    if (!c.anniversary.isEmpty()) {
        line({}, m_v40 ? u"ANNIVERSARY"_s : u"X-ANNIVERSARY"_s, {}, date(c.anniversary));
    }
    if (!c.notes.isEmpty()) line({}, u"NOTE"_s, {}, escapeText(c.notes));
    if (!c.categories.isEmpty()) {
        QStringList categories;
        for (const QString &category : c.categories) categories.append(escapeText(category));
        line({}, u"CATEGORIES"_s, {}, categories.join(u','));
    }
    photo(c);
    const QString uid = c.uid.isEmpty() ? c.id : c.uid;
    if (!uid.isEmpty()) line({}, u"UID"_s, {}, escapeText(uid));
    if (!c.revision.isEmpty()) line({}, u"REV"_s, {}, singleLine(c.revision));
    for (const VCardProperty &p : c.extra) extra(p);
    line({}, u"END"_s, {}, u"VCARD"_s);
    return m_out;
}

} // namespace

QByteArray write(const QList<Contact> &contacts, Version version)
{
    Writer writer(version);
    QByteArray out;
    for (const Contact &contact : contacts) out += writer.card(contact);
    return out;
}

} // namespace QindaOffice::Mail::VCard
