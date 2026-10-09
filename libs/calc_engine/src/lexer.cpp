// SPDX-License-Identifier: GPL-3.0-or-later
#include "lexer.h"

#include <QMap>

namespace QindaOffice::Calc {
namespace {

bool isRefTail(const QString &s, int i)
{
    // $?\d+ must follow the letters at position i.
    if (i < s.size() && s[i] == QLatin1Char('$')) {
        ++i;
    }
    bool any = false;
    while (i < s.size() && s[i].isDigit()) {
        ++i;
        any = true;
    }
    return any && i == s.size();
}

bool lexesAsCellRef(const QString &word)
{
    // $?[A-Z]{1,3}$?[0-9]+
    QString s = word;
    if (s.startsWith(QLatin1Char('$'))) {
        s.remove(0, 1);
    }
    int letters = 0;
    while (letters < s.size() && s[letters] >= QLatin1Char('A') && s[letters] <= QLatin1Char('Z')) {
        ++letters;
    }
    if (letters == 0 || letters > 3 || letters == s.size()) {
        return false;
    }
    return isRefTail(s, letters);
}

// One side of a whole-row/column reference without its optional '$'
// anchor: "$C" → "C". Excel spells absolute whole columns $A:$C and whole
// rows $1:$3 (M61, D-069); before that those lexed as identifiers and every
// VLOOKUP(x,$A:$C,…) failed to parse.
QString unanchored(const QString &side)
{
    return side.startsWith(QLatin1Char('$')) ? side.mid(1) : side;
}

bool lexesAsWholeCol(const QString &word)
{
    // \$?[A-Z]{1,3}:\$?[A-Z]{1,3}
    const int colon = word.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon != word.lastIndexOf(QLatin1Char(':'))) {
        return false;
    }
    const QString a = unanchored(word.left(colon));
    const QString b = unanchored(word.mid(colon + 1));
    if (a.isEmpty() || b.isEmpty() || a.size() > 3 || b.size() > 3) {
        return false;
    }
    const auto allCaps = [](const QString &x) {
        return std::all_of(x.begin(), x.end(), [](QChar c) {
            return c >= QLatin1Char('A') && c <= QLatin1Char('Z');
        });
    };
    return allCaps(a) && allCaps(b);
}

bool lexesAsCellRange(const QString &word)
{
    // cellref:cellref (exactly one colon, both sides cell refs).
    const int colon = word.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon != word.lastIndexOf(QLatin1Char(':'))) {
        return false;
    }
    return lexesAsCellRef(word.left(colon)) && lexesAsCellRef(word.mid(colon + 1));
}

// The end of the bracket group that opens at s[i] (one past its ']'), or
// -1 when it never closes. Inside, a single quote escapes the next
// character ('[ '] '# '' — Excel's structured-reference escapes).
int bracketGroupEnd(const QString &s, int i)
{
    int depth = 0;
    for (int j = i; j < s.size(); ++j) {
        const QChar c = s[j];
        if (c == QLatin1Char('\'')) {
            ++j;
        } else if (c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(']') && --depth == 0) {
            return j + 1;
        }
    }
    return -1;
}

bool lexesAsWholeRow(const QString &word)
{
    // \$?[0-9]+:\$?[0-9]+
    const int colon = word.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon != word.lastIndexOf(QLatin1Char(':'))) {
        return false;
    }
    const QString a = unanchored(word.left(colon));
    const QString b = unanchored(word.mid(colon + 1));
    const auto allDigits = [](const QString &x) {
        return !x.isEmpty() && std::all_of(x.begin(), x.end(), [](QChar c) { return c.isDigit(); });
    };
    return allDigits(a) && allDigits(b);
}

} // namespace

