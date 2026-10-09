// SPDX-License-Identifier: GPL-3.0-or-later
// The sanitizer's element and attribute tables and its output helpers
// (html_sanitizer_p.h).

#include "html_sanitizer_p.h"

#include <algorithm>
#include <iterator>

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail::Detail {

namespace {

// AGENT-NOTE: what each element becomes. Emit = what QTextDocument renders
// (names Qt does not know are mapped: HTML5 sectioning → div, strike → s,
// ins → u, abbr/q/mark/label/time → span — the Qt 6.11 parser knows del but
// neither strike nor ins). Drop = active or remote content, removed with its
// content. Unwrap = the tag goes and the content stays (form, button,
// noscript — scripts never run, so a browser would show it). caption and
// colgroup/col are Unwrap/Ignore because Qt ignores colgroup/col and a
// caption's text is kept as the table's first cell.
// AGENT-GUARD: sorted by name and in the order of enum Tag (checked below).
constexpr TagInfo kTags[] = {
    {"a", "a", TagAction::Emit, 0}, {"abbr", "span", TagAction::Emit, 0},
    {"acronym", "span", TagAction::Emit, 0}, {"address", "address", TagAction::Emit, Block},
    {"applet", "applet", TagAction::Drop, Counted}, {"area", "area", TagAction::Drop, Void | Counted},
    {"article", "div", TagAction::Emit, Block}, {"aside", "div", TagAction::Emit, Block},
    {"audio", "audio", TagAction::Drop, Counted}, {"b", "b", TagAction::Emit, 0},
    {"base", "base", TagAction::Drop, Void | Counted}, {"basefont", "basefont", TagAction::Drop, Void | Counted},
    {"bdi", "span", TagAction::Emit, 0}, {"bdo", "span", TagAction::Emit, 0},
    {"bgsound", "bgsound", TagAction::Drop, Void | Counted}, {"big", "big", TagAction::Emit, 0},
    {"blink", "blink", TagAction::Unwrap, 0}, {"blockquote", "blockquote", TagAction::Emit, Block},
    {"body", "body", TagAction::Structure, 0}, {"br", "br", TagAction::Emit, Void},
    {"button", "button", TagAction::Unwrap, 0}, {"canvas", "canvas", TagAction::Drop, Counted},
    {"caption", "caption", TagAction::Unwrap, 0}, {"center", "center", TagAction::Emit, Block},
    {"cite", "cite", TagAction::Emit, 0}, {"code", "code", TagAction::Emit, 0},
    {"col", "col", TagAction::Ignore, Void}, {"colgroup", "colgroup", TagAction::Ignore, 0},
    {"data", "data", TagAction::Unwrap, 0}, {"datalist", "datalist", TagAction::Drop, Counted},
    {"dd", "dd", TagAction::Emit, Block}, {"del", "del", TagAction::Emit, 0},
    {"details", "div", TagAction::Emit, Block}, {"dfn", "dfn", TagAction::Emit, 0},
    {"dialog", "dialog", TagAction::Drop, Counted}, {"dir", "ul", TagAction::Emit, Block | ListBox},
    {"div", "div", TagAction::Emit, Block}, {"dl", "dl", TagAction::Emit, Block | ListBox},
    {"dt", "dt", TagAction::Emit, Block}, {"em", "em", TagAction::Emit, 0},
    {"embed", "embed", TagAction::Drop, Void | Counted}, {"fencedframe", "fencedframe", TagAction::Drop, Counted},
    {"fieldset", "div", TagAction::Emit, Block}, {"figcaption", "div", TagAction::Emit, Block},
    {"figure", "div", TagAction::Emit, Block}, {"font", "font", TagAction::Emit, 0},
    {"footer", "div", TagAction::Emit, Block}, {"form", "form", TagAction::Unwrap, 0},
    {"frame", "frame", TagAction::Drop, Void | Counted}, {"frameset", "frameset", TagAction::Drop, Counted},
    {"h1", "h1", TagAction::Emit, Block | Heading}, {"h2", "h2", TagAction::Emit, Block | Heading},
    {"h3", "h3", TagAction::Emit, Block | Heading}, {"h4", "h4", TagAction::Emit, Block | Heading},
    {"h5", "h5", TagAction::Emit, Block | Heading}, {"h6", "h6", TagAction::Emit, Block | Heading},
    {"head", "head", TagAction::Structure, 0}, {"header", "div", TagAction::Emit, Block},
    {"hgroup", "div", TagAction::Emit, Block}, {"hr", "hr", TagAction::Emit, Void | Block},
    {"html", "html", TagAction::Structure, 0}, {"i", "i", TagAction::Emit, 0},
    {"iframe", "iframe", TagAction::DropRaw, Counted}, {"image", "img", TagAction::Emit, Void},
    {"img", "img", TagAction::Emit, Void}, {"input", "input", TagAction::Drop, Void | Counted},
    {"ins", "u", TagAction::Emit, 0}, {"kbd", "kbd", TagAction::Emit, 0},
    {"keygen", "keygen", TagAction::Drop, Void | Counted}, {"label", "span", TagAction::Emit, 0},
    {"legend", "div", TagAction::Emit, Block}, {"li", "li", TagAction::Emit, Block},
    {"link", "link", TagAction::Drop, Void | Counted}, {"listing", "pre", TagAction::Emit, Block},
    {"main", "div", TagAction::Emit, Block}, {"map", "map", TagAction::Drop, Counted},
    {"mark", "span", TagAction::Emit, 0}, {"marquee", "marquee", TagAction::Unwrap, 0},
    {"math", "math", TagAction::Drop, Foreign | Counted}, {"menu", "ul", TagAction::Emit, Block | ListBox},
    {"meta", "meta", TagAction::Drop, Void}, {"meter", "meter", TagAction::Unwrap, 0},
    {"nav", "div", TagAction::Emit, Block}, {"nobr", "nobr", TagAction::Emit, 0},
    {"noembed", "noembed", TagAction::DropRaw, Counted}, {"noframes", "noframes", TagAction::DropRaw, Counted},
    {"noscript", "noscript", TagAction::Unwrap, 0}, {"object", "object", TagAction::Drop, Counted},
    {"ol", "ol", TagAction::Emit, Block | ListBox}, {"optgroup", "optgroup", TagAction::Drop, Counted},
    {"option", "option", TagAction::Drop, Counted}, {"output", "output", TagAction::Unwrap, 0},
    {"p", "p", TagAction::Emit, Block}, {"param", "param", TagAction::Drop, Void | Counted},
    {"picture", "picture", TagAction::Unwrap, 0}, {"plaintext", "pre", TagAction::Emit, Block},
    {"portal", "portal", TagAction::Drop, Counted}, {"pre", "pre", TagAction::Emit, Block},
    {"progress", "progress", TagAction::Unwrap, 0}, {"q", "span", TagAction::Emit, 0},
    {"rb", "rb", TagAction::Unwrap, 0}, {"rp", "rp", TagAction::Unwrap, 0},
    {"rt", "rt", TagAction::Unwrap, 0}, {"rtc", "rtc", TagAction::Unwrap, 0},
    {"ruby", "ruby", TagAction::Unwrap, 0}, {"s", "s", TagAction::Emit, 0},
    {"samp", "samp", TagAction::Emit, 0}, {"script", "script", TagAction::DropRaw, Counted},
    {"search", "div", TagAction::Emit, Block}, {"section", "div", TagAction::Emit, Block},
    {"select", "select", TagAction::Drop, Counted}, {"slot", "slot", TagAction::Unwrap, 0},
    {"small", "small", TagAction::Emit, 0}, {"source", "source", TagAction::Drop, Void | Counted},
    {"span", "span", TagAction::Emit, 0}, {"strike", "s", TagAction::Emit, 0},
    {"strong", "strong", TagAction::Emit, 0}, {"style", "style", TagAction::Style, 0},
    {"sub", "sub", TagAction::Emit, 0}, {"summary", "div", TagAction::Emit, Block},
    {"sup", "sup", TagAction::Emit, 0}, {"svg", "svg", TagAction::Drop, Foreign | Counted},
    {"table", "table", TagAction::Emit, Block | Scope | TableFamily}, {"tbody", "tbody", TagAction::Emit, Scope | TableFamily},
    {"td", "td", TagAction::Emit, Scope | TableFamily}, {"template", "template", TagAction::Drop, Counted},
    {"textarea", "textarea", TagAction::DropRaw, Counted}, {"tfoot", "tfoot", TagAction::Emit, Scope | TableFamily},
    {"th", "th", TagAction::Emit, Scope | TableFamily}, {"thead", "thead", TagAction::Emit, Scope | TableFamily},
    {"time", "span", TagAction::Emit, 0}, {"title", "title", TagAction::Title, 0},
    {"tr", "tr", TagAction::Emit, Scope | TableFamily}, {"track", "track", TagAction::Drop, Void | Counted},
    {"tt", "tt", TagAction::Emit, 0}, {"u", "u", TagAction::Emit, 0},
    {"ul", "ul", TagAction::Emit, Block | ListBox}, {"var", "var", TagAction::Emit, 0},
    {"video", "video", TagAction::Drop, Counted}, {"wbr", "wbr", TagAction::Emit, Void},
    {"xml", "xml", TagAction::Drop, Counted}, {"xmp", "pre", TagAction::Emit, Block},
};

constexpr bool sameName(const char *a, const char *b)
{
    for (; *a && *a == *b; ++a, ++b) {
    }
    return *a == *b;
}

constexpr bool lessName(const char *a, const char *b)
{
    for (; *a && *a == *b; ++a, ++b) {
    }
    return static_cast<unsigned char>(*a) < static_cast<unsigned char>(*b);
}

constexpr bool tagsSorted()
{
    for (std::size_t i = 1; i < std::size(kTags); ++i) {
        if (!lessName(kTags[i - 1].name, kTags[i].name))
            return false;
    }
    return true;
}

constexpr bool tagIs(Tag tag, const char *name)
{
    return sameName(kTags[index(tag)].name, name);
}

static_assert(std::size(kTags) == kTagCount, "one kTags entry per Tag");
static_assert(tagsSorted(), "kTags must stay sorted by name");
static_assert(tagIs(Tag::A, "a") && tagIs(Tag::Address, "address") && tagIs(Tag::Body, "body")
                  && tagIs(Tag::Br, "br") && tagIs(Tag::Dd, "dd") && tagIs(Tag::Dir, "dir")
                  && tagIs(Tag::Div, "div") && tagIs(Tag::Dt, "dt") && tagIs(Tag::Image, "image")
                  && tagIs(Tag::Img, "img") && tagIs(Tag::Li, "li") && tagIs(Tag::Menu, "menu")
                  && tagIs(Tag::Meta, "meta") && tagIs(Tag::Ol, "ol") && tagIs(Tag::P, "p")
                  && tagIs(Tag::Plaintext, "plaintext") && tagIs(Tag::Table, "table")
                  && tagIs(Tag::Tbody, "tbody") && tagIs(Tag::Td, "td") && tagIs(Tag::Tfoot, "tfoot")
                  && tagIs(Tag::Th, "th") && tagIs(Tag::Thead, "thead") && tagIs(Tag::Tr, "tr")
                  && tagIs(Tag::Ul, "ul") && tagIs(Tag::Wbr, "wbr") && tagIs(Tag::Xmp, "xmp"),
              "enum Tag and kTags disagree");

enum Where : quint8 { Anywhere, OnA, OnImg, OnOl, OnLists, OnLi };

struct AttrInfo {
    const char *name;
    Where where;
};

// AGENT-NOTE: the attribute allowlist. Everything else is dropped — every
// on* handler, srcset, background, formaction, action, xmlns*, xlink:href,
// data-*, ping, poster, dynsrc, lowsrc, target, rel. Same order as enum Attr.
constexpr AttrInfo kAttributes[] = {
    {"align", Anywhere}, {"alt", OnImg}, {"bgcolor", Anywhere}, {"border", Anywhere},
    {"cellpadding", Anywhere}, {"cellspacing", Anywhere}, {"class", Anywhere},
    {"color", Anywhere}, {"colspan", Anywhere}, {"dir", Anywhere}, {"face", Anywhere},
    {"height", Anywhere}, {"href", OnA}, {"hspace", OnImg}, {"id", OnA}, {"lang", Anywhere},
    {"name", OnA}, {"nowrap", Anywhere}, {"rowspan", Anywhere}, {"size", Anywhere},
    {"src", OnImg}, {"start", OnOl}, {"style", Anywhere}, {"title", Anywhere},
    {"type", OnLists}, {"valign", Anywhere}, {"value", OnLi}, {"vspace", OnImg},
    {"width", Anywhere},
};

constexpr bool attributesSorted()
{
    for (std::size_t i = 1; i < std::size(kAttributes); ++i) {
        if (!lessName(kAttributes[i - 1].name, kAttributes[i].name))
            return false;
    }
    return true;
}

static_assert(std::size(kAttributes) == std::size_t(Attr::Count), "one entry per Attr");
static_assert(attributesSorted(), "kAttributes must stay sorted by name");
static_assert(sameName(kAttributes[std::size_t(Attr::Href)].name, "href")
                  && sameName(kAttributes[std::size_t(Attr::Width)].name, "width"),
              "enum Attr and kAttributes disagree");

bool allowedOn(Where where, Tag tag)
{
    switch (where) {
    case Anywhere:
        return true;
    case OnA:
        return tag == Tag::A;
    case OnImg:
        return tag == Tag::Img || tag == Tag::Image;
    case OnOl:
        return tag == Tag::Ol;
    case OnLists:
        return tag == Tag::Ol || tag == Tag::Ul || tag == Tag::Li || tag == Tag::Dir || tag == Tag::Menu;
    case OnLi:
        return tag == Tag::Li;
    }
    return false;
}

bool isAsciiDigit(QChar c)
{
    return c.unicode() >= u'0' && c.unicode() <= u'9';
}

// "12", "12px", "12.5", "12%" → the integer, whether it ended in '%';
// false for anything else ("auto", "-3", "1e9", "12 em").
bool parseLength(QStringView value, qint64 &number, bool &percent)
{
    qsizetype i = 0;
    number = 0;
    while (i < value.size() && isAsciiDigit(value[i]) && i < 9)
        number = number * 10 + (value[i++].unicode() - u'0');
    if (i == 0 || (i < value.size() && isAsciiDigit(value[i])))
        return false;
    if (i < value.size() && value[i] == u'.') {
        ++i;
        while (i < value.size() && isAsciiDigit(value[i]))
            ++i;
    }
    const QStringView unit = value.sliced(i).trimmed();
    percent = unit == u"%";
    return unit.isEmpty() || percent || unit.compare("px"_L1, Qt::CaseInsensitive) == 0;
}

bool isKeyword(QStringView value, qsizetype maximum, bool allowHash)
{
    if (value.isEmpty() || value.size() > maximum)
        return false;
    for (const QChar c : value) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || isAsciiDigit(c)
                        || u == u'-' || u == u'_' || (allowHash && u == u'#');
        if (!ok)
            return false;
    }
    return true;
}

} // namespace

