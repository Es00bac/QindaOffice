// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal parts of the Access SQL view (base_access_sql.h): a tokenizer
// that keeps each token's place in the text (so expressions and criteria
// are carried as the user wrote them), the boolean structure of a WHERE or
// HAVING clause, and the printing helpers both directions share.

#include "base_access_sql.h"

namespace QindaOffice::Base::AccessSql {

struct Token {
    enum Kind { End, Word, Bracket, String, Number, Date, Op };
    Kind kind = End;
    QString text;  // a String's or Bracket's content, an Op's characters
    int begin = 0; // [begin, end) in the source, quotes and brackets included
    int end = 0;
    [[nodiscard]] bool is(const char *word) const;   // a Word, case-blind
    [[nodiscard]] bool op(const char *chars) const;  // an Op
};

// Access SQL tokens: words, [names], "text" and 'text', numbers, #dates#
// and operators (= <> != < > <= >= + - * / \ & ^ ( ) , . ; !). `error` for
// an unclosed quote, bracket or date, or a character Access SQL has not.
[[nodiscard]] QList<Token> tokenize(const QString &src, QString *error);
// A Word or a [Bracketed] name.
[[nodiscard]] inline bool nameToken(const Token &t)
{
    return t.kind == Token::Word || t.kind == Token::Bracket;
}
// The source text of tokens [first, last).
[[nodiscard]] QString textOf(const QString &src, const QList<Token> &tokens, int first, int last);
// Index of the token closing the parenthesis opened at `open`, or -1.
[[nodiscard]] int closing(const QList<Token> &tokens, int open);

// A WHERE/HAVING clause: OR and AND over terms; Group is a parenthesized
// boolean (not a parenthesized value, which stays inside its term).
struct BoolNode {
    enum Kind { Term, And, Or, Not, Group };
    Kind kind = Term;
    QList<BoolNode> children;
    int first = 0; // a Term's tokens [first, last)
    int last = 0;
};
[[nodiscard]] bool parseBool(const QList<Token> &tokens, int first, int last, BoolNode *out, QString *error);

// A comparison term split at its operator: "donations.amount > 100" →
// subject [first, opAt), criteria "> 100" (an equality drops its "=").
struct SplitTerm {
    int first = 0;
    int opAt = 0;
    QString criteria;
};
[[nodiscard]] bool splitTerm(const QString &src, const QList<Token> &tokens, int first, int last, SplitTerm *out);

// A name as Access SQL writes it: bare when it is a plain word that is not
// a keyword, else [bracketed].
[[nodiscard]] QString ident(const QString &name);
[[nodiscard]] bool isKeyword(const QString &word);
// "Sum" for sum, "Avg" for avg, … ("" for anything else).
[[nodiscard]] QString accessAggregate(const QString &sqlFunction);
// M86: a union design as Access SQL (base_access_sql_union.cpp).
[[nodiscard]] QString unionText(const QueryDesign &design);
// Access PARAMETERS types ↔ field types.
[[nodiscard]] QString parameterType(FieldType type);
[[nodiscard]] std::optional<FieldType> parameterTypeFrom(const QString &word);

} // namespace QindaOffice::Base::AccessSql
