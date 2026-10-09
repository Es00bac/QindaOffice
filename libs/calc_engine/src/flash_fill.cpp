// SPDX-License-Identifier: GPL-3.0-or-later
// Flash Fill (flash_fill.h, M78, D-087): the search for a programme that
// explains every example, the ranking, and the fill.
//
// AGENT-NOTE: the search walks the examples' outputs left to right at once
// — a state is how far each example's output has been explained — taking a
// piece only when it produces the next characters of EVERY example (an
// extractor's value there, or the same literal text). States are expanded
// cheapest first (uniform-cost search), so complete programmes come out in
// order of cost; at most a few partial programmes are kept per state.
//
// The ranking (pieceCost, flash_fill_atoms.cpp): every piece costs 1, so
// fewer pieces win; a literal costs one more per punctuation character and
// two per letter or digit, so taking text from the sources beats typing it
// (and "J. " is not typed when "J" can be taken); among extractors, the
// whole cell, then
// words, runs of letters or digits, fields between a delimiter, cuts after
// or before a delimiter, and last character-count cuts; a token counted
// from the nearer end of the cell is cheaper (the last word of a name is
// "the last word"); a case change costs a little; a piece beginning or
// ending with a space costs a whole piece more. Programmes made only of
// literals are never taken: a constant is not a pattern. The cheapest
// programme is then charged half a piece times the share of rows to fill
// it cannot be applied to.
//
// Ambiguity: when the two cheapest programmes cost the same (to 1e-9) and
// give different results on a row to fill — the examples fit two source
// columns that differ further down, say — nothing is filled and the user
// is asked for one more example where they differ (Excel's answer is to
// fill its guess; asking is the honest answer when the guess is a coin
// toss). Anything strictly cheaper is clearly preferred and filled.

#include "flash_fill.h"

#include "flash_fill_internal.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <queue>