const TagInfo &tagInfo(Tag tag)
{
    return kTags[index(tag)];
}

std::optional<Tag> lookupTag(QStringView lowerName)
{
    const auto *end = std::end(kTags);
    const auto *it = std::lower_bound(std::begin(kTags), end, lowerName, [](const TagInfo &info, QStringView name) {
        return QLatin1StringView(info.name).compare(name) < 0;
    });
    if (it == end || QLatin1StringView(it->name) != lowerName)
        return std::nullopt;
    return Tag(it - std::begin(kTags));
}

std::optional<Attr> allowedAttribute(Tag tag, QStringView name)
{
    const auto *end = std::end(kAttributes);
    const auto *it = std::lower_bound(std::begin(kAttributes), end, name, [](const AttrInfo &info, QStringView n) {
        return QLatin1StringView(info.name).compare(n) < 0;
    });
    if (it == end || QLatin1StringView(it->name) != name || !allowedOn(it->where, tag))
        return std::nullopt;
    return Attr(it - std::begin(kAttributes));
}

QLatin1StringView attributeName(Attr attr)
{
    return QLatin1StringView(kAttributes[std::size_t(attr)].name);
}

int leadingInteger(QStringView value)
{
    qint64 number = 0;
    bool percent = false;
    return parseLength(value.trimmed(), number, percent) && !percent ? int(number) : -1;
}

