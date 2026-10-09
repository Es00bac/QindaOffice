// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to html_sanitizer*.cpp — nothing else may include it. The element
// and attribute tables and output helpers live in html_sanitizer_tags.cpp,
// URL checks in html_sanitizer_urls.cpp, the CSS filter in
// html_sanitizer_css.cpp; the Sanitizer state machine is driven by
// html_sanitizer.cpp, removes hidden and dropped content in
// html_sanitizer_hidden.cpp and keeps its open-element stack and table
// repair in html_sanitizer_tree.cpp.

#include "html_sanitizer.h"
#include "html_tokenizer.h"

#include <QHash>
#include <QSet>

#include <array>
#include <optional>
#include <vector>

namespace QindaOffice::Mail::Detail {

// Every element name the sanitizer knows. Unknown names are unwrapped.
// AGENT-GUARD: alphabetical and in the order of kTags (html_sanitizer_tags.cpp):
// the enum value is the table index. static_asserts there check the order and
// the entries the code refers to by name.
enum class Tag : quint8 {
    A, Abbr, Acronym, Address, Applet, Area, Article, Aside, Audio, B,
    Base, Basefont, Bdi, Bdo, Bgsound, Big, Blink, Blockquote, Body, Br,
    Button, Canvas, Caption, Center, Cite, Code, Col, Colgroup, Data, Datalist,
    Dd, Del, Details, Dfn, Dialog, Dir, Div, Dl, Dt, Em,
    Embed, Fencedframe, Fieldset, Figcaption, Figure, Font, Footer, Form, Frame, Frameset,
    H1, H2, H3, H4, H5, H6, Head, Header, Hgroup, Hr,
    Html, I, Iframe, Image, Img, Input, Ins, Kbd, Keygen, Label,
    Legend, Li, Link, Listing, Main, Map, Mark, Marquee, Math, Menu,
    Meta, Meter, Nav, Nobr, Noembed, Noframes, Noscript, Object, Ol, Optgroup,
    Option, Output, P, Param, Picture, Plaintext, Portal, Pre, Progress, Q,
    Rb, Rp, Rt, Rtc, Ruby, S, Samp, Script, Search, Section,
    Select, Slot, Small, Source, Span, Strike, Strong, Style, Sub, Summary,
    Sup, Svg, Table, Tbody, Td, Template, Textarea, Tfoot, Th, Thead,
    Time, Title, Tr, Track, Tt, U, Ul, Var, Video, Wbr,
    Xml, Xmp,
    Count
};

inline constexpr std::size_t kTagCount = std::size_t(Tag::Count);
constexpr std::size_t index(Tag tag) { return std::size_t(tag); }

enum class TagAction : quint8 {
    Emit,      // kept, written as TagInfo::emitName
    Unwrap,    // the tag goes, its content stays (unknown elements too)
    Drop,      // removed with its content (void ones: just the tag)
    DropRaw,   // raw-text element removed with its content: script, iframe, …
    Ignore,    // removed silently, content kept: col, colgroup
    Structure, // html, head, body: the sanitizer writes its own skeleton
    Style,     // <style>: filtered into the head
    Title,     // <title>: recorded in SanitizedHtml::title, not shown
};

enum TagFlag : quint16 {
    Void = 0x01,        // no content and no end tag
    Block = 0x02,       // opening it closes an open <p> (HTML5 "close a p element")
    Scope = 0x04,       // table parts: end tags and implied end tags never cross them
    TableFamily = 0x08, // table tbody thead tfoot tr td th
    Heading = 0x10,     // h1–h6
    ListBox = 0x20,     // ul ol dir menu dl: where an <li>/<dd>/<dt> search stops
    Foreign = 0x40,     // svg math: "/>" really closes them
    Counted = 0x80,     // removing it counts in removedElements
};

struct TagInfo {
    const char *name;
    const char *emitName;
    TagAction action;
    quint16 flags;
};

const TagInfo &tagInfo(Tag tag);
std::optional<Tag> lookupTag(QStringView lowerName);

// Attributes that may survive, alphabetical (the value is the table index).
enum class Attr : quint8 {
    Align, Alt, Bgcolor, Border, Cellpadding, Cellspacing, Class, Color, Colspan, Dir,
    Face, Height, Href, Hspace, Id, Lang, Name, Nowrap, Rowspan, Size,
    Src, Start, Style, Title, Type, Valign, Value, Vspace, Width,
    Count
};
static_assert(int(Attr::Count) <= 32, "Sanitizer::attributes() keeps a 32-bit seen mask");

// The attribute when `name` may appear on `tag`, std::nullopt when it must go.
std::optional<Attr> allowedAttribute(Tag tag, QStringView name);
QLatin1StringView attributeName(Attr attr);
// Validates and normalizes a plain value (sizes clamped, colours, numbers,
// keywords). False: drop the attribute quietly — a malformed width is not an
// attack. Not for Href, Src, Style and Colspan, which have their own checks.
bool cleanAttributeValue(Attr attr, QStringView value, QString &out);
// The integer at the start of "12", " 12px", "12.5" — -1 when there is none.
int leadingInteger(QStringView value);

// Output helpers. appendEscaped escapes & < > " and turns NUL and unpaired
// surrogates into U+FFFD.
void appendEscaped(QString &out, QStringView text);
void appendAttribute(QString &out, QLatin1StringView name, QStringView value);
bool isBlank(QStringView text); // HTML whitespace only (U+00A0 is content)

// URLs (html_sanitizer_urls.cpp). `url` is the checked form: controls
// removed, trimmed, scheme lower-cased; protocol-relative "//h/x" → "https://h/x".
enum class UrlKind : quint8 { Empty, Fragment, Http, Https, Mailto, Cid, Data, Other };
struct CheckedUrl {
    UrlKind kind = UrlKind::Empty;
    QString url;
};
CheckedUrl checkUrl(QStringView value);
// The Content-ID of "cid:…" (the part after "cid:"): percent-decoded,
// trimmed, without <>; empty when unusable.
QString decodeCid(QStringView afterScheme);
// The payload of "data:image/…;base64,…" (the part after "data:") when it is
// an allowed raster type, valid base64, at most `maxBytes` and starts with
// that kind of image's signature.
std::optional<QByteArray> decodeDataImage(QStringView afterScheme, int maxBytes);

// CSS (html_sanitizer_css.cpp).
struct CssReport {
    bool allowRemote = false;
    bool remoteBlocked = false; // a remote url()/@import was dropped while !allowRemote
};
// style="…" → "name: value; …" of the declarations that survive (may be empty).
QString filterInlineStyle(QStringView css, CssReport &report);
// display:none or mso-hide:all (last declaration wins, !important first).
bool inlineStyleHides(QStringView css);
// The px value of `property` ("width", "height") or -1.
int inlineStylePixels(QStringView css, QLatin1StringView property);
// <style> → filtered rules (no at-rules); adds ".class", "tag.class", "#id",
// "tag#id" of simple selectors whose rules say display:none to `hidden`.
QString filterStyleSheet(QStringView css, CssReport &report, QSet<QString> &hidden);

// An open element; its flags travel with it so that stack walks need no
// table lookups.
struct OpenElement {
    Tag tag;
    quint16 flags;
};

class Sanitizer
{
public:
    Sanitizer(const HtmlSanitizeOptions &options, SanitizedHtml &result);
    void run(QStringView html);

private:
    enum class Raw : quint8 { None, Drop, Style, Title, Text };
    struct TableState {
        int table = -1;   // stack index of the <table>
        int section = -1; // current <thead>/<tbody>/<tfoot>, or -1
        int row = -1;     // current <tr>, or -1
        int cell = -1;    // current <td>/<th>, or -1 (kept open after </td>)
        int rows = 0;     // rows written
        int cols = 0;     // widest row so far (colspans included)
        int rowCols = 0;  // columns of the current row
        bool rowClosed = false; // </tr> seen: the next cell starts a row
        bool frozen = false;    // over budget: the rest flows into the current cell
    };

