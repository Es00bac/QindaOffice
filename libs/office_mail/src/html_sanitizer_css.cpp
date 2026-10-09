// SPDX-License-Identifier: GPL-3.0-or-later
// The CSS filter of the HTML mail sanitizer (html_sanitizer_p.h): style=""
// attributes and <style> sheets.
//
// AGENT-NOTE: what survives is plain declarations ("name: value") and, in
// sheets, plain rules ("selector { declarations }"). Dropped:
//  * every at-rule — @import and @font-face load remote resources, and
//    @media with a query or @page makes Qt 6.11's CSS parser abandon the
//    rest of the sheet (mail's @media blocks are mobile overrides anyway);
//  * declarations that could load something (url(), image(), image-set(),
//    src(), cross-fade(), element()), run something (expression(),
//    javascript:, behavior, -moz-binding) or hide an escape: any backslash
//    ("\75 rl(" is "url("); Qt's internal -qt-* properties;
//  * position: fixed | absolute | sticky (overlays over the real content);
//  * numbers above 10000 or in exponent form: a font-size or width that
//    large is a memory attack on Qt's layout;
//  * rules whose selector has characters outside a plain set (no "(",
//    so :not()/:nth-child() hacks cannot trip Qt's parser either).
// AGENT-GUARD: the emitted text is checked once more as a whole
// (dangerous()), so no tokenisation difference between this filter and Qt's
// QCss parser can smuggle "url(" or "@import" through: the substrings are
// simply not in the output.

#include "html_sanitizer_p.h"

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail::Detail {

namespace {

bool isCssSpace(QChar c)
{
    const char16_t u = c.unicode();
    return u == u' ' || u == u'\t' || u == u'\n' || u == u'\r' || u == u'\f';
}

bool isAsciiLetter(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
}

bool isAsciiDigit(QChar c)
{
    return c.unicode() >= u'0' && c.unicode() <= u'9';
}

bool isNameCharacter(QChar c)
{
    return isAsciiLetter(c) || isAsciiDigit(c) || c == u'-' || c == u'_';
}

// Lower case without any whitespace: the form every check looks at, so
// "URL (" and "u r l(" cannot hide from it.
QString squashed(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (!c.isSpace())
            out += c.toLower();
    }
    return out;
}

bool dangerous(const QString &squashedText)
{
    static constexpr QLatin1StringView kCalls[] = {"url("_L1, "image("_L1, "image-set("_L1, "cross-fade("_L1,
                                                   "element("_L1, "src("_L1, "expression("_L1};
    static constexpr QLatin1StringView kSchemes[] = {"javascript:"_L1, "vbscript:"_L1, "livescript:"_L1,
                                                     "behavior:"_L1};
    if (squashedText.contains(u'\\') || squashedText.contains(u'<') || squashedText.contains("-moz-binding"_L1)
        || squashedText.contains("@import"_L1))
        return true;
    // Cheap pre-checks: every other needle has a '(' or a ':'.
    if (squashedText.contains(u'(')) {
        for (const QLatin1StringView needle : kCalls) {
            if (squashedText.contains(needle))
                return true;
        }
    }
    if (squashedText.contains(u':')) {
        for (const QLatin1StringView needle : kSchemes) {
            if (squashedText.contains(needle))
                return true;
        }
    }
    return false;
}

bool isRemoteReference(const QString &squashedText)
{
    return squashedText.contains("http:"_L1) || squashedText.contains("https:"_L1)
           || squashedText.contains("(//"_L1) || squashedText.contains("\"//"_L1)
           || squashedText.contains("'//"_L1);
}

// A number above 10000 or in exponent form, outside identifiers ("h1") and
// #hex colours.
bool hasHugeNumber(QStringView value)
{
    const qsizetype n = value.size();
    for (qsizetype i = 0; i < n;) {
        const QChar c = value[i];
        const bool identifierStart = isAsciiLetter(c) || c == u'_'
                                     || (c == u'-' && i + 1 < n && !isAsciiDigit(value[i + 1]) && value[i + 1] != u'.');
        if (c == u'#' || identifierStart) {
            ++i;
            while (i < n && isNameCharacter(value[i]))
                ++i;
            continue;
        }
        if (!isAsciiDigit(c)) {
            ++i;
            continue;
        }
        qint64 number = 0;
        while (i < n && isAsciiDigit(value[i])) {
            number = qMin(number * 10 + qint64(value[i].unicode() - u'0'), qint64(1000000));
            ++i;
        }
        if (number > 10000)
            return true;
        if (i + 1 < n && (value[i] == u'e' || value[i] == u'E')
            && (isAsciiDigit(value[i + 1]) || value[i + 1] == u'+' || value[i + 1] == u'-'))
            return true;
    }
    return false;
}

// Removes /* comments */ (an unterminated one runs to the end) and the
// "<!--" "-->" wrappers mail clients put around sheets; strings stay as they
// are. Each removal leaves a space, as a comment separates tokens in CSS.
QString stripComments(QStringView css)
{
    QString out;
    out.reserve(css.size());
    const qsizetype n = css.size();
    QChar quote;
    for (qsizetype i = 0; i < n;) {
        const QChar c = css[i++];
        if (!quote.isNull()) {
            out += c;
            if (c == u'\\' && i < n)
                out += css[i++];
            else if (c == quote || c == u'\n')
                quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'/' && i < n && css[i] == u'*') {
            const qsizetype end = css.indexOf("*/"_L1, i + 1);
            i = end < 0 ? n : end + 2;
            out += u' ';
            continue;
        } else if (c == u'<' && css.sliced(i - 1).startsWith("<!--"_L1)) {
            i += 3;
            out += u' ';
            continue;
        } else if (c == u'-' && css.sliced(i - 1).startsWith("-->"_L1)) {
            i += 2;
            out += u' ';
            continue;
        }
        out += c;
    }
    return out;
}

// Scans css[from…] outside strings: `wanted` '{' finds the next block start;
// '}' (from inside a block) the end of that block, nested blocks skipped;
// `atRule` finds the ';' or '}' that ends an at-rule. -1 at the end.
qsizetype scan(QStringView css, qsizetype from, QChar wanted, bool atRule)
{
    const qsizetype n = css.size();
    int depth = wanted == u'}' ? 1 : 0;
    QChar quote;
    for (qsizetype i = from; i < n; ++i) {
        const QChar c = css[i];
        if (!quote.isNull()) {
            if (c == u'\\')
                ++i;
            else if (c == quote || c == u'\n')
                quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'{') {
            if (wanted == u'{' && !atRule)
                return i;
            ++depth;
        } else if (c == u'}') {
            if (--depth <= 0 && (atRule || wanted == u'}'))
                return i;
        } else if (c == u';' && atRule && depth == 0) {
            return i;
        }
    }
    return -1;
}

template<typename Take>
void forEachDeclaration(QStringView block, Take take)
{
    const qsizetype n = block.size();
    qsizetype start = 0;
    QChar quote;
    int parens = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = block[i];
        if (!quote.isNull()) {
            if (c == u'\\')
                ++i;
            else if (c == quote || c == u'\n')
                quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'(') {
            ++parens;
        } else if (c == u')') {
            parens = qMax(0, parens - 1);
        } else if (c == u';' && parens == 0) {
            take(block.sliced(start, i - start));
            start = i + 1;
        }
    }
    if (start < n)
        take(block.sliced(start));
}

