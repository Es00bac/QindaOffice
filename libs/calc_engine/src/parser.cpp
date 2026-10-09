// SPDX-License-Identifier: GPL-3.0-or-later
#include "parser.h"

#include "lexer.h"

namespace QindaOffice::Calc {

NodePtr Node::makeNumber(double v)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Number;
    n->number = v;
    return n;
}
NodePtr Node::makeText(const QString &s)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Text;
    n->text = s;
    return n;
}
NodePtr Node::makeBool(bool b)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Bool;
    n->boolean = b;
    return n;
}
NodePtr Node::makeError(Err e)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Error;
    n->error = e;
    return n;
}
NodePtr Node::makeRef(const RangeRef &r)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Ref;
    n->ref = r;
    return n;
}
NodePtr Node::makeUnary(Op op, NodePtr operand)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Unary;
    n->op = op;
    n->left = std::move(operand);
    return n;
}
NodePtr Node::makeBinary(Op op, NodePtr lhs, NodePtr rhs)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Binary;
    n->op = op;
    n->left = std::move(lhs);
    n->right = std::move(rhs);
    return n;
}
NodePtr Node::makeCall(const QString &name, QVector<NodePtr> args)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Call;
    n->text = name.toUpper();
    n->args = std::move(args);
    return n;
}
NodePtr Node::makeName(const QString &identifier)
{
    auto n = std::make_shared<Node>();
    n->kind = Kind::Name;
    n->text = identifier;
    return n;
}

namespace {

struct Parser {
    QVector<Token> tokens;
    int pos = 0;
    bool failed = false;

    const Token &peek() const { return tokens[pos]; }
    const Token &take()
    {
        Q_ASSERT(pos < tokens.size());
        return tokens[pos++];
    }
    bool isOp(const QString &op) const
    {
        return peek().type == Token::Type::Op && peek().text == op;
    }

    static int binaryPrecedence(const QString &op)
    {
        if (op == QLatin1String(":")) return 50;
        if (op == QLatin1String("^")) return 35;
        if (op == QLatin1String("*") || op == QLatin1String("/")) return 30;
        if (op == QLatin1String("+") || op == QLatin1String("-")) return 25;
        if (op == QLatin1String("&")) return 20;
        if (op == QLatin1String("=") || op == QLatin1String("<>") || op == QLatin1String("<")
            || op == QLatin1String(">") || op == QLatin1String("<=") || op == QLatin1String(">=")) {
            return 10;
        }
        if (op == QLatin1String(",")) return 5;
        return -1;
    }

    [[nodiscard]] ParseResult fail(const QString &message, int at)
    {
        return {nullptr, message, at};
    }

