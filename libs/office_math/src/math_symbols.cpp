// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_symbols.h"

#include <QChar>
#include <QStringList>

#include <string_view>

namespace QindaOffice::Math {

namespace {

bool inList(char32_t c, std::u32string_view list)
{
    return list.find(c) != std::u32string_view::npos;
}

// Relations from Mathematical Operators and its supplements; every arrow
// block is a relation too (rangeIsArrow).
constexpr std::u32string_view kRelations =
    U"=<>:≤≥≠≈≡≢∼≃≅≇≆∝≪≫≺≻≼≽⊂⊃⊆⊇⊄⊅⊊⊋∈∉∋∌∥∦∣∤⊢⊣⊨⊩⊪⊫⊬⊭≐≑≒≓≔≕≖≗≘≙≚≛≜≝≞≟∶∷≍≎≏⋈⊲⊳⊴⊵≲≳≶≷≮≯≰≱≺≻⊏⊐⊑⊒"
    U"⋐⋑⋖⋗⋘⋙⋚⋛⋜⋝⋞⋟⋠⋡⋢⋣⋤⋥⋦⋧⋨⋩⋪⋫⋬⋭⩽⩾⪅⪆⪇⪈⪉⪊⪋⪌⪕⪖⪯⪰⪱⪲⪳⪴⪵⪶⪷⪸⪹⪺⫅⫆⫋⫌⊥∽≀⊸⊷⊶⧟⟂";
constexpr std::u32string_view kBinaries =
    U"+-−±∓×÷·⋅∙∗*∘∩∪⊎⊓⊔∧∨⊕⊖⊗⊘⊙⊚⊛⊝⊞⊟⊠⊡∖⋆⋄⨯⨿†‡⋒⋓⊼⊽⊻⋉⋊⋋⋌∔∸∺⊹⊺⋇⋎⋏⨁⨂⨀";
constexpr std::u32string_view kOpening = U"([{⟨〈⌊⌈⟦⟪⦃⦅⦇⦉⦋⦍⦏⦑⦓⦕⦗⟮⟬⌜⌞❲〖├";
constexpr std::u32string_view kClosing = U")]}⟩〉⌋⌉⟧⟫⦄⦆⦈⦊⦌⦎⦐⦒⦔⦖⦘⟯⟭⌝⌟❳〗┤";
constexpr std::u32string_view kNary = U"∑∏∐∫∬∭⨌∮∯∰∱∲∳⨍⨎⨏⨐⨑⨒⨓⨔⨕⨖⨗⨘⨙⨚⨛⨜⋀⋁⋂⋃⨀⨁⨂⨃⨄⨅⨆⨉⫿⅀";
constexpr std::u32string_view kIntegrals = U"∫∬∭⨌∮∯∰∱∲∳⨍⨎⨏⨐⨑⨒⨓⨔⨕⨖⨗⨘⨙⨚⨛⨜";

bool rangeIsArrow(char32_t c)
{
    return (c >= 0x2190 && c <= 0x21FF) || (c >= 0x27F0 && c <= 0x27FF) || (c >= 0x2900 && c <= 0x297F)
        || (c >= 0x2B04 && c <= 0x2B0D) || (c >= 0x2B30 && c <= 0x2B4F);
}

// AGENT-NOTE: Word's function list (the "Function" gallery and the names
// its linear format turns into m:func without help); limit-like names put
// their subscript below (m:limLow).
const QStringList &functionNames()
{
    static const QStringList names{
        QStringLiteral("arcsin"), QStringLiteral("arccos"), QStringLiteral("arctan"), QStringLiteral("arccot"),
        QStringLiteral("arcsec"), QStringLiteral("arccsc"), QStringLiteral("arsinh"), QStringLiteral("arcosh"),
        QStringLiteral("artanh"), QStringLiteral("arcoth"), QStringLiteral("sinh"),   QStringLiteral("cosh"),
        QStringLiteral("tanh"),   QStringLiteral("coth"),   QStringLiteral("sech"),   QStringLiteral("csch"),
        QStringLiteral("sin"),    QStringLiteral("cos"),    QStringLiteral("tan"),    QStringLiteral("cot"),
        QStringLiteral("sec"),    QStringLiteral("csc"),    QStringLiteral("log"),    QStringLiteral("ln"),
        QStringLiteral("lg"),     QStringLiteral("exp"),    QStringLiteral("liminf"), QStringLiteral("limsup"),
        QStringLiteral("lim"),    QStringLiteral("min"),    QStringLiteral("max"),    QStringLiteral("sup"),
        QStringLiteral("inf"),    QStringLiteral("det"),    QStringLiteral("dim"),    QStringLiteral("gcd"),
        QStringLiteral("lcm"),    QStringLiteral("arg"),    QStringLiteral("deg"),    QStringLiteral("hom"),
        QStringLiteral("ker"),    QStringLiteral("Pr"),     QStringLiteral("mod"),
    };
    return names;
}

} // namespace

AtomClass atomClassOf(char32_t c)
{
    if (inList(c, kNary)) {
        return AtomClass::Op;
    }
    if (inList(c, kRelations) || rangeIsArrow(c)) {
        return AtomClass::Rel;
    }
    if (inList(c, kBinaries)) {
        return AtomClass::Bin;
    }
    if (inList(c, kOpening) && c != U'├') {
        return AtomClass::Open;
    }
    if (inList(c, kClosing) || c == U'!' || c == U'?') {
        return c == U'┤' ? AtomClass::Ord : AtomClass::Close;
    }
    if (c == U',' || c == U';') {
        return AtomClass::Punct;
    }
    return AtomClass::Ord;
}

bool isNaryOperator(char32_t c)
{
    return inList(c, kNary);
}

bool isIntegral(char32_t c)
{
    return inList(c, kIntegrals);
}

bool isOpeningBracket(char32_t c)
{
    return inList(c, kOpening);
}

bool isClosingBracket(char32_t c)
{
    return inList(c, kClosing);
}

bool isCombiningMark(char32_t c)
{
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x20D0 && c <= 0x20FF);
}

