// SPDX-License-Identifier: GPL-3.0-or-later
// Word's field codes split into tokens (docx_merge_fields_p.h): text,
// quoted text, nested fields in braces, switches with their arguments.

#include "docx_merge_fields_p.h"

namespace QindaOffice::Docx {

namespace detail {

QList<FieldToken> tokenizeField(const QString &code)
{
    QList<FieldToken> tokens;
    const qsizetype n = code.size();
    qsizetype i = 0;
    while (i < n) {
        const QChar c = code.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == QLatin1Char('"')) {
            QString text;
            int depth = 0;
            qsizetype j = i + 1;
            for (; j < n; ++j) {
                const QChar ch = code.at(j);
                if (ch == QLatin1Char('\\') && j + 1 < n && code.at(j + 1) == QLatin1Char('"') && depth == 0) {
                    text += QLatin1Char('"');
                    ++j;
                    continue;
                }
                if (ch == QLatin1Char('{')) {
                    ++depth;
                } else if (ch == QLatin1Char('}')) {
                    depth = qMax(0, depth - 1);
                } else if (ch == QLatin1Char('"') && depth == 0) {
                    break;
                }
                text += ch;
            }
            tokens.append({FieldToken::Quoted, text});
            i = j + 1;
            continue;
        }
        if (c == QLatin1Char('{')) {
            int depth = 1;
            bool quoted = false;
            qsizetype j = i + 1;
            for (; j < n; ++j) {
                const QChar ch = code.at(j);
                if (ch == QLatin1Char('"')) {
                    quoted = !quoted;
                } else if (!quoted && ch == QLatin1Char('{')) {
                    ++depth;
                } else if (!quoted && ch == QLatin1Char('}') && --depth == 0) {
                    break;
                }
            }
            tokens.append({FieldToken::Field, code.mid(i + 1, j - i - 1).trimmed()});
            i = j + 1;
            continue;
        }
        if (c == QLatin1Char('\\') && i + 1 < n) {
            tokens.append({FieldToken::Switch, code.mid(i, 2)});
            i += 2;
            continue;
        }
        qsizetype j = i;
        while (j < n && !code.at(j).isSpace() && code.at(j) != QLatin1Char('"') && code.at(j) != QLatin1Char('{')) {
            ++j;
        }
        tokens.append({FieldToken::Text, code.mid(i, j - i)});
        i = j;
    }
    return tokens;
}

FieldCode parseField(const QString &instruction)
{
    static const QStringList kWithArgument{QStringLiteral("\\*"), QStringLiteral("\\b"), QStringLiteral("\\f"),
                                           QStringLiteral("\\@"), QStringLiteral("\\#"), QStringLiteral("\\l"),
                                           QStringLiteral("\\c"), QStringLiteral("\\e")};
    FieldCode code;
    QList<FieldToken> tokens = tokenizeField(instruction);
    if (tokens.isEmpty() || tokens.first().kind != FieldToken::Text) {
        return code;
    }
    code.type = tokens.takeFirst().text.toUpper();
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const FieldToken &t = tokens.at(i);
        if (t.kind != FieldToken::Switch) {
            if (code.switches.isEmpty()) {
                code.args.append(t);
            }
            continue;
        }
        FieldToken argument;
        const QString name = t.text.left(1) + t.text.mid(1).toLower();
        if (kWithArgument.contains(name, Qt::CaseInsensitive) && i + 1 < tokens.size()
            && tokens.at(i + 1).kind != FieldToken::Switch) {
            argument = tokens.at(++i);
        }
        code.switches.append({t.text, argument});
    }
    return code;
}

const FieldToken *FieldCode::switchArgument(QStringView name) const
{
    for (const auto &[key, value] : switches) {
        if (key.compare(name, Qt::CaseSensitive) == 0) {
            return &value;
        }
    }
    return nullptr;
}

} // namespace detail

} // namespace QindaOffice::Docx
