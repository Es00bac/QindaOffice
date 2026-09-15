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
        while (isOp(QStringLiteral("%"))) {
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
        auto parseWholeCol = [](const QString &part, qint32 &col) {
            col = colFromName(part).value_or(0);
        };
        auto parseWholeRow = [](const QString &part, qint32 &row) { row = part.toInt() - 1; };

        const bool wholeA = colon >= 0 && colFromName(a).has_value()
            && colFromName(b).has_value();
        const bool wholeRowA = colon >= 0 && std::all_of(a.begin(), a.end(), [](QChar c) {
                                   return c.isDigit();
                               });
        if (wholeA) {
            ref.wholeCols = true;
            parseWholeCol(a, ref.range.topLeft.col);
            parseWholeCol(b, ref.range.bottomRight.col);
            ref.range.topLeft.row = 0;
            ref.range.bottomRight.row = 1048575;
            return ref;
        }
        if (wholeRowA) {
            ref.wholeRows = true;
            parseWholeRow(a, ref.range.topLeft.row);
            parseWholeRow(b, ref.range.bottomRight.row);
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

QString nodeText(const NodePtr &node)
{
    if (!node) {
        return QString();
    }
    switch (node->kind) {
    case Node::Kind::Number: {
        QString t = QString::number(node->number, 'g', 15);
        return t;
    }
    case Node::Kind::Text:
        return QStringLiteral("\"") + QString(node->text).replace(QLatin1Char('"'),
                                                                  QStringLiteral("\"\""))
               + QStringLiteral("\"");
    case Node::Kind::Bool:
        return node->boolean ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    case Node::Kind::Error:
        return errText(node->error);
    case Node::Kind::Ref:
        return rangeRefText(node->ref);
    case Node::Kind::Unary: {
        const QString inner = nodeText(node->left);
        switch (node->op) {
        case Op::Neg: return QStringLiteral("-") + inner;
        case Op::Pos: return QStringLiteral("+") + inner;
        case Op::Percent: return inner + QStringLiteral("%");
        default: return inner;
        }
    }
    case Node::Kind::Binary: {
        const QString l = nodeText(node->left);
        const QString r = nodeText(node->right);
        QString opText;
        switch (node->op) {
        case Op::Range: opText = QStringLiteral(":"); break;
        case Op::Union: opText = QStringLiteral(","); break;
        case Op::Add: opText = QStringLiteral("+"); break;
        case Op::Sub: opText = QStringLiteral("-"); break;
        case Op::Mul: opText = QStringLiteral("*"); break;
        case Op::Div: opText = QStringLiteral("/"); break;
        case Op::Pow: opText = QStringLiteral("^"); break;
        case Op::Concat: opText = QStringLiteral("&"); break;
        case Op::Eq: opText = QStringLiteral("="); break;
        case Op::Ne: opText = QStringLiteral("<>"); break;
        case Op::Lt: opText = QStringLiteral("<"); break;
        case Op::Le: opText = QStringLiteral("<="); break;
        case Op::Gt: opText = QStringLiteral(">"); break;
        case Op::Ge: opText = QStringLiteral(">="); break;
        default: opText = QStringLiteral("+"); break;
        }
        return l + opText + r;
    }
    case Node::Kind::Call: {
        QString out = node->text + QStringLiteral("(");
        for (int i = 0; i < node->args.size(); ++i) {
            if (i > 0) out += QStringLiteral(",");
            out += nodeText(node->args.at(i));
        }
        return out + QStringLiteral(")");
    }
    case Node::Kind::Name:
        return node->text;
    }
    return QString();
}

static RangeRef shiftRef(const RangeRef &ref, qint32 dr, qint32 dc)
{
    RangeRef out = ref;
    auto shiftAddr = [](CellAddr &addr, bool absRow, bool absCol, qint32 dr, qint32 dc) {
        if (!absRow) addr.row += dr;
        if (!absCol) addr.col += dc;
    };
    shiftAddr(out.range.topLeft, ref.absRow0, ref.absCol0, dr, dc);
    shiftAddr(out.range.bottomRight, ref.absRow1, ref.absCol1, dr, dc);
    return out;
}

NodePtr shiftNode(const NodePtr &node, qint32 dr, qint32 dc)
{
    if (!node) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::Number:
    case Node::Kind::Text:
    case Node::Kind::Bool:
    case Node::Kind::Error:
    case Node::Kind::Name:
        return node;
    case Node::Kind::Ref:
        return Node::makeRef(shiftRef(node->ref, dr, dc));
    case Node::Kind::Unary:
        return Node::makeUnary(node->op, shiftNode(node->left, dr, dc));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op, shiftNode(node->left, dr, dc),
                                shiftNode(node->right, dr, dc));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        for (const NodePtr &arg : node->args) {
            args.append(shiftNode(arg, dr, dc));
        }
        return Node::makeCall(node->text, std::move(args));
    }
    }
    return node;
}

} // namespace QindaOffice::Calc
