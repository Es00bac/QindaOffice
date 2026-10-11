// SPDX-License-Identifier: GPL-3.0-or-later
// UnicodeMath in (math_linear.h): n-ary operators, radicals, matrices and
// equation arrays, prefix structures and functions.
#include "math_linear_p.h"
#include "math_symbols.h"

namespace QindaOffice::Math::detail {

namespace {

bool isTopGroup(char32_t c)
{
    return c == U'⏞' || c == U'⏜' || c == U'⎴' || c == U'⏠';
}

} // namespace

void markFunctionName(Argument &name)
{
    for (const NodePtr &n : name.items) {
        if (n->kind == NodeKind::Run && !runIsNormalText(*n) && runStyle(*n) == MathStyle::Default) {
            setRunStyle(*n, MathStyle::Plain);
        } else if (!n->args.isEmpty()
                   && (n->kind == NodeKind::Superscript || n->kind == NodeKind::Subscript
                       || n->kind == NodeKind::SubSup || n->kind == NodeKind::LimLow
                       || n->kind == NodeKind::LimUpp)) {
            markFunctionName(n->args.first());
        }
    }
}

// ∑_(i=1)^n▒a_i: limits after _ ^ (beside) or ┬ ┴ (below/above), then the
// operand after ▒ or a space.
NodePtr LinearParser::nary()
{
    const char32_t op = peek();
    ++m_i;
    NodePtr n = makeNode(NodeKind::Nary);
    if (op != U'∫') {
        setPropValue(*n, u"chr", charText(op));
    }
    bool underOver = false;
    bool hasSub = false;
    bool hasSup = false;
    for (;;) {
        const char32_t c = peek();
        if ((c == U'_' || c == U'┬') && !hasSub) {
            ++m_i;
            n->args[0] = scriptOperand();
            hasSub = true;
            underOver = underOver || c == U'┬';
        } else if ((c == U'^' || c == U'┴') && !hasSup) {
            ++m_i;
            n->args[1] = scriptOperand();
            hasSup = true;
            underOver = underOver || c == U'┴';
        } else {
            break;
        }
    }
    // AGENT-NOTE: written explicitly, as Word does, so the equation does
    // not depend on the document's m:intLim / m:naryLim.
    setPropValue(*n, u"limLoc", underOver || !isIntegral(op) ? QStringLiteral("undOvr") : QStringLiteral("subSup"));
    if (!hasSub) {
        setPropOn(*n, u"subHide", true);
    }
    if (!hasSup) {
        setPropOn(*n, u"supHide", true);
    }
    skipSpaces();
    if (peek() == kNaryand) {
        ++m_i;
    }
    n->args[2] = operand(false);
    return n;
}

// √x, √(x), √(n&x), ∛x, ∜x.
NodePtr LinearParser::radical()
{
    const char32_t c = peek();
    ++m_i;
    NodePtr r = makeNode(NodeKind::Radical);
    if (c != U'√') {
        r->args[0] = singleItem(makeRun(c == U'∛' ? QStringLiteral("3") : QStringLiteral("4")));
        r->args[1] = operand(true);
        return r;
    }
    if (peek() != U'(') {
        r->args[1] = operand(true);
        setPropOn(*r, u"degHide", true);
        return r;
    }
    ++m_i;
    ++m_depth;
    const Argument first = expression(U"&", false).finish();
    if (peek() == U'&') {
        ++m_i;
        r->args[0] = first;
        r->args[1] = expression({}, false).finish();
    } else {
        r->args[1] = first;
        setPropOn(*r, u"degHide", true);
    }
    if (peek() == U')') {
        ++m_i;
    }
    --m_depth;
    return r;
}

// ■(a&b@c&d) matrix, █(…@…) equation array (its & stay alignment marks),
// Ⓒ cases, ⒨ a matrix in parentheses, ⒩ in double bars.
NodePtr LinearParser::array()
{
    const char32_t kind = peek();
    ++m_i;
    const bool matrix = kind != U'█' && kind != U'Ⓒ';
    skipSpaces();
    QList<Argument> cells;
    QList<int> rows;
    if (peek() == U'(') {
        ++m_i;
        ++m_depth;
        int inRow = 0;
        for (;;) {
            cells.append(expression(matrix ? U"&@" : U"@", false).finish());
            ++inRow;
            if (matrix && peek() == U'&') {
                ++m_i;
                continue;
            }
            rows.append(inRow);
            inRow = 0;
            if (peek() == U'@') {
                ++m_i;
                continue;
            }
            if (peek() == U')') {
                ++m_i;
            }
            break;
        }
        --m_depth;
    } else {
        cells.append(Argument());
        rows.append(1);
    }
    NodePtr n = makeNode(matrix ? NodeKind::Matrix : NodeKind::EqArray, int(cells.size()));
    n->args = cells;
    if (matrix) {
        n->rowSizes = rows;
    }
    if (kind == U'■' || kind == U'█') {
        return n;
    }
    NodePtr d = structure(NodeKind::Delimiter, {singleItem(n)});
    if (kind == U'Ⓒ') {
        setPropValue(*d, u"begChr", QStringLiteral("{"));
        setPropValue(*d, u"endChr", QString());
    } else if (kind == U'⒩') {
        setPropValue(*d, u"begChr", QStringLiteral("‖"));
        setPropValue(*d, u"endChr", QStringLiteral("‖"));
    }
    return d;
}

// ¯ ▁ bars, □ ▭ boxes, ⏞ ⏟ … group characters (a following ^ or _ is the
// brace's label), ⟡ ⬄ ⇳ ⬍ ⬆ ⬇ ⬌ phantoms.
NodePtr LinearParser::prefixed()
{
    const char32_t c = peek();
    ++m_i;
    const Argument e = operand(true);
    NodePtr n;
    if (c == U'¯' || c == U'▁') {
        n = structure(NodeKind::Bar, {e});
        if (c == U'¯') {
            setPropValue(*n, u"pos", QStringLiteral("top"));
        }
    } else if (c == U'□' || c == U'▭') {
        n = structure(c == U'□' ? NodeKind::Box : NodeKind::BorderBox, {e});
    } else if (kGroupChars.find(c) != std::u32string_view::npos) {
        n = structure(NodeKind::GroupChar, {e});
        if (c != U'⏟') {
            setPropValue(*n, u"chr", charText(c));
        }
        if (isTopGroup(c)) {
            setPropValue(*n, u"pos", QStringLiteral("top"));
            setPropValue(*n, u"vertJc", QStringLiteral("bot"));
        }
        const char32_t next = peek();
        if (isTopGroup(c) && (next == U'^' || next == U'┴')) {
            ++m_i;
            n = structure(NodeKind::LimUpp, {singleItem(n), scriptOperand()});
        } else if (!isTopGroup(c) && (next == U'_' || next == U'┬')) {
            ++m_i;
            n = structure(NodeKind::LimLow, {singleItem(n), scriptOperand()});
        }
    } else {
        n = structure(NodeKind::Phantom, {e});
        PhantomFlags f;
        f.show = c == U'⬍' || c == U'⬆' || c == U'⬇' || c == U'⬌';
        f.zeroAscent = c == U'⬄' || c == U'⬍' || c == U'⬆';
        f.zeroDescent = c == U'⬄' || c == U'⬍' || c == U'⬇';
        f.zeroWidth = c == U'⇳' || c == U'⬌';
        setPhantomFlags(*n, f);
    }
    return n;
}

// sin x, log_2\u2061x, lim_(n→∞) a_n: a known name followed by a space, U+2061,
// a script or a bracket is a function.
bool LinearParser::function(AtomList &atoms)
{
    QString rest;
    for (qsizetype k = 0; k < 8 && m_i + k < m_s.size(); ++k) {
        rest += charText(m_s.at(m_i + k));
    }
    const qsizetype length = functionNameAt(rest);
    if (length == 0) {
        return false;
    }
    const char32_t after = peek(length);
    if (!(after == U' ' || after == kApply || after == U'_' || after == U'^' || after == U'┬' || after == U'┴'
          || after == U'(')) {
        return false;
    }
    const QString name = rest.left(length);
    m_i += length;
    Argument fname = singleItem(makeRun(name));
    markFunctionName(fname);
    for (;;) {
        const char32_t c = peek();
        if (c == U'_' || c == U'┬') {
            ++m_i;
            const NodeKind kind = c == U'┬' || isLimitFunction(name) ? NodeKind::LimLow : NodeKind::Subscript;
            fname = singleItem(structure(kind, {fname, scriptOperand()}));
        } else if (c == U'^' || c == U'┴') {
            ++m_i;
            const Argument sup = scriptOperand();
            const Node &inner = *fname.items.first();
            if (c == U'^' && inner.kind == NodeKind::Subscript) {
                fname = singleItem(structure(NodeKind::SubSup, {inner.args.at(0), inner.args.at(1), sup}));
            } else {
                fname = singleItem(structure(c == U'┴' ? NodeKind::LimUpp : NodeKind::Superscript, {fname, sup}));
            }
        } else {
            break;
        }
    }
    if (peek() == kApply || peek() == U' ') {
        ++m_i;
    }
    atoms.addNode(structure(NodeKind::Function, {fname, operand(false)}));
    return true;
}

} // namespace QindaOffice::Math::detail