QVector<Token> lexFormula(const QString &input)
{
    QVector<Token> out;
    int i = 0;
    const int n = input.size();
    while (i < n) {
        const QChar c = input[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        const int start = i;
        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && input[i + 1].isDigit())) {
            bool dot = false;
            bool exp = false;
            while (i < n) {
                const QChar d = input[i];
                if (d.isDigit()) {
                    ++i;
                } else if (d == QLatin1Char('.') && !dot && !exp) {
                    dot = true;
                    ++i;
                } else if ((d == QLatin1Char('e') || d == QLatin1Char('E')) && !exp && i + 1 < n
                           && (input[i + 1].isDigit()
                               || ((input[i + 1] == QLatin1Char('+')
                                    || input[i + 1] == QLatin1Char('-'))
                                   && i + 2 < n && input[i + 2].isDigit()))) {
                    exp = true;
                    ++i;
                    if (input[i] == QLatin1Char('+') || input[i] == QLatin1Char('-')) {
                        ++i;
                    }
                } else {
                    break;
                }
            }
            // Whole-row refs (1:3, 1:$3) start with digits; promote when
            // the number is immediately followed by ':' and more digits.
            const int anchor = i + 1 < n && input[i + 1] == QLatin1Char('$') ? 1 : 0;
            if (i < n && input[i] == QLatin1Char(':') && i + 1 + anchor < n
                && input[i + 1 + anchor].isDigit()) {
                int j = i + 1 + anchor;
                while (j < n && input[j].isDigit()) {
                    ++j;
                }
                const QString candidate = input.mid(start, j - start);
                if (lexesAsWholeRow(candidate)) {
                    out.append({Token::Type::Ref, candidate, start});
                    i = j;
                    continue;
                }
            }
            out.append({Token::Type::Number, input.mid(start, i - start), start});
            continue;
        }
        if (c == QLatin1Char('"')) {
            ++i;
            QString s;
            while (i < n) {
                if (input[i] == QLatin1Char('"')) {
                    if (i + 1 < n && input[i + 1] == QLatin1Char('"')) {
                        s += QLatin1Char('"');
                        i += 2;
                    } else {
                        ++i;
                        break;
                    }
                } else {
                    s += input[i];
                    ++i;
                }
            }
            out.append({Token::Type::String, s, start});
            continue;
        }
        if (c == QLatin1Char('#')) {
            static const QMap<QString, void *> kErrors = {
                {QStringLiteral("#NULL!"), nullptr},  {QStringLiteral("#DIV/0!"), nullptr},
                {QStringLiteral("#VALUE!"), nullptr}, {QStringLiteral("#REF!"), nullptr},
                {QStringLiteral("#NAME?"), nullptr}, {QStringLiteral("#NUM!"), nullptr},
                {QStringLiteral("#N/A"), nullptr},
                {QStringLiteral("#SPILL!"), nullptr}, {QStringLiteral("#CALC!"), nullptr},
            };
            bool matched = false;
            for (auto it = kErrors.begin(); it != kErrors.end(); ++it) {
                if (input.mid(i, it.key().size()).compare(it.key(), Qt::CaseSensitive) == 0) {
                    out.append({Token::Type::ErrorLit, it.key(), start});
                    i += it.key().size();
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }
            out.append({Token::Type::Op, QString(c), start});
            ++i;
            continue;
        }
        if (c == QLatin1Char('\'')) {
            // Quoted sheet name: '...' with '' escape, must be followed by '!'.
            ++i;
            QString s;
            while (i < n) {
                if (input[i] == QLatin1Char('\'')) {
                    if (i + 1 < n && input[i + 1] == QLatin1Char('\'')) {
                        s += QLatin1Char('\'');
                        i += 2;
                    } else {
                        ++i;
                        break;
                    }
                } else {
                    s += input[i];
                    ++i;
                }
            }
            if (i < n && input[i] == QLatin1Char('!')) {
                out.append({Token::Type::QuotedSheetName, s, start});
                ++i;
            } else {
                out.append({Token::Type::Op, QString(c), start});
            }
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$')) {
            // Consume a base run of letters/digits/'_'/'.'/'$', then glue
            // colon-separated range parts while the result still lexes as a
            // reference (A1:B2, A:A, 1:3). Anything else classifies as an
            // identifier or sheet name.
            while (i < n && (input[i].isLetterOrNumber() || input[i] == QLatin1Char('_')
                             || input[i] == QLatin1Char('.') || input[i] == QLatin1Char('$'))) {
                ++i;
            }
            QString word = input.mid(start, i - start);
            if (i < n && input[i] == QLatin1Char('[')) {
                // Table1[Sales]: the table name and its brackets are one
                // structured-reference token (M69, D-078).
                const int end = bracketGroupEnd(input, i);
                if (end > 0) {
                    out.append({Token::Type::StructRef, input.mid(start, end - start), start});
                    i = end;
                    continue;
                }
            }
            while (i < n && input[i] == QLatin1Char(':')) {
                int j = i + 1;
                while (j < n && (input[j].isLetterOrNumber() || input[j] == QLatin1Char('_')
                                 || input[j] == QLatin1Char('.')
                                 || input[j] == QLatin1Char('$'))) {
                    ++j;
                }
                if (j == i + 1) {
                    break; // ':' with empty rhs stays an operator
                }
                const QString candidate =
                    word + QLatin1Char(':') + input.mid(i + 1, j - i - 1);
                if (lexesAsCellRange(candidate) || lexesAsWholeCol(candidate)
                    || lexesAsWholeRow(candidate)) {
                    word = candidate;
                    i = j;
                } else {
                    break;
                }
            }
            if (i < n && input[i] == QLatin1Char('(')) {
                // A name followed by '(' is a function call even when the
                // word also spells a cell address (LOG10(...)).
                out.append({Token::Type::Identifier, word, start});
            } else if (lexesAsCellRef(word) || lexesAsCellRange(word) || lexesAsWholeCol(word)
                || lexesAsWholeRow(word)) {
                out.append({Token::Type::Ref, word, start});
            } else if (i < n && input[i] == QLatin1Char('!')) {
                out.append({Token::Type::SheetName, word, start});
                ++i;
            } else {
                out.append({Token::Type::Identifier, word, start});
            }
            continue;
        }
        if (c == QLatin1Char('[')) {
            // An unqualified structured reference ([@Sales], [Sales]). A
            // group that runs straight into a name is an external
            // workbook's index ([1]Sheet1!A1, [1]!Total): it stays the Op
            // tokens it always was, which the parser refuses.
            const int end = bracketGroupEnd(input, i);
            const bool external = end > 0 && end < n
                && (input[end].isLetterOrNumber() || input[end] == QLatin1Char('_') || input[end] == QLatin1Char('!')
                    || input[end] == QLatin1Char('$') || input[end] == QLatin1Char('\''));
            if (end > 0 && !external) {
                out.append({Token::Type::StructRef, input.mid(start, end - start), start});
                i = end;
                continue;
            }
        }
        // Two-character comparison operators.
        if ((c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('='))
            && i + 1 < n) {
            const QChar d = input[i + 1];
            if ((c == QLatin1Char('<') && (d == QLatin1Char('=') || d == QLatin1Char('>')))
                || (c == QLatin1Char('>') && d == QLatin1Char('='))) {
                out.append({Token::Type::Op, input.mid(start, 2), start});
                i += 2;
                continue;
            }
        }
        out.append({Token::Type::Op, QString(c), start});
        ++i;
    }
    out.append({Token::Type::End, QString(), n});
    return out;
}

} // namespace QindaOffice::Calc
