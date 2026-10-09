// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A small, forgiving HTML tokenizer for untrusted mail bodies (M64, D-072).
//
// It follows the HTML5 tokenizer (WHATWG HTML, "Tokenization") closely enough
// that a tag, an attribute or a character reference means here what it would
// mean in a browser — `<img/src="x"/onerror=y>` has the attributes a browser
// sees, `&#106avascript:` decodes the same way — but it builds no tree and
// never backtracks: every input character is examined a bounded number of
// times, so 1 MB of "<", an unterminated quote or comment, or 100 000 nested
// tags all cost linear time.
//
// AGENT-NOTE: html_sanitizer*.cpp is the consumer. It rebuilds its output from
// these tokens and never copies input through, so a token read differently
// from a browser can lose or garble text but never inject markup. Keep it that
// way: there is deliberately no "pass the source through" token.
//
// Simplifications against HTML5, all of them safe for the sanitizer:
//  * the end of the input inside a tag drops that tag (as HTML5 does);
//  * raw-text elements end at the first matching end tag — the script
//    "escape" states are not modelled, which can only end a script earlier
//    than a browser would (the rest is then sanitized as markup);
//  * noscript is ordinary markup (scripting is off);
//  * <![CDATA[…]]> is one Cdata token wherever it appears (no foreign
//    content); bogus comments ("<!x>", "</ x>") are Comment tokens.

#include <QList>
#include <QString>
#include <QStringView>

namespace QindaOffice::Mail {

struct HtmlAttribute {
    QString name;   // ASCII lower-cased, never empty
    QString value;  // character references decoded; empty when no value was given
};

struct HtmlToken {
    enum class Type {
        Text,                  // `text`: character data, references decoded
        StartTag,              // `name`, `attributes`, `selfClosing`
        EndTag,                // `name` (an end tag's attributes are parsed and dropped)
        RawText,               // `name`: the raw-text element; `text`: all of its content
        Comment,               // `text`: between "<!--" and "-->" (also bogus comments)
        Doctype,               // `text`: between "<!" and ">"
        Cdata,                 // `text`: between "<![CDATA[" and "]]>"
        ProcessingInstruction, // `text`: between "<" and ">" ("?xml …?")
    };
    Type type = Type::Text;
    QString name;                     // tag names are ASCII lower-cased
    QString text;
    QList<HtmlAttribute> attributes;  // source order, duplicates included (HTML5 uses the first)
    bool selfClosing = false;         // the start tag ended with "/>"
    int droppedAttributes = 0;        // attributes past kMaxAttributes (parsed, not kept)
};

class HtmlTokenizer
{
public:
    // Attributes kept per start tag; the rest only count in droppedAttributes,
    // so `<x a a a …>` with a million attributes stays cheap.
    static constexpr int kMaxAttributes = 256;

    // `input` is not copied and must outlive the tokenizer.
    explicit HtmlTokenizer(QStringView input);

    // Reads the next token into `token` (reusing its storage). Returns false
    // at the end of the input. After the start tag of a raw-text element the
    // next token is always its RawText (possibly empty).
    bool next(HtmlToken &token);

    // Elements whose content is one RawText token: script style xmp iframe
    // noembed noframes (verbatim), title textarea (references decoded) and
    // plaintext (verbatim, to the end of the input). `name` is lower case.
    static bool isRawTextElement(QStringView name);

private:
    enum class Markup { Token, Consumed, Literal };
    enum class Raw : quint8 { None, Verbatim, Decoded, ToEnd };

    static Raw rawKind(QStringView name);
    bool startsMarkup(qsizetype at) const;
    Markup readMarkup(HtmlToken &token);
    Markup readTag(HtmlToken &token, qsizetype nameStart, bool endTag);
    Markup readComment(HtmlToken &token);
    Markup readUntil(HtmlToken &token, qsizetype from, QLatin1StringView terminator,
                     HtmlToken::Type type);
    void readText(HtmlToken &token);
    void readRawText(HtmlToken &token);

    QStringView m_input;
    qsizetype m_pos = 0;
    QString m_rawName;
    Raw m_raw = Raw::None;
};

// Appends `text` to `out` with character references decoded as HTML5 does:
// named references (the HTML 4 set plus common HTML5 ones; the legacy
// Latin-1 names also without ';'), decimal and hexadecimal ones. NUL,
// surrogates and values past U+10FFFF become U+FFFD; &#128;–&#159; map
// through Windows-1252. In an attribute value (`inAttribute`) a legacy name
// without ';' followed by a letter, digit or '=' stays as written, so
// "?a=1&copy=2" keeps its "&copy". Raw NULs become U+FFFD.
void appendDecodedHtml(QString &out, QStringView text, bool inAttribute);
QString decodeHtml(QStringView text, bool inAttribute = false);

} // namespace QindaOffice::Mail
