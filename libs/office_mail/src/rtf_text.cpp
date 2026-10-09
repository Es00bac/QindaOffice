// SPDX-License-Identifier: GPL-3.0-or-later
// RTF → HTML (MS-OXRTFEX de-encapsulation) and RTF → plain text (rtf_text.h).

#include "rtf_text.h"

#include "rtf_codepage.h"
#include "rtf_compressed.h"

#include <QByteArrayView>
#include <QHash>
#include <QList>

#include <climits>

namespace QindaOffice::Mail {
namespace {

constexpr int kMaxFonts = 4096;
constexpr int kMaxHeaderTokens = 64;

enum class Mode {
    Html,   // MS-OXRTFEX: HTML text + {\*\htmltag} contents, \htmlrtf suppressed
    Text,   // visible RTF text
};

bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is(QByteArrayView word, const char *name) { return word == QByteArrayView(name); }

struct ControlWord {
    QByteArrayView word;
    bool hasParam = false;
    qint64 param = 0;
};

// Parses a control word whose first letter is at `pos` (just past the
// backslash): letters, an optional signed decimal parameter and the one
// space delimiter that belongs to the word (RTF 1.9.1, "Control words").
ControlWord parseWord(const char *data, qsizetype size, qsizetype &pos)
{
    ControlWord result;
    const qsizetype start = pos;
    while (pos < size && isLetter(data[pos])) ++pos;
    result.word = QByteArrayView(data + start, pos - start);
    bool negative = false;
    if (pos + 1 < size && data[pos] == '-' && isDigit(data[pos + 1])) {
        negative = true;
        ++pos;
    }
    int digits = 0;
    while (pos < size && isDigit(data[pos])) {
        // AGENT-GUARD: saturate hostile parameters instead of overflowing.
        if (digits < 12) result.param = result.param * 10 + (data[pos] - '0');
        ++digits;
        ++pos;
        result.hasParam = true;
    }
    if (negative) result.param = -result.param;
    if (pos < size && data[pos] == ' ') ++pos;
    return result;
}

// Destinations whose contents are never body text (RTF 1.9.1). Every \*
// destination is skipped too, except \*\htmltag in HTML mode. \object is
// NOT listed: its {\result …} is the visible rendering; only its \objdata
// and \objclass groups are skipped.
bool isSkippedDestination(QByteArrayView word)
{
    static constexpr const char *kNames[] = {
        "annotation", "atnauthor", "atndate", "atnicn", "atnid", "atnparent", "atnref",
        "atntime", "author", "background", "bkmkend", "bkmkstart", "colorschememapping",
        "colortbl", "comment", "company", "creatim", "datafield", "datastore", "do",
        "doccomm", "docvar", "falt", "fldinst", "footer", "footerf", "footerl", "footerr",
        "footnote", "ftncn", "ftnsep", "ftnsepc", "generator", "header", "headerf",
        "headerl", "headerr", "info", "keywords", "latentstyles", "listoverridetable",
        "listtable", "manager", "nonshppict", "objclass", "objdata", "operator", "pict",
        "printim", "private", "revtbl", "revtim", "rsidtbl", "rxe", "shp", "shpinst",
        "shppict", "stylesheet", "subject", "tc", "template", "themedata", "title", "txe",
        "xe", "xmlnstbl",
    };
    for (const char *name : kNames) {
        if (is(word, name)) return true;
    }
    return false;
}

struct GroupState {
    bool skip = false;        // inside an ignorable destination
    bool fontTable = false;   // inside \fonttbl: font names are not text
    bool htmlRtf = false;     // between \htmlrtf and \htmlrtf0
    int uc = 1;               // \ucN: fallback characters after each \uN
    int font = -1;            // current \fN
};

// AGENT-NOTE: one pass over the token stream; group state (including the
// \htmlrtf flag and \ucN) is saved on '{' and restored on '}', as for every
// RTF character/destination property. Outlook's encoder always toggles
// \htmlrtf explicitly around group boundaries, so this agrees with readers
// that treat \htmlrtf as unscoped.
class RtfScanner {
public:
    RtfScanner(const QByteArray &rtf, Mode mode, bool honourHtmlRtf)
        : m_rtf(rtf), m_mode(mode), m_honourHtmlRtf(honourHtmlRtf)
    {
    }

