// SPDX-License-Identifier: GPL-3.0-or-later
// Baselines in Microsoft Project XML (M79, D-088; mspdi_p.h has the
// contract): one <Baseline> parsed, the kept fragments by number, and the
// "does the file's element still say this?" test the writer replays by.
#include "mspdi_p.h"

#include "plan_time.h"

#include <QJsonArray>
#include <QXmlStreamReader>

namespace QindaOffice::Plan {

namespace Mspdi {

RawBaseline parseBaseline(const QString &fragment)
{
    RawBaseline b;
    b.number = -1;
    b.fragment = fragment;
    b.data.set = true;
    QXmlStreamReader in(fragment);
    if (!in.readNextStartElement()) {
        return b;
    }
    while (in.readNextStartElement()) {
        const QStringView n = in.name();
        const QString text = in.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (n == u"Number") {
            bool ok = false;
            const int number = text.toInt(&ok);
            b.number = ok ? number : -1;
        } else if (n == u"Start") {
            b.data.start = parseWireDateTime(text);
        } else if (n == u"Finish") {
            b.data.finish = parseWireDateTime(text);
        } else if (n == u"Duration") {
            b.data.duration = parseWireDuration(text).value_or(0);
        } else if (n == u"Work") {
            b.data.work = parseWireDuration(text).value_or(0);
        } else if (n == u"Cost") {
            b.costCents = scaledDecimal(text, 0).value_or(0);
        }
    }
    return b;
}

QHash<int, QString> baselineFragments(const QJsonObject &extra, const QString &key)
{
    QHash<int, QString> out;
    for (const QJsonValue &v : extra.value(key).toArray()) {
        const QJsonObject o = v.toObject();
        const int n = o.value(QStringLiteral("n")).toInt(-1);
        const QString xml = o.value(QStringLiteral("xml")).toString();
        if (n >= 0 && n < kBaselineCount && !xml.isEmpty()) {
            out.insert(n, xml);
        }
    }
    return out;
}

void storeBaselineFragments(QJsonObject &extra, const QList<RawBaseline> &baselines, const QString &key)
{
    QJsonArray out;
    for (const RawBaseline &b : baselines) {
        if (b.number >= 0 && b.number < kBaselineCount && !b.fragment.isEmpty()) {
            out.append(QJsonObject{{QStringLiteral("n"), b.number}, {QStringLiteral("xml"), b.fragment}});
        }
    }
    if (out.isEmpty()) {
        extra.remove(key);
    } else {
        extra.insert(key, out);
    }
}

bool baselineMatches(const QString &fragment, const BaselineData &model, int decimals, bool assignment)
{
    const RawBaseline wire = parseBaseline(fragment);
    return model.set && wire.data.start == model.start && wire.data.finish == model.finish
        && (assignment || wire.data.duration == model.duration) && wire.data.work == model.work
        && centsToMinor(wire.costCents, decimals) == model.cost;
}

} // namespace Mspdi

} // namespace QindaOffice::Plan
