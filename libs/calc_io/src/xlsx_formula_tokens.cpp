// SPDX-License-Identifier: GPL-3.0-or-later
// Structured references and '@' between the formula bar's spelling and the
// file's (xlsx_formula_tokens.h, M69, D-078).

#include "xlsx_formula_tokens.h"

#include "lexer.h"
#include "parser.h"
#include "structured_ref.h"

namespace QindaOffice::Calc::IO {

namespace {

bool isOp(const Token &token, QLatin1StringView op)
{
    return token.type == Token::Type::Op && token.text == op;
}

// The index of the token closing the bracket opened at tokens[open].
qsizetype matchingClose(const QVector<Token> &tokens, qsizetype open, QLatin1StringView opening, QLatin1StringView closing)
{
    int depth = 0;
    for (qsizetype i = open; i < tokens.size(); ++i) {
        if (isOp(tokens.at(i), opening)) {
            ++depth;
        } else if (isOp(tokens.at(i), closing) && --depth == 0) {
            return i;
        }
    }
    return -1;
}

qsizetype rangeOperandEnd(const QVector<Token> &tokens, qsizetype k);

// One primary of the parser's grammar (with its prefix signs) from
// tokens[k]; the index after it, or -1.
qsizetype primaryEnd(const QVector<Token> &tokens, qsizetype k)
{
    if (k < 0 || k >= tokens.size() - 1) {
        return -1; // the last token is End
    }
    const Token &token = tokens.at(k);
    switch (token.type) {
    case Token::Type::Number:
    case Token::Type::String:
    case Token::Type::ErrorLit:
    case Token::Type::Ref:
    case Token::Type::StructRef:
        return k + 1;
    case Token::Type::SheetName:
    case Token::Type::QuotedSheetName:
        return tokens.at(k + 1).type == Token::Type::Ref ? k + 2 : -1;
    case Token::Type::Identifier:
        if (isOp(tokens.at(k + 1), QLatin1StringView("("))) {
            const qsizetype close = matchingClose(tokens, k + 1, QLatin1StringView("("), QLatin1StringView(")"));
            return close < 0 ? -1 : close + 1;
        }
        return k + 1;
    case Token::Type::Op:
        if (token.text == QLatin1String("(") || token.text == QLatin1String("{")) {
            const bool brace = token.text == QLatin1String("{");
            const qsizetype close = matchingClose(tokens, k, QLatin1StringView(brace ? "{" : "("),
                                                  QLatin1StringView(brace ? "}" : ")"));
            return close < 0 ? -1 : close + 1;
        }
        if (token.text == QLatin1String("-") || token.text == QLatin1String("+")) {
            return primaryEnd(tokens, k + 1);
        }
        if (token.text == QLatin1String("@")) {
            return rangeOperandEnd(tokens, k + 1);
        }
        return -1;
    default:
        return -1;
    }
}

// What the parser's '@' takes (parseRangeOperand): primaries with their
// postfix % and #, joined by ':'.
qsizetype rangeOperandEnd(const QVector<Token> &tokens, qsizetype k)
{
    qsizetype end = primaryEnd(tokens, k);
    while (end >= 0) {
        while (end < tokens.size() && (isOp(tokens.at(end), QLatin1StringView("%")) || isOp(tokens.at(end), QLatin1StringView("#")))) {
            ++end;
        }
        if (end >= tokens.size() || !isOp(tokens.at(end), QLatin1StringView(":"))) {
            return end;
        }
        end = primaryEnd(tokens, end + 1);
    }
    return -1;
}

QString structRefs(const QString &text, const QString &containingTable, bool display)
{
    if (!text.contains(QLatin1Char('['))) {
        return text;
    }
    const QVector<Token> tokens = lexFormula(text);
    QString out;
    qsizetype copied = 0;
    for (const Token &token : tokens) {
        if (token.type != Token::Type::StructRef) {
            continue;
        }
        std::optional<StructRef> ref = parseStructRef(token.text);
        if (!ref) {
            continue; // not one Excel accepts either: left as written
        }
        if (display && !containingTable.isEmpty() && ref->table.compare(containingTable, Qt::CaseInsensitive) == 0) {
            ref->table.clear();
        }
        const QString spelled = structRefText(*ref, display ? StructRefSpelling::Display : StructRefSpelling::File,
                                              containingTable);
        out += QStringView(text).mid(copied, token.pos - copied);
        out += spelled;
        copied = token.pos + token.text.size();
    }
    out += QStringView(text).mid(copied);
    return out;
}

} // namespace

QString structRefsForDisplay(const QString &text, const QString &containingTable)
{
    return structRefs(text, containingTable, true);
}

QString structRefsForFile(const QString &text, const QString &containingTable)
{
    return structRefs(text, containingTable, false);
}

QString singleAsAt(const QString &text)
{
    if (!text.contains(QLatin1String("SINGLE"), Qt::CaseInsensitive)) {
        return text;
    }
    const QVector<Token> tokens = lexFormula(text);
    QString out;
    qsizetype copied = 0;
    for (qsizetype i = 0; i + 1 < tokens.size(); ++i) {
        const Token &token = tokens.at(i);
        if (token.type != Token::Type::Identifier || token.text.compare(QLatin1String("SINGLE"), Qt::CaseInsensitive) != 0
            || !isOp(tokens.at(i + 1), QLatin1StringView("("))) {
            continue;
        }
        const qsizetype close = matchingClose(tokens, i + 1, QLatin1StringView("("), QLatin1StringView(")"));
        // '@' binds to a range operand: where a ':' or a postfix touches the
        // call, the call's own spelling is the only safe one.
        const bool touched = close < 0 || (i > 0 && isOp(tokens.at(i - 1), QLatin1StringView(":")))
            || isOp(tokens.at(close + 1), QLatin1StringView(":")) || isOp(tokens.at(close + 1), QLatin1StringView("%"))
            || isOp(tokens.at(close + 1), QLatin1StringView("#"));
        if (touched) {
            continue;
        }
        const qsizetype innerStart = tokens.at(i + 1).pos + 1;
        const QString inner = text.mid(innerStart, tokens.at(close).pos - innerStart).trimmed();
        const ParseResult parsed = parseFormula(inner);
        if (inner.isEmpty() || !parsed.error.isEmpty() || !parsed.root) {
            continue;
        }
        const Node::Kind kind = parsed.root->kind;
        const bool bare = kind == Node::Kind::Ref || kind == Node::Kind::StructRef || kind == Node::Kind::Name
            || kind == Node::Kind::Call || kind == Node::Kind::Array
            || (kind == Node::Kind::Binary && parsed.root->op == Op::Range);
        const QString converted = singleAsAt(inner);
        out += QStringView(text).mid(copied, token.pos - copied);
        out += QLatin1Char('@') + (bare ? converted : QLatin1Char('(') + converted + QLatin1Char(')'));
        copied = tokens.at(close).pos + 1;
        i = close;
    }
    out += QStringView(text).mid(copied);
    return out;
}

QString atAsSingle(const QString &text)
{
    if (!text.contains(QLatin1Char('@'))) {
        return text;
    }
    const QVector<Token> tokens = lexFormula(text);
    QString out;
    qsizetype copied = 0;
    for (qsizetype i = 0; i + 1 < tokens.size(); ++i) {
        if (!isOp(tokens.at(i), QLatin1StringView("@"))) {
            continue;
        }
        const qsizetype end = rangeOperandEnd(tokens, i + 1);
        if (end < 0) {
            continue; // nothing after it: the formula does not parse anyway
        }
        const qsizetype from = tokens.at(i + 1).pos;
        const QString region = text.mid(from, tokens.at(end).pos - from);
        const QString inner = region.trimmed();
        out += QStringView(text).mid(copied, tokens.at(i).pos - copied);
        out += QStringLiteral("SINGLE(") + atAsSingle(inner) + QLatin1Char(')');
        out += region.mid(inner.size()); // the space after it (the region starts at a token)
        copied = tokens.at(end).pos;
        i = end - 1;
    }
    out += QStringView(text).mid(copied);
    return out;
}

} // namespace QindaOffice::Calc::IO
