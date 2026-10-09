// SPDX-License-Identifier: GPL-3.0-or-later
// plainTextToHtml (plain_text_html.h).

#include "plain_text_html.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail {

namespace {

// AGENT-NOTE: QTextDocument (Qt 6.11) drops one newline right after the
// opening tag of a pre-wrap block and one at its end, as it does for <pre>;
// one extra of each is written so the text survives exactly (measured:
// "\n\nfirst" and "last\n\n" only round-trip this way).
constexpr QLatin1StringView kOpen = "<div style=\"white-space: pre-wrap;\">\n"_L1;
constexpr QLatin1StringView kClose = "</div>"_L1;
// A quoted line keeps its '>' and gets a muted ink — the mail body is the
// document surface (paper, D-004), not themed chrome.
constexpr QLatin1StringView kQuoteOpen = "<span style=\"color: #5f6368;\">"_L1;

void appendEscaped(QString &out, QStringView text)
{
    const char16_t *data = text.utf16();
    qsizetype run = 0; // start of the characters copied as they are
    for (qsizetype i = 0; i < text.size(); ++i) {
        QLatin1StringView entity;
        switch (data[i]) {
        case u'&':
            entity = "&amp;"_L1;
            break;
        case u'<':
            entity = "&lt;"_L1;
            break;
        case u'>':
            entity = "&gt;"_L1;
            break;
        case u'"':
            entity = "&quot;"_L1;
            break;
        case 0:
            break;
        default:
            continue;
        }
        out.append(text.sliced(run, i - run));
        if (entity.isNull())
            out += QChar(QChar::ReplacementCharacter);
        else
            out += entity;
        run = i + 1;
    }
    out.append(text.sliced(run));
}

bool isAsciiAlnum(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
}

// A link may start here: not inside a word, a host name or a path.
bool atWordStart(QStringView line, qsizetype at)
{
    if (at == 0)
        return true;
    const QChar before = line[at - 1];
    return !before.isLetterOrNumber() && before != u'.' && before != u'-' && before != u'_'
           && before != u'@' && before != u'/';
}

bool isUrlCharacter(QChar c)
{
    const char16_t u = c.unicode();
    return !c.isSpace() && u >= 0x20 && u != 0x7f && u != u'<' && u != u'>' && u != u'"' && u != u'`';
}

// The end of the URL at line[start…] whose scheme or "www." is `prefix`
// long: up to white space or <>"`, then without trailing punctuation or an
// unbalanced closing bracket ("(see https://x.org/a)." ends after "/a").
qsizetype urlEnd(QStringView line, qsizetype start, qsizetype prefix)
{
    qsizetype end = start + prefix;
    while (end < line.size() && isUrlCharacter(line[end]))
        ++end;
    int round = 0;
    int square = 0;
    int curly = 0;
    for (qsizetype i = start; i < end; ++i) {
        const char16_t u = line[i].unicode();
        round += u == u'(' ? 1 : u == u')' ? -1 : 0;
        square += u == u'[' ? 1 : u == u']' ? -1 : 0;
        curly += u == u'{' ? 1 : u == u'}' ? -1 : 0;
    }
    static constexpr QStringView kTrailing = u".,;:!?'";
    while (end > start + prefix) {
        const QChar last = line[end - 1];
        if (kTrailing.contains(last)) {
            --end;
        } else if (last == u')' && round < 0) {
            ++round;
            --end;
        } else if (last == u']' && square < 0) {
            ++square;
            --end;
        } else if (last == u'}' && curly < 0) {
            ++curly;
            --end;
        } else {
            break;
        }
    }
    return end;
}

// "example.org", "a.b.example": a label, a dot and another label.
bool looksLikeHost(QStringView host)
{
    const qsizetype dot = host.indexOf(u'.');
    return dot > 0 && isAsciiAlnum(host[0]) && dot + 1 < host.size() && host[dot + 1].isLetterOrNumber();
}

bool isLocalCharacter(QChar c)
{
    return isAsciiAlnum(c) || c == u'.' || c == u'_' || c == u'%' || c == u'+' || c == u'-';
}

bool isDomainCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == u'-' || c == u'.';
}

