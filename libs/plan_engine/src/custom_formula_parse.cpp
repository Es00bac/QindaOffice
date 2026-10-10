// SPDX-License-Identifier: GPL-3.0-or-later
// Project's custom-field formulas: the text into a tree (custom_formula_p.h).
// Precedence as in VBA, loosest first: Or/Xor, And, Not, comparisons, &,
// + -, Mod, \, * /, unary minus, ^.
#include "custom_formula_p.h"

#include <QLocale>

namespace QindaOffice::Plan::Formula {

namespace {

NodePtr make(Node::Kind kind, const QString &text, QList<NodePtr> args = {}, double number = 0)
{
    auto n = std::make_shared<Node>();
    n->kind = kind;
    n->text = text;
    n->args = std::move(args);
    n->number = number;
    return n;
}

class Parser {
public:
    explicit Parser(const QString &source)
        : m_s(source)
    {
        next();
    }
    NodePtr parse(QString *error)
    {
        NodePtr n = orExpr();
        if (m_error.isEmpty() && m_kind != End) {
            fail(QStringLiteral("“%1” is not expected there").arg(m_text));
        }
        *error = m_error;
        return m_error.isEmpty() ? n : nullptr;
    }

private:
    enum Kind { End, Num, Str, Fld, Ident, Op };