    // False only when groups nest deeper than kMaxRtfGroupDepth.
    bool run();
    QString takeText()
    {
        flushBytes();
        return std::move(m_out);
    }

private:
    void parseControl();
    void controlSymbol(char symbol);
    void controlWord(const ControlWord &cw);
    void fontTableWord(const ControlWord &cw);
    bool emitting() const;
    bool consumeFallback();
    int currentCodePage() const;
    void appendByte(char byte);
    void appendChar(char16_t c);
    void appendText(QStringView text);
    void flushBytes();
    void newline() { appendText(m_mode == Mode::Html ? u"\r\n" : u"\n"); }

    const QByteArray &m_rtf;
    const Mode m_mode;
    const bool m_honourHtmlRtf;
    qsizetype m_pos = 0;
    QList<GroupState> m_stack;   // saved states of the enclosing groups
    GroupState m_state;
    bool m_groupStart = false;   // the next token is the group's first
    bool m_starred = false;      // "\*" opened the current group
    int m_skipFallback = 0;      // fallback characters still to skip after \uN
    int m_ansiCodePage = 1252;
    bool m_explicitCodePage = false;
    int m_defaultFont = -1;
    int m_fontTableFont = -1;
    QHash<int, int> m_fontCodePages;
    QByteArray m_pending;        // undecoded bytes in m_pendingCodePage
    int m_pendingCodePage = 1252;
    CodePageDecoder m_decoder{1252};
    QString m_out;
};

bool RtfScanner::run()
{
    const qsizetype size = m_rtf.size();
    const char *data = m_rtf.constData();
    while (m_pos < size) {
        const char c = data[m_pos++];
        if (c == '{' || c == '}') flushBytes();   // a stray lead byte never eats the next group
        if (c == '{') {
            m_skipFallback = 0;
            if (m_stack.size() >= kMaxRtfGroupDepth) return false;
            m_stack.append(m_state);
            m_groupStart = true;
            m_starred = false;
            continue;
        }
        if (c == '}') {
            m_skipFallback = 0;
            m_groupStart = false;
            if (m_stack.isEmpty()) continue;   // unbalanced '}': ignored
            m_state = m_stack.takeLast();
            if (m_stack.isEmpty()) break;      // the document group closed
            continue;
        }
        if (c == '\\') {
            parseControl();
            continue;
        }
        if (c == '\r' || c == '\n' || c == '\0') continue;   // not text in RTF
        m_groupStart = false;
        if (consumeFallback()) continue;
        if (emitting()) appendByte(c);
    }
    return true;
}

void RtfScanner::parseControl()
{
    const qsizetype size = m_rtf.size();
    const char *data = m_rtf.constData();
    if (m_pos >= size) return;   // a lone backslash at the very end
    if (!isLetter(data[m_pos])) {
        controlSymbol(data[m_pos++]);
        return;
    }
    controlWord(parseWord(data, size, m_pos));
}

void RtfScanner::controlSymbol(char symbol)
{
    const bool atGroupStart = m_groupStart;
    m_groupStart = false;
    switch (symbol) {
    case '*':
        if (atGroupStart) {
            m_starred = true;
            m_groupStart = true;
        }
        return;
    case '\'': {
        if (m_pos + 2 > m_rtf.size()) {   // truncated escape at the end
            m_pos = m_rtf.size();
            return;
        }
        const int high = hexValue(m_rtf.at(m_pos));
        const int low = hexValue(m_rtf.at(m_pos + 1));
        if (high < 0 || low < 0) return;   // malformed escape: dropped
        m_pos += 2;
        if (consumeFallback()) return;
        if (emitting()) appendByte(char(high * 16 + low));
        return;
    }
    case '{':
    case '}':
    case '\\':
        if (consumeFallback()) return;
        if (emitting()) appendByte(symbol);
        return;
    default:
        break;
    }
    if (consumeFallback() || !emitting()) return;
    switch (symbol) {
    case '~': appendChar(u' '); return;   // non-breaking space
    case '_': appendChar(u'‑'); return;   // non-breaking hyphen
    case '\t': appendChar(u'\t'); return;
    case '\r':
    case '\n': newline(); return;              // "\<newline>" means \par
    default: return;                           // \- \| \: and unknown symbols
    }
}

void RtfScanner::controlWord(const ControlWord &cw)
{
    const bool atGroupStart = m_groupStart;
    const bool starred = m_starred;
    m_groupStart = false;
    m_starred = false;
    if (is(cw.word, "bin")) {
        // \binN: N raw bytes follow, in any state; they may contain braces.
        const qint64 count = cw.hasParam && cw.param > 0 ? cw.param : 0;
        m_pos = count >= m_rtf.size() - m_pos ? m_rtf.size() : m_pos + count;
        return;
    }
    if (m_state.skip) return;
    if (atGroupStart) {
        if (starred) {
            if (!(m_mode == Mode::Html && is(cw.word, "htmltag"))) m_state.skip = true;
            return;
        }
        if (is(cw.word, "fonttbl")) {
            m_state.fontTable = true;
            return;
        }
        if (isSkippedDestination(cw.word)) {
            m_state.skip = true;
            return;
        }
    }
    if (m_state.fontTable) {
        fontTableWord(cw);
        return;
    }
    // Document-wide settings and state changes are never fallback text.
    if (is(cw.word, "htmlrtf")) {
        m_state.htmlRtf = !cw.hasParam || cw.param != 0;
        return;
    }
    if (is(cw.word, "ansicpg")) {
        if (cw.param > 0 && cw.param < 100000) {
            m_ansiCodePage = int(cw.param);
            m_explicitCodePage = true;
        }
        return;
    }
    if (is(cw.word, "mac") || is(cw.word, "pc") || is(cw.word, "pca")) {
        if (!m_explicitCodePage) m_ansiCodePage = is(cw.word, "mac") ? 10000 : is(cw.word, "pc") ? 437 : 850;
        return;
    }
    if (is(cw.word, "f") || is(cw.word, "deff")) {
        const int font = cw.param >= 0 && cw.param < INT_MAX ? int(cw.param) : -1;
        if (is(cw.word, "f")) m_state.font = font;
        else m_defaultFont = font;
        return;
    }
    if (is(cw.word, "uc")) {
        m_state.uc = int(qBound(qint64(0), cw.hasParam ? cw.param : qint64(1), qint64(16)));
        return;
    }
    if (consumeFallback()) return;
    if (is(cw.word, "u")) {
        qint64 value = cw.param;
        if (value < 0) value += 65536;   // RTF writes \uN as a signed 16-bit value
        m_skipFallback = m_state.uc;
        if (!cw.hasParam || value == 0 || !emitting()) return;
        if (value < 0 || value > 0xFFFF) appendChar(u'�');
        else if (value >= 0x20 || value == '\t') appendChar(char16_t(value));
        return;
    }
    if (!emitting()) return;
    if (is(cw.word, "par") || is(cw.word, "line") || is(cw.word, "sect") || is(cw.word, "page")
        || is(cw.word, "row")) {
        newline();
        return;
    }
    if (is(cw.word, "tab") || is(cw.word, "cell")) return appendChar(u'\t');
    if (is(cw.word, "emdash")) return appendChar(u'—');
    if (is(cw.word, "endash")) return appendChar(u'–');
    if (is(cw.word, "bullet")) return appendChar(u'•');
    if (is(cw.word, "lquote")) return appendChar(u'‘');
    if (is(cw.word, "rquote")) return appendChar(u'’');
    if (is(cw.word, "ldblquote")) return appendChar(u'“');
    if (is(cw.word, "rdblquote")) return appendChar(u'”');
    if (is(cw.word, "emspace")) return appendChar(u' ');
    if (is(cw.word, "enspace")) return appendChar(u' ');
}

void RtfScanner::fontTableWord(const ControlWord &cw)
{
    if (is(cw.word, "f")) {
        m_fontTableFont = cw.hasParam && cw.param >= 0 && cw.param < INT_MAX ? int(cw.param) : -1;
        return;
    }
    const bool charset = is(cw.word, "fcharset");
    if (!(charset || is(cw.word, "cpg")) || !cw.hasParam || m_fontTableFont < 0) return;
    if (cw.param <= 0 || cw.param >= 100000) return;
    const int codePage = charset ? codePageForRtfCharset(int(cw.param)) : int(cw.param);
    if (codePage <= 0) return;
    if (m_fontCodePages.size() < kMaxFonts || m_fontCodePages.contains(m_fontTableFont))
        m_fontCodePages.insert(m_fontTableFont, codePage);
}

bool RtfScanner::emitting() const
{
    if (m_stack.isEmpty() || m_state.skip || m_state.fontTable) return false;
    return !(m_state.htmlRtf && (m_mode == Mode::Html || m_honourHtmlRtf));
}

bool RtfScanner::consumeFallback()
{
    if (m_skipFallback <= 0) return false;
    --m_skipFallback;
    return true;
}

int RtfScanner::currentCodePage() const
{
    const int font = m_state.font >= 0 ? m_state.font : m_defaultFont;
    if (font >= 0) {
        const auto it = m_fontCodePages.constFind(font);
        if (it != m_fontCodePages.cend()) return it.value();
    }
    return m_ansiCodePage;
}

void RtfScanner::appendByte(char byte)
{
    // AGENT-NOTE: consecutive bytes (text and \'hh alike) are decoded as one
    // run so double-byte code pages (932, 936, 949, 950) see whole characters.
    const int codePage = currentCodePage();
    if (!m_pending.isEmpty() && codePage != m_pendingCodePage) flushBytes();
    m_pendingCodePage = codePage;
    m_pending.append(byte);
}

void RtfScanner::appendChar(char16_t c)
{
    flushBytes();
    m_out.append(QChar(c));
}

void RtfScanner::appendText(QStringView text)
{
    flushBytes();
    m_out.append(text);
}

void RtfScanner::flushBytes()
{
    if (m_pending.isEmpty()) return;
    if (m_decoder.codePage() != m_pendingCodePage) m_decoder = CodePageDecoder(m_pendingCodePage);
    m_out.append(m_decoder.decode(m_pending));
    m_pending.clear();
}

} // namespace

RtfBodyKind rtfEncapsulation(const QByteArray &rtf)
{
    const qsizetype size = rtf.size();
    const char *data = rtf.constData();
    qsizetype pos = 0;
    while (pos < size && (data[pos] == ' ' || data[pos] == '\r' || data[pos] == '\n' || data[pos] == '\t'))
        ++pos;
    if (!QByteArrayView(data + pos, size - pos).startsWith("{\\rtf")) return RtfBodyKind::None;
    ++pos;
    // MS-OXRTFEX 2.1.3.1.1: \fromhtml1 / \fromtext sit in the header, before
    // the font table (the first nested group).
    for (int tokens = 0; pos < size && tokens < kMaxHeaderTokens; ++tokens) {
        const char c = data[pos];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            ++pos;
            continue;
        }
        if (c != '\\' || pos + 1 >= size) break;
        ++pos;
        if (!isLetter(data[pos])) {
            ++pos;
            continue;
        }
        const ControlWord cw = parseWord(data, size, pos);
        if (is(cw.word, "fromhtml") && cw.hasParam && cw.param == 1) return RtfBodyKind::Html;
        if (is(cw.word, "fromtext")) return RtfBodyKind::Text;
    }
    return RtfBodyKind::Rtf;
}