struct Declaration {
    QString name;      // lower case
    QStringView value; // trimmed, without "!important"
    bool important = false;
};

std::optional<Declaration> parseDeclaration(QStringView text)
{
    const qsizetype colon = text.indexOf(u':');
    if (colon < 0)
        return std::nullopt;
    Declaration declaration;
    declaration.name = text.first(colon).trimmed().toString().toLower();
    const QStringView name(declaration.name);
    const qsizetype first = name.startsWith(u'-') ? 1 : 0;
    if (name.size() <= first || !(isAsciiLetter(name[first]) || name[first] == u'_'))
        return std::nullopt;
    for (const QChar c : name) {
        if (!isNameCharacter(c))
            return std::nullopt;
    }
    QStringView value = text.sliced(colon + 1).trimmed();
    const qsizetype bang = value.lastIndexOf(u'!');
    if (bang >= 0 && value.sliced(bang + 1).trimmed().compare("important"_L1, Qt::CaseInsensitive) == 0) {
        declaration.important = true;
        value = value.first(bang).trimmed();
    }
    if (value.isEmpty())
        return std::nullopt;
    declaration.value = value;
    return declaration;
}

// Appends the declaration to `out` when it is safe.
void keepDeclaration(const Declaration &d, CssReport &report, QString &out)
{
    // Qt's own -qt-* properties (texture cache keys, list and table
    // internals) have no business in mail.
    if (d.name == "behavior"_L1 || d.name == "-ms-behavior"_L1 || d.name == "-moz-binding"_L1
        || d.name == "binding"_L1 || d.name.startsWith("-qt-"_L1) || d.name.startsWith("qproperty-"_L1))
        return;
    const QString value = squashed(d.value);
    if (dangerous(value) || value.contains(u'{') || value.contains(u'}') || value.contains(u'>')) {
        if (!report.allowRemote && isRemoteReference(value))
            report.remoteBlocked = true;
        return;
    }
    if (value.count(u'"') % 2 != 0 || value.count(u'\'') % 2 != 0) // unterminated string
        return;
    if (d.name == "position"_L1
        && (value.startsWith("fixed"_L1) || value.startsWith("absolute"_L1) || value.startsWith("sticky"_L1)
            || value.startsWith("-webkit-sticky"_L1)))
        return;
    if (hasHugeNumber(d.value))
        return;
    if (!out.isEmpty())
        out += "; "_L1;
    out += d.name;
    out += ": "_L1;
    out += d.value;
    if (d.important)
        out += " !important"_L1;
}