    void fail(const QString &why)
    {
        if (m_error.isEmpty()) {
            m_error = why;
        }
    }
    bool op(const char *o) const { return m_kind == Op && m_text == QLatin1String(o); }
    bool word(const char *w) const { return m_kind == Ident && m_text.compare(QLatin1String(w), Qt::CaseInsensitive) == 0; }
    void next()
    {
        const qsizetype n = m_s.size();
        while (m_pos < n && m_s.at(m_pos).isSpace()) {
            ++m_pos;
        }
        m_kind = End;
        m_text.clear();
        if (m_pos >= n) {
            return;
        }
        const QChar c = m_s.at(m_pos);
        const qsizetype start = m_pos;
        if (c.isDigit() || (c == QLatin1Char('.') && m_pos + 1 < n && m_s.at(m_pos + 1).isDigit())) {
            while (m_pos < n && (m_s.at(m_pos).isDigit() || m_s.at(m_pos) == QLatin1Char('.'))) {
                ++m_pos;
            }
            m_kind = Num;
            m_text = m_s.mid(start, m_pos - start);
            bool ok = false;
            m_number = QLocale::c().toDouble(m_text, &ok);
            if (!ok) {
                fail(QStringLiteral("“%1” is not a number").arg(m_text));
            }
        } else if (c == QLatin1Char('"')) {
            ++m_pos;
            while (true) {
                if (m_pos >= n) {
                    fail(QStringLiteral("a text is not closed with a quote"));
                    break;
                }
                if (m_s.at(m_pos) == QLatin1Char('"')) {
                    if (m_pos + 1 < n && m_s.at(m_pos + 1) == QLatin1Char('"')) {
                        m_text += QLatin1Char('"');
                        m_pos += 2;
                        continue;
                    }
                    ++m_pos;
                    break;
                }
                m_text += m_s.at(m_pos++);
            }
            m_kind = Str;
        } else if (c == QLatin1Char('[')) {
            const qsizetype close = m_s.indexOf(QLatin1Char(']'), m_pos);
            if (close < 0) {
                fail(QStringLiteral("a field name is not closed with ]"));
                m_pos = n;
                return;
            }
            m_kind = Fld;
            m_text = m_s.mid(m_pos + 1, close - m_pos - 1).simplified();
            m_pos = close + 1;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            while (m_pos < n && (m_s.at(m_pos).isLetterOrNumber() || m_s.at(m_pos) == QLatin1Char('_'))) {
                ++m_pos;
            }
            m_kind = Ident;
            m_text = m_s.mid(start, m_pos - start);
        } else if (m_s.mid(m_pos, 2) == QLatin1String("<>") || m_s.mid(m_pos, 2) == QLatin1String("<=")
                   || m_s.mid(m_pos, 2) == QLatin1String(">=")) {
            m_kind = Op;
            m_text = m_s.mid(m_pos, 2);
            m_pos += 2;
        } else if (QStringLiteral("+-*/\\^&=<>(),").contains(c)) {
            m_kind = Op;
            m_text = c;
            ++m_pos;
        } else {
            fail(QStringLiteral("“%1” is not part of a formula").arg(c));
            m_pos = n;
        }
    }
    NodePtr binary(const QString &o, const NodePtr &a, const NodePtr &b) { return make(Node::Binary, o, {a, b}); }
    NodePtr orExpr()
    {
        NodePtr n = andExpr();
        while (m_error.isEmpty() && (word("Or") || word("Xor"))) {
            const QString o = m_text.toLower();
            next();
            n = binary(o, n, andExpr());
        }
        return n;
    }
    NodePtr andExpr()
    {
        NodePtr n = notExpr();
        while (m_error.isEmpty() && word("And")) {
            next();
            n = binary(QStringLiteral("and"), n, notExpr());
        }
        return n;
    }
    NodePtr notExpr()
    {
        if (word("Not")) {
            next();
            return make(Node::Unary, QStringLiteral("not"), {notExpr()});
        }
        return compare();
    }
    NodePtr compare()
    {
        NodePtr n = concat();
        while (m_error.isEmpty() && (op("=") || op("<>") || op("<") || op(">") || op("<=") || op(">="))) {
            const QString o = m_text;
            next();
            n = binary(o, n, concat());
        }
        return n;
    }
    NodePtr concat()
    {
        NodePtr n = additive();
        while (m_error.isEmpty() && op("&")) {
            next();
            n = binary(QStringLiteral("&"), n, additive());
        }
        return n;
    }
    NodePtr additive()
    {
        NodePtr n = modTerm();
        while (m_error.isEmpty() && (op("+") || op("-"))) {
            const QString o = m_text;
            next();
            n = binary(o, n, modTerm());
        }
        return n;
    }
    NodePtr modTerm()
    {
        NodePtr n = idivTerm();
        while (m_error.isEmpty() && word("Mod")) {
            next();
            n = binary(QStringLiteral("mod"), n, idivTerm());
        }
        return n;
    }
    NodePtr idivTerm()
    {
        NodePtr n = mulTerm();
        while (m_error.isEmpty() && op("\\")) {
            next();
            n = binary(QStringLiteral("\\"), n, mulTerm());
        }
        return n;
    }
    NodePtr mulTerm()
    {
        NodePtr n = negTerm();
        while (m_error.isEmpty() && (op("*") || op("/"))) {
            const QString o = m_text;
            next();
            n = binary(o, n, negTerm());
        }
        return n;
    }
    NodePtr negTerm()
    {
        if (op("-") || op("+")) {
            const bool minus = op("-");
            next();
            NodePtr inner = negTerm();
            return minus ? make(Node::Unary, QStringLiteral("neg"), {inner}) : inner;
        }
        NodePtr n = primary();
        while (m_error.isEmpty() && op("^")) {
            next();
            const bool minus = op("-");
            if (minus) {
                next();
            }
            NodePtr rhs = primary();
            n = binary(QStringLiteral("^"), n, minus ? make(Node::Unary, QStringLiteral("neg"), {rhs}) : rhs);
        }
        return n;
    }
    NodePtr primary()
    {
        if (!m_error.isEmpty()) {
            return make(Node::Number, {});
        }
        if (m_kind == Num) {
            const double v = m_number;
            next();
            return make(Node::Number, {}, {}, v);
        }
        if (m_kind == Str || m_kind == Fld) {
            NodePtr n = make(m_kind == Str ? Node::Text : Node::Field, m_text);
            next();
            return n;
        }
        if (op("(")) {
            next();
            NodePtr n = orExpr();
            if (!op(")")) {
                fail(QStringLiteral("a “(” is not closed"));
            }
            next();
            return n;
        }
        if (m_kind == Ident) {
            const QString name = m_text.toLower();
            next();
            QList<NodePtr> args;
            if (op("(")) {
                next();
                while (m_error.isEmpty() && !op(")")) {
                    args.append(orExpr());
                    if (op(",")) {
                        next();
                    } else if (!op(")")) {
                        fail(QStringLiteral("a “,” or “)” is missing after an argument of %1").arg(name));
                    }
                }
                next();
            }
            return make(Node::Call, name, args);
        }
        fail(m_kind == End ? QStringLiteral("the formula ends too early")
                           : QStringLiteral("“%1” is not expected there").arg(m_text));
        return make(Node::Number, {});
    }

    QString m_s;
    qsizetype m_pos = 0;
    Kind m_kind = End;
    QString m_text;
    double m_number = 0;
    QString m_error;
};

} // namespace

NodePtr parseFormula(const QString &source, QString *error)
{
    Parser parser(source);
    return parser.parse(error);
}

} // namespace QindaOffice::Plan::Formula