namespace QindaOffice::Calc {

using namespace FlashFillInternal;

namespace {

constexpr int kKeepPerState = 4;      // partial programmes kept per search state
constexpr int kKeepPerValues = 3;     // equivalent pieces kept per value tuple
constexpr int kMaxProgrammes = 16;    // complete programmes ranked
constexpr int kMaxPops = 200000;      // the search's budget …
constexpr int kMaxNodes = 300000;     // … and its memory
constexpr double kSlack = 4.0;        // how far past the best programme the search looks
constexpr int kMaxOutput = 256;       // longer examples are not searched

struct Atom {
    Piece piece;
    QStringList values; // per example, the case applied
    double cost = 0;
};

struct Example {
    int row = 0;
    QString output;
    QStringList sources;
};

// The pieces that produce a part of every example's output.
QList<Atom> atomsFor(const QList<Example> &examples)
{
    const Example &first = examples.first();
    QList<Atom> atoms;
    for (const Offer &offer : offers(first.sources)) {
        for (const CaseMode mode : {CaseMode::AsIs, CaseMode::Upper, CaseMode::Lower, CaseMode::Proper}) {
            const QString value = applyCase(offer.value, mode);
            if (mode != CaseMode::AsIs && value == offer.value) {
                continue; // the change does nothing here: the plain piece says it
            }
            if (!first.output.contains(value)) {
                continue;
            }
            Atom atom;
            atom.piece.extractor = offer.extractor;
            atom.piece.caseMode = mode;
            atom.values.append(value);
            bool fits = true;
            for (qsizetype e = 1; e < examples.size() && fits; ++e) {
                const std::optional<QString> raw = extract(offer.extractor, examples.at(e).sources);
                const QString other = raw ? applyCase(*raw, mode) : QString();
                fits = !other.isEmpty() && examples.at(e).output.contains(other);
                atom.values.append(other);
            }
            if (fits) {
                atom.cost = pieceCost(atom.piece, value, first.sources);
                atoms.append(atom);
            }
        }
    }
    // Pieces that agree on every example are interchangeable for the
    // search; the cheapest few of each kind stay (two are enough to see a
    // tie, the ranking needs no more).
    std::stable_sort(atoms.begin(), atoms.end(), [](const Atom &a, const Atom &b) { return a.cost < b.cost; });
    QHash<QString, int> perValues;
    QList<Atom> kept;
    for (const Atom &atom : std::as_const(atoms)) {
        int &count = perValues[atom.values.join(QChar(0x1f))];
        if (count++ < kKeepPerValues) {
            kept.append(atom);
        }
    }
    return kept;
}

struct Programme {
    QList<Piece> pieces;
    double cost = 0;
};

// Uniform-cost search over how far each example is explained.
QList<Programme> searchProgrammes(const QList<Example> &examples, const QList<Atom> &atoms)
{
    const QString &out0 = examples.first().output;
    QList<QList<int>> startsAt(out0.size() + 1);
    for (int a = 0; a < atoms.size(); ++a) {
        const QString &value = atoms.at(a).values.first();
        for (qsizetype at = out0.indexOf(value); at >= 0; at = out0.indexOf(value, at + 1)) {
            startsAt[at].append(a);
        }
    }
    struct Node {
        QList<int> positions;
        int parent = -1;
        int atom = -1;      // the piece taken to get here: an atom, or
        QString literal;    // a literal when atom < 0
        double cost = 0;
    };
    QList<Node> nodes;
    struct Entry {
        double cost;
        qint64 order;
        int node;
        bool operator<(const Entry &other) const
        {
            return cost != other.cost ? cost > other.cost : order > other.order; // a min-queue, first in first out
        }
    };
    std::priority_queue<Entry> queue;
    qint64 order = 0;
    nodes.append({QList<int>(examples.size(), 0), -1, -1, QString(), 0});
    queue.push({0, order++, 0});
    QHash<QByteArray, int> pops;
    QList<Programme> found;
    double bestComplete = -1;
    int popped = 0;
    // A node is worth keeping while the memory budget lasts and it can
    // still come within the slack of the best programme found.
    const auto worthKeeping = [&nodes, &bestComplete](double cost) {
        return nodes.size() < kMaxNodes && (bestComplete < 0 || cost <= bestComplete + kSlack);
    };
    while (!queue.empty() && popped < kMaxPops && found.size() < kMaxProgrammes) {
        const Entry entry = queue.top();
        queue.pop();
        ++popped;
        if (bestComplete >= 0 && entry.cost > bestComplete + kSlack) {
            break;
        }
        const Node node = nodes.at(entry.node);
        const bool lastLiteral = node.parent >= 0 && node.atom < 0;
        QByteArray key;
        for (const int position : node.positions) {
            key += QByteArray::number(position) + ',';
        }
        key += lastLiteral ? 'L' : 'X';
        if (pops[key]++ >= kKeepPerState) {
            continue;
        }
        bool complete = true;
        for (qsizetype e = 0; e < examples.size(); ++e) {
            complete = complete && node.positions.at(e) == examples.at(e).output.size();
        }
        if (complete) {
            Programme programme;
            programme.cost = node.cost;
            bool extracts = false;
            for (int at = entry.node; nodes.at(at).parent >= 0; at = nodes.at(at).parent) {
                const Node &step = nodes.at(at);
                Piece piece;
                if (step.atom >= 0) {
                    piece = atoms.at(step.atom).piece;
                    extracts = true;
                } else {
                    piece.literal = true;
                    piece.text = step.literal;
                }
                programme.pieces.prepend(piece);
            }
            if (extracts) { // a constant is not a pattern
                found.append(programme);
                bestComplete = bestComplete < 0 ? programme.cost : bestComplete;
            }
            continue;
        }
        const int p0 = node.positions.first();
        for (const int a : startsAt.value(p0)) {
            const Atom &atom = atoms.at(a);
            QList<int> next = node.positions;
            bool fits = true;
            for (qsizetype e = 0; e < examples.size() && fits; ++e) {
                fits = QStringView(examples.at(e).output).mid(next.at(e)).startsWith(atom.values.at(e));
                next[e] += int(atom.values.at(e).size());
            }
            if (fits && worthKeeping(node.cost + atom.cost)) {
                nodes.append({next, entry.node, a, QString(), node.cost + atom.cost});
                queue.push({nodes.last().cost, order++, int(nodes.size()) - 1});
            }
        }
        if (lastLiteral) {
            continue; // two literals in a row are one literal
        }
        for (int length = 1; p0 + length <= out0.size(); ++length) {
            const QString literal = out0.mid(p0, length);
            QList<int> next = node.positions;
            bool fits = true;
            for (qsizetype e = 0; e < examples.size() && fits; ++e) {
                fits = QStringView(examples.at(e).output).mid(next.at(e)).startsWith(literal);
                next[e] += length;
            }
            if (!fits) {
                break; // a longer literal cannot fit where this one does not
            }
            Piece piece;
            piece.literal = true;
            piece.text = literal;
            const double cost = node.cost + pieceCost(piece, literal, {});
            if (!worthKeeping(cost)) {
                break; // longer literals only cost more
            }
            nodes.append({next, entry.node, -1, literal, cost});
            queue.push({nodes.last().cost, order++, int(nodes.size()) - 1});
        }
    }
    return found;
}

// The programme on one row; nullopt when the row lacks a piece it takes.
std::optional<QString> run(const Programme &programme, const QStringList &sources)
{
    QString out;
    for (const Piece &piece : programme.pieces) {
        if (piece.literal) {
            out += piece.text;
            continue;
        }
        const std::optional<QString> value = extract(piece.extractor, sources);
        if (!value) {
            return std::nullopt;
        }
        out += applyCase(*value, piece.caseMode);
    }
    return out;
}

QString describe(const Programme &programme, const QStringList &columnNames)
{
    QStringList parts;
    for (const Piece &piece : programme.pieces) {
        parts.append(describePiece(piece, columnNames));
    }
    return parts.join(QStringLiteral(", then "));
}

bool hasSource(const QStringList &sources)
{
    return std::any_of(sources.cbegin(), sources.cend(), [](const QString &text) { return !text.isEmpty(); });
}

} // namespace

FlashFillResult flashFill(const QList<QStringList> &sources, const QHash<int, QString> &examples,
                          const QStringList &columnNames)
{
    FlashFillResult result;
    const int rows = int(sources.size());
    result.outputs = QStringList(rows);
    QList<Example> taught;
    QList<int> fillRows;
    for (int row = 0; row < rows; ++row) {
        const QString example = examples.value(row);
        if (!example.isEmpty()) {
            result.outputs[row] = example;
            // An example beside empty cells teaches nothing about the cells.
            if (hasSource(sources.at(row))) {
                taught.append({row, example, sources.at(row)});
            }
        } else if (hasSource(sources.at(row))) {
            fillRows.append(row);
        }
    }
    if (taught.isEmpty()) {
        result.failure = examples.isEmpty()
            ? QStringLiteral("Type an example of what you want in the first cell beside your data, then use Flash Fill.")
            : QStringLiteral("Flash Fill needs your examples beside data in the neighbouring columns.");
        return result;
    }
    if (fillRows.isEmpty()) {
        result.failure = QStringLiteral("There is nothing left to fill: every row beside the data already has a value.");
        return result;
    }
    for (const Example &example : std::as_const(taught)) {
        if (example.output.size() > kMaxOutput) {
            result.failure = QStringLiteral("Flash Fill works on shorter text than these examples.");
            return result;
        }
    }
    QList<Programme> programmes = searchProgrammes(taught, atomsFor(taught));
    if (programmes.isEmpty()) {
        result.failure = QStringLiteral(
            "Flash Fill didn't recognize a pattern in your examples. It can split, join, change the case of and "
            "pick out parts of the text in the neighbouring columns; it cannot do arithmetic, rewrite dates or "
            "look values up. Type another example or use a formula.");
        return result;
    }
    // Ranked: cost, plus half a piece for the share of rows a programme
    // cannot be applied to.
    struct Ranked {
        const Programme *programme;
        QList<std::optional<QString>> outputs;
        double score;
    };
    QList<Ranked> ranked;
    for (const Programme &programme : std::as_const(programmes)) {
        Ranked entry{&programme, {}, programme.cost};
        int failed = 0;
        for (const int row : std::as_const(fillRows)) {
            entry.outputs.append(run(programme, sources.at(row)));
            failed += entry.outputs.last().has_value() ? 0 : 1;
        }
        entry.score += 0.5 * double(failed) / double(fillRows.size());
        ranked.append(entry);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.score < b.score; });
    const Ranked &best = ranked.first();
    for (qsizetype other = 1; other < ranked.size(); ++other) {
        if (std::abs(ranked.at(other).score - best.score) > 1e-9) {
            break;
        }
        for (qsizetype i = 0; i < fillRows.size(); ++i) {
            if (ranked.at(other).outputs.at(i) != best.outputs.at(i)) {
                result.ambiguousRow = fillRows.at(i);
                result.failure = QStringLiteral("Your examples fit more than one pattern (%1; or %2), and they give "
                                                "different results further down. Type one more example where they "
                                                "differ, then use Flash Fill again.")
                                     .arg(describe(*best.programme, columnNames),
                                          describe(*ranked.at(other).programme, columnNames));
                return result;
            }
        }
    }
    for (qsizetype i = 0; i < fillRows.size(); ++i) {
        const QString value = best.outputs.at(i).value_or(QString());
        result.outputs[fillRows.at(i)] = value;
        result.filled += value.isEmpty() ? 0 : 1;
    }
    result.ok = true;
    result.pattern = describe(*best.programme, columnNames);
    return result;
}

} // namespace QindaOffice::Calc