    // html_sanitizer.cpp
    void handle(const HtmlToken &token);
    void startTag(const HtmlToken &token);
    void startElement(Tag tag, const HtmlToken &token);
    void endTag(const HtmlToken &token);
    void text(QStringView text);
    void rawText(const HtmlToken &token);
    void image(const HtmlToken &token);
    int inlineImage(const QString &dataUrl);
    bool isTrackingPixel(const HtmlToken &token) const;
    bool styleApplies(const HtmlToken &token) const;
    void noteBackground(QStringView value);
    QString attributes(Tag tag, const HtmlToken &token, int colspan = 0);
    void finish();

    // html_sanitizer_hidden.cpp
    bool hides(const HtmlToken &token) const;
    bool matchesHidden(QChar kind, QStringView name, const QString &tagName) const;
    void beginSkip(const QString &name, std::optional<Tag> tag);
    void endSkip();
    bool endsSkippedElement(Tag next) const;
    bool skipToken(const HtmlToken &token);

    // html_sanitizer_tree.cpp
    bool open(Tag tag, const HtmlToken *token);
    void openWith(Tag tag, const QString &attributes);
    void popTo(std::size_t size);
    void closeImplied(Tag tag);
    void close(Tag tag);
    void lineBreak();
    bool atTableLevel() const;
    void ensureFlow();
    void openTable(const HtmlToken &token);
    void tableStructure(Tag tag, const HtmlToken &token);
    void openRow(const HtmlToken *token, bool force);
    void openCell(Tag tag, const HtmlToken *token, bool force);
    void openSection(Tag tag, const HtmlToken &token);
    void separateCell(TableState &table);
    void tableEnd(Tag tag);
    void suppressedTableTag(Tag tag, bool start);

    const HtmlSanitizeOptions &m_options;
    SanitizedHtml &m_result;
    QString m_body;           // the body content as it is written
    QString m_bodyAttributes; // from the first <body>
    QString m_styles;         // filtered style sheets, written into the head
    bool m_bodySeen = false;
    bool m_contentStarted = false; // blank text before the first content is dropped
    Raw m_raw = Raw::None;         // what the next RawText token is for

    std::vector<OpenElement> m_stack;          // open elements, innermost last
    std::array<int, kTagCount> m_open{};       // open elements per tag
    std::array<int, kTagCount> m_suppressed{}; // start tags not written (depth cap)
    std::vector<TableState> m_tables;          // open tables, innermost last
    int m_suppressedTables = 0;                // open tables being flattened
    qint64 m_usedCells = 0;                    // Σ rows × cols written

    bool m_skipping = false; // inside an element removed with its content
    QString m_skipName;
    std::optional<Tag> m_skipTag;
    bool m_skipForeign = false;
    int m_skipDepth = 0;                   // nested elements named m_skipName
    std::array<int, kTagCount> m_skipInner{}; // elements opened inside the skipped one
    std::vector<Tag> m_skipTouched;

    CssReport m_css;
    QSet<QString> m_hiddenSelectors;
    QSet<QString> m_remoteSeen;
    QSet<QString> m_cidSeen;
    QHash<QString, int> m_dataIndex;
};

} // namespace QindaOffice::Mail::Detail