// The safe declarations of `block`; `hides` (optional) tells whether they
// hide the element: display:none or mso-hide:all, where a later declaration
// wins unless an earlier one was !important — as in the cascade.
QString filterDeclarations(QStringView block, CssReport &report, bool *hides)
{
    QString out;
    QString display;
    QString msoHide;
    bool displayImportant = false;
    bool msoImportant = false;
    forEachDeclaration(block, [&](QStringView text) {
        const std::optional<Declaration> d = parseDeclaration(text);
        if (!d)
            return;
        if (d->name == "display"_L1 && (d->important || !displayImportant)) {
            display = d->value.toString().toLower();
            displayImportant = d->important;
        } else if (d->name == "mso-hide"_L1 && (d->important || !msoImportant)) {
            msoHide = d->value.toString().toLower();
            msoImportant = d->important;
        }
        keepDeclaration(*d, report, out);
    });
    if (hides)
        *hides = display == "none"_L1 || msoHide == "all"_L1;
    return out;
}

bool safeSelector(QStringView selector)
{
    if (selector.isEmpty() || selector.size() > 1024)
        return false;
    static constexpr QStringView kPunctuation = u" \t\n\r\f.#,>+~*:-_[]=\"'^$|";
    for (const QChar c : selector) {
        if (!isAsciiLetter(c) && !isAsciiDigit(c) && !kPunctuation.contains(c))
            return false;
    }
    return !dangerous(squashed(selector));
}

// ".name", "tag.name", "#name", "tag#name" of a hiding rule, for
// Sanitizer::hides(): Qt ignores display:none, so the sanitizer removes
// such elements itself (hidden preheaders, mobile-only blocks).
void collectHidden(QStringView selector, QSet<QString> &hidden)
{
    for (QStringView part : selector.split(u',')) {
        part = part.trimmed();
        qsizetype i = 0;
        while (i < part.size() && (isAsciiLetter(part[i]) || isAsciiDigit(part[i])))
            ++i;
        if (i >= part.size() || (part[i] != u'.' && part[i] != u'#'))
            continue;
        const QStringView name = part.sliced(i + 1);
        bool plain = !name.isEmpty();
        for (const QChar c : name)
            plain = plain && isNameCharacter(c);
        if (plain)
            hidden.insert(part.first(i).toString().toLower() + part[i] + name.toString());
    }
}

} // namespace

QString filterInlineStyle(QStringView css, CssReport &report)
{
    const QString out = filterDeclarations(stripComments(css), report, nullptr);
    return dangerous(squashed(out)) ? QString() : out; // see the AGENT-GUARD above
}

bool inlineStyleHides(QStringView css)
{
    // Only a literal "display" or "mso-hide" can hide: escapes and comments
    // inside the name make the declaration invalid for this parser too.
    if (!css.contains("display"_L1, Qt::CaseInsensitive) && !css.contains("mso-hide"_L1, Qt::CaseInsensitive))
        return false;
    CssReport scratch;
    bool hides = false;
    filterDeclarations(stripComments(css), scratch, &hides);
    return hides;
}

int inlineStylePixels(QStringView css, QLatin1StringView property)
{
    int pixels = -1;
    forEachDeclaration(stripComments(css), [&](QStringView text) {
        const std::optional<Declaration> d = parseDeclaration(text);
        if (d && d->name == property)
            pixels = leadingInteger(d->value);
    });
    return pixels;
}

QString filterStyleSheet(QStringView css, CssReport &report, QSet<QString> &hidden)
{
    const QString source = stripComments(css);
    const QStringView s(source);
    const qsizetype n = s.size();
    QString out;
    for (qsizetype i = 0; i < n;) {
        while (i < n && isCssSpace(s[i]))
            ++i;
        if (i >= n)
            break;
        if (s[i] == u'@') {
            const qsizetype end = scan(s, i, u';', true);
            const qsizetype next = end < 0 ? n : end + 1;
            const QString rule = squashed(s.sliced(i, next - i));
            if (!report.allowRemote && rule.startsWith("@import"_L1) && isRemoteReference(rule))
                report.remoteBlocked = true;
            i = next;
            continue;
        }
        const qsizetype open = scan(s, i, u'{', false);
        if (open < 0)
            break; // a trailing selector without a block
        const qsizetype close = scan(s, open + 1, u'}', false);
        const qsizetype blockEnd = close < 0 ? n : close;
        const QStringView selector = s.sliced(i, open - i).trimmed();
        const QStringView block = s.sliced(open + 1, blockEnd - open - 1);
        i = close < 0 ? n : close + 1;
        if (!safeSelector(selector))
            continue;
        bool hides = false;
        const QString declarations = filterDeclarations(block, report, &hides);
        if (declarations.isEmpty())
            continue;
        if (hides)
            collectHidden(selector, hidden);
        out += selector;
        out += " { "_L1;
        out += declarations;
        out += " }\n"_L1;
    }
    return dangerous(squashed(out)) ? QString() : out;
}

} // namespace QindaOffice::Mail::Detail
