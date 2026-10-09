// SPDX-License-Identifier: GPL-3.0-or-later
// URL checks for the HTML mail sanitizer (html_sanitizer_p.h) and the
// resource names it writes (html_sanitizer.h: cidUrl, inlineImageUrl).

#include "html_sanitizer_p.h"

#include <QUrl>

#include <algorithm>
#include <iterator>

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail {

// AGENT-CONTRACT: the renderer registers QUrl(cidUrl(id)) for the attachment
// whose Content-ID is `id` and the sanitizer writes exactly this string as
// the <img> source, so both build the same QUrl from the same string. The id
// is percent-encoded (UTF-8) except unreserved characters and "@!$()*+,;=:/",
// which keeps the string free of spaces, quotes and '&' — nothing an HTML
// parser could alter on the way into QTextImageFormat::name().
QString cidUrl(const QString &contentId)
{
    return "cid:"_L1 + QString::fromLatin1(QUrl::toPercentEncoding(contentId, QByteArrayLiteral("@!$()*+,;=:/")));
}

QString inlineImageUrl(int index)
{
    return "qindamail-inline:"_L1 + QString::number(index);
}

namespace Detail {

namespace {

bool isSchemeCharacter(QChar c, bool first)
{
    const char16_t u = c.unicode();
    const bool letter = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
    if (first)
        return letter;
    return letter || (u >= u'0' && u <= u'9') || u == u'+' || u == u'-' || u == u'.';
}

bool hasRasterSignature(const QByteArray &data)
{
    return data.startsWith("\x89PNG\r\n\x1a\n") || data.startsWith("\xff\xd8\xff")
           || data.startsWith("GIF87a") || data.startsWith("GIF89a") || data.startsWith("BM")
           || (data.size() >= 12 && data.startsWith("RIFF") && data.mid(8, 4) == "WEBP");
}

} // namespace

// AGENT-GUARD: the value is already entity-decoded (the tokenizer did it), so
// "jav&#x61;script:" arrives as "javascript:". Then: C0 controls and DEL go
// everywhere (browsers drop tab/newline inside URLs; "java\x01script:" is
// treated as javascript too), the ends are trimmed, and whitespace inside the
// scheme part is ignored, so "java script:", " JAVASCRIPT:" and
// "jav&#x09;ascript:" all classify as the javascript scheme — never as a
// harmless relative URL that a laxer consumer might still run.
CheckedUrl checkUrl(QStringView value)
{
    QString s;
    s.reserve(value.size());
    for (const QChar c : value) {
        if (c.unicode() >= 0x20 && c.unicode() != 0x7f)
            s += c;
    }
    s = s.trimmed();
    if (s.isEmpty())
        return {};
    if (s.startsWith(u'#'))
        return {UrlKind::Fragment, s};
    if (s.startsWith("//"_L1)) // protocol-relative: what a mail client would fetch is https
        return {UrlKind::Https, "https:"_L1 + s};
    qsizetype stop = s.size();
    for (const QChar c : {QChar(u'/'), QChar(u'?'), QChar(u'#'), QChar(u'\\')}) {
        const qsizetype at = s.indexOf(c);
        if (at >= 0 && at < stop)
            stop = at;
    }
    const qsizetype colon = s.indexOf(u':');
    if (colon < 0 || colon > stop)
        return {UrlKind::Other, s}; // relative: never fetched, never linked
    QString scheme;
    for (qsizetype i = 0; i < colon; ++i) {
        const QChar c = s[i];
        if (c.isSpace())
            continue;
        if (!isSchemeCharacter(c, scheme.isEmpty()))
            return {UrlKind::Other, s};
        scheme += QChar(c.toLower());
    }
    QString url;
    url.reserve(s.size());
    url += scheme;
    url += u':';
    url += QStringView(s).sliced(colon + 1);
    if (scheme == "http"_L1)
        return {UrlKind::Http, url};
    if (scheme == "https"_L1)
        return {UrlKind::Https, url};
    if (scheme == "mailto"_L1)
        return {UrlKind::Mailto, url};
    if (scheme == "cid"_L1)
        return {UrlKind::Cid, url};
    if (scheme == "data"_L1)
        return {UrlKind::Data, url};
    return {UrlKind::Other, url};
}

QString decodeCid(QStringView afterScheme)
{
    QString id = QUrl::fromPercentEncoding(afterScheme.toUtf8()).trimmed();
    if (id.startsWith(u'<') && id.endsWith(u'>'))
        id = id.sliced(1, id.size() - 2).trimmed();
    return id;
}

std::optional<QByteArray> decodeDataImage(QStringView afterScheme, int maxBytes)
{
    const qsizetype comma = afterScheme.indexOf(u',');
    if (comma < 0 || maxBytes <= 0)
        return std::nullopt;
    QString meta;
    for (const QChar c : afterScheme.first(comma)) {
        if (!c.isSpace())
            meta += c.toLower();
    }
    const QList<QStringView> parts = QStringView(meta).split(u';');
    static constexpr QLatin1StringView kTypes[] = {"image/png"_L1, "image/jpeg"_L1, "image/jpg"_L1,
                                                   "image/gif"_L1, "image/webp"_L1, "image/bmp"_L1};
    if (parts.size() < 2 || parts.last() != "base64"_L1
        || std::find(std::begin(kTypes), std::end(kTypes), parts.first()) == std::end(kTypes))
        return std::nullopt; // svg+xml, text/html, non-base64 … never decoded
    QByteArray payload;
    payload.reserve(afterScheme.size() - comma);
    for (const QChar c : afterScheme.sliced(comma + 1)) {
        const char16_t u = c.unicode();
        if (u == u' ' || u == u'\t' || u == u'\n' || u == u'\r' || u == u'\f')
            continue;
        if (u > 0x7f)
            return std::nullopt;
        payload += char(u);
    }
    if (payload.contains('%'))
        payload = QByteArray::fromPercentEncoding(payload);
    if (payload.size() / 4 * 3 > qsizetype(maxBytes) + 3) // too large: do not even decode
        return std::nullopt;
    const QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(payload, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > maxBytes || !hasRasterSignature(decoded.decoded))
        return std::nullopt;
    return decoded.decoded;
}

} // namespace Detail

} // namespace QindaOffice::Mail