    // range ':' binds tightest among binaries; handled here so A1:B2:C4 is a
    // left-assoc chain and the rhs of ':' cannot swallow comparisons.
    NodePtr parseRangeOperand()
    {
        NodePtr left = parseUnaryPercent();
        while (isOp(QStringLiteral(":"))) {
            take();
            NodePtr right = parseUnaryPercent();
            left = Node::makeBinary(Op::Range, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr parseBinary(int minPrec)
    {
        NodePtr left = parseRangeOperand();
        while (true) {
            const int prec = binaryPrecedence(peek().text);
            if (peek().type != Token::Type::Op || prec < minPrec || prec <= 0) {
                return left;
            }
            const QString opText = take().text;
            NodePtr right = parseBinary(prec + 1); // left-associative
            left = Node::makeBinary(toOp(opText), std::move(left), std::move(right));
        }
    }

    static Op toOp(const QString &text)
    {
        if (text == QLatin1String(":")) return Op::Range;
        if (text == QLatin1String(",")) return Op::Union;
        if (text == QLatin1String("+")) return Op::Add;
        if (text == QLatin1String("-")) return Op::Sub;
        if (text == QLatin1String("*")) return Op::Mul;
        if (text == QLatin1String("/")) return Op::Div;
        if (text == QLatin1String("^")) return Op::Pow;
        if (text == QLatin1String("&")) return Op::Concat;
        if (text == QLatin1String("=")) return Op::Eq;
        if (text == QLatin1String("<>")) return Op::Ne;
        if (text == QLatin1String("<")) return Op::Lt;
        if (text == QLatin1String(">")) return Op::Gt;
        if (text == QLatin1String("<=")) return Op::Le;
        if (text == QLatin1String(">=")) return Op::Ge;
        return Op::Add;
    }

    NodePtr parseUnaryPercent()
    {
        if (isOp(QStringLiteral("-"))) {
            take();
            return Node::makeUnary(Op::Neg, parseUnaryPercent());
        }
        if (isOp(QStringLiteral("+"))) {
            take();
            return Node::makeUnary(Op::Pos, parseUnaryPercent());
        }
        NodePtr node = parsePrimary();
        while (isOp(QStringLiteral("%")) || isOp(QStringLiteral("#"))) {
            if (isOp(QStringLiteral("#"))) {
                // A1# — the spill range of the dynamic array anchored at A1
                // (M66, D-075); Excel stores it as _xlfn.ANCHORARRAY(A1).
                take();
                if (node->kind != Node::Kind::Ref || !node->ref.isSingleCell()) {
                    failed = true;
                    return Node::makeError(Err::Ref);
                }
                node = Node::makeCall(QStringLiteral("ANCHORARRAY"), {std::move(node)});
                continue;
            }
            take();
            node = Node::makeUnary(Op::Percent, std::move(node));
        }
        return node;
    }

    NodePtr parsePrimary()
    {
        const Token token = peek();
        switch (token.type) {
        case Token::Type::Number:
            take();
            return Node::makeNumber(token.text.toDouble());
        case Token::Type::String:
            take();
            return Node::makeText(token.text);
        case Token::Type::ErrorLit:
            take();
            return Node::makeError(errFromExport(token.text));
        case Token::Type::Ref:
            take();
            return Node::makeRef(parseRefText(token.text, QString(), QString()));
        case Token::Type::Identifier: {
            take();
            if (isOp(QStringLiteral("("))) {
                take();
                QVector<NodePtr> args;
                if (!isOp(QStringLiteral(")"))) {
                    while (true) {
                        // AGENT-NOTE: min precedence 6 keeps ',' out of the
                        // arg (it is the separator here, not the union
                        // operator, which lives only in non-call parens).
                        args.append(parseBinary(6));
                        if (isOp(QStringLiteral(",")) || isOp(QStringLiteral(";"))) {
                            take();
                            continue;
                        }
                        break;
                    }
                }
                if (!isOp(QStringLiteral(")"))) {
                    failed = true;
                    return Node::makeError(Err::Value);
                }
                take();
                return Node::makeCall(token.text, std::move(args));
            }
            if (token.text.compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0) {
                return Node::makeBool(true);
            }
            if (token.text.compare(QLatin1String("FALSE"), Qt::CaseInsensitive) == 0) {
                return Node::makeBool(false);
            }
            return Node::makeName(token.text);
        }
        case Token::Type::SheetName:
        case Token::Type::QuotedSheetName: {
            take();
            QString sheet = token.text;
            if (peek().type == Token::Type::Ref) {
                const Token refTok = take();
                return Node::makeRef(parseRefText(refTok.text, sheet,
                                                  token.type == Token::Type::QuotedSheetName
                                                      ? QStringLiteral("'") + sheet + QStringLiteral("'")
                                                      : sheet));
            }
            return Node::makeError(Err::Ref);
        }
        case Token::Type::Op:
            if (token.text == QLatin1String("(")) {
                take();
                NodePtr inner = parseBinary(0);
                if (!isOp(QStringLiteral(")"))) {
                    return Node::makeError(Err::Value);
                }
                take();
                return inner;
            }
            if (token.text == QLatin1String("{")) {
                // Array constants are #NAME? in v1 (plan section 7.2).
                int depth = 0;
                do {
                    if (peek().text == QLatin1String("{") && peek().type == Token::Type::Op) ++depth;
                    if (peek().text == QLatin1String("}") && peek().type == Token::Type::Op) --depth;
                    take();
                } while (depth > 0 && peek().type != Token::Type::End);
                return Node::makeError(Err::Name);
            }
            failed = true;
            return Node::makeError(Err::Value);
        case Token::Type::End:
            failed = true;
            return Node::makeError(Err::Value);
        }
        failed = true;
        return Node::makeError(Err::Value);
    }

    static RangeRef parseRefText(const QString &text, const QString &sheetName,
                                 const QString &sheetDisplay)
    {
        RangeRef ref;
        ref.sheet = sheetDisplay.isEmpty() ? sheetName : sheetDisplay;
        const int colon = text.indexOf(QLatin1Char(':'));
        const QString a = colon < 0 ? text : text.left(colon);
        const QString b = colon < 0 ? text : text.mid(colon + 1);
        auto parseCell = [](const QString &part, CellAddr &addr, bool &absRow, bool &absCol) {
            QString p = part;
            absCol = p.startsWith(QLatin1Char('$'));
            if (absCol) {
                p.remove(0, 1);
            }
            int split = 0;
            while (split < p.size() && p.at(split) >= QLatin1Char('A')
                   && p.at(split) <= QLatin1Char('Z')) {
                ++split;
            }
            const QString letters = p.left(split);
            QString digits = p.mid(split);
            absRow = digits.startsWith(QLatin1Char('$'));
            if (absRow) {
                digits.remove(0, 1);
            }
            addr.col = colFromName(letters).value_or(0);
            addr.row = digits.toInt() - 1;
        };
        // Whole columns ($A:C) and whole rows (1:$3): the '$' anchors land
        // in the same abs flags a cell range uses, so printing and shifting
        // treat both forms alike (M61, D-069).
        const auto anchored = [](const QString &side) { return side.startsWith(QLatin1Char('$')); };
        const auto bare = [&anchored](const QString &side) { return anchored(side) ? side.mid(1) : side; };
        const bool wholeA = colon >= 0 && colFromName(bare(a)).has_value()
            && colFromName(bare(b)).has_value();
        const auto digits = [](const QString &x) {
            return !x.isEmpty() && std::all_of(x.begin(), x.end(), [](QChar c) { return c.isDigit(); });
        };
        const bool wholeRowA = colon >= 0 && digits(bare(a)) && digits(bare(b));
        if (wholeA) {
            ref.wholeCols = true;
            ref.absCol0 = anchored(a);
            ref.absCol1 = anchored(b);
            ref.range.topLeft.col = colFromName(bare(a)).value_or(0);
            ref.range.bottomRight.col = colFromName(bare(b)).value_or(0);
            ref.range.topLeft.row = 0;
            ref.range.bottomRight.row = 1048575;
            return ref;
        }
        if (wholeRowA) {
            ref.wholeRows = true;
            ref.absRow0 = anchored(a);
            ref.absRow1 = anchored(b);
            ref.range.topLeft.row = bare(a).toInt() - 1;
            ref.range.bottomRight.row = bare(b).toInt() - 1;
            // AGENT-GUARD: a whole row spans every column. The columns were
            // left at -1 before M61, so the range was invalid and SUM(1:1)
            // evaluated to #REF!.
            ref.range.topLeft.col = 0;
            ref.range.bottomRight.col = 16383;
            return ref;
        }
        parseCell(a, ref.range.topLeft, ref.absRow0, ref.absCol0);
        if (colon >= 0) {
            parseCell(b, ref.range.bottomRight, ref.absRow1, ref.absCol1);
        } else {
            ref.range.bottomRight = ref.range.topLeft;
            ref.absRow1 = ref.absRow0;
            ref.absCol1 = ref.absCol0;
        }
        return ref;
    }
};

} // namespace

ParseResult parseFormula(const QString &input)
{
    Parser parser{lexFormula(input), 0};
    if (parser.peek().type == Token::Type::End) {
        return {Node::makeText(QString()), QString(), -1};
    }
    NodePtr root = parser.parseBinary(0);
    if (parser.failed) {
        return {nullptr, QStringLiteral("missing or invalid operand"), input.size()};
    }
    if (parser.peek().type != Token::Type::End) {
        return {nullptr, QStringLiteral("unexpected trailing input"),
                parser.peek().pos};
    }
    return {std::move(root), QString(), -1};
}

// nodeText and shiftNode live in formula_print.cpp (M61, D-069).

} // namespace QindaOffice::Calc
