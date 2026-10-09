// SPDX-License-Identifier: GPL-3.0-or-later
// Flash Fill's pieces (flash_fill_internal.h, M78, D-087): tokens,
// the extractors a row offers, applying them to other rows, their prices
// and their descriptions.

#include "flash_fill_internal.h"

#include <QPair>
#include <QSet>

namespace QindaOffice::Calc::FlashFillInternal {

namespace {

// Tokens from each end that become extractors (a cell with forty words is
// not split forty ways).
constexpr int kMaxTokens = 12;

bool inClass(QChar c, TokenClass tokenClass)
{
    switch (tokenClass) {
    case TokenClass::Word: return !c.isSpace();
    case TokenClass::Alnum: return c.isLetterOrNumber();
    case TokenClass::Letters: return c.isLetter();
    case TokenClass::Digits: return c.isDigit();
    }
    return false;
}

// (start, length) of each token of the class.
QList<QPair<int, int>> tokens(const QString &text, TokenClass tokenClass)
{
    QList<QPair<int, int>> out;
    int start = -1;
    for (int i = 0; i <= text.size(); ++i) {
        const bool in = i < text.size() && inClass(text.at(i), tokenClass);
        if (in && start < 0) {
            start = i;
        } else if (!in && start >= 0) {
            out.append({start, i - start});
            start = -1;
        }
    }
    return out;
}

std::optional<QString> tokenValue(const QString &text, const Extractor &e)
{
    const QList<QPair<int, int>> list = tokens(text, e.tokenClass);
    const qsizetype at = e.fromEnd ? list.size() - 1 - e.index : e.index;
    if (at < 0 || at >= list.size()) {
        return std::nullopt;
    }
    return text.mid(list.at(at).first, list.at(at).second);
}

double classCost(TokenClass tokenClass)
{
    switch (tokenClass) {
    case TokenClass::Word: return 0.10;
    case TokenClass::Digits: return 0.11; // "4521" in "Order #4521" is a number before it is a word
    case TokenClass::Alnum: return 0.12;
    case TokenClass::Letters: return 0.15;
    }
    return 0.2;
}

QString ordinal(int index)
{
    static const QStringList words{QStringLiteral("first"),   QStringLiteral("second"), QStringLiteral("third"),
                                   QStringLiteral("fourth"),  QStringLiteral("fifth"),  QStringLiteral("sixth"),
                                   QStringLiteral("seventh"), QStringLiteral("eighth"), QStringLiteral("ninth"),
                                   QStringLiteral("tenth")};
    return index < words.size() ? words.at(index) : QStringLiteral("%1th").arg(index + 1);
}

QString tokenNoun(TokenClass tokenClass)
{
    switch (tokenClass) {
    case TokenClass::Word:
    case TokenClass::Alnum: return QStringLiteral("word");
    case TokenClass::Letters: return QStringLiteral("run of letters");
    case TokenClass::Digits: return QStringLiteral("number");
    }
    return {};
}

QString quoted(const QString &text)
{
    if (text == QLatin1String(" ")) {
        return QStringLiteral("a space");
    }
    return QStringLiteral("“%1”").arg(text);
}

QString tokenPhrase(const Extractor &e, const QString &column)
{
    const QString position = e.fromEnd ? (e.index == 0 ? QStringLiteral("last") : ordinal(e.index) + QStringLiteral("-to-last"))
                                       : ordinal(e.index);
    return QStringLiteral("the %1 %2 of %3").arg(position, tokenNoun(e.tokenClass), column);
}

} // namespace

QList<Offer> offers(const QStringList &sources)
{
    QList<Offer> out;
    for (int column = 0; column < sources.size(); ++column) {
        const QString &text = sources.at(column);
        if (text.isEmpty()) {
            continue;
        }
        Extractor base;
        base.column = column;
        out.append({base, text});
        for (const TokenClass tokenClass : {TokenClass::Word, TokenClass::Alnum, TokenClass::Letters, TokenClass::Digits}) {
            const QList<QPair<int, int>> list = tokens(text, tokenClass);
            const int n = int(list.size());
            for (int i = 0; i < n; ++i) {
                const QString token = text.mid(list.at(i).first, list.at(i).second);
                for (const bool fromEnd : {false, true}) {
                    const int index = fromEnd ? n - 1 - i : i;
                    if (index >= kMaxTokens) {
                        continue;
                    }
                    Extractor e = base;
                    e.kind = Extractor::Kind::Token;
                    e.tokenClass = tokenClass;
                    e.index = index;
                    e.fromEnd = fromEnd;
                    out.append({e, token});
                    for (int length = 1; length < token.size() && length <= kMaxCut; ++length) {
                        Extractor prefix = e;
                        prefix.kind = Extractor::Kind::TokenPrefix;
                        prefix.length = length;
                        out.append({prefix, token.left(length)});
                        Extractor suffix = e;
                        suffix.kind = Extractor::Kind::TokenSuffix;
                        suffix.length = length;
                        out.append({suffix, token.right(length)});
                    }
                }
            }
        }
        // Fields between any character that is not a letter or a digit.
        QSet<QChar> delimiters;
        for (const QChar c : text) {
            if (!c.isLetterOrNumber()) {
                delimiters.insert(c);
            }
        }
        for (const QChar delimiter : std::as_const(delimiters)) {
            const QStringList parts = text.split(delimiter);
            const int n = int(parts.size());
            for (int i = 0; i < n; ++i) {
                for (const bool fromEnd : {false, true}) {
                    const int index = fromEnd ? n - 1 - i : i;
                    if (index >= kMaxTokens || parts.at(i).isEmpty()) {
                        continue;
                    }
                    Extractor e = base;
                    e.kind = Extractor::Kind::Field;
                    e.delimiter = delimiter;
                    e.index = index;
                    e.fromEnd = fromEnd;
                    out.append({e, parts.at(i)});
                    if (const QString trimmed = parts.at(i).trimmed(); !trimmed.isEmpty() && trimmed != parts.at(i)) {
                        e.trimmed = true;
                        out.append({e, trimmed});
                    }
                }
            }
            if (n >= 3) { // with one delimiter these are field 1 and field 0
                Extractor after = base;
                after.kind = Extractor::Kind::AfterFirst;
                after.delimiter = delimiter;
                out.append({after, text.mid(text.indexOf(delimiter) + 1)});
                Extractor before = after;
                before.kind = Extractor::Kind::BeforeLast;
                out.append({before, text.left(text.lastIndexOf(delimiter))});
            }
        }
        for (int length = 1; length < text.size() && length <= kMaxCut; ++length) {
            Extractor prefix = base;
            prefix.kind = Extractor::Kind::Prefix;
            prefix.length = length;
            out.append({prefix, text.left(length)});
            Extractor suffix = prefix;
            suffix.kind = Extractor::Kind::Suffix;
            out.append({suffix, text.right(length)});
        }
    }
    // Empty values explain nothing.
    out.removeIf([](const Offer &offer) { return offer.value.isEmpty(); });
    return out;
}

std::optional<QString> extract(const Extractor &e, const QStringList &sources)
{
    if (e.column < 0 || e.column >= sources.size() || sources.at(e.column).isEmpty()) {
        return std::nullopt;
    }
    const QString &text = sources.at(e.column);
    switch (e.kind) {
    case Extractor::Kind::Whole:
        return text;
    case Extractor::Kind::Token:
        return tokenValue(text, e);
    case Extractor::Kind::TokenPrefix:
    case Extractor::Kind::TokenSuffix: {
        const std::optional<QString> token = tokenValue(text, e);
        if (!token) {
            return std::nullopt;
        }
        return e.kind == Extractor::Kind::TokenPrefix ? token->left(e.length) : token->right(e.length);
    }
    case Extractor::Kind::Field: {
        if (!text.contains(e.delimiter)) {
            return std::nullopt;
        }
        const QStringList parts = text.split(e.delimiter);
        const qsizetype at = e.fromEnd ? parts.size() - 1 - e.index : e.index;
        if (at < 0 || at >= parts.size()) {
            return std::nullopt;
        }
        return e.trimmed ? parts.at(at).trimmed() : parts.at(at);
    }
    case Extractor::Kind::AfterFirst: {
        const qsizetype at = text.indexOf(e.delimiter);
        return at < 0 ? std::nullopt : std::optional<QString>(text.mid(at + 1));
    }
    case Extractor::Kind::BeforeLast: {
        const qsizetype at = text.lastIndexOf(e.delimiter);
        return at < 0 ? std::nullopt : std::optional<QString>(text.left(at));
    }
    case Extractor::Kind::Prefix:
        return text.left(e.length);
    case Extractor::Kind::Suffix:
        return text.right(e.length);
    }
    return std::nullopt;
}

QString applyCase(const QString &text, CaseMode mode)
{
    switch (mode) {
    case CaseMode::AsIs:
        return text;
    case CaseMode::Upper:
        return text.toUpper();
    case CaseMode::Lower:
        return text.toLower();
    case CaseMode::Proper: {
        // Excel's PROPER: a letter after a non-letter is a capital, every
        // other letter small ("o'brien-smith" → "O'Brien-Smith").
        QString out = text;
        bool previousLetter = false;
        for (qsizetype i = 0; i < out.size(); ++i) {
            const QChar c = out.at(i);
            if (c.isLetter()) {
                out[i] = previousLetter ? c.toLower() : c.toUpper();
            }
            previousLetter = c.isLetter();
        }
        return out;
    }
    }
    return text;
}

double pieceCost(const Piece &piece, const QString &value, const QStringList &sources)
{
    if (piece.literal) {
        // Typed text is the last resort: a letter or digit in a literal
        // costs twice what punctuation does, so "J. Smith" reads as the
        // first letter, ". " and the last word, not as "J. " typed in.
        double cost = 1.0;
        for (const QChar c : piece.text) {
            cost += c.isLetterOrNumber() ? 2.0 : 1.0;
        }
        return cost;
    }
    const Extractor &e = piece.extractor;
    const auto direction = [&e] { return 0.05 * e.index + (e.fromEnd ? 0.02 : 0.0); };
    // A character-count cut through the middle of a word is a weaker
    // reading than one at a word's edge.
    const auto midToken = [&e, &sources](int cut) {
        const QString &text = sources.value(e.column);
        return cut > 0 && cut < text.size() && text.at(cut - 1).isLetterOrNumber() && text.at(cut).isLetterOrNumber()
            ? 0.3
            : 0.0;
    };
    double cost = 1.0;
    switch (e.kind) {
    case Extractor::Kind::Whole:
        break;
    case Extractor::Kind::Token:
        cost += classCost(e.tokenClass) + direction();
        break;
    case Extractor::Kind::Field:
        cost += 0.25 + direction() + (e.trimmed ? 0.02 : 0.0);
        break;
    case Extractor::Kind::AfterFirst:
    case Extractor::Kind::BeforeLast:
        cost += 0.30;
        break;
    case Extractor::Kind::Prefix:
        cost += 0.80 + 0.01 * e.length + midToken(e.length);
        break;
    case Extractor::Kind::Suffix:
        cost += 0.82 + 0.01 * e.length + midToken(int(sources.value(e.column).size()) - e.length);
        break;
    case Extractor::Kind::TokenPrefix:
        cost += classCost(e.tokenClass) + direction() + 0.60 + 0.01 * e.length;
        break;
    case Extractor::Kind::TokenSuffix:
        cost += classCost(e.tokenClass) + direction() + 0.65 + 0.01 * e.length;
        break;
    }
    if (piece.caseMode != CaseMode::AsIs) {
        cost += 0.10;
    }
    // A piece that starts or ends with a space is a cut in the wrong place
    // (the space belongs to a literal between pieces).
    if (!value.isEmpty() && (value.front().isSpace() || value.back().isSpace())) {
        cost += 1.0;
    }
    return cost;
}

QString describePiece(const Piece &piece, const QStringList &columnNames)
{
    if (piece.literal) {
        return quoted(piece.text);
    }
    const Extractor &e = piece.extractor;
    const QString column = columnNames.value(e.column).isEmpty() ? QStringLiteral("column %1").arg(e.column + 1)
                                                                 : columnNames.value(e.column);
    QString text;
    switch (e.kind) {
    case Extractor::Kind::Whole:
        text = column;
        break;
    case Extractor::Kind::Token:
        text = tokenPhrase(e, column);
        break;
    case Extractor::Kind::Field: {
        const QString position = e.fromEnd ? (e.index == 0 ? QStringLiteral("last") : ordinal(e.index) + QStringLiteral("-to-last"))
                                           : ordinal(e.index);
        text = QStringLiteral("the %1 part of %2 split at %3").arg(position, column, quoted(QString(e.delimiter)));
        break;
    }
    case Extractor::Kind::AfterFirst:
        text = QStringLiteral("what follows the first %1 in %2").arg(quoted(QString(e.delimiter)), column);
        break;
    case Extractor::Kind::BeforeLast:
        text = QStringLiteral("what comes before the last %1 in %2").arg(quoted(QString(e.delimiter)), column);
        break;
    case Extractor::Kind::Prefix:
        text = e.length == 1 ? QStringLiteral("the first character of %1").arg(column)
                             : QStringLiteral("the first %1 characters of %2").arg(e.length).arg(column);
        break;
    case Extractor::Kind::Suffix:
        text = e.length == 1 ? QStringLiteral("the last character of %1").arg(column)
                             : QStringLiteral("the last %1 characters of %2").arg(e.length).arg(column);
        break;
    case Extractor::Kind::TokenPrefix:
        text = e.length == 1 ? QStringLiteral("the first letter of %1").arg(tokenPhrase(e, column))
                             : QStringLiteral("the first %1 characters of %2").arg(e.length).arg(tokenPhrase(e, column));
        break;
    case Extractor::Kind::TokenSuffix:
        text = e.length == 1 ? QStringLiteral("the last letter of %1").arg(tokenPhrase(e, column))
                             : QStringLiteral("the last %1 characters of %2").arg(e.length).arg(tokenPhrase(e, column));
        break;
    }
    switch (piece.caseMode) {
    case CaseMode::AsIs: break;
    case CaseMode::Upper: text += QStringLiteral(", in capitals"); break;
    case CaseMode::Lower: text += QStringLiteral(", in lower case"); break;
    case CaseMode::Proper: text += QStringLiteral(", capitalised"); break;
    }
    return text;
}

} // namespace QindaOffice::Calc::FlashFillInternal
