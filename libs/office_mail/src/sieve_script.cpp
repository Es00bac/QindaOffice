// SPDX-License-Identifier: GPL-3.0-or-later
// QindaMail's vacation block inside a Sieve script (sieve_script.h).
#include "sieve_script.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace QindaOffice::Mail::Sieve {

using namespace Qt::StringLiterals;

namespace {

QString eolOf(const QString &script)
{
    if (script.contains("\r\n"_L1)) return u"\r\n"_s;
    if (script.contains(u'\n')) return u"\n"_s;
    return u"\r\n"_s;
}

// Skips white space, `#` comments and `/* */` comments from `p`.
qsizetype skipBlank(const QString &s, qsizetype p)
{
    const qsizetype n = s.size();
    while (p < n) {
        if (s.at(p).isSpace()) {
            ++p;
        } else if (s.at(p) == u'#') {
            const qsizetype nl = s.indexOf(u'\n', p);
            p = nl < 0 ? n : nl + 1;
        } else if (s.mid(p, 2) == "/*"_L1) {
            const qsizetype close = s.indexOf("*/"_L1, p + 2);
            p = close < 0 ? n : close + 2;
        } else {
            break;
        }
    }
    return p;
}

QString zoneText(const QTimeZone &zone, const QDateTime &at)
{
    const int offset = zone.isValid() ? zone.offsetFromUtc(at) : 0;
    const int minutes = qAbs(offset) / 60;
    return u"%1%2%3"_s.arg(offset < 0 ? u'-' : u'+').arg(minutes / 60, 2, 10, u'0').arg(minutes % 60, 2, 10, u'0');
}

// The [begin, end) span of QindaMail's block, or {-1, -1}.
QPair<qsizetype, qsizetype> blockSpan(const QString &script)
{
    const qsizetype begin = script.indexOf(QLatin1String(kBeginMarker));
    if (begin < 0 || (begin > 0 && script.at(begin - 1) != u'\n')) return {-1, -1};
    const qsizetype endMarker = script.indexOf(QLatin1String(kEndMarker), begin);
    if (endMarker < 0) return {-1, -1};
    const qsizetype nl = script.indexOf(u'\n', endMarker);
    return {begin, nl < 0 ? script.size() : nl + 1};
}

} // namespace

QString quoted(const QString &text)
{
    QString out = text;
    out.replace(u'\\', "\\\\"_L1).replace(u'"', "\\\""_L1);
    out.replace("\r\n"_L1, " "_L1).replace(u'\r', u' ').replace(u'\n', u' ');
    return u'"' + out + u'"';
}

QString multiline(const QString &text, const QString &eol)
{
    QString normalized = text;
    normalized.replace("\r\n"_L1, "\n"_L1).replace(u'\r', u'\n');
    QStringList lines = normalized.split(u'\n');
    for (QString &line : lines) {
        if (line.startsWith(u'.')) line.prepend(u'.');       // RFC 5228 §2.4.2 dot-stuffing
    }
    return u"text:"_s + eol + lines.join(eol) + eol + u'.' + eol;
}

QStringList requiredExtensions(const AutoReply &reply)
{
    QStringList extensions{u"vacation"_s};
    if (reply.scheduled) extensions << u"date"_s << u"relational"_s;
    return extensions;
}

qsizetype requireSectionEnd(const QString &s)
{
    const qsizetype n = s.size();
    qsizetype pos = 0;
    qsizetype end = 0;
    for (;;) {
        const qsizetype p = skipBlank(s, pos);
        if (s.mid(p, 7).compare("require"_L1, Qt::CaseInsensitive) != 0) break;
        qsizetype q = p + 7;
        if (q < n && !(s.at(q).isSpace() || s.at(q) == u'[' || s.at(q) == u'"')) break;
        bool inString = false;
        for (; q < n; ++q) {
            const QChar c = s.at(q);
            if (inString) {
                if (c == u'\\') ++q;
                else if (c == u'"') inString = false;
            } else if (c == u'"') {
                inString = true;
            } else if (c == u';') {
                break;
            }
        }
        if (q >= n) break;                      // unterminated: keep the previous end
        pos = q + 1;
        // The rest of the line goes with it when it is only blanks or a comment.
        qsizetype r = pos;
        while (r < n && (s.at(r) == u' ' || s.at(r) == u'\t')) ++r;
        if (r >= n || s.at(r) == u'\n' || s.at(r) == u'\r' || s.at(r) == u'#') {
            const qsizetype nl = s.indexOf(u'\n', pos);
            end = nl < 0 ? n : nl + 1;
        } else {
            end = pos;
        }
        pos = end;
    }
    return end;
}

