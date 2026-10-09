// SPDX-License-Identifier: GPL-3.0-or-later
// The HTML tokenizer (html_tokenizer.h). Character references are decoded in
// html_tokenizer_entities.cpp.

#include "html_tokenizer.h"

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail {

namespace {

bool isAsciiAlpha(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
}

bool isHtmlSpace(QChar c)
{
    const char16_t u = c.unicode();
    return u == u' ' || u == u'\t' || u == u'\n' || u == u'\f' || u == u'\r';
}

// HTML5 lower-cases ASCII only and turns NUL into U+FFFD.
QString lowerName(QStringView name)
{
    QString out(name.size(), Qt::Uninitialized);
    QChar *d = out.data();
    for (const QChar c : name) {
        const char16_t u = c.unicode();
        if (u >= u'A' && u <= u'Z')
            *d++ = QChar(char16_t(u + 32));
        else
            *d++ = u == 0 ? QChar(QChar::ReplacementCharacter) : c;
    }
    return out;
}

// AGENT-GUARD: ASCII-only case folding. QString's Unicode folding would let
// "</ſcript>" (U+017F) end a script, which no browser does.
bool equalsAsciiLower(QStringView s, QStringView lower)
{
    if (s.size() != lower.size())
        return false;
    for (qsizetype i = 0; i < s.size(); ++i) {
        char16_t u = s[i].unicode();
        if (u >= u'A' && u <= u'Z')
            u = char16_t(u + 32);
        if (u != lower[i].unicode())
            return false;
    }
    return true;
}

void addAttribute(HtmlToken &token, QStringView name, QStringView value)
{
    if (token.attributes.size() >= HtmlTokenizer::kMaxAttributes) {
        ++token.droppedAttributes;
        return;
    }
    HtmlAttribute attribute;
    attribute.name = lowerName(name);
    appendDecodedHtml(attribute.value, value, true);
    token.attributes.append(std::move(attribute));
}

} // namespace

HtmlTokenizer::HtmlTokenizer(QStringView input)
    : m_input(input)
{
}

HtmlTokenizer::Raw HtmlTokenizer::rawKind(QStringView name)
{
    if (name == u"script" || name == u"style" || name == u"xmp" || name == u"iframe"
        || name == u"noembed" || name == u"noframes")
        return Raw::Verbatim;
    if (name == u"title" || name == u"textarea")
        return Raw::Decoded;
    if (name == u"plaintext")
        return Raw::ToEnd;
    return Raw::None;
}

bool HtmlTokenizer::isRawTextElement(QStringView name)
{
    return rawKind(name) != Raw::None;
}

bool HtmlTokenizer::next(HtmlToken &token)
{
    token.type = HtmlToken::Type::Text;
    token.name.clear();
    token.text.clear();
    token.attributes.clear();
    token.selfClosing = false;
    token.droppedAttributes = 0;
    if (m_raw != Raw::None) {
        readRawText(token);
        return true;
    }
    while (m_pos < m_input.size()) {
        if (m_input[m_pos] == u'<') {
            const Markup markup = readMarkup(token);
            if (markup == Markup::Token)
                return true;
            if (markup == Markup::Consumed)
                continue;
        }
        readText(token);
        return true;
    }
    return false;
}

// '<' starts markup when followed by a letter ("<a"), '!' or '?', or by '/'
// with more input after it; otherwise it is text ("a < b", "<3", a final "</").
bool HtmlTokenizer::startsMarkup(qsizetype at) const
{
    if (at + 1 >= m_input.size())
        return false;
    const QChar c = m_input[at + 1];
    if (isAsciiAlpha(c) || c == u'!' || c == u'?')
        return true;
    return c == u'/' && at + 2 < m_input.size();
}

void HtmlTokenizer::readText(HtmlToken &token)
{
    const qsizetype n = m_input.size();
    qsizetype end = m_pos + 1; // the first character is text, even a literal '<'
    while (end < n) {
        if (m_input[end] != u'<') {
            end = m_input.indexOf(u'<', end);
            if (end < 0) {
                end = n;
                break;
            }
        }
        if (startsMarkup(end))
            break;
        ++end;
    }
    token.type = HtmlToken::Type::Text;
    appendDecodedHtml(token.text, m_input.sliced(m_pos, end - m_pos), false);
    m_pos = end;
}

HtmlTokenizer::Markup HtmlTokenizer::readMarkup(HtmlToken &token)
{
    if (!startsMarkup(m_pos))
        return Markup::Literal;
    const QChar c = m_input[m_pos + 1];
    if (isAsciiAlpha(c))
        return readTag(token, m_pos + 1, false);
    if (c == u'/') {
        const QChar d = m_input[m_pos + 2];
        if (isAsciiAlpha(d))
            return readTag(token, m_pos + 2, true);
        if (d == u'>') { // "</>" is dropped
            m_pos += 3;
            return Markup::Consumed;
        }
        return readUntil(token, m_pos + 2, ">"_L1, HtmlToken::Type::Comment);
    }
    if (c == u'?')
        return readUntil(token, m_pos + 1, ">"_L1, HtmlToken::Type::ProcessingInstruction);
    const QStringView rest = m_input.sliced(m_pos + 2); // after "<!"
    if (rest.startsWith("--"_L1))
        return readComment(token);
    if (rest.startsWith("[CDATA["_L1))
        return readUntil(token, m_pos + 9, "]]>"_L1, HtmlToken::Type::Cdata);
    if (rest.size() >= 7 && equalsAsciiLower(rest.first(7), u"doctype"))
        return readUntil(token, m_pos + 2, ">"_L1, HtmlToken::Type::Doctype);
    return readUntil(token, m_pos + 2, ">"_L1, HtmlToken::Type::Comment);
}

HtmlTokenizer::Markup HtmlTokenizer::readUntil(HtmlToken &token, qsizetype from,
                                               QLatin1StringView terminator, HtmlToken::Type type)
{
    const qsizetype n = m_input.size();
    from = qMin(from, n);
    const qsizetype end = m_input.indexOf(terminator, from);
    token.type = type;
    token.text = m_input.sliced(from, (end < 0 ? n : end) - from).toString();
    m_pos = end < 0 ? n : end + terminator.size();
    return Markup::Token;
}

// "<!--" … "-->" (also "--!>"); "<!-->" and "<!--->" are empty comments, and
// an unterminated comment runs to the end of the input, as in browsers.
HtmlTokenizer::Markup HtmlTokenizer::readComment(HtmlToken &token)
{
    const qsizetype n = m_input.size();
    const qsizetype start = m_pos + 4;
    token.type = HtmlToken::Type::Comment;
    if (start < n && m_input[start] == u'>') {
        m_pos = start + 1;
        return Markup::Token;
    }
    if (start + 1 < n && m_input[start] == u'-' && m_input[start + 1] == u'>') {
        m_pos = start + 2;
        return Markup::Token;
    }
    for (qsizetype from = start;;) {
        const qsizetype dash = m_input.indexOf("--"_L1, from);
        if (dash < 0) {
            token.text = m_input.sliced(start).toString();
            m_pos = n;
            return Markup::Token;
        }
        qsizetype end = -1;
        if (dash + 2 < n && m_input[dash + 2] == u'>')
            end = dash + 3;
        else if (dash + 3 < n && m_input[dash + 2] == u'!' && m_input[dash + 3] == u'>')
            end = dash + 4;
        if (end > 0) {
            token.text = m_input.sliced(start, dash - start).toString();
            m_pos = end;
            return Markup::Token;
        }
        from = dash + 1;
    }
}

HtmlTokenizer::Markup HtmlTokenizer::readTag(HtmlToken &token, qsizetype nameStart, bool endTag)
{
    const qsizetype n = m_input.size();
    auto endsName = [this](qsizetype p) {
        const QChar c = m_input[p];
        return isHtmlSpace(c) || c == u'/' || c == u'>';
    };
    qsizetype p = nameStart;
    while (p < n && !endsName(p))
        ++p;
    token.name = lowerName(m_input.sliced(nameStart, p - nameStart));
    bool closed = false;
    while (p < n) {
        const QChar c = m_input[p];
        if (isHtmlSpace(c)) {
            ++p;
            continue;
        }
        if (c == u'>') {
            ++p;
            closed = true;
            break;
        }
        if (c == u'/') { // "/>" closes; a lone '/' separates attributes
            if (p + 1 < n && m_input[p + 1] == u'>') {
                token.selfClosing = !endTag;
                p += 2;
                closed = true;
                break;
            }
            ++p;
            continue;
        }
        // An attribute name runs to space, '/', '>' or '='; its first
        // character may itself be '=' (HTML5 keeps it in the name).
        const qsizetype nameBegin = p++;
        while (p < n && !endsName(p) && m_input[p] != u'=')
            ++p;
        const QStringView attributeName = m_input.sliced(nameBegin, p - nameBegin);
        while (p < n && isHtmlSpace(m_input[p]))
            ++p;
        QStringView value;
        if (p < n && m_input[p] == u'=') {
            ++p;
            while (p < n && isHtmlSpace(m_input[p]))
                ++p;
            if (p >= n)
                break;
            const QChar quote = m_input[p];
            if (quote == u'"' || quote == u'\'') {
                const qsizetype close = m_input.indexOf(quote, p + 1);
                if (close < 0) { // the input ends inside the value
                    p = n;
                    break;
                }
                value = m_input.sliced(p + 1, close - p - 1);
                p = close + 1;
            } else if (quote != u'>') {
                const qsizetype begin = p;
                while (p < n && !isHtmlSpace(m_input[p]) && m_input[p] != u'>')
                    ++p;
                value = m_input.sliced(begin, p - begin);
            }
        }
        if (!endTag)
            addAttribute(token, attributeName, value);
    }
    if (!closed) {
        // AGENT-GUARD: a tag cut off by the end of the input is dropped, as in
        // HTML5 — never emitted half-parsed (`<img src=x onerror=…` at EOF).
        token.attributes.clear();
        m_pos = n;
        return Markup::Consumed;
    }
    m_pos = p;
    token.type = endTag ? HtmlToken::Type::EndTag : HtmlToken::Type::StartTag;
    if (!endTag) {
        m_raw = rawKind(token.name);
        if (m_raw != Raw::None)
            m_rawName = token.name;
    }
    return Markup::Token;
}

// The content of a raw-text element: up to "</name" followed by space, '/' or
// '>' (ASCII case-insensitive), or the end of the input.
void HtmlTokenizer::readRawText(HtmlToken &token)
{
    const qsizetype n = m_input.size();
    qsizetype end = n;
    if (m_raw != Raw::ToEnd) {
        const qsizetype length = m_rawName.size();
        for (qsizetype from = m_pos;;) {
            const qsizetype lt = m_input.indexOf("</"_L1, from);
            if (lt < 0)
                break;
            const qsizetype after = lt + 2 + length;
            if (after < n && equalsAsciiLower(m_input.sliced(lt + 2, length), m_rawName)) {
                const QChar c = m_input[after];
                if (isHtmlSpace(c) || c == u'/' || c == u'>') {
                    end = lt;
                    break;
                }
            }
            from = lt + 2;
        }
    }
    token.type = HtmlToken::Type::RawText;
    token.name = m_rawName;
    const QStringView content = m_input.sliced(m_pos, end - m_pos);
    if (m_raw == Raw::Decoded)
        appendDecodedHtml(token.text, content, false);
    else
        token.text = content.toString();
    m_pos = end;
    m_raw = Raw::None;
    m_rawName.clear();
}

} // namespace QindaOffice::Mail
