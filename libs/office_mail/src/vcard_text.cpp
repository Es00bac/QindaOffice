// SPDX-License-Identifier: GPL-3.0-or-later
#include "vcard_p.h"

#include <QStringDecoder>

namespace QindaOffice::Mail::VCard::Detail {

using namespace Qt::StringLiterals;

namespace {

int hexValue(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

QStringList sortedTokens(const QString &type)
{
    QStringList tokens;
    for (const QString &token : type.split(u',', Qt::SkipEmptyParts)) {
        const QString t = token.trimmed().toLower();
        if (!t.isEmpty() && t != "pref"_L1 && !tokens.contains(t)) tokens.append(t);
    }
    tokens.sort();
    return tokens;
}

} // namespace

QString escapeText(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        switch (ch.unicode()) {
        case u'\\': out += "\\\\"_L1; break;
        case u',': out += "\\,"_L1; break;
        case u';': out += "\\;"_L1; break;
        case u'\r':
            if (i + 1 < text.size() && text.at(i + 1) == u'\n') ++i;
            out += "\\n"_L1;
            break;
        case u'\n': out += "\\n"_L1; break;
        default: out.append(ch);
        }
    }
    return out;
}

QString unescapeText(const QString &text, bool v21)
{
    if (!text.contains(u'\\')) return text;
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != u'\\' || i + 1 == text.size()) {
            out.append(ch);
            continue;
        }
        const QChar next = text.at(i + 1);
        if ((next == u'n' || next == u'N') && !v21) {
            out.append(u'\n');
            ++i;
        } else if (next == u'\\' || next == u',' || next == u';' || next == u':') {
            out.append(next);
            ++i;
        } else {
            out.append(ch);
        }
    }
    return out;
}

QStringList splitUnescaped(const QString &text, QChar separator)
{
    QStringList parts;
    QString current;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == u'\\' && i + 1 < text.size()) {
            current.append(ch);
            current.append(text.at(++i));
        } else if (ch == separator) {
            parts.append(current);
            current.clear();
        } else {
            current.append(ch);
        }
    }
    parts.append(current);
    return parts;
}

QByteArray decodeQuotedPrintable(QByteArrayView raw)
{
    QByteArray out;
    out.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const char ch = raw.at(i);
        if (ch == '=') {
            if (i + 2 < raw.size() && hexValue(raw.at(i + 1)) >= 0 && hexValue(raw.at(i + 2)) >= 0) {
                out.append(char(hexValue(raw.at(i + 1)) * 16 + hexValue(raw.at(i + 2))));
                i += 2;
                continue;
            }
            if (i + 1 == raw.size()) break; // trailing soft line break
        }
        out.append(ch);
    }
    return out;
}

QByteArray decodeBase64(QByteArrayView raw)
{
    QByteArray compact;
    compact.reserve(raw.size());
    for (char ch : raw) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') compact.append(ch);
    }
    // Some writers drop the trailing '=' padding; restore it before the
    // strict decode (a remainder of 1 cannot be valid and stays an error).
    if (compact.size() % 4 == 2 || compact.size() % 4 == 3) compact.append(4 - compact.size() % 4, '=');
    QByteArray::FromBase64Result result =
        QByteArray::fromBase64Encoding(std::move(compact), QByteArray::AbortOnBase64DecodingErrors);
    if (result.decodingStatus != QByteArray::Base64DecodingStatus::Ok) return {};
    if (result.decoded.isNull()) return QByteArray("");
    return result.decoded;
}

QString decodeBytes(const QByteArray &bytes, const QString &charset, bool honourCharset)
{
    if (honourCharset && !charset.trimmed().isEmpty()) {
        QStringDecoder decoder(charset.trimmed());
        if (decoder.isValid()) {
            const QString text = decoder.decode(bytes);
            return text;
        }
    }
    if (bytes.isValidUtf8()) return QString::fromUtf8(bytes);
    QStringDecoder ansi("windows-1252"_L1);
    if (ansi.isValid()) {
        const QString text = ansi.decode(bytes);
        return text;
    }
    return QString::fromLatin1(bytes);
}

QString decodeCaret(const QString &value)
{
    if (!value.contains(u'^')) return value;
    QString out;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        const QChar next = i + 1 < value.size() ? value.at(i + 1) : QChar();
        if (ch == u'^' && (next == u'n' || next == u'^' || next == u'\'')) {
            out.append(next == u'n' ? QChar(u'\n') : next == u'^' ? QChar(u'^') : QChar(u'"'));
            ++i;
        } else {
            out.append(ch);
        }
    }
    return out;
}

QString encodeCaret(const QString &value)
{
    QString out;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch == u'^') out += "^^"_L1;
        else if (ch == u'"') out += "^'"_L1;
        else if (ch == u'\r' && i + 1 < value.size() && value.at(i + 1) == u'\n') continue;
        else if (ch == u'\n' || ch == u'\r') out += "^n"_L1;
        else out.append(ch);
    }
    return out;
}

QString mimeTypeForTypeParam(const QString &type)
{
    const QString t = type.trimmed().toLower();
    if (t.startsWith("image/"_L1)) return t == "image/jpg"_L1 ? u"image/jpeg"_s : t;
    if (t == "jpeg"_L1 || t == "jpg"_L1) return u"image/jpeg"_s;
    static const QStringList images = {u"png"_s, u"gif"_s, u"bmp"_s, u"webp"_s, u"tiff"_s, u"heic"_s, u"heif"_s};
    if (images.contains(t)) return "image/"_L1 + t;
    return {};
}

QString typeParamForMimeType(const QString &mimeType)
{
    const QString m = mimeType.trimmed().toLower();
    if (m == "image/jpeg"_L1 || m == "image/jpg"_L1) return u"JPEG"_s;
    if (m.startsWith("image/"_L1) && m.size() > 6) return m.mid(6).toUpper();
    return {};
}

QByteArray foldLine(const QByteArray &line)
{
    QByteArray out;
    out.reserve(line.size() + line.size() / 25 + 4);
    qsizetype pos = 0;
    qsizetype limit = 75; // RFC 6350 §3.2: 75 octets; continuations carry a leading space
    while (line.size() - pos > limit) {
        qsizetype end = pos + limit;
        // AGENT-GUARD: back off to the lead byte of a UTF-8 sequence; a split
        // sequence makes every physical line invalid UTF-8 on its own and
        // breaks readers that decode before unfolding.
        while (end > pos && (static_cast<uchar>(line.at(end)) & 0xC0) == 0x80) --end;
        if (end == pos) end = pos + limit; // not UTF-8 at all: cut anyway
        out.append(line.constData() + pos, end - pos);
        out.append("\r\n ");
        pos = end;
        limit = 74;
    }
    out.append(line.constData() + pos, line.size() - pos);
    out.append("\r\n");
    return out;
}

bool sameTypeTokens(const QString &a, const QString &b)
{
    return sortedTokens(a) == sortedTokens(b);
}

} // namespace QindaOffice::Mail::VCard::Detail
