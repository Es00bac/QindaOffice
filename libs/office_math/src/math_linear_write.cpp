// SPDX-License-Identifier: GPL-3.0-or-later
// UnicodeMath out (math_linear.h): the model as Word's linear format, in
// the forms math_linear_parse.cpp builds back up.
#include "math_alphabets.h"
#include "math_linear.h"
#include "math_omml.h"
#include "math_props.h"
#include "math_symbols.h"

#include <QStringList>

#include <string_view>

namespace QindaOffice::Math {

namespace {

QString linear(const Argument &arg);

// Characters a run must quote with a backslash so they stay text.
constexpr std::u32string_view kQuoted = U"/^_()[]{}|‖¦⊘∕┬┴▒■█√∛∜〖〗├┤@\"\\⟨⟩〈〉⌊⌋⌈⌉⟦⟧⟪⟫Ⓒ⒨⒩¯▁□▭⏞⏟⏜⏝⎴⎵⏠⏡⟡⬄⇳⬍⬆⬇⬌│∣ \u2061";

char32_t first(const QString &s)
{
    const QList<uint> chars = s.toUcs4();
    return chars.isEmpty() ? 0 : char32_t(chars.first());
}

QString text(char32_t c)
{
    return QString::fromUcs4(&c, 1);
}

QString runText(const Node &run)
{
    const QString source = run.text();
    if (runIsNormalText(run)) {
        QString quoted = source;
        quoted.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        quoted.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    const MathStyle style = runStyle(run);
    const MathScript script = runScript(run);
    const bool bold = style == MathStyle::Bold || style == MathStyle::BoldItalic;
    QString out;
    const QList<uint> chars = source.toUcs4();
    for (qsizetype i = 0; i < chars.size(); ++i) {
        char32_t c = chars.at(i);
        const bool italic = style == MathStyle::Italic || style == MathStyle::BoldItalic
            || (style == MathStyle::Default && italicByDefault(c));
        if (bold || script != MathScript::Roman || (style == MathStyle::Italic && !italicByDefault(c))) {
            c = mathAlphanumeric(c, script, bold, italic);
        }
        // A function name in plain letters would come back as a function.
        bool startsName = false;
        if (QChar::isLetter(c) && (i == 0 || !QChar::isLetter(chars.at(i - 1)))) {
            QString tail;
            for (qsizetype j = i; j < chars.size() && j < i + 8; ++j) {
                tail += text(chars.at(j));
            }
            startsName = functionNameAt(tail) > 0;
        }
        if (startsName || kQuoted.find(c) != std::u32string_view::npos || isNaryOperator(c) || isCombiningMark(c)
            || isPrime(c)) {
            out += QLatin1Char('\\');
        }
        out += text(c);
    }
    return out;
}

bool isSimple(const Argument &arg)
{
    if (arg.items.size() != 1 || arg.items.first()->kind != NodeKind::Run) {
        return false;
    }
    const Node &run = *arg.items.first();
    const QString t = run.text();
    if (t.isEmpty() || runIsNormalText(run) || functionNameAt(t) > 0) {
        return false;
    }
    for (const uint c : t.toUcs4()) {
        if (!isAlphanumeric(c) && c != U'.') {
            return false;
        }
    }
    return true;
}

bool isSingleChar(const Argument &arg)
{
    return arg.items.size() == 1 && arg.items.first()->kind == NodeKind::Run && !runIsNormalText(*arg.items.first())
        && arg.items.first()->text().toUcs4().size() == 1;
}

// Structures that come back as one atom without a group around them.
bool selfDelimiting(const Argument &arg)
{
    if (arg.items.size() != 1) {
        return false;
    }
    const Node &n = *arg.items.first();
    switch (n.kind) {
    case NodeKind::Delimiter:
    case NodeKind::Radical:
    case NodeKind::Matrix:
    case NodeKind::EqArray:
    case NodeKind::Bar:
    case NodeKind::Box:
    case NodeKind::BorderBox:
    case NodeKind::Phantom:
    case NodeKind::Accent:
        return true;
    case NodeKind::GroupChar:
        return std::u32string_view(U"⏞⏟⏜⏝⎴⎵⏠⏡").find(first(groupChar(n))) != std::u32string_view::npos;
    default:
        return false;
    }
}

bool isParens(const Argument &arg)
{
    if (arg.items.size() != 1 || arg.items.first()->kind != NodeKind::Delimiter) {
        return false;
    }
    const Node &d = *arg.items.first();
    return d.args.size() == 1 && delimiterBegin(d) == u"(" && delimiterEnd(d) == u")";
}

// An operand the parser strips parentheses from (fractions, scripts, √).
QString stripped(const Argument &arg)
{
    if (isSimple(arg) || (selfDelimiting(arg) && !isParens(arg))) {
        return linear(arg);
    }
    return QLatin1Char('(') + linear(arg) + QLatin1Char(')');
}

// An operand the parser keeps parentheses on (n-ary and function arguments).
QString grouped(const Argument &arg)
{
    if (isSimple(arg) || selfDelimiting(arg)) {
        return linear(arg);
    }
    return QStringLiteral("〖") + linear(arg) + QStringLiteral("〗");
}

// A script's, limit's or accent's base: the last atom only.
QString base(const Argument &arg)
{
    if (isSingleChar(arg) || selfDelimiting(arg)) {
        return linear(arg);
    }
    return QStringLiteral("〖") + linear(arg) + QStringLiteral("〗");
}

QString functionName(const Argument &name)
{
    if (name.items.size() == 1) {
        const Node &n = *name.items.first();
        if (n.kind == NodeKind::Run && !runIsNormalText(n)) {
            return n.text();
        }
        const bool word = !n.args.isEmpty() && n.args.first().items.size() == 1
            && n.args.first().items.first()->kind == NodeKind::Run;
        if (word && n.kind == NodeKind::Superscript) {
            return n.args.first().items.first()->text() + QLatin1Char('^') + stripped(n.args.at(1));
        }
        if (word && n.kind == NodeKind::Subscript) {
            return n.args.first().items.first()->text() + QLatin1Char('_') + stripped(n.args.at(1));
        }
        if (word && n.kind == NodeKind::LimLow) {
            return n.args.first().items.first()->text() + QStringLiteral("┬") + stripped(n.args.at(1));
        }
    }
    return base(name);
}

QString phantomChar(const Node &n)
{
    const PhantomFlags f = phantomFlags(n);
    if (!f.show) {
        if (f.zeroAscent && f.zeroDescent && !f.zeroWidth) {
            return QStringLiteral("⬄");
        }
        return f.zeroWidth && !f.zeroAscent && !f.zeroDescent ? QStringLiteral("⇳") : QStringLiteral("⟡");
    }
    if (f.zeroWidth) {
        return QStringLiteral("⬌");
    }
    if (f.zeroAscent && f.zeroDescent) {
        return QStringLiteral("⬍");
    }
    return f.zeroAscent ? QStringLiteral("⬆") : QStringLiteral("⬇");
}

QString fence(const QString &chr, bool open)
{
    if (chr.isEmpty()) {
        return open ? QStringLiteral("├") : QStringLiteral("┤");
    }
    return chr;
}

QString node(const Node &n)
{
    const auto arg = [&n](int i) { return i < n.args.size() ? n.args.at(i) : Argument(); };
    switch (n.kind) {
    case NodeKind::Run:
        return runText(n);
    case NodeKind::Fraction: {
        static const QStringList ops{QStringLiteral("/"), QStringLiteral("∕"), QStringLiteral("⊘"),
                                     QStringLiteral("¦")};
        return stripped(arg(0)) + ops.at(int(fractionType(n))) + stripped(arg(1));
    }
    case NodeKind::Superscript: {
        const QString sup = linear(arg(1));
        const bool primes = !sup.isEmpty() && isSingleChar(arg(1)) && isPrime(first(arg(1).items.first()->text()));
        return base(arg(0)) + (primes ? arg(1).items.first()->text() : QLatin1Char('^') + stripped(arg(1)));
    }
    case NodeKind::Subscript:
        return base(arg(0)) + QLatin1Char('_') + stripped(arg(1));
    case NodeKind::SubSup:
        return base(arg(0)) + QLatin1Char('_') + stripped(arg(1)) + QLatin1Char('^') + stripped(arg(2));
    case NodeKind::PreScript:
        return QStringLiteral(" _") + stripped(arg(0)) + QLatin1Char('^') + stripped(arg(1)) + QLatin1Char(' ')
            + stripped(arg(2));
    case NodeKind::Radical:
        if (degreeHidden(n)) {
            return QStringLiteral("√(") + linear(arg(1)) + QLatin1Char(')');
        }
        return QStringLiteral("√(") + linear(arg(0)) + QLatin1Char('&') + linear(arg(1)) + QLatin1Char(')');
    case NodeKind::Nary: {
        const char32_t op = first(naryChar(n));
        const bool underOver = isIntegral(op) && limitLocation(n) == LimitLocation::UnderOver;
        QString out = naryChar(n);
        if (!subHidden(n)) {
            out += (underOver ? QStringLiteral("┬") : QStringLiteral("_")) + stripped(arg(0));
        }
        if (!supHidden(n)) {
            out += (underOver ? QStringLiteral("┴") : QStringLiteral("^")) + stripped(arg(1));
        }
        return out + QStringLiteral("▒") + grouped(arg(2));
    }
    case NodeKind::Delimiter: {
        const QString sep = delimiterSeparator(n) == u"|" ? QStringLiteral("∣") : QStringLiteral("│");
        QStringList parts;
        for (const Argument &e : n.args) {
            parts.append(linear(e));
        }
        return fence(delimiterBegin(n), true) + parts.join(sep) + fence(delimiterEnd(n), false);
    }
    case NodeKind::Function:
        return functionName(arg(0)) + QStringLiteral("\u2061") + grouped(arg(1));
    case NodeKind::Accent: {
        const QString e = isSingleChar(arg(0)) ? linear(arg(0)) : QLatin1Char('(') + linear(arg(0)) + QLatin1Char(')');
        return e + accentChar(n);
    }
    case NodeKind::Bar:
        return (barPosition(n) == VerticalPosition::Top ? QStringLiteral("¯(") : QStringLiteral("▁("))
            + linear(arg(0)) + QLatin1Char(')');
    case NodeKind::Box:
        return QStringLiteral("□(") + linear(arg(0)) + QLatin1Char(')');
    case NodeKind::BorderBox:
        return QStringLiteral("▭(") + linear(arg(0)) + QLatin1Char(')');
    case NodeKind::EqArray: {
        QStringList rows;
        for (const Argument &row : n.args) {
            rows.append(linear(row));
        }
        return QStringLiteral("█(") + rows.join(QLatin1Char('@')) + QLatin1Char(')');
    }
    case NodeKind::Matrix: {
        QStringList rows;
        qsizetype cell = 0;
        for (const int count : n.rowSizes) {
            QStringList cells;
            for (int c = 0; c < count && cell < n.args.size(); ++c) {
                cells.append(linear(n.args.at(cell++)));
            }
            rows.append(cells.join(QLatin1Char('&')));
        }
        return QStringLiteral("■(") + rows.join(QLatin1Char('@')) + QLatin1Char(')');
    }
    case NodeKind::GroupChar: {
        const QString chr = groupChar(n);
        const bool top = groupCharPosition(n) == VerticalPosition::Top;
        if (std::u32string_view(U"⏞⏟⏜⏝⎴⎵⏠⏡").find(first(chr)) != std::u32string_view::npos) {
            return chr + QLatin1Char('(') + linear(arg(0)) + QLatin1Char(')');
        }
        return base(arg(0)) + (top ? QStringLiteral("┴") : QStringLiteral("┬")) + chr;
    }
    case NodeKind::LimLow:
    case NodeKind::LimUpp: {
        // A brace's label keeps the brace's own form: ⏞(…)^n, ⏟(…)_n.
        const bool low = n.kind == NodeKind::LimLow;
        if (selfDelimiting(arg(0)) && arg(0).items.first()->kind == NodeKind::GroupChar) {
            return linear(arg(0)) + (low ? QLatin1Char('_') : QLatin1Char('^')) + stripped(arg(1));
        }
        return base(arg(0)) + (low ? QStringLiteral("┬") : QStringLiteral("┴")) + stripped(arg(1));
    }
    case NodeKind::Phantom:
        return phantomChar(n) + QLatin1Char('(') + linear(arg(0)) + QLatin1Char(')');
    case NodeKind::Kept:
        if (const std::optional<Argument> shown = keptMath(n)) {
            return linear(*shown);
        }
        return {};
    }
    return {};
}

bool endsOperand(const QString &s)
{
    const char32_t c = s.isEmpty() ? 0 : char32_t(s.toUcs4().last());
    return isAlphanumeric(c) || c == U'.' || isClosingBracket(c) || c == U'|' || c == U'‖' || isPrime(c)
        || isCombiningMark(c);
}

bool startsOperand(const QString &s)
{
    const char32_t c = first(s);
    return isAlphanumeric(c) || c == U'.' || isOpeningBracket(c) || c == U'|' || c == U'‖' || c == U'"'
        || c == U'\\' || c == U'√' || c == U'■' || c == U'█' || isNaryOperator(c)
        || std::u32string_view(U"¯▁□▭⏞⏟⏜⏝⎴⎵⏠⏡⟡⬄⇳⬍⬆⬇⬌").find(c) != std::u32string_view::npos;
}

QString linear(const Argument &arg)
{
    QString out;
    bool previousRun = false;
    for (const NodePtr &item : arg.items) {
        const QString part = node(*item);
        const bool run = item->kind == NodeKind::Run && !runIsNormalText(*item);
        // A space keeps two atoms from reading as one operand (spaces are
        // separators in UnicodeMath and are dropped when read back).
        if (!out.isEmpty() && !part.isEmpty() && !(run && previousRun) && endsOperand(out) && startsOperand(part)) {
            out += QLatin1Char(' ');
        }
        out += part;
        previousRun = run;
    }
    return out;
}

} // namespace

QString toLinear(const Argument &argument)
{
    QString out = linear(argument);
    if (out.startsWith(QLatin1Char(' '))) {
        out.remove(0, 1); // a leading prescript's separator
    }
    return out;
}

QString toLinear(const Equation &equation)
{
    QStringList lines;
    for (const Argument &line : equation.lines) {
        lines.append(toLinear(line));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace QindaOffice::Math
