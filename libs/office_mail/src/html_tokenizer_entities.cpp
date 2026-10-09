// SPDX-License-Identifier: GPL-3.0-or-later
// Character references for html_tokenizer.h: the named-reference table and
// the HTML5 decoding rules of appendDecodedHtml().

#include "html_tokenizer.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace QindaOffice::Mail {

namespace {

struct NamedReference {
    std::string_view name;
    char16_t code;
    bool legacy; // HTML5 also decodes it without the ';'
};

// The HTML 4 set (Latin-1, symbols, Greek, special), HTML5's upper-case
// legacy aliases and a few common HTML5 names; all of them map to one BMP
// code point. Generated, one entry per name.
// AGENT-GUARD: sorted by name in byte order — findReference() binary-searches
// and the static_assert below fails the build when an edit breaks the order.
constexpr NamedReference kReferences[] = {
    {"AElig", 0x00C6, true}, {"AMP", 0x0026, true}, {"Aacute", 0x00C1, true},
    {"Acirc", 0x00C2, true}, {"Agrave", 0x00C0, true}, {"Alpha", 0x0391, false},
    {"Aring", 0x00C5, true}, {"Atilde", 0x00C3, true}, {"Auml", 0x00C4, true},
    {"Beta", 0x0392, false}, {"COPY", 0x00A9, true}, {"Ccedil", 0x00C7, true},
    {"Chi", 0x03A7, false}, {"Dagger", 0x2021, false}, {"Delta", 0x0394, false},
    {"ETH", 0x00D0, true}, {"Eacute", 0x00C9, true}, {"Ecirc", 0x00CA, true},
    {"Egrave", 0x00C8, true}, {"Epsilon", 0x0395, false}, {"Eta", 0x0397, false},
    {"Euml", 0x00CB, true}, {"GT", 0x003E, true}, {"Gamma", 0x0393, false},
    {"Hat", 0x005E, false}, {"Iacute", 0x00CD, true}, {"Icirc", 0x00CE, true},
    {"Igrave", 0x00CC, true}, {"Iota", 0x0399, false}, {"Iuml", 0x00CF, true},
    {"Kappa", 0x039A, false}, {"LT", 0x003C, true}, {"Lambda", 0x039B, false},
    {"Mu", 0x039C, false}, {"NewLine", 0x000A, false}, {"NoBreak", 0x2060, false},
    {"Ntilde", 0x00D1, true}, {"Nu", 0x039D, false}, {"OElig", 0x0152, false},
    {"Oacute", 0x00D3, true}, {"Ocirc", 0x00D4, true}, {"Ograve", 0x00D2, true},
    {"Omega", 0x03A9, false}, {"Omicron", 0x039F, false}, {"Oslash", 0x00D8, true},
    {"Otilde", 0x00D5, true}, {"Ouml", 0x00D6, true}, {"Phi", 0x03A6, false},
    {"Pi", 0x03A0, false}, {"Prime", 0x2033, false}, {"Psi", 0x03A8, false},
    {"QUOT", 0x0022, true}, {"REG", 0x00AE, true}, {"Rho", 0x03A1, false},
    {"Scaron", 0x0160, false}, {"Sigma", 0x03A3, false}, {"THORN", 0x00DE, true},
    {"Tab", 0x0009, false}, {"Tau", 0x03A4, false}, {"Theta", 0x0398, false},
    {"Uacute", 0x00DA, true}, {"Ucirc", 0x00DB, true}, {"Ugrave", 0x00D9, true},
    {"Upsilon", 0x03A5, false}, {"Uuml", 0x00DC, true}, {"Xi", 0x039E, false},
    {"Yacute", 0x00DD, true}, {"Yuml", 0x0178, false}, {"ZeroWidthSpace", 0x200B, false},
    {"Zeta", 0x0396, false}, {"aacute", 0x00E1, true}, {"acirc", 0x00E2, true},
    {"acute", 0x00B4, true}, {"aelig", 0x00E6, true}, {"agrave", 0x00E0, true},
    {"alefsym", 0x2135, false}, {"alpha", 0x03B1, false}, {"amp", 0x0026, true},
    {"and", 0x2227, false}, {"ang", 0x2220, false}, {"apos", 0x0027, false},
    {"aring", 0x00E5, true}, {"ast", 0x002A, false}, {"asymp", 0x2248, false},
    {"atilde", 0x00E3, true}, {"auml", 0x00E4, true}, {"bdquo", 0x201E, false},
    {"beta", 0x03B2, false}, {"brvbar", 0x00A6, true}, {"bsol", 0x005C, false},
    {"bull", 0x2022, false}, {"cap", 0x2229, false}, {"ccedil", 0x00E7, true},
    {"cedil", 0x00B8, true}, {"cent", 0x00A2, true}, {"check", 0x2713, false},
    {"chi", 0x03C7, false}, {"circ", 0x02C6, false}, {"clubs", 0x2663, false},
    {"colon", 0x003A, false}, {"comma", 0x002C, false}, {"commat", 0x0040, false},
    {"cong", 0x2245, false}, {"copy", 0x00A9, true}, {"copysr", 0x2117, false},
    {"crarr", 0x21B5, false}, {"cross", 0x2717, false}, {"cup", 0x222A, false},
    {"curren", 0x00A4, true}, {"dArr", 0x21D3, false}, {"dagger", 0x2020, false},
    {"darr", 0x2193, false}, {"dash", 0x2010, false}, {"deg", 0x00B0, true},
    {"delta", 0x03B4, false}, {"diams", 0x2666, false}, {"divide", 0x00F7, true},
    {"dollar", 0x0024, false}, {"eacute", 0x00E9, true}, {"ecirc", 0x00EA, true},
    {"egrave", 0x00E8, true}, {"empty", 0x2205, false}, {"emsp", 0x2003, false},
    {"ensp", 0x2002, false}, {"epsilon", 0x03B5, false}, {"equals", 0x003D, false},
    {"equiv", 0x2261, false}, {"eta", 0x03B7, false}, {"eth", 0x00F0, true},
    {"euml", 0x00EB, true}, {"euro", 0x20AC, false}, {"excl", 0x0021, false},
    {"exist", 0x2203, false}, {"female", 0x2640, false}, {"fnof", 0x0192, false},
    {"forall", 0x2200, false}, {"frac12", 0x00BD, true}, {"frac14", 0x00BC, true},
    {"frac34", 0x00BE, true}, {"frasl", 0x2044, false}, {"gamma", 0x03B3, false},
    {"ge", 0x2265, false}, {"grave", 0x0060, false}, {"gt", 0x003E, true},
    {"hArr", 0x21D4, false}, {"hairsp", 0x200A, false}, {"harr", 0x2194, false},
    {"hearts", 0x2665, false}, {"hellip", 0x2026, false}, {"hyphen", 0x2010, false},
    {"iacute", 0x00ED, true}, {"icirc", 0x00EE, true}, {"iexcl", 0x00A1, true},
    {"igrave", 0x00EC, true}, {"image", 0x2111, false}, {"infin", 0x221E, false},
    {"int", 0x222B, false}, {"iota", 0x03B9, false}, {"iquest", 0x00BF, true},
    {"isin", 0x2208, false}, {"iuml", 0x00EF, true}, {"kappa", 0x03BA, false},
    {"lArr", 0x21D0, false}, {"lambda", 0x03BB, false}, {"lang", 0x27E8, false},
    {"laquo", 0x00AB, true}, {"larr", 0x2190, false}, {"lceil", 0x2308, false},
    {"lcub", 0x007B, false}, {"ldquo", 0x201C, false}, {"le", 0x2264, false},
    {"lfloor", 0x230A, false}, {"lowast", 0x2217, false}, {"lowbar", 0x005F, false},
    {"loz", 0x25CA, false}, {"lpar", 0x0028, false}, {"lrm", 0x200E, false},
    {"lsaquo", 0x2039, false}, {"lsqb", 0x005B, false}, {"lsquo", 0x2018, false},
    {"lt", 0x003C, true}, {"macr", 0x00AF, true}, {"male", 0x2642, false},
    {"mdash", 0x2014, false}, {"micro", 0x00B5, true}, {"middot", 0x00B7, true},
    {"minus", 0x2212, false}, {"mu", 0x03BC, false}, {"nabla", 0x2207, false},
    {"nbsp", 0x00A0, true}, {"ndash", 0x2013, false}, {"ne", 0x2260, false},
    {"ni", 0x220B, false}, {"not", 0x00AC, true}, {"notin", 0x2209, false},
    {"nsub", 0x2284, false}, {"ntilde", 0x00F1, true}, {"nu", 0x03BD, false},
    {"num", 0x0023, false}, {"oacute", 0x00F3, true}, {"ocirc", 0x00F4, true},
    {"oelig", 0x0153, false}, {"ograve", 0x00F2, true}, {"oline", 0x203E, false},
    {"omega", 0x03C9, false}, {"omicron", 0x03BF, false}, {"oplus", 0x2295, false},
    {"or", 0x2228, false}, {"ordf", 0x00AA, true}, {"ordm", 0x00BA, true},
    {"oslash", 0x00F8, true}, {"otilde", 0x00F5, true}, {"otimes", 0x2297, false},
    {"ouml", 0x00F6, true}, {"para", 0x00B6, true}, {"part", 0x2202, false},
    {"percnt", 0x0025, false}, {"period", 0x002E, false}, {"permil", 0x2030, false},
    {"perp", 0x22A5, false}, {"phi", 0x03C6, false}, {"phone", 0x260E, false},
    {"pi", 0x03C0, false}, {"piv", 0x03D6, false}, {"plus", 0x002B, false},
    {"plusmn", 0x00B1, true}, {"pound", 0x00A3, true}, {"prime", 0x2032, false},
    {"prod", 0x220F, false}, {"prop", 0x221D, false}, {"psi", 0x03C8, false},
    {"quest", 0x003F, false}, {"quot", 0x0022, true}, {"rArr", 0x21D2, false},
    {"radic", 0x221A, false}, {"rang", 0x27E9, false}, {"raquo", 0x00BB, true},
    {"rarr", 0x2192, false}, {"rceil", 0x2309, false}, {"rcub", 0x007D, false},
    {"rdquo", 0x201D, false}, {"real", 0x211C, false}, {"reg", 0x00AE, true},
    {"rfloor", 0x230B, false}, {"rho", 0x03C1, false}, {"rlm", 0x200F, false},
    {"rpar", 0x0029, false}, {"rsaquo", 0x203A, false}, {"rsqb", 0x005D, false},
    {"rsquo", 0x2019, false}, {"sbquo", 0x201A, false}, {"scaron", 0x0161, false},
    {"sdot", 0x22C5, false}, {"sect", 0x00A7, true}, {"semi", 0x003B, false},
    {"shy", 0x00AD, true}, {"sigma", 0x03C3, false}, {"sigmaf", 0x03C2, false},
    {"sim", 0x223C, false}, {"sol", 0x002F, false}, {"spades", 0x2660, false},
    {"star", 0x2606, false}, {"starf", 0x2605, false}, {"sub", 0x2282, false},
    {"sube", 0x2286, false}, {"sum", 0x2211, false}, {"sup", 0x2283, false},
    {"sup1", 0x00B9, true}, {"sup2", 0x00B2, true}, {"sup3", 0x00B3, true},
    {"supe", 0x2287, false}, {"szlig", 0x00DF, true}, {"tau", 0x03C4, false},
    {"there4", 0x2234, false}, {"theta", 0x03B8, false}, {"thetasym", 0x03D1, false},
    {"thinsp", 0x2009, false}, {"thorn", 0x00FE, true}, {"tilde", 0x02DC, false},
    {"times", 0x00D7, true}, {"trade", 0x2122, false}, {"uArr", 0x21D1, false},
    {"uacute", 0x00FA, true}, {"uarr", 0x2191, false}, {"ucirc", 0x00FB, true},
    {"ugrave", 0x00F9, true}, {"uml", 0x00A8, true}, {"upsih", 0x03D2, false},
    {"upsilon", 0x03C5, false}, {"uuml", 0x00FC, true}, {"verbar", 0x007C, false},
    {"vert", 0x007C, false}, {"weierp", 0x2118, false}, {"xi", 0x03BE, false},
    {"yacute", 0x00FD, true}, {"yen", 0x00A5, true}, {"yuml", 0x00FF, true},
    {"zeta", 0x03B6, false}, {"zwj", 0x200D, false}, {"zwnj", 0x200C, false},
};

constexpr bool referencesSorted()
{
    for (std::size_t i = 1; i < std::size(kReferences); ++i) {
        if (!(kReferences[i - 1].name < kReferences[i].name))
            return false;
    }
    return true;
}
static_assert(referencesSorted(), "kReferences must stay sorted by name");

constexpr qsizetype kMaxReferenceName = 32; // HTML5's longest name
constexpr qsizetype kMaxLegacyName = 6;     // "frac12", "middot", …

// &#128;–&#159; are Windows-1252 in practice; 0 marks the five unmapped ones.
constexpr char16_t kWindows1252[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

bool isAsciiAlnum(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
}

int digitValue(QChar c, bool hex)
{
    const char16_t u = c.unicode();
    if (u >= u'0' && u <= u'9')
        return u - u'0';
    if (hex && u >= u'a' && u <= u'f')
        return u - u'a' + 10;
    if (hex && u >= u'A' && u <= u'F')
        return u - u'A' + 10;
    return -1;
}

const NamedReference *findReference(QStringView name)
{
    if (name.size() > kMaxReferenceName)
        return nullptr;
    char key[kMaxReferenceName];
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (name[i].unicode() > 0x7f)
            return nullptr;
        key[i] = char(name[i].unicode());
    }
    const std::string_view wanted(key, std::size_t(name.size()));
    const auto *end = std::end(kReferences);
    const auto *it = std::lower_bound(std::begin(kReferences), end, wanted,
                                      [](const NamedReference &r, std::string_view v) { return r.name < v; });
    return it != end && it->name == wanted ? it : nullptr;
}

// HTML5 "numeric character reference end state".
char32_t numericReplacement(char32_t value)
{
    if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        return 0xFFFD;
    if (value >= 0x80 && value <= 0x9F)
        return kWindows1252[value - 0x80] ? kWindows1252[value - 0x80] : 0xFFFD;
    const bool control = value < 0x20 && value != 0x09 && value != 0x0A && value != 0x0C && value != 0x0D;
    return control || value == 0x7F ? 0xFFFD : value;
}

void appendCodePoint(QString &out, char32_t value)
{
    if (value > 0xFFFF) {
        out += QChar(QChar::highSurrogate(value));
        out += QChar(QChar::lowSurrogate(value));
    } else {
        out += QChar(char16_t(value));
    }
}

// Decodes the reference starting at text[at] == '&' into `out`; returns the
// number of characters consumed, 0 when it is not a reference ('&' is text).
qsizetype decodeReference(QString &out, QStringView text, qsizetype at, bool inAttribute)
{
    const qsizetype n = text.size();
    qsizetype k = at + 1;
    if (k < n && text[k] == u'#') {
        ++k;
        const bool hex = k < n && (text[k] == u'x' || text[k] == u'X');
        if (hex)
            ++k;
        const qsizetype digits = k;
        char32_t value = 0;
        bool overflow = false;
        for (; k < n; ++k) {
            const int digit = digitValue(text[k], hex);
            if (digit < 0)
                break;
            if (!overflow) { // stays below 2^32: value <= 0x10FFFF before the step
                value = value * (hex ? 16 : 10) + char32_t(digit);
                overflow = value > 0x10FFFF;
            }
        }
        if (k == digits) // "&#" or "&#x" without digits is text
            return 0;
        if (k < n && text[k] == u';')
            ++k;
        appendCodePoint(out, overflow ? 0xFFFD : numericReplacement(value));
        return k - at;
    }
    const qsizetype nameStart = k;
    while (k < n && k - nameStart < kMaxReferenceName && isAsciiAlnum(text[k]))
        ++k;
    if (k == nameStart)
        return 0;
    if (k < n && text[k] == u';') {
        if (const NamedReference *reference = findReference(text.sliced(nameStart, k - nameStart))) {
            out += QChar(reference->code);
            return k + 1 - at;
        }
    }
    // Without ';' only the legacy names decode, longest match first
    // ("&notit;" is "¬it;"), and in attributes not before [A-Za-z0-9=].
    for (qsizetype length = qMin(k - nameStart, kMaxLegacyName); length >= 2; --length) {
        const NamedReference *reference = findReference(text.sliced(nameStart, length));
        if (!reference || !reference->legacy)
            continue;
        const qsizetype after = nameStart + length;
        if (inAttribute && after < n && (isAsciiAlnum(text[after]) || text[after] == u'='))
            return 0;
        out += QChar(reference->code);
        return 1 + length;
    }
    return 0;
}

} // namespace

void appendDecodedHtml(QString &out, QStringView text, bool inAttribute)
{
    out.reserve(out.size() + text.size());
    const char16_t *data = text.utf16();
    const qsizetype n = text.size();
    qsizetype i = 0;
    while (i < n) {
        qsizetype j = i;
        while (j < n && data[j] != u'&' && data[j] != 0)
            ++j;
        out.append(text.sliced(i, j - i));
        if (j >= n)
            break;
        if (text[j].unicode() == 0) {
            out += QChar(QChar::ReplacementCharacter);
            i = j + 1;
            continue;
        }
        const qsizetype used = decodeReference(out, text, j, inAttribute);
        if (used > 0) {
            i = j + used;
        } else {
            out += u'&';
            i = j + 1;
        }
    }
}

QString decodeHtml(QStringView text, bool inAttribute)
{
    QString out;
    appendDecodedHtml(out, text, inAttribute);
    return out;
}

} // namespace QindaOffice::Mail
