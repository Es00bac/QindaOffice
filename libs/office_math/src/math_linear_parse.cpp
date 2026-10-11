// SPDX-License-Identifier: GPL-3.0-or-later
// UnicodeMath in (math_linear.h): the expression loop, operands, scripts,
// fractions, accents and brackets. Structures: math_linear_structs.cpp.
#include "math_linear.h"
#include "math_linear_p.h"
#include "math_symbols.h"

#include <QChar>

namespace QindaOffice::Math::detail {

namespace {

constexpr std::u32string_view kSeparators = U"│∣";

bool isFractionOp(char32_t c)
{
    return c == U'/' || c == U'¦' || c == U'⊘' || c == U'∕';
}

bool isSpace(char32_t c)
{
    return c == U' ' || c == U'\t' || c == U'\r' || c == U'\n';
}

// Anything that can begin or continue an operand span.
bool continuesSpan(char32_t c)
{
    return isAlphanumeric(c) || c == U'.' || isOpeningBracket(c) || c == U'|' || c == U'‖' || isNaryOperator(c)
        || isRadicalSign(c) || kArrays.find(c) != std::u32string_view::npos
        || kPrefixes.find(c) != std::u32string_view::npos || c == U'"' || c == U'\\' || c == U'_' || c == U'^'
        || isCombiningMark(c) || isPrime(c);
}

} // namespace

LinearParser::LinearParser(const QString &source)
{
    for (const uint u : applyAutoCorrect(source).toUcs4()) {
        m_s.append(char32_t(u));
    }
}

Argument LinearParser::parse()
{
    return expression({}, false).finish();
}

void LinearParser::skipSpaces()
{
    while (!atEnd() && isSpace(peek()) && peek() != U'\n') {
        ++m_i;
    }
}

// Up to a stop character (not consumed). A span stops at the first space
// or operator: it is the operand of a build-up operator.
AtomList LinearParser::expression(std::u32string_view stops, bool span)
{
    AtomList atoms;
    while (!atEnd()) {
        const char32_t c = peek();
        if (stops.find(c) != std::u32string_view::npos) {
            break;
        }
        if (isSpace(c)) {
            if (span) {
                break;
            }
            ++m_i;
            atoms.breakSpan();
            continue;
        }
        const bool closingBar = (c == U'|' || c == U'‖') && !atoms.spanIsEmpty();
        if ((closingBar && m_bars > 0) || (isClosingBracket(c) && c != U'┤' && m_depth > 0)
            || (c == U'┤' && (m_depth > 0 || m_bars > 0))) {
            break; // the enclosing bracket's
        }
        if (span && !continuesSpan(c)) {
            break;
        }
        if (closingBar || isClosingBracket(c)) {
            ++m_i;
            character(atoms, c); // unmatched: plain text
            continue;
        }
        step(atoms);
    }
    return atoms;
}

void LinearParser::step(AtomList &atoms)
{
    const char32_t c = peek();
    if (c == U'"') {
        literal(atoms);
    } else if (c == U'\\' && m_i + 1 < m_s.size()) {
        const char32_t quoted = peek(1);
        m_i += 2;
        atoms.addChar(quoted, RunStyle(), isAlphanumeric(quoted));
    } else if (isFractionOp(c)) {
        fraction(atoms);
    } else if (c == U'_' || c == U'^') {
        scripts(atoms);
    } else if (c == U'┬' || c == U'┴') {
        limit(atoms);
    } else if (c == kApply) {
        apply(atoms);
    } else if (isCombiningMark(c)) {
        accent(atoms);
    } else if (isPrime(c) && !atoms.spanIsEmpty()) {
        primes(atoms);
    } else if (isNaryOperator(c)) {
        atoms.addNode(nary());
    } else if (isRadicalSign(c)) {
        atoms.addNode(radical());
    } else if (kArrays.find(c) != std::u32string_view::npos) {
        atoms.addNode(array());
    } else if (kPrefixes.find(c) != std::u32string_view::npos) {
        atoms.addNode(prefixed());
    } else if (c == U'〖') {
        group(atoms);
    } else if (isOpeningBracket(c) || c == U'|' || c == U'‖') {
        bracket(atoms);
    } else if (!(QChar::isLetter(c) && !atoms.endsWithLetter() && function(atoms))) {
        ++m_i;
        character(atoms, c);
    }
}

void LinearParser::character(AtomList &atoms, char32_t c)
{
    char32_t plain = c;
    const RunStyle style = styleFor(c, &plain);
    atoms.addChar(plain, style, isAlphanumeric(c) || c == U'.');
}

void LinearParser::literal(AtomList &atoms)
{
    ++m_i;
    QString content;
    while (!atEnd() && peek() != U'"') {
        if (peek() == U'\\' && (peek(1) == U'"' || peek(1) == U'\\')) {
            ++m_i;
        }
        content += charText(peek());
        ++m_i;
    }
    if (!atEnd()) {
        ++m_i;
    }
    if (!content.isEmpty()) {
        atoms.addNode(styledRun(content, RunStyle{MathStyle::Default, MathScript::Roman, true}));
    }
}

// The operand after a build-up operator: a span; parentheses that only
// delimit it are removed when `strip`.
Argument LinearParser::operand(bool strip)
{
    skipSpaces();
    return toArgument(expression({}, true).atoms(), strip);
}

// A script's (or limit's) operand: one bracketed group or structure, a run
// of letters and digits (with a leading sign), or one other character.
Argument LinearParser::scriptOperand()
{
    AtomList atoms;
    const char32_t c = peek();
    if (atEnd() || isSpace(c)) {
        return {};
    }
    if (isAlphanumeric(c) || ((c == U'-' || c == U'−' || c == U'+' || c == U'±') && isAlphanumeric(peek(1)))) {
        character(atoms, c);
        ++m_i;
        while (!atEnd() && (isAlphanumeric(peek()) || (peek() == U'.' && isAlphanumeric(peek(1))))) {
            character(atoms, peek());
            ++m_i;
        }
        return atoms.finish();
    }
    if (continuesSpan(c) && c != U'_' && c != U'^' && !isCombiningMark(c) && !isPrime(c)) {
        step(atoms);
        return toArgument(atoms.atoms(), true);
    }
    if (!isClosingBracket(c) && !isFractionOp(c)) {
        ++m_i;
        character(atoms, c == U'\'' ? char32_t(0x2032) : c);
    }
    return atoms.finish();
}

void LinearParser::fraction(AtomList &atoms)
{
    const char32_t op = peek();
    ++m_i;
    const QList<Atom> numerator = atoms.takeSpan();
    NodePtr f = structure(NodeKind::Fraction, {toArgument(numerator, true), operand(true)});
    if (op == U'¦') {
        setFractionType(*f, FractionType::NoBar);
    } else if (op == U'⊘') {
        setFractionType(*f, FractionType::Linear);
    } else if (op == U'∕') {
        setFractionType(*f, FractionType::Skewed);
    }
    atoms.addNode(f);
}

void LinearParser::readScripts(Argument &sub, Argument &sup, bool &hasSub, bool &hasSup)
{
    while (peek() == U'_' || peek() == U'^') {
        const bool isSub = peek() == U'_';
        if (isSub ? hasSub : hasSup) {
            break;
        }
        ++m_i;
        (isSub ? sub : sup) = scriptOperand();
        (isSub ? hasSub : hasSup) = true;
    }
}

void LinearParser::scripts(AtomList &atoms)
{
    Argument sub;
    Argument sup;
    bool hasSub = false;
    bool hasSup = false;
    if (atoms.spanIsEmpty()) { // _a^b C: prescripts on the operand after them
        readScripts(sub, sup, hasSub, hasSup);
        atoms.addNode(structure(NodeKind::PreScript, {sub, sup, operand(true)}));
        return;
    }
    const std::optional<Atom> base = atoms.takeLast();
    readScripts(sub, sup, hasSub, hasSup);
    const Argument e = atomArgument(*base, false);
    if (hasSub && hasSup) {
        atoms.addNode(structure(NodeKind::SubSup, {e, sub, sup}));
    } else if (hasSub) {
        atoms.addNode(structure(NodeKind::Subscript, {e, sub}));
    } else {
        atoms.addNode(structure(NodeKind::Superscript, {e, sup}));
    }
}

// base┬limit, base┴limit: the whole span is the base (lim┬(n→∞)).
void LinearParser::limit(AtomList &atoms)
{
    const bool below = peek() == U'┬';
    ++m_i;
    Argument base = toArgument(atoms.takeSpan(), false);
    if (base.items.size() == 1 && base.items.first()->kind == NodeKind::Run
        && isFunctionName(base.items.first()->text())) {
        markFunctionName(base);
    }
    atoms.addNode(structure(below ? NodeKind::LimLow : NodeKind::LimUpp, {base, scriptOperand()}));
}

// name\u2061argument: the span before U+2061 is the function's name.
void LinearParser::apply(AtomList &atoms)
{
    ++m_i;
    Argument name = toArgument(atoms.takeSpan(), false);
    markFunctionName(name);
    atoms.addNode(structure(NodeKind::Function, {name, operand(false)}));
}

// A combining mark accents the atom before it ((a+b)\u0302 loses the brackets).
void LinearParser::accent(AtomList &atoms)
{
    const char32_t mark = peek();
    ++m_i;
    const std::optional<Atom> base = atoms.spanIsEmpty() ? std::nullopt : atoms.takeLast();
    NodePtr acc = structure(NodeKind::Accent, {base ? atomArgument(*base, true) : Argument()});
    if (mark != 0x0302) {
        setPropValue(*acc, u"chr", charText(mark));
    }
    atoms.addNode(acc);
}

void LinearParser::primes(AtomList &atoms)
{
    QString marks;
    while (!atEnd() && isPrime(peek())) {
        marks += peek() == U'\'' ? QStringLiteral("′") : charText(peek());
        ++m_i;
    }
    const std::optional<Atom> base = atoms.takeLast();
    atoms.addNode(structure(NodeKind::Superscript, {atomArgument(*base, false), singleItem(makeRun(marks))}));
}

void LinearParser::group(AtomList &atoms)
{
    ++m_i;
    ++m_depth;
    const Argument inner = expression({}, false).finish();
    if (peek() == U'〗') {
        ++m_i;
    }
    --m_depth;
    atoms.addGroup(inner);
}

// Any opening bracket matches any closing one ([0,1) is an interval);
// ├ and ┤ are invisible sides; │ or ∣ separate elements.
void LinearParser::bracket(AtomList &atoms)
{
    const char32_t open = peek();
    ++m_i;
    const bool bar = open == U'|' || open == U'‖';
    (bar ? m_bars : m_depth) += 1;
    QList<Argument> elements;
    char32_t separator = 0;
    char32_t close = 0;
    for (;;) {
        elements.append(expression(kSeparators, false).finish());
        const char32_t c = peek();
        if (!atEnd() && kSeparators.find(c) != std::u32string_view::npos) {
            separator = c;
            ++m_i;
            continue;
        }
        if (!atEnd() && (isClosingBracket(c) || (bar && (c == U'|' || c == U'‖')))) {
            close = c;
            ++m_i;
        }
        break;
    }
    (bar ? m_bars : m_depth) -= 1;
    NodePtr d = makeNode(NodeKind::Delimiter, int(elements.size()));
    d->args = elements;
    const QString begin = open == U'├' ? QString() : charText(open);
    const QString end = close == 0 || close == U'┤' ? QString() : charText(close);
    if (begin != u"(") {
        setPropValue(*d, u"begChr", begin);
    }
    if (separator == U'∣') {
        setPropValue(*d, u"sepChr", QStringLiteral("|"));
    }
    if (end != u")") {
        setPropValue(*d, u"endChr", end);
    }
    atoms.addNode(d, open == U'(' && close == U')' && elements.size() == 1);
}

} // namespace QindaOffice::Math::detail

namespace QindaOffice::Math {

std::optional<Equation> fromLinear(const QString &text, bool display, QString *error)
{
    Equation eq;
    eq.display = display;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        detail::LinearParser parser(line);
        eq.lines.append(parser.parse());
    }
    if (error != nullptr) {
        error->clear();
    }
    return eq;
}

Argument parseLinear(const QString &text)
{
    detail::LinearParser parser(text);
    return parser.parse();
}

} // namespace QindaOffice::Math
