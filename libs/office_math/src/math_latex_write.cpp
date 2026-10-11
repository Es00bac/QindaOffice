// SPDX-License-Identifier: GPL-3.0-or-later
// LaTeX out (math_latex.h): the model in the forms math_latex_parse.cpp reads.
#include "math_latex.h"
#include "math_omml.h"
#include "math_props.h"
#include "math_symbols.h"

#include <QHash>
#include <QStringList>

namespace QindaOffice::Math {

namespace {

QString latex(const Argument &arg);

char32_t first(const QString &s)
{
    const QList<uint> chars = s.toUcs4();
    return chars.isEmpty() ? 0 : char32_t(chars.first());
}

QString braced(const Argument &arg)
{
    return QLatin1Char('{') + latex(arg) + QLatin1Char('}');
}

QString charLatex(char32_t c)
{
    switch (c) {
    case U'{':
        return QStringLiteral("\\{");
    case U'}':
        return QStringLiteral("\\}");
    case U'#':
    case U'$':
    case U'%':
    case U'_':
        return QLatin1Char('\\') + QString::fromUcs4(&c, 1);
    case U'\\':
        return QStringLiteral("\\backslash ");
    case U'^':
        return QStringLiteral("\\hat{}");
    case U'~':
        return QStringLiteral("\\sim ");
    case 0x2009:
    case 0x2006:
        return QStringLiteral("\\,");
    case 0x205F:
        return QStringLiteral("\\:");
    case 0x2004:
    case 0x2005:
        return QStringLiteral("\\;");
    case 0x2003:
        return QStringLiteral("\\quad ");
    case 0x00A0:
        return QStringLiteral("~");
    case 0x2061:
    case 0x2062:
        return {};
    default:
        break;
    }
    if (const QString command = latexCommandFor(c); !command.isEmpty()) {
        return command;
    }
    return QString::fromUcs4(&c, 1);
}

QString styleCommand(const Node &run)
{
    switch (runScript(run)) {
    case MathScript::DoubleStruck:
        return QStringLiteral("\\mathbb");
    case MathScript::Script:
        return QStringLiteral("\\mathcal");
    case MathScript::Fraktur:
        return QStringLiteral("\\mathfrak");
    case MathScript::SansSerif:
        return QStringLiteral("\\mathsf");
    case MathScript::Monospace:
        return QStringLiteral("\\mathtt");
    case MathScript::Roman:
        break;
    }
    switch (runStyle(run)) {
    case MathStyle::Plain:
        return QStringLiteral("\\mathrm");
    case MathStyle::Bold:
        return QStringLiteral("\\mathbf");
    case MathStyle::Italic:
        return QStringLiteral("\\mathit");
    case MathStyle::BoldItalic:
        return QStringLiteral("\\boldsymbol");
    case MathStyle::Default:
        break;
    }
    return {};
}

QString runLatex(const Node &run)
{
    const QString text = run.text();
    if (runIsNormalText(run)) {
        return QStringLiteral("\\text{") + text + QLatin1Char('}');
    }
    QString body;
    for (const uint c : text.toUcs4()) {
        const QString part = charLatex(c);
        // A command name runs into a following letter: "\alpha x".
        if (!part.isEmpty() && part.at(0).isLetter() && body.contains(QLatin1Char('\\'))) {
            const qsizetype slash = body.lastIndexOf(QLatin1Char('\\'));
            bool command = slash + 1 < body.size();
            for (qsizetype i = slash + 1; i < body.size() && command; ++i) {
                command = body.at(i).isLetter();
            }
            if (command) {
                body += QLatin1Char(' ');
            }
        }
        body += part;
    }
    const QString style = styleCommand(run);
    return style.isEmpty() ? body : style + QLatin1Char('{') + body + QLatin1Char('}');
}

QString fence(const QString &chr)
{
    if (chr.isEmpty()) {
        return QStringLiteral(".");
    }
    const char32_t c = first(chr);
    if (c == U'{' || c == U'}') {
        return QLatin1Char('\\') + chr;
    }
    if (c == U'‖') {
        return QStringLiteral("\\|");
    }
    const QString command = latexCommandFor(c);
    return command.isEmpty() ? chr : command;
}

const QHash<char32_t, QString> &accentCommands()
{
    static const QHash<char32_t, QString> table{
        {0x0302, QStringLiteral("\\hat")},  {0x030C, QStringLiteral("\\check")}, {0x0303, QStringLiteral("\\tilde")},
        {0x0301, QStringLiteral("\\acute")}, {0x0300, QStringLiteral("\\grave")}, {0x0307, QStringLiteral("\\dot")},
        {0x0308, QStringLiteral("\\ddot")}, {0x20DB, QStringLiteral("\\dddot")}, {0x0306, QStringLiteral("\\breve")},
        {0x0305, QStringLiteral("\\bar")},  {0x0304, QStringLiteral("\\bar")},   {0x20D7, QStringLiteral("\\vec")},
        {0x20D6, QStringLiteral("\\overleftarrow")}, {0x20E1, QStringLiteral("\\overleftrightarrow")},
        {0x030A, QStringLiteral("\\mathring")},
    };
    return table;
}

QString scriptBase(const Argument &arg)
{
    if (arg.items.size() == 1 && arg.items.first()->kind == NodeKind::Run && !runIsNormalText(*arg.items.first())
        && arg.items.first()->text().toUcs4().size() == 1 && styleCommand(*arg.items.first()).isEmpty()) {
        return latex(arg);
    }
    return braced(arg);
}

QString environment(const QString &name, const Node &array)
{
    QStringList rows;
    if (array.kind == NodeKind::Matrix) {
        qsizetype cell = 0;
        for (const int count : array.rowSizes) {
            QStringList cells;
            for (int c = 0; c < count && cell < array.args.size(); ++c) {
                cells.append(latex(array.args.at(cell++)));
            }
            rows.append(cells.join(QStringLiteral(" & ")));
        }
    } else {
        for (const Argument &row : array.args) {
            rows.append(latex(row));
        }
    }
    return QStringLiteral("\\begin{") + name + QStringLiteral("} ") + rows.join(QStringLiteral(" \\\\ "))
        + QStringLiteral(" \\end{") + name + QLatin1Char('}');
}

// \binom, \begin{pmatrix}, \begin{cases}: a delimiter LaTeX names itself.
QString delimiterLatex(const Node &d)
{
    const QString begin = delimiterBegin(d);
    const QString end = delimiterEnd(d);
    if (d.args.size() == 1 && d.args.first().items.size() == 1) {
        const Node &inner = *d.args.first().items.first();
        if (inner.kind == NodeKind::Matrix) {
            static const QHash<QString, QString> kinds{
                {QStringLiteral("()"), QStringLiteral("pmatrix")}, {QStringLiteral("[]"), QStringLiteral("bmatrix")},
                {QStringLiteral("{}"), QStringLiteral("Bmatrix")}, {QStringLiteral("||"), QStringLiteral("vmatrix")},
                {QStringLiteral("‖‖"), QStringLiteral("Vmatrix")},
            };
            if (const QString kind = kinds.value(begin + end); !kind.isEmpty()) {
                return environment(kind, inner);
            }
        }
        if (inner.kind == NodeKind::EqArray && begin == u"{" && end.isEmpty()) {
            return environment(QStringLiteral("cases"), inner);
        }
        if (inner.kind == NodeKind::Fraction && fractionType(inner) == FractionType::NoBar && begin == u"("
            && end == u")") {
            return QStringLiteral("\\binom") + braced(inner.args.value(0)) + braced(inner.args.value(1));
        }
    }
    QStringList parts;
    for (const Argument &e : d.args) {
        parts.append(latex(e));
    }
    if (!delimiterGrows(d)) {
        return fence(begin) + parts.join(fence(delimiterSeparator(d))) + fence(end);
    }
    const QString middle = QStringLiteral(" \\middle") + fence(delimiterSeparator(d)) + QLatin1Char(' ');
    return QStringLiteral("\\left") + fence(begin) + QLatin1Char(' ') + parts.join(middle) + QStringLiteral(" \\right")
        + fence(end);
}

QString functionLatex(const Node &f)
{
    const Argument name = f.args.value(0);
    QString head;
    if (name.items.size() == 1) {
        const Node &n = *name.items.first();
        const auto word = [](const Node &run) {
            const QString t = run.text();
            return isFunctionName(t) ? QLatin1Char('\\') + t : QStringLiteral("\\operatorname{") + t + QLatin1Char('}');
        };
        const bool simple = !n.args.isEmpty() && n.args.first().items.size() == 1
            && n.args.first().items.first()->kind == NodeKind::Run;
        if (n.kind == NodeKind::Run) {
            head = word(n);
        } else if (simple && n.kind == NodeKind::Superscript) {
            head = word(*n.args.first().items.first()) + QLatin1Char('^') + braced(n.args.at(1));
        } else if (simple && (n.kind == NodeKind::Subscript || n.kind == NodeKind::LimLow)) {
            head = word(*n.args.first().items.first()) + QLatin1Char('_') + braced(n.args.at(1));
        }
    }
    if (head.isEmpty()) {
        head = QStringLiteral("\\operatorname{") + latex(name) + QLatin1Char('}');
    }
    return head + braced(f.args.value(1));
}

QString phantomLatex(const Node &n)
{
    const PhantomFlags f = phantomFlags(n);
    QString command = QStringLiteral("\\phantom");
    if (f.show && f.zeroAscent && f.zeroDescent) {
        command = QStringLiteral("\\smash");
    } else if (!f.show && f.zeroAscent && f.zeroDescent) {
        command = QStringLiteral("\\hphantom");
    } else if (!f.show && f.zeroWidth) {
        command = QStringLiteral("\\vphantom");
    }
    return command + braced(n.args.value(0));
}

QString node(const Node &n)
{
    const auto arg = [&n](int i) { return n.args.value(i); };
    switch (n.kind) {
    case NodeKind::Run:
        return runLatex(n);
    case NodeKind::Fraction:
        switch (fractionType(n)) {
        case FractionType::Skewed:
            return QStringLiteral("\\sfrac") + braced(arg(0)) + braced(arg(1));
        case FractionType::Linear:
            return braced(arg(0)) + QLatin1Char('/') + braced(arg(1));
        case FractionType::NoBar:
            return QStringLiteral("\\genfrac{}{}{0pt}{}") + braced(arg(0)) + braced(arg(1));
        case FractionType::Bar:
            break;
        }
        return QStringLiteral("\\frac") + braced(arg(0)) + braced(arg(1));
    case NodeKind::Superscript:
        return scriptBase(arg(0)) + QLatin1Char('^') + braced(arg(1));
    case NodeKind::Subscript:
        return scriptBase(arg(0)) + QLatin1Char('_') + braced(arg(1));
    case NodeKind::SubSup:
        return scriptBase(arg(0)) + QLatin1Char('_') + braced(arg(1)) + QLatin1Char('^') + braced(arg(2));
    case NodeKind::PreScript:
        return QStringLiteral("{}_") + braced(arg(0)) + QLatin1Char('^') + braced(arg(1)) + braced(arg(2));
    case NodeKind::Radical:
        if (degreeHidden(n)) {
            return QStringLiteral("\\sqrt") + braced(arg(1));
        }
        return QStringLiteral("\\sqrt[") + latex(arg(0)) + QLatin1Char(']') + braced(arg(1));
    case NodeKind::Nary: {
        const char32_t op = first(naryChar(n));
        QString command = latexCommandFor(op);
        if (command.isEmpty()) {
            command = naryChar(n);
        }
        const LimitLocation location = limitLocation(n);
        if (location != LimitLocation::Default && (location == LimitLocation::UnderOver) == isIntegral(op)) {
            command += location == LimitLocation::UnderOver ? QStringLiteral("\\limits") : QStringLiteral("\\nolimits");
        }
        if (!subHidden(n)) {
            command += QLatin1Char('_') + braced(arg(0));
        }
        if (!supHidden(n)) {
            command += QLatin1Char('^') + braced(arg(1));
        }
        return command + braced(arg(2));
    }
    case NodeKind::Delimiter:
        return delimiterLatex(n);
    case NodeKind::Function:
        return functionLatex(n);
    case NodeKind::Accent: {
        const QString command = accentCommands().value(first(accentChar(n)));
        if (command.isEmpty()) {
            return QStringLiteral("\\overset{") + accentChar(n) + QLatin1Char('}') + braced(arg(0));
        }
        return command + braced(arg(0));
    }
    case NodeKind::Bar:
        return (barPosition(n) == VerticalPosition::Top ? QStringLiteral("\\overline") : QStringLiteral("\\underline"))
            + braced(arg(0));
    case NodeKind::Box:
        return braced(arg(0));
    case NodeKind::BorderBox:
        return QStringLiteral("\\boxed") + braced(arg(0));
    case NodeKind::EqArray:
        return environment(QStringLiteral("aligned"), n);
    case NodeKind::Matrix:
        return environment(QStringLiteral("matrix"), n);
    case NodeKind::GroupChar: {
        const char32_t c = first(groupChar(n));
        if (c == U'⏞') {
            return QStringLiteral("\\overbrace") + braced(arg(0));
        }
        if (c == U'⏟') {
            return QStringLiteral("\\underbrace") + braced(arg(0));
        }
        const QString over = groupCharPosition(n) == VerticalPosition::Top ? QStringLiteral("\\overset{")
                                                                            : QStringLiteral("\\underset{");
        return over + charLatex(c) + QLatin1Char('}') + braced(arg(0));
    }
    case NodeKind::LimLow:
    case NodeKind::LimUpp: {
        const bool low = n.kind == NodeKind::LimLow;
        const Argument base = arg(0);
        if (base.items.size() == 1 && base.items.first()->kind == NodeKind::GroupChar) {
            const char32_t c = first(groupChar(*base.items.first()));
            if ((low && c == U'⏟') || (!low && c == U'⏞')) {
                return node(*base.items.first()) + (low ? QLatin1Char('_') : QLatin1Char('^')) + braced(arg(1));
            }
        }
        return (low ? QStringLiteral("\\underset") : QStringLiteral("\\overset")) + braced(arg(1)) + braced(base);
    }
    case NodeKind::Phantom:
        return phantomLatex(n);
    case NodeKind::Kept:
        if (const std::optional<Argument> shown = keptMath(n)) {
            return latex(*shown);
        }
        return {};
    }
    return {};
}

QString latex(const Argument &arg)
{
    QString out;
    for (const NodePtr &item : arg.items) {
        const QString part = node(*item);
        // "\alpha x": a command name must not run into the next letter.
        if (!part.isEmpty() && part.at(0).isLetter() && !out.isEmpty()) {
            const qsizetype slash = out.lastIndexOf(QLatin1Char('\\'));
            bool command = slash >= 0 && slash + 1 < out.size();
            for (qsizetype i = slash + 1; command && i < out.size(); ++i) {
                command = out.at(i).isLetter();
            }
            if (command) {
                out += QLatin1Char(' ');
            }
        }
        out += part;
    }
    return out;
}

} // namespace

QString toLatex(const Argument &argument)
{
    return latex(argument);
}

QString toLatex(const Equation &equation)
{
    QStringList lines;
    for (const Argument &line : equation.lines) {
        lines.append(latex(line));
    }
    return lines.join(QStringLiteral(" \\\\ "));
}

} // namespace QindaOffice::Math
