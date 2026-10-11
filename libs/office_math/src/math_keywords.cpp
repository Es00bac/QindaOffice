// SPDX-License-Identifier: GPL-3.0-or-later
// The keyword tables of math_symbols.h: Word's Math AutoCorrect list and the
// LaTeX symbol commands.
#include "math_alphabets.h"
#include "math_symbols.h"

#include <QHash>

#include <string_view>

namespace QindaOffice::Math {

namespace {

struct Entry {
    QStringView name;
    QStringView text;
};

// AGENT-NOTE: Word's Math AutoCorrect list (File ▸ Options ▸ Proofing ▸
// AutoCorrect Options ▸ Math AutoCorrect), as Microsoft documents it for
// Word's linear format. Structure keywords map to the UnicodeMath operator
// the linear parser builds up (√ ■ █ ▒ ┬ ┴ 〖 〗 ├ ┤ …); accent keywords to
// the combining mark that follows its base.
constexpr Entry kWord[] = {
    {u"above", u"┴"}, {u"acute", u"\u0301"}, {u"aleph", u"ℵ"}, {u"alpha", u"α"}, {u"Alpha", u"Α"},
    {u"amalg", u"∐"}, {u"angle", u"∠"}, {u"aoint", u"∳"}, {u"approx", u"≈"}, {u"asmash", u"⬆"},
    {u"ast", u"∗"}, {u"asymp", u"≍"}, {u"atop", u"¦"}, {u"bar", u"\u0305"}, {u"Bar", u"\u033F"},
    {u"because", u"∵"}, {u"begin", u"〖"}, {u"below", u"┬"}, {u"bet", u"ℶ"}, {u"beta", u"β"},
    {u"Beta", u"Β"}, {u"beth", u"ℶ"}, {u"bigcap", u"⋂"}, {u"bigcup", u"⋃"}, {u"bigodot", u"⨀"},
    {u"bigoplus", u"⨁"}, {u"bigotimes", u"⨂"}, {u"bigsqcup", u"⨆"}, {u"biguplus", u"⨄"}, {u"bigvee", u"⋁"},
    {u"bigwedge", u"⋀"}, {u"bot", u"⊥"}, {u"bowtie", u"⋈"}, {u"box", u"□"}, {u"boxdot", u"⊡"},
    {u"boxminus", u"⊟"}, {u"boxplus", u"⊞"}, {u"bra", u"⟨"}, {u"break", u"⤶"}, {u"breve", u"\u0306"},
    {u"bullet", u"∙"}, {u"cap", u"∩"}, {u"cbrt", u"∛"}, {u"cases", u"Ⓒ"}, {u"cdot", u"⋅"},
    {u"cdots", u"⋯"}, {u"check", u"\u030C"}, {u"chi", u"χ"}, {u"Chi", u"Χ"}, {u"circ", u"∘"},
    {u"close", u"┤"}, {u"clubsuit", u"♣"}, {u"coint", u"∲"}, {u"cong", u"≅"}, {u"coprod", u"∐"},
    {u"cup", u"∪"}, {u"dalet", u"ℸ"}, {u"daleth", u"ℸ"}, {u"dashv", u"⊣"}, {u"dd", u"ⅆ"}, {u"Dd", u"ⅅ"},
    {u"ddddot", u"\u20DC"}, {u"dddot", u"\u20DB"}, {u"ddot", u"\u0308"}, {u"ddots", u"⋱"}, {u"defeq", u"≝"},
    {u"degc", u"℃"}, {u"degf", u"℉"}, {u"degree", u"°"}, {u"delta", u"δ"}, {u"Delta", u"Δ"},
    {u"Deltaeq", u"≜"}, {u"diamond", u"⋄"}, {u"diamondsuit", u"♢"}, {u"div", u"÷"}, {u"dot", u"\u0307"},
    {u"doteq", u"≐"}, {u"dots", u"…"}, {u"downarrow", u"↓"}, {u"Downarrow", u"⇓"}, {u"dsmash", u"⬇"},
    {u"ee", u"ⅇ"}, {u"ell", u"ℓ"}, {u"emptyset", u"∅"}, {u"emsp", u"\u2003"}, {u"end", u"〗"},
    {u"ensp", u"\u2002"}, {u"epsilon", u"ϵ"}, {u"Epsilon", u"Ε"}, {u"eqarray", u"█"}, {u"equiv", u"≡"},
    {u"eta", u"η"}, {u"Eta", u"Η"}, {u"exists", u"∃"}, {u"forall", u"∀"}, {u"funcapply", u"\u2061"},
    {u"gamma", u"γ"}, {u"Gamma", u"Γ"}, {u"ge", u"≥"}, {u"geq", u"≥"}, {u"gets", u"←"}, {u"gg", u"≫"},
    {u"gimel", u"ℷ"}, {u"grave", u"\u0300"}, {u"hairsp", u"\u200A"}, {u"hat", u"\u0302"}, {u"hbar", u"ℏ"},
    {u"heartsuit", u"♡"}, {u"hookleftarrow", u"↩"}, {u"hookrightarrow", u"↪"}, {u"hphantom", u"⬄"},
    {u"hsmash", u"⬌"}, {u"hvec", u"\u20D1"}, {u"ii", u"ⅈ"}, {u"iiiint", u"⨌"}, {u"iiint", u"∭"},
    {u"iint", u"∬"}, {u"Im", u"ℑ"}, {u"imath", u"ı"}, {u"in", u"∈"}, {u"inc", u"∆"}, {u"infty", u"∞"},
    {u"int", u"∫"}, {u"iota", u"ι"}, {u"Iota", u"Ι"}, {u"itimes", u"\u2062"}, {u"jj", u"ⅉ"},
    {u"jmath", u"ȷ"}, {u"kappa", u"κ"}, {u"Kappa", u"Κ"}, {u"ket", u"⟩"}, {u"lambda", u"λ"},
    {u"Lambda", u"Λ"}, {u"langle", u"⟨"}, {u"lbbrack", u"⟦"}, {u"lbrace", u"{"}, {u"lbrack", u"["},
    {u"lceil", u"⌈"}, {u"ldiv", u"∕"}, {u"ldivide", u"∕"}, {u"ldots", u"…"}, {u"le", u"≤"},
    {u"left", u"├"}, {u"leftarrow", u"←"}, {u"Leftarrow", u"⇐"}, {u"leftharpoondown", u"↽"},
    {u"leftharpoonup", u"↼"}, {u"leftrightarrow", u"↔"}, {u"Leftrightarrow", u"⇔"}, {u"leq", u"≤"},
    {u"lfloor", u"⌊"}, {u"lhvec", u"\u20D0"}, {u"ll", u"≪"}, {u"lmoust", u"⎰"}, {u"Longleftarrow", u"⟸"},
    {u"Longleftrightarrow", u"⟺"}, {u"Longrightarrow", u"⟹"}, {u"lrhar", u"⇋"}, {u"lvec", u"\u20D6"},
    {u"mapsto", u"↦"}, {u"matrix", u"■"}, {u"medsp", u"\u205F"}, {u"mid", u"∣"}, {u"middle", u"ⓜ"},
    {u"models", u"⊨"}, {u"mp", u"∓"}, {u"mu", u"μ"}, {u"Mu", u"Μ"}, {u"nabla", u"∇"}, {u"naryand", u"▒"},
    {u"nbsp", u"\u00A0"}, {u"ne", u"≠"}, {u"nearrow", u"↗"}, {u"neg", u"¬"}, {u"neq", u"≠"}, {u"ni", u"∋"},
    {u"norm", u"‖"}, {u"notcontain", u"∌"}, {u"notelement", u"∉"}, {u"notin", u"∉"}, {u"nu", u"ν"},
    {u"Nu", u"Ν"}, {u"nwarrow", u"↖"}, {u"o", u"ο"}, {u"O", u"Ο"}, {u"odot", u"⊙"}, {u"of", u"▒"},
    {u"oiiint", u"∰"}, {u"oiint", u"∯"}, {u"oint", u"∮"}, {u"omega", u"ω"}, {u"Omega", u"Ω"},
    {u"ominus", u"⊖"}, {u"open", u"├"}, {u"oplus", u"⊕"}, {u"otimes", u"⊗"}, {u"over", u"/"},
    {u"overbar", u"¯"}, {u"overbrace", u"⏞"}, {u"overbracket", u"⎴"}, {u"overline", u"¯"},
    {u"overparen", u"⏜"}, {u"overshell", u"⏠"}, {u"parallel", u"∥"}, {u"partial", u"∂"},
    {u"pmatrix", u"⒨"}, {u"perp", u"⊥"}, {u"phantom", u"⟡"}, {u"phi", u"ϕ"}, {u"Phi", u"Φ"},
    {u"pi", u"π"}, {u"Pi", u"Π"}, {u"pm", u"±"}, {u"pppprime", u"⁗"}, {u"ppprime", u"‴"}, {u"pprime", u"″"},
    {u"prec", u"≺"}, {u"preceq", u"≼"}, {u"prime", u"′"}, {u"prod", u"∏"}, {u"propto", u"∝"},
    {u"psi", u"ψ"}, {u"Psi", u"Ψ"}, {u"qdrt", u"∜"}, {u"rangle", u"⟩"}, {u"Rangle", u"⟫"},
    {u"ratio", u"∶"}, {u"rbrace", u"}"}, {u"rbrack", u"]"}, {u"Rbrack", u"⟧"}, {u"rceil", u"⌉"},
    {u"rddots", u"⋰"}, {u"Re", u"ℜ"}, {u"rect", u"▭"}, {u"rfloor", u"⌋"}, {u"rho", u"ρ"}, {u"Rho", u"Ρ"},
    {u"rhvec", u"\u20D1"}, {u"right", u"┤"}, {u"rightarrow", u"→"}, {u"Rightarrow", u"⇒"},
    {u"rightharpoondown", u"⇁"}, {u"rightharpoonup", u"⇀"}, {u"rmoust", u"⎱"}, {u"root", u"⒭"},
    {u"sdiv", u"⁄"}, {u"sdivide", u"⁄"}, {u"searrow", u"↘"}, {u"setminus", u"∖"}, {u"sigma", u"σ"},
    {u"Sigma", u"Σ"}, {u"sim", u"∼"}, {u"simeq", u"≃"}, {u"smash", u"⬍"}, {u"smile", u"⌣"},
    {u"spadesuit", u"♠"}, {u"sqcap", u"⊓"}, {u"sqcup", u"⊔"}, {u"sqrt", u"√"}, {u"sqsubseteq", u"⊑"},
    {u"sqsuperseteq", u"⊒"}, {u"star", u"⋆"}, {u"subset", u"⊂"}, {u"subseteq", u"⊆"}, {u"succ", u"≻"},
    {u"succeq", u"≽"}, {u"sum", u"∑"}, {u"superset", u"⊃"}, {u"superseteq", u"⊇"}, {u"swarrow", u"↙"},
    {u"tau", u"τ"}, {u"Tau", u"Τ"}, {u"therefore", u"∴"}, {u"theta", u"θ"}, {u"Theta", u"Θ"},
    {u"thicksp", u"\u2005"}, {u"thinsp", u"\u2006"}, {u"tilde", u"\u0303"}, {u"times", u"×"}, {u"to", u"→"},
    {u"top", u"⊤"}, {u"tvec", u"\u20E1"}, {u"ubar", u"\u0332"}, {u"Ubar", u"\u0333"}, {u"underbar", u"▁"},
    {u"underbrace", u"⏟"}, {u"underbracket", u"⎵"}, {u"underline", u"▁"}, {u"underparen", u"⏝"},
    {u"uparrow", u"↑"}, {u"Uparrow", u"⇑"}, {u"updownarrow", u"↕"}, {u"Updownarrow", u"⇕"},
    {u"uplus", u"⊎"}, {u"upsilon", u"υ"}, {u"Upsilon", u"Υ"}, {u"varepsilon", u"ε"}, {u"varphi", u"φ"},
    {u"varpi", u"ϖ"}, {u"varrho", u"ϱ"}, {u"varsigma", u"ς"}, {u"vartheta", u"ϑ"}, {u"vbar", u"│"},
    {u"vdash", u"⊢"}, {u"vdots", u"⋮"}, {u"vec", u"\u20D7"}, {u"vee", u"∨"}, {u"vert", u"|"},
    {u"Vert", u"‖"}, {u"Vmatrix", u"⒩"}, {u"vphantom", u"⇳"}, {u"vthicksp", u"\u2004"},
    {u"wedge", u"∧"}, {u"wp", u"℘"}, {u"wr", u"≀"}, {u"xi", u"ξ"}, {u"Xi", u"Ξ"}, {u"zeta", u"ζ"},
    {u"Zeta", u"Ζ"}, {u"zwnj", u"\u200C"}, {u"zwsp", u"\u200B"},
};

// LaTeX's symbol commands, in the order the writer prefers them for a
// character (Greek comes from kWord, whose names are LaTeX's).
constexpr Entry kLatex[] = {
    {u"le", u"≤"}, {u"ge", u"≥"}, {u"ne", u"≠"}, {u"leq", u"≤"}, {u"geq", u"≥"}, {u"neq", u"≠"},
    {u"approx", u"≈"}, {u"equiv", u"≡"}, {u"sim", u"∼"}, {u"simeq", u"≃"}, {u"cong", u"≅"},
    {u"propto", u"∝"}, {u"ll", u"≪"}, {u"gg", u"≫"}, {u"prec", u"≺"}, {u"succ", u"≻"}, {u"preceq", u"≼"},
    {u"succeq", u"≽"}, {u"subset", u"⊂"}, {u"supset", u"⊃"}, {u"subseteq", u"⊆"}, {u"supseteq", u"⊇"},
    {u"sqsubseteq", u"⊑"}, {u"sqsupseteq", u"⊒"}, {u"in", u"∈"}, {u"notin", u"∉"}, {u"ni", u"∋"},
    {u"owns", u"∋"}, {u"perp", u"⊥"}, {u"parallel", u"∥"}, {u"mid", u"∣"}, {u"nmid", u"∤"},
    {u"vdash", u"⊢"}, {u"dashv", u"⊣"}, {u"models", u"⊨"}, {u"doteq", u"≐"}, {u"asymp", u"≍"},
    {u"bowtie", u"⋈"}, {u"smile", u"⌣"}, {u"frown", u"⌢"}, {u"leqslant", u"⩽"}, {u"geqslant", u"⩾"},
    {u"rightarrow", u"→"}, {u"to", u"→"}, {u"leftarrow", u"←"}, {u"gets", u"←"}, {u"leftrightarrow", u"↔"},
    {u"Rightarrow", u"⇒"}, {u"Leftarrow", u"⇐"}, {u"Leftrightarrow", u"⇔"}, {u"longrightarrow", u"⟶"},
    {u"longleftarrow", u"⟵"}, {u"longleftrightarrow", u"⟷"}, {u"Longrightarrow", u"⟹"},
    {u"Longleftarrow", u"⟸"}, {u"Longleftrightarrow", u"⟺"}, {u"implies", u"⟹"}, {u"impliedby", u"⟸"},
    {u"iff", u"⟺"}, {u"mapsto", u"↦"}, {u"longmapsto", u"⟼"}, {u"uparrow", u"↑"}, {u"downarrow", u"↓"},
    {u"updownarrow", u"↕"}, {u"Uparrow", u"⇑"}, {u"Downarrow", u"⇓"}, {u"Updownarrow", u"⇕"},
    {u"nearrow", u"↗"}, {u"searrow", u"↘"}, {u"swarrow", u"↙"}, {u"nwarrow", u"↖"},
    {u"hookrightarrow", u"↪"}, {u"hookleftarrow", u"↩"}, {u"rightharpoonup", u"⇀"},
    {u"rightharpoondown", u"⇁"}, {u"leftharpoonup", u"↼"}, {u"leftharpoondown", u"↽"},
    {u"rightleftharpoons", u"⇌"}, {u"pm", u"±"}, {u"mp", u"∓"}, {u"times", u"×"}, {u"div", u"÷"},
    {u"cdot", u"⋅"}, {u"ast", u"∗"}, {u"star", u"⋆"}, {u"circ", u"∘"}, {u"bullet", u"∙"}, {u"cap", u"∩"},
    {u"cup", u"∪"}, {u"uplus", u"⊎"}, {u"sqcap", u"⊓"}, {u"sqcup", u"⊔"}, {u"wedge", u"∧"}, {u"land", u"∧"},
    {u"vee", u"∨"}, {u"lor", u"∨"}, {u"setminus", u"∖"}, {u"smallsetminus", u"∖"}, {u"oplus", u"⊕"},
    {u"ominus", u"⊖"}, {u"otimes", u"⊗"}, {u"oslash", u"⊘"}, {u"odot", u"⊙"}, {u"diamond", u"⋄"},
    {u"wr", u"≀"}, {u"amalg", u"⨿"}, {u"dagger", u"†"}, {u"ddagger", u"‡"}, {u"cdotp", u"·"},
    {u"infty", u"∞"}, {u"partial", u"∂"}, {u"nabla", u"∇"}, {u"forall", u"∀"}, {u"exists", u"∃"},
    {u"nexists", u"∄"}, {u"neg", u"¬"}, {u"lnot", u"¬"}, {u"emptyset", u"∅"}, {u"varnothing", u"∅"},
    {u"angle", u"∠"}, {u"measuredangle", u"∡"}, {u"prime", u"′"}, {u"hbar", u"ℏ"}, {u"hslash", u"ℏ"},
    {u"ell", u"ℓ"}, {u"wp", u"℘"}, {u"Re", u"ℜ"}, {u"Im", u"ℑ"}, {u"aleph", u"ℵ"}, {u"beth", u"ℶ"},
    {u"gimel", u"ℷ"}, {u"daleth", u"ℸ"}, {u"imath", u"ı"}, {u"jmath", u"ȷ"}, {u"therefore", u"∴"},
    {u"because", u"∵"}, {u"ldots", u"…"}, {u"cdots", u"⋯"}, {u"vdots", u"⋮"}, {u"ddots", u"⋱"},
    {u"dots", u"…"}, {u"top", u"⊤"}, {u"bot", u"⊥"}, {u"triangle", u"△"}, {u"square", u"□"},
    {u"clubsuit", u"♣"}, {u"diamondsuit", u"♢"}, {u"heartsuit", u"♡"}, {u"spadesuit", u"♠"},
    {u"flat", u"♭"}, {u"natural", u"♮"}, {u"sharp", u"♯"}, {u"surd", u"√"}, {u"colon", u":"},
    {u"backslash", u"\\"}, {u"lt", u"<"}, {u"gt", u">"}, {u"sum", u"∑"}, {u"prod", u"∏"},
    {u"coprod", u"∐"}, {u"int", u"∫"}, {u"iint", u"∬"}, {u"iiint", u"∭"}, {u"iiiint", u"⨌"},
    {u"oint", u"∮"}, {u"oiint", u"∯"}, {u"oiiint", u"∰"}, {u"bigcup", u"⋃"}, {u"bigcap", u"⋂"},
    {u"bigvee", u"⋁"}, {u"bigwedge", u"⋀"}, {u"bigoplus", u"⨁"}, {u"bigotimes", u"⨂"},
    {u"bigodot", u"⨀"}, {u"biguplus", u"⨄"}, {u"bigsqcup", u"⨆"}, {u"langle", u"⟨"}, {u"rangle", u"⟩"},
    {u"lfloor", u"⌊"}, {u"rfloor", u"⌋"}, {u"lceil", u"⌈"}, {u"rceil", u"⌉"}, {u"lbrace", u"{"},
    {u"rbrace", u"}"}, {u"lbrack", u"["}, {u"rbrack", u"]"}, {u"vert", u"|"}, {u"Vert", u"‖"},
    {u"lvert", u"|"}, {u"rvert", u"|"}, {u"lVert", u"‖"}, {u"rVert", u"‖"}, {u"llbracket", u"⟦"},
    {u"rrbracket", u"⟧"}, {u"varkappa", u"ϰ"}, {u"digamma", u"ϝ"},
};

// Characters that are UnicodeMath structure operators: never a LaTeX symbol.
constexpr std::u32string_view kStructural = U"┴┬〖〗├┤/¦■█Ⓒ⒨⒩▒ⓜ□▭√∛∜⒭¯▁⏞⏟⏜⏝⎴⎵⏠⟡⬄⇳⬍⬆⬇⬌⤶\u2062⁄∕│\u2061";

QHash<QString, QString> build(const Entry *rows, size_t count)
{
    QHash<QString, QString> out;
    for (size_t i = 0; i < count; ++i) {
        out.insert(rows[i].name.toString(), rows[i].text.toString());
    }
    return out;
}

const QHash<QString, QString> &wordTable()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> t = build(kWord, std::size(kWord));
        // \doubleA … \doublez, \frakturA …, \scriptA … (Word's styled alphabets).
        for (char32_t c = U'A'; c <= U'z'; ++c) {
            if (c > U'Z' && c < U'a') {
                continue;
            }
            const QString letter = QString::fromUcs4(&c, 1);
            const auto add = [&](const char *prefix, MathScript script) {
                const char32_t styled = mathAlphanumeric(c, script, false, false);
                t.insert(QLatin1String(prefix) + letter, QString::fromUcs4(&styled, 1));
            };
            add("double", MathScript::DoubleStruck);
            add("fraktur", MathScript::Fraktur);
            add("script", MathScript::Script);
        }
        return t;
    }();
    return table;
}