bool rtfToHtml(const QByteArray &rtf, QString *html, QString *error)
{
    html->clear();
    if (rtf.size() > kMaxRtfRawSize) {
        if (error) *error = QStringLiteral("The RTF body is larger than 64 MiB");
        return false;
    }
    if (rtfEncapsulation(rtf) != RtfBodyKind::Html) {
        if (error) *error = QStringLiteral("The RTF body does not encapsulate HTML (no \\fromhtml1)");
        return false;
    }
    RtfScanner scanner(rtf, Mode::Html, true);
    if (!scanner.run()) {
        if (error) *error = QStringLiteral("The RTF body nests groups more than %1 deep").arg(kMaxRtfGroupDepth);
        return false;
    }
    *html = scanner.takeText();
    return true;
}

QString rtfToPlainText(const QByteArray &rtf)
{
    const RtfBodyKind kind = rtfEncapsulation(rtf);
    if (kind == RtfBodyKind::None || rtf.size() > kMaxRtfRawSize) return {};
    // \fromtext de-encapsulation honours \htmlrtf; for any other RTF the
    // \htmlrtf fragments ARE the visible rendering, so they are kept.
    RtfScanner scanner(rtf, Mode::Text, kind == RtfBodyKind::Text);
    scanner.run();   // too deep: keep the text read so far
    return scanner.takeText();
}

} // namespace QindaOffice::Mail
