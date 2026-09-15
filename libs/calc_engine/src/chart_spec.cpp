// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_spec.h"

namespace QindaOffice::Calc {

namespace {

bool parseEndpoint(const QString &token, qint32 *row, qint32 *col)
{
    QString t = token;
    t.remove(QLatin1Char('$'));
    int split = 0;
    while (split < t.size() && t.at(split).isLetter()) {
        ++split;
    }
    if (split == 0 || split == t.size()) {
        return false;
    }
    qint32 c = 0;
    for (int i = 0; i < split; ++i) {
        const QChar ch = t.at(i).toUpper();
        if (ch < QLatin1Char('A') || ch > QLatin1Char('Z')) {
            return false;
        }
        c = c * 26 + (ch.unicode() - QLatin1Char('A').unicode() + 1);
    }
    bool ok = false;
    const qint32 r = t.mid(split).toInt(&ok);
    if (!ok || r < 1 || c < 1) {
        return false;
    }
    *row = r - 1; // engine addresses are 0-based
    *col = c - 1;
    return true;
}

} // namespace

CellRange parseChartRange(const QString &text)
{
    CellRange range;
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        qint32 row;
        qint32 col;
        if (!parseEndpoint(text.trimmed(), &row, &col)) {
            return {};
        }
        range.topLeft = {row, col};
        range.bottomRight = {row, col};
        return range;
    }
    qint32 r1;
    qint32 c1;
    qint32 r2;
    qint32 c2;
    if (!parseEndpoint(text.left(colon).trimmed(), &r1, &c1)
        || !parseEndpoint(text.mid(colon + 1).trimmed(), &r2, &c2)) {
        return {};
    }
    range.topLeft = {qMin(r1, r2), qMin(c1, c2)};
    range.bottomRight = {qMax(r1, r2), qMax(c1, c2)};
    return range;
}

} // namespace QindaOffice::Calc