const QHash<QString, QString> &latexTable()
{
    static const QHash<QString, QString> table = build(kLatex, std::size(kLatex));
    return table;
}

bool isGreek(uint c)
{
    return c >= 0x0391 && c <= 0x03F6;
}

} // namespace

QString wordAutoCorrect(QStringView keyword)
{
    return wordTable().value(keyword.toString());
}

QString latexSymbol(QStringView command)
{
    const QString name = command.toString();
    if (const auto it = latexTable().constFind(name); it != latexTable().constEnd()) {
        return *it;
    }
    const QString word = wordTable().value(name);
    if (word.isEmpty() || name.startsWith(u"double") || name.startsWith(u"fraktur") || name.startsWith(u"script")) {
        return {};
    }
    const QList<uint> chars = word.toUcs4();
    if (chars.size() != 1 || isCombiningMark(chars.first())
        || kStructural.find(char32_t(chars.first())) != std::u32string_view::npos) {
        return {};
    }
    return word;
}

QString latexCommandFor(char32_t c)
{
    static const QHash<char32_t, QString> reverse = [] {
        QHash<char32_t, QString> out;
        const auto add = [&out](const Entry &e) {
            const QList<uint> chars = e.text.toString().toUcs4();
            if (chars.size() == 1 && chars.first() > 0x7F && !out.contains(chars.first())) {
                out.insert(chars.first(), QLatin1Char('\\') + e.name.toString());
            }
        };
        for (const Entry &e : kLatex) {
            add(e);
        }
        // Greek: LaTeX has no command for the capitals that look Latin (Α, Β, …).
        static constexpr QStringView kGreekCapitals = u"ΓΔΘΛΞΠΣΥΦΨΩ";
        for (const Entry &e : kWord) {
            const QList<uint> chars = e.text.toString().toUcs4();
            if (chars.size() != 1 || !isGreek(chars.first()) || e.name.size() < 2) {
                continue;
            }
            const bool capital = chars.first() <= 0x03A9;
            if (!capital || kGreekCapitals.contains(QChar(char16_t(chars.first())))) {
                add(e);
            }
        }
        return out;
    }();
    return reverse.value(c);
}

} // namespace QindaOffice::Math