QString withoutVacation(const QString &script)
{
    QString out = script;
    for (;;) {
        const auto [begin, end] = blockSpan(out);
        if (begin < 0) break;
        out.remove(begin, end - begin);
    }
    // The require line ends with the marker comment.
    const QRegularExpression require(u"^require[^\\n]*"_s + QRegularExpression::escape(QLatin1String(kRequireMarker))
                                         + u"[ \\t]*\\r?\\n?"_s,
                                     QRegularExpression::MultilineOption);
    out.remove(require);
    return out;
}

bool hasManagedVacation(const QString &script)
{
    return blockSpan(script).first >= 0;
}

QString withVacation(const QString &script, const AutoReply &reply, const QStringList &addresses, const QTimeZone &zone)
{
    const QString base = withoutVacation(script);
    const QString eol = eolOf(base);
    QStringList extensions;
    for (const QString &extension : requiredExtensions(reply)) extensions.append(quoted(extension));
    const QString prefix = u"require ["_s + extensions.join(u", "_s) + u"]; "_s + QLatin1String(kRequireMarker) + eol;

    QString block = QLatin1String(kBeginMarker) + u" (set in QindaMail; changes here are replaced)"_s + eol;
    QVariantMap settings = AutoReplies::toVariantMap(reply);
    block += QLatin1String(kSettingsPrefix)
        + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact)) + eol;
    QString indent;
    if (reply.scheduled) {
        const QString z = quoted(zoneText(zone, reply.start));
        block += u"if allof (currentdate :zone %1 :value \"ge\" \"date\" %2,"_s
                     .arg(z, quoted(reply.start.toTimeZone(zone).date().toString(Qt::ISODate)))
            + eol
            + u"          currentdate :zone %1 :value \"le\" \"date\" %2) {"_s
                  .arg(z, quoted(reply.end.toTimeZone(zone).date().toString(Qt::ISODate)))
            + eol;
        indent = u"    "_s;
    }
    QString vacation = indent + u"vacation :days "_s + QString::number(qBound(1, reply.days, 365));
    if (!reply.subject.isEmpty()) vacation += u" :subject "_s + quoted(reply.subject);
    QStringList own;
    for (const QString &address : addresses) {
        if (!address.trimmed().isEmpty()) own.append(quoted(address.trimmed()));
    }
    if (!own.isEmpty()) vacation += u" :addresses ["_s + own.join(u", "_s) + u']';
    const QString text = reply.internalMessage.trimmed().isEmpty() ? reply.externalMessage : reply.internalMessage;
    block += vacation + u' ' + multiline(text, eol) + indent + u';' + eol;
    if (reply.scheduled) block += u'}' + eol;
    block += QLatin1String(kEndMarker) + eol;

    const qsizetype at = requireSectionEnd(base);
    QString head = base.left(at);
    if (!head.isEmpty() && !head.endsWith(u'\n')) head += eol;
    return prefix + head + block + base.mid(at);
}

AutoReply readManaged(const QString &script, bool *found)
{
    const auto [begin, end] = blockSpan(script);
    if (found != nullptr) *found = begin >= 0;
    if (begin < 0) return {};
    const QString block = script.mid(begin, end - begin);
    const qsizetype at = block.indexOf(QLatin1String(kSettingsPrefix));
    if (at < 0) return {};
    const qsizetype from = at + qsizetype(qstrlen(kSettingsPrefix));
    const qsizetype nl = block.indexOf(u'\n', from);
    const QString json = block.mid(from, nl < 0 ? -1 : nl - from).trimmed();
    AutoReply reply = AutoReplies::fromVariantMap(QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap());
    reply.enabled = true;   // the block exists: the reply is on
    return reply;
}

bool hasForeignVacation(const QString &script)
{
    QString rest = withoutVacation(script);
    // Comments and quoted strings cannot hold the command itself.
    rest.remove(QRegularExpression(u"\"(?:[^\"\\\\]|\\\\.)*\""_s));
    rest.remove(QRegularExpression(u"#[^\\n]*"_s));
    rest.remove(QRegularExpression(u"/\\*.*?\\*/"_s, QRegularExpression::DotMatchesEverythingOption));
    static const QRegularExpression command(u"(^|[;{}\\s])vacation(\\s|$)"_s, QRegularExpression::CaseInsensitiveOption);
    return command.match(rest).hasMatch();
}

} // namespace QindaOffice::Mail::Sieve
