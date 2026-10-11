// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_build_p.h"

#include "math_alphabets.h"

#include <QChar>

namespace QindaOffice::Math {

void AtomList::addChar(char32_t c, const RunStyle &style, bool operand)
{
    Atom a;
    a.ch = c;
    a.style = style;
    m_atoms.append(std::move(a));
    if (!operand) {
        breakSpan();
    }
}

void AtomList::addNode(NodePtr node, bool parens)
{
    Atom a;
    a.node = std::move(node);
    a.parens = parens;
    m_atoms.append(std::move(a));
}

void AtomList::addGroup(Argument inner)
{
    Atom a;
    a.group = true;
    a.inner = std::move(inner);
    m_atoms.append(std::move(a));
}

bool AtomList::endsWithLetter() const
{
    return !m_atoms.isEmpty() && m_atoms.last().isChar() && QChar::isLetter(m_atoms.last().ch);
}

QList<Atom> AtomList::takeSpan()
{
    const qsizetype start = qMin(m_spanStart, m_atoms.size());
    QList<Atom> out = m_atoms.mid(start);
    m_atoms.resize(start);
    m_spanStart = start;
    return out;
}

std::optional<Atom> AtomList::takeLast()
{
    if (m_atoms.isEmpty()) {
        return std::nullopt;
    }
    Atom a = m_atoms.takeLast();
    m_spanStart = qMin(m_spanStart, m_atoms.size());
    return a;
}

Argument AtomList::finish() const
{
    return toArgument(m_atoms, false);
}

Argument toArgument(const QList<Atom> &atoms, bool stripParens)
{
    if (stripParens && atoms.size() == 1 && atoms.first().parens && atoms.first().node
        && !atoms.first().node->args.isEmpty()) {
        return atoms.first().node->args.first();
    }
    Argument out;
    QString text;
    RunStyle style;
    const auto flush = [&]() {
        if (!text.isEmpty()) {
            out.items.append(styledRun(text, style));
        }
        text.clear();
    };
    for (const Atom &a : atoms) {
        if (a.isChar()) {
            if (!text.isEmpty() && !(a.style == style)) {
                flush();
            }
            style = a.style;
            text += QString::fromUcs4(&a.ch, 1);
        } else if (a.group) {
            flush();
            out.items.append(a.inner.items);
        } else {
            flush();
            out.items.append(a.node);
        }
    }
    flush();
    return out;
}

Argument atomArgument(const Atom &atom, bool stripParens)
{
    return toArgument(QList<Atom>{atom}, stripParens);
}

NodePtr styledRun(const QString &text, const RunStyle &style)
{
    NodePtr run = makeRun(text);
    if (style.normal) {
        setRunNormalText(*run, true);
        return run;
    }
    if (style.script != MathScript::Roman) {
        setRunScript(*run, style.script);
    }
    if (style.style != MathStyle::Default) {
        setRunStyle(*run, style.style);
    }
    return run;
}

RunStyle styleFor(char32_t c, char32_t *plain)
{
    RunStyle s;
    PlainLetter p;
    if (!plainLetter(c, &p)) {
        *plain = c;
        return s;
    }
    *plain = p.base;
    s.script = p.script;
    if (p.bold || p.italic != italicByDefault(p.base)) {
        s.style = p.bold ? (p.italic ? MathStyle::BoldItalic : MathStyle::Bold)
                         : (p.italic ? MathStyle::Italic : MathStyle::Plain);
    }
    return s;
}

NodePtr structure(NodeKind kind, QList<Argument> args)
{
    NodePtr node = makeNode(kind, int(qMax<qsizetype>(1, args.size())));
    for (qsizetype i = 0; i < args.size() && i < node->args.size(); ++i) {
        node->args[i] = std::move(args[i]);
    }
    if (kind == NodeKind::Delimiter || kind == NodeKind::EqArray) {
        node->args.resize(qMax<qsizetype>(1, args.size()));
    }
    return node;
}

} // namespace QindaOffice::Math
