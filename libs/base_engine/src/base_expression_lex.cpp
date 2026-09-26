// SPDX-License-Identifier: GPL-3.0-or-later
// The expression tokenizer: numbers, "text" and 'text', #dates#, [names],
// words and operators; bare-word criteria and Like patterns.
#include "base_expression_p.h"

namespace QindaOffice::Base::Expr::detail {

QList<Token> tokenize(const QString &src, QString *error)
{
    QList<Token> out;
    int i = 0;
    const int n = int(src.size());
    while (i < n) {
        const QChar c = src.at(i);
        if (c.isSpace()) {
            ++i;
        } else if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && src.at(i + 1).isDigit())) {
            int j = i;
            while (j < n && (src.at(j).isDigit() || src.at(j) == QLatin1Char('.'))) {
                ++j;
            }
            out.append({Token::Number, src.mid(i, j - i)});
            i = j;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            QString text;
            int j = i + 1;
            for (; j < n; ++j) {
                if (src.at(j) == c) {
                    if (j + 1 < n && src.at(j + 1) == c) {
                        text.append(c);
                        ++j;
                        continue;
                    }
                    break;
                }
                text.append(src.at(j));
            }
            if (j >= n) {
                *error = QStringLiteral("A quoted text is not closed.");
                return {};
            }
            out.append({Token::String, text});
            i = j + 1;
        } else if (c == QLatin1Char('#') || c == QLatin1Char('[')) {
            const QChar close = c == QLatin1Char('#') ? QLatin1Char('#') : QLatin1Char(']');
            const int j = int(src.indexOf(close, i + 1));
            if (j < 0) {
                *error = c == QLatin1Char('#') ? QStringLiteral("A #date# is not closed.")
                                               : QStringLiteral("A [name] is not closed.");
                return {};
            }
            out.append({c == QLatin1Char('#') ? Token::Date : Token::Bracket, src.mid(i + 1, j - i - 1).trimmed()});
            i = j + 1;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (src.at(j).isLetterOrNumber() || src.at(j) == QLatin1Char('_'))) {
                ++j;
            }
            out.append({Token::Word, src.mid(i, j - i)});
            i = j;
        } else {
            static const QStringList two{QStringLiteral("<>"), QStringLiteral("<="), QStringLiteral(">="),
                                         QStringLiteral("!=")};
            const QString pair = src.mid(i, 2);
            if (two.contains(pair)) {
                out.append({Token::Op, pair == QLatin1String("!=") ? QStringLiteral("<>") : pair});
                i += 2;
            } else if (QStringLiteral("=<>+-*/&(),.").contains(c)) {
                out.append({Token::Op, QString(c)});
                ++i;
            } else {
                *error = QStringLiteral("Unexpected character %1.").arg(c);
                return {};
            }
        }
    }
    out.append(Token{Token::End, {}});
    return out;
}

QList<Token> bareWordsAsText(QList<Token> tokens, const QString &text)
{
    // Access reads several bare words (Grace Hopper) as one text value.
    static const QStringList keywords{QStringLiteral("not"), QStringLiteral("like"), QStringLiteral("between"),
                                      QStringLiteral("in"),  QStringLiteral("is"),   QStringLiteral("null"),
                                      QStringLiteral("and"), QStringLiteral("or"),   QStringLiteral("true"),
                                      QStringLiteral("false"), QStringLiteral("yes"), QStringLiteral("no")};
    const bool wordsOnly = tokens.size() > 2 && std::all_of(tokens.cbegin(), tokens.cend() - 1, [](const Token &t) {
        return t.kind == Token::Word && !keywords.contains(t.text.toLower());
    });
    if (wordsOnly) {
        return {Token{Token::String, text.trimmed()}, Token{Token::End, {}}};
    }
    return tokens;
}

QString likePattern(const QString &access)
{
    QString out;
    for (const QChar ch : access) {
        if (ch == QLatin1Char('*')) {
            out.append(QLatin1Char('%'));
        } else if (ch == QLatin1Char('?')) {
            out.append(QLatin1Char('_'));
        } else if (ch == QLatin1Char('%') || ch == QLatin1Char('_') || ch == QLatin1Char('\\')) {
            out.append(QLatin1Char('\\')).append(ch);
        } else {
            out.append(ch);
        }
    }
    return quoteLiteral(out);
}

} // namespace QindaOffice::Base::Expr::detail
