// SPDX-License-Identifier: GPL-3.0-or-later
// LaTeX in (math_latex.h): the commands — fractions, roots, \left…\right,
// environments, styles, accents, braces, big operators and functions.
#include "math_latex_p.h"
#include "math_symbols.h"

#include <QHash>
#include <QStringList>

#include <utility>

namespace QindaOffice::Math::detail {

namespace {

const QHash<QString, char32_t> &accents()
{
    static const QHash<QString, char32_t> table{
        {QStringLiteral("hat"), 0x0302},           {QStringLiteral("widehat"), 0x0302},
        {QStringLiteral("check"), 0x030C},         {QStringLiteral("tilde"), 0x0303},
        {QStringLiteral("widetilde"), 0x0303},     {QStringLiteral("acute"), 0x0301},
        {QStringLiteral("grave"), 0x0300},         {QStringLiteral("dot"), 0x0307},
        {QStringLiteral("ddot"), 0x0308},          {QStringLiteral("dddot"), 0x20DB},
        {QStringLiteral("breve"), 0x0306},         {QStringLiteral("bar"), 0x0305},
        {QStringLiteral("vec"), 0x20D7},           {QStringLiteral("overrightarrow"), 0x20D7},
        {QStringLiteral("overleftarrow"), 0x20D6}, {QStringLiteral("overleftrightarrow"), 0x20E1},
        {QStringLiteral("mathring"), 0x030A},
    };
    return table;
}

const QHash<QString, RunStyle> &styles()
{
    static const QHash<QString, RunStyle> table{
        {QStringLiteral("mathrm"), {MathStyle::Plain, MathScript::Roman, false}},
        {QStringLiteral("mathbf"), {MathStyle::Bold, MathScript::Roman, false}},
        {QStringLiteral("mathit"), {MathStyle::Italic, MathScript::Roman, false}},
        {QStringLiteral("boldsymbol"), {MathStyle::BoldItalic, MathScript::Roman, false}},
        {QStringLiteral("bm"), {MathStyle::BoldItalic, MathScript::Roman, false}},
        {QStringLiteral("mathbb"), {MathStyle::Plain, MathScript::DoubleStruck, false}},
        {QStringLiteral("mathcal"), {MathStyle::Default, MathScript::Script, false}},
        {QStringLiteral("mathscr"), {MathStyle::Default, MathScript::Script, false}},
        {QStringLiteral("mathfrak"), {MathStyle::Default, MathScript::Fraktur, false}},
        {QStringLiteral("mathsf"), {MathStyle::Plain, MathScript::SansSerif, false}},
        {QStringLiteral("mathtt"), {MathStyle::Plain, MathScript::Monospace, false}},
    };
    return table;
}

bool isBig(const QString &name)
{
    static const QStringList names{QStringLiteral("big"),   QStringLiteral("Big"),   QStringLiteral("bigg"),
                                   QStringLiteral("Bigg"),  QStringLiteral("bigl"),  QStringLiteral("bigr"),
                                   QStringLiteral("Bigl"),  QStringLiteral("Bigr"),  QStringLiteral("biggl"),
                                   QStringLiteral("biggr"), QStringLiteral("Biggl"), QStringLiteral("Biggr"),
                                   QStringLiteral("bigm"),  QStringLiteral("Bigm")};
    return names.contains(name);
}

} // namespace

void LatexParser::command(AtomList &atoms, const QString &name, int stops)
{
    static const QHash<QString, QString> spaces{
        {QStringLiteral(","), QStringLiteral("\u2009")},     {QStringLiteral(":"), QStringLiteral("\u205F")},
        {QStringLiteral(">"), QStringLiteral("\u205F")},     {QStringLiteral(";"), QStringLiteral("\u2004")},
        {QStringLiteral(" "), QStringLiteral("\u00A0")},     {QStringLiteral("quad"), QStringLiteral("\u2003")},
        {QStringLiteral("qquad"), QStringLiteral("\u2003\u2003")}, {QStringLiteral("!"), QString()},
    };
    if (const auto it = spaces.constFind(name); it != spaces.constEnd()) {
        for (const uint c : it->toUcs4()) {
            atoms.addChar(c, RunStyle(), false);
        }
        return;
    }
    if (name == u"{" || name == u"lbrace") {
        bracket(atoms, U'{', stops);
    } else if (name == u"|" || name == u"Vert" || name == u"lVert") {
        bracket(atoms, U'‖', stops);
    } else if (name.size() == 1) {
        atoms.addChar(name.at(0).unicode(), RunStyle(), false); // \# \$ \% \& \_ \}
    } else if (name == u"frac" || name == u"dfrac" || name == u"tfrac" || name == u"cfrac" || name == u"sfrac") {
        const Argument num = argument();
        NodePtr f = structure(NodeKind::Fraction, {num, argument()});
        if (name == u"sfrac") {
            setFractionType(*f, FractionType::Skewed);
        }
        atoms.addNode(f);
    } else if (name == u"binom" || name == u"dbinom" || name == u"tbinom" || name == u"genfrac") {
        QString left = QStringLiteral("(");
        QString right = QStringLiteral(")");
        bool bar = false;
        if (name == u"genfrac") {
            left = rawGroup();
            right = rawGroup();
            const QString thickness = rawGroup();
            bar = thickness.isEmpty() || thickness.toDouble() > 0 || !thickness.startsWith(u'0');
            rawGroup(); // style
        }
        const Argument num = argument();
        NodePtr f = structure(NodeKind::Fraction, {num, argument()});
        if (!bar) {
            setFractionType(*f, FractionType::NoBar);
        }
        if (left.isEmpty() && right.isEmpty()) {
            atoms.addNode(f);
            return;
        }
        NodePtr d = structure(NodeKind::Delimiter, {singleItem(f)});
        if (left != u"(") {
            setPropValue(*d, u"begChr", left == u"." ? QString() : left);
        }
        if (right != u")") {
            setPropValue(*d, u"endChr", right == u"." ? QString() : right);
        }
        atoms.addNode(d);
    } else if (name == u"sqrt") {
        NodePtr r = makeNode(NodeKind::Radical);
        skipSpaces();
        if (peek() == U'[') {
            ++m_i;
            r->args[0] = expression(Square).finish();
            if (peek() == U']') {
                ++m_i;
            }
        } else {
            setPropOn(*r, u"degHide", true);
        }
        r->args[1] = argument();
        atoms.addNode(r);
    } else if (name == u"left") {
        atoms.addNode(leftRight());
    } else if (isBig(name)) {
        for (const uint c : delimiterToken().toUcs4()) {
            atoms.addChar(c, RunStyle(), false);
        }
    } else if (name == u"begin") {
        atoms.addNode(environment());
    } else if (name == u"text" || name == u"textrm" || name == u"mbox" || name == u"textnormal") {
        if (const QString t = rawGroup(); !t.isEmpty()) {
            atoms.addNode(styledRun(t, RunStyle{MathStyle::Default, MathScript::Roman, true}));
        }
    } else if (styles().contains(name)) {
        const RunStyle saved = m_style;
        m_style = styles().value(name);
        atoms.addGroup(argument());
        m_style = saved;
    } else if (name == u"operatorname") {
        atoms.addNode(function(rawGroup()));
    } else if (accents().contains(name)) {
        NodePtr acc = structure(NodeKind::Accent, {argument()});
        if (const char32_t chr = accents().value(name); chr != 0x0302) {
            setPropValue(*acc, u"chr", charText(chr));
        }
        atoms.addNode(acc);
    } else if (name == u"overline" || name == u"underline") {
        NodePtr bar = structure(NodeKind::Bar, {argument()});
        if (name == u"overline") {
            setPropValue(*bar, u"pos", QStringLiteral("top"));
        }
        atoms.addNode(bar);
    } else if (name == u"overbrace" || name == u"underbrace") {
        const bool top = name == u"overbrace";
        NodePtr g = structure(NodeKind::GroupChar, {argument()});
        if (top) {
            setPropValue(*g, u"chr", QStringLiteral("⏞"));
            setPropValue(*g, u"pos", QStringLiteral("top"));
            setPropValue(*g, u"vertJc", QStringLiteral("bot"));
        }
        skipSpaces();
        if (peek() == (top ? U'^' : U'_')) {
            ++m_i;
            g = structure(top ? NodeKind::LimUpp : NodeKind::LimLow, {singleItem(g), argument()});
        }
        atoms.addNode(g);
    } else if (name == u"overset" || name == u"underset" || name == u"stackrel") {
        const Argument over = argument();
        atoms.addNode(structure(name == u"underset" ? NodeKind::LimLow : NodeKind::LimUpp, {argument(), over}));
    } else if (name == u"boxed") {
        atoms.addNode(structure(NodeKind::BorderBox, {argument()}));
    } else if (name == u"phantom" || name == u"hphantom" || name == u"vphantom" || name == u"smash") {
        NodePtr p = structure(NodeKind::Phantom, {argument()});
        PhantomFlags f;
        f.show = name == u"smash";
        f.zeroAscent = f.zeroDescent = name == u"hphantom" || name == u"smash";
        f.zeroWidth = name == u"vphantom";
        setPhantomFlags(*p, f);
        atoms.addNode(p);
    } else if (isFunctionName(name)) {
        atoms.addNode(function(name));
    } else if (const QString symbol = latexSymbol(name); !symbol.isEmpty()) {
        const char32_t c = onlyChar(symbol);
        if (c != 0 && isNaryOperator(c)) {
            atoms.addNode(nary(c));
        } else if (c != 0 && isOpeningBracket(c)) {
            bracket(atoms, c, stops);
        } else {
            for (const uint u : symbol.toUcs4()) {
                addChar(atoms, u);
            }
        }
    } else if (name != u"limits" && name != u"nolimits" && !name.endsWith(u"style")) {
        atoms.addNode(styledRun(QLatin1Char('\\') + name, RunStyle{MathStyle::Default, MathScript::Roman, true}));
    }
}

NodePtr LatexParser::nary(char32_t op)
{
    NodePtr n = makeNode(NodeKind::Nary);
    if (op != U'∫') {
        setPropValue(*n, u"chr", charText(op));
    }
    std::optional<bool> limits;
    bool hasSub = false;
    bool hasSup = false;
    for (;;) {
        skipSpaces();
        const QString name = peekCommand();
        if (name == u"limits" || name == u"nolimits") {
            readCommand();
            limits = name == u"limits";
        } else if (peek() == U'_' && !hasSub) {
            ++m_i;
            n->args[0] = argument();
            hasSub = true;
        } else if (peek() == U'^' && !hasSup) {
            ++m_i;
            n->args[1] = argument();
            hasSup = true;
        } else {
            break;
        }
    }
    const bool underOver = limits.value_or(!isIntegral(op));
    setPropValue(*n, u"limLoc", underOver ? QStringLiteral("undOvr") : QStringLiteral("subSup"));
    if (!hasSub) {
        setPropOn(*n, u"subHide", true);
    }
    if (!hasSup) {
        setPropOn(*n, u"supHide", true);
    }
    n->args[2] = argument();
    return n;
}

NodePtr LatexParser::function(const QString &name)
{
    NodePtr run = makeRun(name);
    setRunStyle(*run, MathStyle::Plain);
    Argument fname = singleItem(run);
    for (;;) {
        skipSpaces();
        if (peekCommand() == u"limits" || peekCommand() == u"nolimits") {
            readCommand();
        } else if (peek() == U'_') {
            ++m_i;
            fname = singleItem(structure(isLimitFunction(name) ? NodeKind::LimLow : NodeKind::Subscript, {fname, argument()}));
        } else if (peek() == U'^') {
            ++m_i;
            const Argument sup = argument();
            const Node &inner = *fname.items.first();
            fname = inner.kind == NodeKind::Subscript
                ? singleItem(structure(NodeKind::SubSup, {inner.args.at(0), inner.args.at(1), sup}))
                : singleItem(structure(NodeKind::Superscript, {fname, sup}));
        } else {
            break;
        }
    }
    return structure(NodeKind::Function, {fname, argument()});
}

NodePtr LatexParser::environment()
{
    const QString env = rawGroup();
    if (env.startsWith(u"array") || env.startsWith(u"alignat")) {
        rawGroup(); // column specification / column count
    }
    const bool matrix = env.endsWith(u"matrix") || env == u"array";
    QList<Argument> cells;
    QList<int> rows;
    int inRow = 0;
    for (;;) {
        cells.append(expression(End | Row | (matrix ? Amp : 0)).finish());
        ++inRow;
        if (matrix && peek() == U'&') {
            ++m_i;
            continue;
        }
        if (peekCommand() == u"\\") {
            m_i += 2;
            rows.append(inRow);
            inRow = 0;
            continue;
        }
        rows.append(inRow);
        break;
    }
    // "a \\ b \\" leaves an empty last row.
    if (rows.size() > 1 && rows.last() == 1 && cells.last().isEmpty()) {
        rows.removeLast();
        cells.removeLast();
    }
    if (peekCommand() == u"end") {
        readCommand();
        rawGroup();
    }
    NodePtr n = makeNode(matrix ? NodeKind::Matrix : NodeKind::EqArray, int(cells.size()));
    n->args = cells;
    if (matrix) {
        n->rowSizes = rows;
    }
    static const QHash<QString, std::pair<QString, QString>> fences{
        {QStringLiteral("pmatrix"), {QStringLiteral("("), QStringLiteral(")")}},
        {QStringLiteral("bmatrix"), {QStringLiteral("["), QStringLiteral("]")}},
        {QStringLiteral("Bmatrix"), {QStringLiteral("{"), QStringLiteral("}")}},
        {QStringLiteral("vmatrix"), {QStringLiteral("|"), QStringLiteral("|")}},
        {QStringLiteral("Vmatrix"), {QStringLiteral("‖"), QStringLiteral("‖")}},
        {QStringLiteral("cases"), {QStringLiteral("{"), QString()}},
        {QStringLiteral("dcases"), {QStringLiteral("{"), QString()}},
    };
    const auto fence = fences.constFind(env);
    if (fence == fences.constEnd()) {
        return n;
    }
    NodePtr d = structure(NodeKind::Delimiter, {singleItem(n)});
    if (fence->first != u"(") {
        setPropValue(*d, u"begChr", fence->first);
    }
    if (fence->second != u")") {
        setPropValue(*d, u"endChr", fence->second);
    }
    return d;
}

} // namespace QindaOffice::Math::detail