bool isPrime(char32_t c)
{
    return c == 0x2032 || c == 0x2033 || c == 0x2034 || c == 0x2057 || c == U'\'';
}

bool isAlphanumeric(char32_t c)
{
    if (c < 0x80) {
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9');
    }
    if (c >= 0x1D400 && c <= 0x1D7FF) {
        return true; // Mathematical Alphanumeric Symbols
    }
    if (c == 0x221E || c == 0x2202 || c == 0x2207 || c == 0x210F || c == 0x2113 || c == 0x2118
        || c == 0x2205) {
        return true; // ∞ ∂ ∇ ℏ ℓ ℘ ∅ behave as operands
    }
    const QChar::Category cat = QChar::category(char32_t(c));
    return cat == QChar::Letter_Uppercase || cat == QChar::Letter_Lowercase || cat == QChar::Letter_Titlecase
        || cat == QChar::Letter_Other || cat == QChar::Letter_Modifier || cat == QChar::Number_DecimalDigit
        || cat == QChar::Number_Letter || cat == QChar::Number_Other;
}

char32_t displayChar(char32_t c)
{
    switch (c) {
    case U'-':
        return 0x2212; // minus sign
    case U'\'':
        return 0x2032; // prime
    case U'*':
        return 0x2217; // asterisk operator
    default:
        return c;
    }
}

bool isFunctionName(QStringView name)
{
    return functionNames().contains(name);
}

bool isLimitFunction(QStringView name)
{
    return name == u"lim" || name == u"liminf" || name == u"limsup" || name == u"min" || name == u"max"
        || name == u"sup" || name == u"inf" || name == u"det" || name == u"gcd" || name == u"Pr";
}

qsizetype functionNameAt(QStringView text)
{
    qsizetype best = 0;
    for (const QString &name : functionNames()) {
        if (name.size() > best && text.startsWith(name)) {
            const qsizetype end = name.size();
            const bool wordEnds = end >= text.size() || !text.at(end).isLetter();
            if (wordEnds) {
                best = name.size();
            }
        }
    }
    return best;
}

QString applyAutoCorrect(const QString &text)
{
    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    bool quoted = false;
    while (i < text.size()) {
        if (text.at(i) == u'\\' && i + 1 < text.size()
            && (text.at(i + 1) == u'\\' || text.at(i + 1) == u'"')) {
            out += text.mid(i, 2);
            i += 2;
            continue;
        }
        if (text.at(i) == u'"') {
            quoted = !quoted;
        }
        if (quoted) {
            out += text.at(i++);
            continue;
        }
        if (text.at(i) != u'\\') {
            out += text.at(i++);
            continue;
        }
        qsizetype j = i + 1;
        while (j < text.size() && text.at(j).isLetter() && text.at(j).unicode() < 0x80) {
            ++j;
        }
        // The longest keyword that is a prefix of the letters (Word converts
        // "\alphax" only when typed with a space; the letters must match whole).
        const QString word = text.mid(i + 1, j - i - 1);
        const QString replacement = wordAutoCorrect(word);
        if (word.isEmpty() || replacement.isEmpty()) {
            out += text.at(i++);
            continue;
        }
        out += replacement;
        i = j;
        if (i < text.size() && text.at(i) == u' ') {
            ++i;
        }
    }
    return out;
}

} // namespace QindaOffice::Math
