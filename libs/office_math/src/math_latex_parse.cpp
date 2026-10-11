// SPDX-License-Identifier: GPL-3.0-or-later
// LaTeX in (math_latex.h): tokens, groups, scripts, brackets and
// \left…\right. Commands: math_latex_commands.cpp.
#include "math_latex.h"
#include "math_latex_p.h"
#include "math_symbols.h"

#include <QChar>

namespace QindaOffice::Math::detail {

LatexParser::LatexParser(const QString &source)
{
    for (const uint u : source.toUcs4()) {
        m_s.append(char32_t(u));
    }
}

Argument LatexParser::parse()
{
    return expression(0).finish();
}

void LatexParser::skipSpaces()
{
    while (!atEnd() && QChar::isSpace(peek())) {
        ++m_i;
    }
}

QString LatexParser::peekCommand() const
{
    if (peek() != U'\\') {
        return {};
    }
    qsizetype j = m_i + 1;
    if (j < m_s.size() && !QChar::isLetter(m_s.at(j))) {
        return charText(m_s.at(j));
    }
    QString name;
    while (j < m_s.size() && QChar::isLetter(m_s.at(j)) && m_s.at(j) < 0x80) {
        name += charText(m_s.at(j++));
    }
    return name;
}

QString LatexParser::readCommand()
{
    const QString name = peekCommand();
    m_i += 1 + name.size();
    return name;
}

QString LatexParser::rawGroup()
{
    skipSpaces();
    if (peek() != U'{') {
        return {};
    }
    ++m_i;
    QString out;
    int depth = 1;
    while (!atEnd()) {
        const char32_t c = m_s.at(m_i++);
        if (c == U'{') {
            ++depth;
        } else if (c == U'}' && --depth == 0) {
            break;
        }
        out += charText(c);
    }
    return out;
}

AtomList LatexParser::expression(int stops)
{
    AtomList atoms;
    for (;;) {
        skipSpaces();
        if (atEnd() || !element(atoms, stops)) {
            break;
        }
    }
    return atoms;
}

Argument LatexParser::argument()
{
    skipSpaces();
    if (peek() == U'{') {
        ++m_i;
        const Argument inner = expression(Brace).finish();
        if (peek() == U'}') {
            ++m_i;
        }
        return inner;
    }
    AtomList atoms;
    if (!atEnd()) {
        element(atoms, 0);
    }
    return atoms.finish();
}

bool LatexParser::closes(char32_t c, const AtomList &atoms, int stops) const
{
    if (!(stops & Bracket)) {
        return false;
    }
    return (isClosingBracket(c) && m_depth > 0) || ((c == U'|' || c == U'‖') && m_bars > 0 && !atoms.spanIsEmpty());
}

void LatexParser::addChar(AtomList &atoms, char32_t c)
{
    char32_t plain = c;
    RunStyle style = styleFor(c, &plain);
    if (style == RunStyle()) {
        style = m_style;
    }
    atoms.addChar(plain, style, isAlphanumeric(c) || c == U'.');
}

bool LatexParser::element(AtomList &atoms, int stops)
{
    const char32_t c = peek();
    if (c == U'}' || c == U'&' || c == U']') {
        if ((c == U'}' && (stops & Brace)) || (c == U'&' && (stops & Amp)) || (c == U']' && (stops & Square))
            || (c == U']' && m_depth > 0 && (stops & Bracket))) {
            return false;
        }
        ++m_i;
        atoms.addChar(c, RunStyle(), false);
        return true;
    }
    if (c == U'\\') {
        const QString name = peekCommand();
        const char32_t symbol = name.size() == 1 ? onlyChar(name) : onlyChar(latexSymbol(name));
        if ((name == u"\\" && (stops & Row)) || ((name == u"right" || name == u"middle") && (stops & Right))
            || (name == u"end" && (stops & End)) || closes(symbol, atoms, stops)) {
            return false;
        }
        readCommand();
        command(atoms, name, stops);
        return true;
    }
    if (closes(c, atoms, stops)) {
        return false;
    }
    if (c == U'^' || c == U'_') {
        scripts(atoms);
        return true;
    }
    ++m_i;
    if (c == U'{') {
        const Argument inner = expression(Brace).finish();
        if (peek() == U'}') {
            ++m_i;
        }
        atoms.addGroup(inner);
    } else if (c == U'\'') {
        QString marks = QStringLiteral("′");
        while (peek() == U'\'') {
            marks += QStringLiteral("′");
            ++m_i;
        }
        const std::optional<Atom> base = atoms.takeLast();
        atoms.addNode(structure(NodeKind::Superscript,
                                {base ? atomArgument(*base, false) : Argument(), singleItem(makeRun(marks))}));
    } else if (c == U'~') {
        atoms.addChar(0x00A0, RunStyle(), false);
    } else if ((isOpeningBracket(c) && c != U'├' && c != U'〖') || c == U'|' || c == U'‖') {
        bracket(atoms, c, stops);
    } else {
        addChar(atoms, c);
    }
    return true;
}

// x^{2}, x_{i}, x_{i}^{2}; LaTeX scripts the LAST atom. {}_{a}^{b}X puts
// prescripts on what follows.
void LatexParser::scripts(AtomList &atoms)
{
    const std::optional<Atom> base = atoms.takeLast();
    Argument sub;
    Argument sup;
    bool hasSub = false;
    bool hasSup = false;
    while (peek() == U'_' || peek() == U'^') {
        const bool isSub = peek() == U'_';
        if (isSub ? hasSub : hasSup) {
            break;
        }
        ++m_i;
        (isSub ? sub : sup) = argument();
        (isSub ? hasSub : hasSup) = true;
    }
    if (!base || (base->group && base->inner.isEmpty())) {
        atoms.addNode(structure(NodeKind::PreScript, {sub, sup, argument()}));
        return;
    }
    const Argument e = atomArgument(*base, false);
    if (hasSub && hasSup) {
        atoms.addNode(structure(NodeKind::SubSup, {e, sub, sup}));
    } else {
        atoms.addNode(structure(hasSub ? NodeKind::Subscript : NodeKind::Superscript, {e, hasSub ? sub : sup}));
    }
}

// Plain brackets build up into growing delimiters, as in Word's LaTeX mode.
void LatexParser::bracket(AtomList &atoms, char32_t open, int stops)
{
    const bool bar = open == U'|' || open == U'‖';
    (bar ? m_bars : m_depth) += 1;
    const Argument inner = expression((stops & ~(Square | Brace)) | Bracket).finish();
    char32_t close = 0;
    if (peekCommand() == u"}") {
        m_i += 2;
        close = U'}';
    } else if (peek() == U'\\' && isClosingBracket(onlyChar(latexSymbol(peekCommand())))) {
        close = onlyChar(latexSymbol(readCommand()));
    } else if (!atEnd() && (isClosingBracket(peek()) || peek() == U'|' || peek() == U'‖' || peek() == U']')) {
        close = m_s.at(m_i++);
    }
    (bar ? m_bars : m_depth) -= 1;
    NodePtr d = structure(NodeKind::Delimiter, {inner});
    if (open != U'(') {
        setPropValue(*d, u"begChr", charText(open));
    }
    if (close != U')') {
        setPropValue(*d, u"endChr", close == 0 ? QString() : charText(close));
    }
    atoms.addNode(d);
}

QString LatexParser::delimiterToken()
{
    skipSpaces();
    if (peek() == U'\\') {
        const QString name = readCommand();
        if (name == u"{" || name == u"}") {
            return name;
        }
        return name == u"|" ? QStringLiteral("‖") : latexSymbol(name);
    }
    const char32_t c = peek();
    ++m_i;
    return c == U'.' ? QString() : charText(c);
}

// \left( … \middle| … \right): one element per \middle.
NodePtr LatexParser::leftRight()
{
    const QString open = delimiterToken();
    QList<Argument> elements;
    QString separator;
    QString close;
    const int depth = m_depth;
    const int bars = m_bars;
    m_depth = 0;
    m_bars = 0;
    for (;;) {
        elements.append(expression(Right).finish());
        const QString name = peekCommand();
        if (name == u"middle") {
            readCommand();
            separator = delimiterToken();
            continue;
        }
        if (name == u"right") {
            readCommand();
            close = delimiterToken();
        }
        break;
    }
    m_depth = depth;
    m_bars = bars;
    NodePtr d = makeNode(NodeKind::Delimiter, int(elements.size()));
    d->args = elements;
    if (open != u"(") {
        setPropValue(*d, u"begChr", open);
    }
    if (!separator.isEmpty() && separator != u"│") {
        setPropValue(*d, u"sepChr", separator);
    }
    if (close != u")") {
        setPropValue(*d, u"endChr", close);
    }
    return d;
}

} // namespace QindaOffice::Math::detail

namespace QindaOffice::Math {

std::optional<Equation> fromLatex(const QString &latex, bool display, QString *error)
{
    Equation eq;
    eq.display = display;
    detail::LatexParser parser(latex);
    eq.lines.append(parser.parse());
    if (error != nullptr) {
        error->clear();
    }
    return eq;
}

Argument parseLatex(const QString &latex)
{
    detail::LatexParser parser(latex);
    return parser.parse();
}

} // namespace QindaOffice::Math
