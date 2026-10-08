// SPDX-License-Identifier: GPL-3.0-or-later
// TEXTBEFORE and TEXTAFTER (M61, D-069): the text before or after the n-th
// delimiter. Excel's rules, as documented and as LibreOffice implements
// them: instance_num counts from the end when negative and may not be 0 or
// longer than the text; match_mode 1 ignores case; match_end 1 lets the end
// of the text (the start, counting backwards) stand in for one more
// delimiter; a delimiter argument may be a list of alternatives; an empty
// delimiter matches at the very start (the very end, counting backwards).

#include "functions.h"

#include "../eval_array.h"

namespace QindaOffice::Calc {

namespace {

struct Hit {
    qsizetype at = 0;
    qsizetype length = 0;
};

// The occurrence of any delimiter at `at` (the longest, when several match
// there), or length -1.
qsizetype delimiterAt(const QString &text, qsizetype at, const QStringList &delimiters, Qt::CaseSensitivity cs)
{
    qsizetype best = -1;
    for (const QString &delimiter : delimiters) {
        if (QStringView(text).mid(at).startsWith(delimiter, cs)) {
            best = qMax(best, delimiter.size());
        }
    }
    return best;
}

// Up to `count` delimiter occurrences, scanning forwards or backwards; an
// occurrence is never counted inside another one.
QList<Hit> occurrences(const QString &text, const QStringList &delimiters, Qt::CaseSensitivity cs, bool backwards,
                       qsizetype count)
{
    QList<Hit> hits;
    if (!backwards) {
        for (qsizetype at = 0; at <= text.size() && hits.size() < count;) {
            const qsizetype length = delimiterAt(text, at, delimiters, cs);
            if (length < 0) {
                ++at;
                continue;
            }
            hits.append({at, length});
            at += qMax<qsizetype>(length, 1);
        }
        return hits;
    }
    for (qsizetype end = text.size(); end >= 0 && hits.size() < count;) {
        // The longest delimiter ending exactly at `end`.
        qsizetype found = -1;
        for (const QString &delimiter : delimiters) {
            const qsizetype start = end - delimiter.size();
            if (start >= 0 && QStringView(text).mid(start, delimiter.size()).compare(delimiter, cs) == 0) {
                found = qMax(found, delimiter.size());
            }
        }
        if (found < 0) {
            --end;
            continue;
        }
        hits.append({end - found, found});
        end -= qMax<qsizetype>(found, 1);
    }
    return hits;
}

FunctionSpec textAround(bool before)
{
    return FunctionSpec{2, 6, false, [before](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        const Value textValue = scalarOrValue(args.at(0));
        if (isErr(textValue)) return textValue;
        const QString text = toText(textValue);
        QVector<Value> rawDelimiters;
        if (const Value err = flatArgValues(ctx, args.at(1), &rawDelimiters); isErr(err)) return err;
        QStringList delimiters;
        for (const Value &v : std::as_const(rawDelimiters)) {
            delimiters.append(toText(v));
        }
        double n = 1.0;
        double mode = 0.0;
        double matchEnd = 0.0;
        double *const optionals[] = {&n, &mode, &matchEnd};
        for (int i = 2; i < qMin<qsizetype>(args.size(), 5); ++i) {
            const Value v = scalarOrValue(args.at(i));
            if (isErr(v)) return v;
            const auto d = toNumber(v);
            if (!d) return Err::Value;
            *optionals[i - 2] = std::trunc(*d);
        }
        if (n == 0.0 || std::abs(n) > double(text.size()) || delimiters.isEmpty()) {
            return Err::Value;
        }
        const bool backwards = n < 0.0;
        const auto wanted = qsizetype(std::abs(n));
        const QList<Hit> hits = occurrences(text, delimiters, mode != 0.0 ? Qt::CaseInsensitive : Qt::CaseSensitive,
                                            backwards, wanted);
        Hit hit;
        if (hits.size() == wanted) {
            hit = hits.last();
        } else if (matchEnd != 0.0 && hits.size() == wanted - 1) {
            hit = backwards ? Hit{0, 0} : Hit{text.size(), 0}; // the text's edge counts once
        } else {
            return args.size() > 5 ? scalarOrValue(args.at(5)) : Value(Err::NA);
        }
        return before ? text.left(hit.at) : text.mid(hit.at + hit.length);
    }};
}

} // namespace

void registerTextMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("TEXTBEFORE"), textAround(true));
    r.insert(QStringLiteral("TEXTAFTER"), textAround(false));
}

} // namespace QindaOffice::Calc
