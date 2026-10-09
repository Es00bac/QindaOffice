// SPDX-License-Identifier: GPL-3.0-or-later
// What a .qplan saved by M63–M78 kept verbatim from Microsoft Project XML
// and M79 now models (D-088): baselines 3–10 and assignment baselines, a
// task's LevelingDelay / LevelingDelayFormat / PreLeveledStart /
// PreLeveledFinish, and the project's CriticalSlackLimit and
// MultipleCriticalPaths. readPlanJson lifts them into the model when it
// loads such a file (mspdi.h adoptKeptElements), and derives every
// calendar's recurring exceptions and work weeks from their kept elements.
//
// AGENT-GUARD: without this, the writer would emit those elements twice —
// once replayed from the kept list, once from the model — and Project
// refuses a repeated element in its ordered sequences.
#include "mspdi.h"
#include "mspdi_keep.h"
#include "mspdi_p.h"

#include "plan_time.h"

#include <QXmlStreamReader>

namespace QindaOffice::Plan {

namespace Mspdi {

namespace {

QString textOf(const QString &fragment)
{
    QXmlStreamReader in(fragment);
    return in.readNextStartElement() ? in.readElementText(QXmlStreamReader::SkipChildElements).trimmed() : QString();
}

// Takes the kept elements called `names` out of `extra`'s kept list.
QHash<QString, QString> take(QJsonObject &extra, const QStringList &names)
{
    QHash<QString, QString> taken;
    QList<Kept> rest;
    for (const Kept &k : keptFromJson(extra.value(kKeptKey))) {
        if (names.contains(k.name) && !taken.contains(k.name)) {
            taken.insert(k.name, k.xml);
        } else {
            rest << k;
        }
    }
    if (taken.isEmpty()) {
        return taken;
    }
    if (rest.isEmpty()) {
        extra.remove(kKeptKey);
    } else {
        extra.insert(kKeptKey, keptToJson(rest));
    }
    return taken;
}

// Lifts the kept <Baseline> elements of one object's extra into `list`
// (a number the model already holds keeps the model's) and its fragments.
int adoptBaselines(QJsonObject &extra, std::array<BaselineData, kBaselineCount> &list, int decimals)
{
    QList<Kept> rest;
    QList<RawBaseline> adopted;
    for (const Kept &k : keptFromJson(extra.value(kKeptKey))) {
        const RawBaseline b = k.name == QLatin1String("Baseline") ? parseBaseline(k.xml) : RawBaseline{};
        if (k.name != QLatin1String("Baseline") || b.number < 0 || b.number >= kBaselineCount
            || list[size_t(b.number)].set) {
            rest << k;
            continue;
        }
        BaselineData data = b.data;
        data.cost = centsToMinor(b.costCents, decimals);
        list[size_t(b.number)] = data;
        adopted << b;
    }
    if (adopted.isEmpty()) {
        return 0;
    }
    QList<RawBaseline> all = adopted;
    const QHash<int, QString> existing = baselineFragments(extra);
    for (auto it = existing.cbegin(); it != existing.cend(); ++it) {
        RawBaseline b;
        b.number = it.key();
        b.fragment = it.value();
        all << b;
    }
    storeBaselineFragments(extra, all);
    if (rest.isEmpty()) {
        extra.remove(kKeptKey);
    } else {
        extra.insert(kKeptKey, keptToJson(rest));
    }
    return int(adopted.size());
}

int adoptLeveling(Task &t)
{
    const QHash<QString, QString> kept = take(t.extra, {QStringLiteral("LevelingDelay"), QStringLiteral("LevelingDelayFormat"),
                                                       QStringLiteral("PreLeveledStart"), QStringLiteral("PreLeveledFinish")});
    if (kept.contains(QStringLiteral("LevelingDelay")) && t.levelingDelay == 0) {
        const int format = kept.contains(QStringLiteral("LevelingDelayFormat"))
                               ? textOf(kept.value(QStringLiteral("LevelingDelayFormat"))).toInt()
                               : 8; // Project's default, elapsed days
        const DurationCode code = durationCode(format);
        if (code.known && !code.percent) {
            t.levelingDelay = std::max<Minutes>(0, divRound(scaledDecimal(textOf(kept.value(QStringLiteral("LevelingDelay"))), 0)
                                                                .value_or(0),
                                                            10));
            t.levelingDelayElapsed = code.elapsed;
            t.extra.insert(kLevelingFormatKey, format);
        }
    }
    if (kept.contains(QStringLiteral("PreLeveledStart")) && t.preLeveledStart == kNoDate) {
        t.preLeveledStart = parseWireDateTime(textOf(kept.value(QStringLiteral("PreLeveledStart"))));
    }
    if (kept.contains(QStringLiteral("PreLeveledFinish")) && t.preLeveledFinish == kNoDate) {
        t.preLeveledFinish = parseWireDateTime(textOf(kept.value(QStringLiteral("PreLeveledFinish"))));
    }
    return int(kept.size());
}

} // namespace

} // namespace Mspdi

int adoptKeptElements(Project &project)
{
    using namespace Mspdi;
    const int decimals = currencyDecimals(project.info.currency);
    int adopted = 0;
    // The project's critical-path options.
    const QHash<QString, QString> options =
        take(project.info.extra, {QStringLiteral("CriticalSlackLimit"), QStringLiteral("MultipleCriticalPaths")});
    if (options.contains(QStringLiteral("CriticalSlackLimit"))) {
        project.info.criticalSlackDays = std::clamp(textOf(options.value(QStringLiteral("CriticalSlackLimit"))).toInt(), 0, 999);
    }
    if (options.contains(QStringLiteral("MultipleCriticalPaths"))) {
        const QString v = textOf(options.value(QStringLiteral("MultipleCriticalPaths")));
        project.info.multipleCriticalPaths = v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    adopted += int(options.size());
    // The project summary task's kept baselines become its fragments (the
    // summary has no model baselines; the writer replays or rolls up).
    QJsonObject summary{{kKeptKey, project.info.extra.value(kSummaryKey)}};
    std::array<BaselineData, kBaselineCount> unused;
    if (adoptBaselines(summary, unused, decimals) > 0) {
        project.info.extra.insert(kSummaryBaselinesKey, summary.value(kBaselinesKey));
        if (summary.contains(kKeptKey)) {
            project.info.extra.insert(kSummaryKey, summary.value(kKeptKey));
        } else {
            project.info.extra.remove(kSummaryKey);
        }
    }
    for (Task &t : project.tasks) {
        adopted += adoptBaselines(t.extra, t.baselines, decimals);
        adopted += adoptLeveling(t);
    }
    // Recurring exceptions and work weeks are derived, never stored: every
    // load rebuilds them from the calendar's kept elements.
    for (Calendar &c : project.calendars) {
        (void)deriveRecurrences(c);
    }
    for (Assignment &a : project.assignments) {
        adopted += adoptBaselines(a.extra, a.baselines, decimals);
    }
    return adopted;
}

} // namespace QindaOffice::Plan
