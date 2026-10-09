// SPDX-License-Identifier: GPL-3.0-or-later
// vCard 2.1 / 3.0 / 4.0 reader (M64): content lines, unfolding and cards.
// The property mapping is vcard_reader_properties.cpp. See vcard.h.

#include "vcard_reader_p.h"

#include "vcard_p.h"

#include <QHash>
#include <QStringDecoder>

#include <algorithm>
#include <functional>
#include <utility>

namespace QindaOffice::Mail::VCard {

using namespace Qt::StringLiterals;

namespace Detail {

QString paramValue(const Params &params, QLatin1StringView name)
{
    for (const auto &[n, v] : params) {
        if (n.compare(name, Qt::CaseInsensitive) == 0) return v;
    }
    return {};
}

static QPair<QString, QString> bareParam(const QString &value)
{
    const QString upper = value.toUpper();
    if (upper == "QUOTED-PRINTABLE"_L1 || upper == "BASE64"_L1 || upper == "8BIT"_L1 || upper == "7BIT"_L1) {
        return {u"ENCODING"_s, upper};
    }
    return {u"TYPE"_s, value};
}

static bool validName(QByteArrayView name)
{
    if (name.isEmpty()) return false;
    for (char ch : name) {
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
            || ch == '-' || ch == '_';
        if (!ok) return false;
    }
    return true;
}

// `[group.]NAME;param=value;param="quoted,value";BARE:value` (RFC 6350 §3.3).
std::optional<ContentLine> parseContentLine(const QByteArray &line)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    while (i < n && line.at(i) != ';' && line.at(i) != ':') ++i;
    if (i >= n) return std::nullopt;
    const QByteArray fullName = line.left(i).trimmed();
    const qsizetype dot = fullName.indexOf('.');
    const QByteArray group = dot >= 0 ? fullName.left(dot) : QByteArray();
    const QByteArray name = dot >= 0 ? fullName.mid(dot + 1) : fullName;
    if (!validName(name) || (dot >= 0 && !validName(group))) return std::nullopt;
    ContentLine cl;
    cl.group = QString::fromLatin1(group);
    cl.name = QString::fromLatin1(name);
    cl.key = cl.name.toUpper();
    while (i < n && line.at(i) == ';') {
        ++i;
        QByteArray raw;
        qsizetype eq = -1;
        bool quoted = false;
        for (; i < n; ++i) {
            const char ch = line.at(i);
            if (quoted) {
                if (ch == '"') quoted = false;
                else raw.append(ch);
            } else if (ch == '"') {
                quoted = true;
            } else if (ch == ';' || ch == ':') {
                break;
            } else {
                if (ch == '=' && eq < 0) eq = raw.size();
                raw.append(ch);
            }
        }
        if (i >= n) return std::nullopt; // an unterminated quote, or no value
        if (eq < 0) {
            const QString bare = QString::fromLatin1(raw.trimmed());
            if (!bare.isEmpty()) cl.params.append(bareParam(bare));
        } else {
            const QByteArray value = raw.mid(eq + 1).trimmed();
            cl.params.append({QString::fromLatin1(raw.left(eq).trimmed()),
                              decodeCaret(decodeBytes(value, {}, false))});
        }
    }
    cl.value = line.mid(i + 1);
    return cl;
}

// The parameters end at the first ':' outside quotes; only they name the
// transfer encoding that decides how the following lines continue this one.
LineKind lineKind(QByteArrayView line)
{
    bool quoted = false;
    const qsizetype limit = qMin<qsizetype>(line.size(), 4096);
    for (qsizetype i = 0; i < limit; ++i) {
        const char ch = line.at(i);
        if (ch == '"') {
            quoted = !quoted;
        } else if (ch == ':' && !quoted) {
            const QByteArray params = line.first(i).toByteArray().toUpper();
            if (params.contains("QUOTED-PRINTABLE")) return LineKind::QuotedPrintable;
            if (params.contains("BASE64") || params.contains("ENCODING=B")) return LineKind::Base64;
            return LineKind::Plain;
        }
    }
    return LineKind::Plain;
}

bool isBase64Line(QByteArrayView line)
{
    bool content = false;
    for (char ch : line) {
        if (ch == ' ' || ch == '\t') continue;
        const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
            || ch == '+' || ch == '/' || ch == '=';
        if (!ok) return false;
        content = true;
    }
    return content;
}

static bool isNoiseType(const QString &token, ValueKind kind)
{
    switch (kind) {
    case ValueKind::Email: return token == "internet"_L1;
    case ValueKind::Phone: return token == "voice"_L1;
    case ValueKind::Address:
        return token == "postal"_L1 || token == "parcel"_L1 || token == "dom"_L1 || token == "intl"_L1;
    case ValueKind::Url: return false;
    }
    return false;
}

// TYPE tokens (repeated params, comma lists, 2.1 bare values) without the
// noise; pref (3.0 TYPE=pref, 2.1 PREF) and PREF=1 (4.0) → preferred.
QPair<QString, bool> readTypes(const Params &params, ValueKind kind)
{
    QStringList tokens;
    bool preferred = false;
    for (const auto &[name, value] : params) {
        if (name.compare("PREF"_L1, Qt::CaseInsensitive) == 0) {
            if (value.trimmed() == "1"_L1) preferred = true;
            continue;
        }
        if (name.compare("TYPE"_L1, Qt::CaseInsensitive) != 0) continue;
        for (const QString &raw : value.split(u',', Qt::SkipEmptyParts)) {
            QString token = raw.trimmed().toLower();
            if (token == "pref"_L1) {
                preferred = true;
                continue;
            }
            if (token.isEmpty() || isNoiseType(token, kind)) continue;
            if (token == "mobile"_L1) token = u"cell"_s;
            if (!tokens.contains(token)) tokens.append(token);
        }
    }
    return {tokens.join(u','), preferred};
}

// Apple's itemN.X-ABLabel: "_$!<Mobile>!$_" is a built-in label, anything
// else is the user's own label, kept verbatim as the type.
QString appleLabelType(const QString &label)
{
    const QString t = label.trimmed();
    if (t.size() > 8 && t.startsWith("_$!<"_L1) && t.endsWith(">!$_"_L1)) {
        static const QHash<QString, QString> known = {
            {u"mobile"_s, u"cell"_s}, {u"iphone"_s, u"cell"_s}, {u"homefax"_s, u"home,fax"_s},
            {u"workfax"_s, u"work,fax"_s}, {u"otherfax"_s, u"other,fax"_s},
        };
        const QString inner = t.mid(4, t.size() - 8).toLower();
        return known.value(inner, inner);
    }
    return t;
}

void Reader::warn(const QString &message)
{
    if (m_result.warnings.size() < ContactLimits::kMaxWarnings) m_result.warnings.append(message);
    else ++m_droppedWarnings;
}

void Reader::tooMany()
{
    if (!m_card.warnedValues) {
        warn(u"Card %1 has more than %2 values of one kind; the rest were dropped."_s.arg(m_card.number)
                 .arg(ContactLimits::kMaxMultiValues));
    }
    m_card.warnedValues = true;
}

QByteArray Reader::toUtf8(const QByteArray &input)
{
    if (input.startsWith("\xEF\xBB\xBF")) return input.mid(3);
    std::optional<QStringConverter::Encoding> encoding;
    qsizetype skip = 0;
    if (input.startsWith("\xFF\xFE")) {
        encoding = QStringConverter::Utf16LE;
        skip = 2;
    } else if (input.startsWith("\xFE\xFF")) {
        encoding = QStringConverter::Utf16BE;
        skip = 2;
    } else if (input.size() >= 4 && input.at(0) != 0 && input.at(1) == 0 && input.at(2) != 0 && input.at(3) == 0) {
        encoding = QStringConverter::Utf16LE; // "B\0E\0G\0…" without a BOM
    } else if (input.size() >= 4 && input.at(0) == 0 && input.at(1) != 0 && input.at(2) == 0 && input.at(3) != 0) {
        encoding = QStringConverter::Utf16BE;
    }
    if (!encoding) return input;
    m_unicodeFile = true;
    QStringDecoder decoder(*encoding);
    const QString text = decoder.decode(QByteArrayView(input).sliced(skip));
    return text.toUtf8();
}

void Reader::run(const QByteArray &input)
{
    if (input.size() > ContactLimits::kMaxImportFileBytes) {
        warn(u"The file is larger than 64 MiB; it was not read."_s);
        return;
    }
    const QByteArray data = toUtf8(input);
    const char *p = data.constData();
    const qsizetype n = data.size();
    qsizetype pos = 0;
    while (pos < n) {
        qsizetype end = pos;
        while (end < n && p[end] != '\n' && p[end] != '\r') ++end;
        ++m_lineNo;
        feed(QByteArrayView(p + pos, end - pos));
        pos = (end + 1 < n && p[end] == '\r' && p[end + 1] == '\n') ? end + 2 : end + 1;
    }
    flush();
    if (m_inCard) finishCard(false);
    if (m_cards == 0) warn(u"No vCard (BEGIN:VCARD … END:VCARD) was found."_s);
    if (m_droppedWarnings > 0) m_result.warnings.append(u"…and %1 more warnings."_s.arg(m_droppedWarnings));
}

Continuation Reader::continuation(QByteArrayView line) const
{
    if (!m_haveCurrent) return Continuation::None;
    // vCard 2.1 QUOTED-PRINTABLE soft line break: the next line is NOT
    // whitespace-prefixed. AGENT-GUARD: never swallow the card's END.
    if (m_currentKind == LineKind::QuotedPrintable && m_current.endsWith('=')
        && line.trimmed().compare("END:VCARD", Qt::CaseInsensitive) != 0) {
        return Continuation::SoftBreak;
    }
    if (line.front() == ' ' || line.front() == '\t') return Continuation::Fold;
    // The 2.1 base64 block runs until an empty line; a property line always
    // has a ':' and so never looks like base64.
    if (m_currentKind == LineKind::Base64 && isBase64Line(line)) return Continuation::Base64;
    return Continuation::None;
}

void Reader::feed(QByteArrayView line)
{
    if (line.isEmpty()) {
        flush();
        return;
    }
    const Continuation how = continuation(line);
    if (how == Continuation::None) {
        flush();
        m_haveCurrent = true;
        m_currentLine = m_lineNo;
        m_oversized = line.size() > ContactLimits::kMaxLineBytes;
        m_current = m_oversized ? QByteArray() : line.toByteArray();
        m_currentKind = m_oversized ? LineKind::Plain : lineKind(line);
        return;
    }
    if (m_oversized) return;
    QByteArrayView part = line;
    if (how == Continuation::SoftBreak) m_current.chop(1);
    // AGENT-NOTE: RFC 2425 §5.8.1 / RFC 6350 §3.2 unfold by removing the
    // line break AND one whitespace character; vCard 2.1 folded only at
    // existing whitespace, so there the whitespace is content and stays.
    if (how == Continuation::Fold && !(m_inCard && v21())) part = line.sliced(1);
    if (m_current.size() + part.size() > ContactLimits::kMaxLineBytes) {
        m_oversized = true;
        m_current.clear();
        return;
    }
    m_current.append(part);
}

void Reader::flush()
{
    if (!m_haveCurrent) return;
    m_haveCurrent = false;
    if (m_oversized) {
        m_oversized = false;
        warn(u"Line %1 is longer than 2 MiB; it was dropped."_s.arg(m_currentLine));
        return;
    }
    handleLine(std::exchange(m_current, QByteArray()));
}

void Reader::handleLine(const QByteArray &line)
{
    const std::optional<ContentLine> parsed = parseContentLine(line);
    if (!parsed) {
        if (m_inCard && m_nested == 0) warn(u"Line %1 is not a vCard property; it was ignored."_s.arg(m_currentLine));
        return;
    }
    const ContentLine &cl = *parsed;
    const bool vcard = cl.value.trimmed().toUpper() == "VCARD";
    if (cl.key == "BEGIN"_L1 && vcard) {
        if (m_inCard) {
            if (m_nested > 0 || m_card.previousWasEmptyAgent) {
                ++m_nested;
                m_card.previousWasEmptyAgent = false;
                return;
            }
            finishCard(false);
        }
        m_inCard = true;
        m_nested = 0;
        m_card = Card();
        m_card.number = ++m_cards;
        m_card.line = m_currentLine;
        return;
    }
    if (cl.key == "END"_L1 && vcard) {
        if (m_nested > 0) {
            if (--m_nested == 0) warn(u"Card %1: an embedded vCard (AGENT) was dropped."_s.arg(m_card.number));
            return;
        }
        if (m_inCard) finishCard(true);
        return;
    }
    if (!m_inCard || m_nested > 0) return;
    m_card.previousWasEmptyAgent = cl.key == "AGENT"_L1 && cl.value.trimmed().isEmpty();
    apply(cl);
}

void Reader::finishCard(bool terminated)
{
    m_inCard = false;
    m_nested = 0;
    Card card = std::exchange(m_card, Card());
    Contact &c = card.contact;
    if (!terminated) {
        warn(u"Card %1 (line %2) has no END:VCARD; it was read up to the next card or the end."_s
                 .arg(card.number).arg(card.line));
    }
    QList<qsizetype> consumed;
    for (const auto &[group, index] : std::as_const(card.labels)) {
        const QString type = appleLabelType(unescapeText(c.extra.at(index).value, false));
        bool used = false;
        for (const GroupedValue &g : std::as_const(card.grouped)) {
            if (g.group != group || type.isEmpty()) continue;
            switch (g.kind) {
            case ValueKind::Email: c.emails[g.index].type = type; break;
            case ValueKind::Phone: c.phones[g.index].type = type; break;
            case ValueKind::Url: c.urls[g.index].type = type; break;
            case ValueKind::Address: c.addresses[g.index].type = type; break;
            }
            used = true;
        }
        if (used) consumed.append(index);
    }
    for (const PendingLabel &pending : std::as_const(card.pendingLabels)) {
        for (ContactAddress &a : c.addresses) {
            if (a.label.isEmpty() && (pending.type.isEmpty() || sameTypeTokens(pending.type, a.type))) {
                a.label = pending.text;
                consumed.append(pending.extraIndex);
                break;
            }
        }
    }
    std::sort(consumed.begin(), consumed.end(), std::greater<>());
    consumed.erase(std::unique(consumed.begin(), consumed.end()), consumed.end());
    for (qsizetype index : std::as_const(consumed)) c.extra.removeAt(index);

    if (!card.sawVersion) {
        warn(u"Card %1 (line %2) has no VERSION; it was read as vCard 3.0."_s.arg(card.number).arg(card.line));
    }
    bool usable = !c.primaryEmail().isEmpty();
    for (const QString &field : {c.displayName, c.givenName, c.middleName, c.familyName, c.nickname, c.organization}) {
        if (!field.isEmpty()) usable = true;
    }
    if (!usable) {
        ++m_result.skipped;
        warn(u"Card %1 (line %2) has no name, organization or e-mail address; it was skipped."_s
                 .arg(card.number).arg(card.line));
        return;
    }
    if (m_result.contacts.size() >= ContactLimits::kMaxContacts) {
        ++m_result.skipped;
        if (!m_warnedFull) warn(u"Only the first %1 cards were read."_s.arg(ContactLimits::kMaxContacts));
        m_warnedFull = true;
        return;
    }
    m_result.contacts.append(std::move(c));
}

} // namespace Detail

ReadResult read(const QByteArray &data)
{
    ReadResult result;
    Detail::Reader reader(result);
    reader.run(data);
    return result;
}

} // namespace QindaOffice::Mail::VCard