// The address around the '@' at line[at], not reaching back before `from`:
// [start, end) or start == end when there is none.
std::pair<qsizetype, qsizetype> addressAround(QStringView line, qsizetype at, qsizetype from)
{
    qsizetype start = at;
    while (start > from && isLocalCharacter(line[start - 1]))
        --start;
    while (start < at && line[start] == u'.')
        ++start;
    qsizetype end = at + 1;
    while (end < line.size() && isDomainCharacter(line[end]))
        ++end;
    while (end > at + 1 && (line[end - 1] == u'.' || line[end - 1] == u'-'))
        --end;
    const QStringView domain = line.sliced(at + 1, end - at - 1);
    const qsizetype lastDot = domain.lastIndexOf(u'.');
    bool topLevel = lastDot > 0 && domain.size() - lastDot - 1 >= 2;
    for (qsizetype i = lastDot + 1; topLevel && i < domain.size(); ++i)
        topLevel = domain[i].isLetter();
    if (start == at || line[at - 1] == u'.' || !topLevel || domain.startsWith(u'.')
        || domain.startsWith(u'-') || domain.contains(".."_L1))
        return {at, at};
    return {start, end};
}

void appendLink(QString &out, QStringView href, QStringView text)
{
    out += "<a href=\""_L1;
    appendEscaped(out, href);
    out += "\">"_L1;
    appendEscaped(out, text);
    out += "</a>"_L1;
}

void appendLinkified(QString &out, QStringView line)
{
    const qsizetype n = line.size();
    qsizetype written = 0;
    for (qsizetype i = 0; i < n;) {
        const QChar c = line[i];
        qsizetype start = i;
        qsizetype end = i;
        QString href;
        if ((c == u'h' || c == u'H') && atWordStart(line, i)) {
            const QStringView rest = line.sliced(i);
            const qsizetype scheme = rest.startsWith("https://"_L1, Qt::CaseInsensitive) ? 8
                                     : rest.startsWith("http://"_L1, Qt::CaseInsensitive) ? 7
                                                                                           : 0;
            if (scheme > 0 && i + scheme < n && (line[i + scheme].isLetterOrNumber() || line[i + scheme] == u'[')) {
                end = urlEnd(line, i, scheme);
                href = line.sliced(i, end - i).toString();
            }
        } else if ((c == u'w' || c == u'W') && atWordStart(line, i)
                   && line.sliced(i).startsWith("www."_L1, Qt::CaseInsensitive)) {
            const qsizetype urlStop = urlEnd(line, i, 4);
            if (looksLikeHost(line.sliced(i + 4, urlStop - i - 4))) {
                end = urlStop;
                href = "https://"_L1 + line.sliced(i, end - i).toString();
            }
        } else if (c == u'@') {
            const auto [addressStart, addressEnd] = addressAround(line, i, written);
            start = addressStart;
            end = addressEnd;
            if (end > start)
                href = "mailto:"_L1 + line.sliced(start, end - start).toString();
        }
        if (end > start) {
            appendEscaped(out, line.sliced(written, start - written));
            appendLink(out, href, line.sliced(start, end - start));
            written = i = end;
        } else {
            ++i;
        }
    }
    appendEscaped(out, line.sliced(written));
}

} // namespace

QString plainTextToHtml(const QString &text)
{
    const QStringView all(text);
    const qsizetype n = all.size();
    QString out;
    out.reserve(n + n / 4 + 64);
    out += kOpen;
    for (qsizetype start = 0;;) {
        qsizetype end = start;
        while (end < n && all[end] != u'\n' && all[end] != u'\r')
            ++end;
        if (start > 0)
            out += u'\n';
        const QStringView line = all.sliced(start, end - start);
        if (line.startsWith(u'>')) {
            out += kQuoteOpen;
            appendLinkified(out, line);
            out += "</span>"_L1;
        } else {
            appendLinkified(out, line);
        }
        if (end >= n)
            break;
        start = end + (all[end] == u'\r' && end + 1 < n && all[end + 1] == u'\n' ? 2 : 1);
    }
    if (n > 0 && (all[n - 1] == u'\n' || all[n - 1] == u'\r'))
        out += u'\n';
    out += kClose;
    return out;
}

} // namespace QindaOffice::Mail