bool cleanAttributeValue(Attr attr, QStringView value, QString &out)
{
    const QStringView v = value.trimmed();
    qint64 number = 0;
    bool percent = false;
    switch (attr) {
    case Attr::Width:
    case Attr::Height:
        // AGENT-NOTE: clamped — Qt lays out and scales whatever it is given,
        // and width="99999999" on an image or table is a cheap memory attack.
        if (!parseLength(v, number, percent))
            return false;
        out = percent ? QString::number(qMin(number, qint64(100))) + u'%'
                      : QString::number(qMin(number, qint64(8192)));
        return true;
    case Attr::Border:
    case Attr::Cellpadding:
    case Attr::Cellspacing:
    case Attr::Hspace:
    case Attr::Vspace:
    case Attr::Rowspan:
        if (!parseLength(v, number, percent) || percent)
            return false;
        out = QString::number(qBound(qint64(attr == Attr::Rowspan ? 1 : 0), number, qint64(1000)));
        return true;
    case Attr::Start:
    case Attr::Value: {
        const bool negative = v.startsWith(u'-');
        if (!parseLength(negative ? v.sliced(1) : v, number, percent) || percent)
            return false;
        out = QString::number(negative ? -qMin(number, qint64(1000000)) : qMin(number, qint64(1000000)));
        return true;
    }
    case Attr::Size:
        if (!isKeyword(v.startsWith(u'+') || v.startsWith(u'-') ? v.sliced(1) : v, 2, false))
            return false;
        out = v.toString();
        return true;
    case Attr::Color:
    case Attr::Bgcolor:
        if (!isKeyword(v, 32, true))
            return false;
        out = v.toString();
        return true;
    case Attr::Align:
    case Attr::Valign:
    case Attr::Dir:
    case Attr::Type:
    case Attr::Lang:
        if (!isKeyword(v, attr == Attr::Lang ? 35 : 16, false))
            return false;
        out = v.toString();
        return true;
    case Attr::Nowrap:
        out = QStringLiteral("nowrap");
        return true;
    default: // alt, class, face, id, name, title: text, escaped on output
        out = value.toString();
        return true;
    }
}

void appendEscaped(QString &out, QStringView text)
{
    out.reserve(out.size() + text.size());
    const char16_t *data = text.utf16();
    const qsizetype n = text.size();
    qsizetype run = 0; // start of the characters copied as they are
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t u = data[i];
        QLatin1StringView entity;
        switch (u) {
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
            if (!QChar::isSurrogate(u))
                continue;
            if (QChar::isHighSurrogate(u) && i + 1 < n && QChar::isLowSurrogate(data[i + 1])) {
                ++i;
                continue;
            }
        }
        out.append(text.sliced(run, i - run));
        if (entity.isNull())
            out += QChar(QChar::ReplacementCharacter); // NUL or an unpaired surrogate
        else
            out += entity;
        run = i + 1;
    }
    out.append(text.sliced(run));
}

void appendAttribute(QString &out, QLatin1StringView name, QStringView value)
{
    out += u' ';
    out += name;
    out += "=\""_L1;
    appendEscaped(out, value);
    out += u'"';
}

bool isBlank(QStringView text)
{
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u != u' ' && u != u'\t' && u != u'\n' && u != u'\f' && u != u'\r')
            return false;
    }
    return true;
}

} // namespace QindaOffice::Mail::Detail
