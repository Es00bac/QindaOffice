// SPDX-License-Identifier: GPL-3.0-or-later
// Writing a sheet's conditional formats (xlsx_rules.h, M78, D-087): the
// model's rules in Excel's markup — one <conditionalFormatting> per sqref,
// a data bar's x14 twin and Excel 2010's icon sets in the worksheet's
// extLst — and the rules kept as written, byte for byte unless their cells
// moved (only the sqref is rewritten then) or the priorities had to be
// renumbered. Validations are xlsx_write_validation.cpp's.
//
// AGENT-CONTRACT (priorities): a rule's priority is its place in Excel's
// precedence across BOTH markups. While the model's rules still carry the
// priorities they were read with, in increasing list order, and no number
// is used twice, every rule — modelled and kept — keeps its number. Else
// all are renumbered 1..N: the model's rules in list order (the engine's
// precedence), each kept rule placed before the first model rule that came
// after it in the file.

#include "xlsx_rules.h"

#include "xlsx_rules_internal.h"

#include "workbook.h"

#include <QObject>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace QindaOffice::Calc::IO {

using namespace Rules;

namespace {

using Op = CondFormatRule::Op;

struct Placed {
    QString xml;
    int priority = 0; // where the block sorts
};

bool usesDxf(const CondFormatRule &rule)
{
    return rule.op != Op::ColorScale && rule.op != Op::DataBar && rule.op != Op::IconSet;
}

bool isFormatKind(KeptRule::Kind kind)
{
    return kind == KeptRule::Kind::CondFormat || kind == KeptRule::Kind::CondFormatX14;
}

// A {GUID} for a rule that has none: the same rule on the same sheet gets
// the same one on every save.
QString ruleId(const Sheet &sheet, qsizetype index, const CondFormatRule &rule)
{
    static const QUuid space(QStringLiteral("{3c3f1e64-5f0e-4f8f-9b3e-7a1d2c5e6f70}"));
    const QString key = sheet.name() + QLatin1Char('|') + QString::number(index) + QLatin1Char('|')
        + sqrefText(rule.ranges()) + QLatin1Char('|') + QString::number(int(rule.op));
    return QUuid::createUuidV5(space, key).toString(QUuid::WithBraces).toUpper();
}

// The priorities every rule is written with (see the contract above).
void assignPriorities(const QList<CondFormatRule> &rules, const QList<const KeptRule *> &kept, QList<int> *modelled,
                      QList<QList<int>> *keptOut)
{
    QSet<int> seen;
    bool keep = true;
    int last = 0;
    for (const CondFormatRule &rule : rules) {
        keep = keep && rule.priority > last && !seen.contains(rule.priority);
        last = qMax(last, rule.priority);
        seen.insert(rule.priority);
    }
    for (const KeptRule *rule : kept) {
        for (const int priority : rule->priorities) {
            keep = keep && priority > 0 && !seen.contains(priority);
            seen.insert(priority);
        }
    }
    modelled->clear();
    keptOut->clear();
    for (const KeptRule *rule : kept) {
        keptOut->append(rule->priorities);
    }
    if (keep) {
        for (const CondFormatRule &rule : rules) {
            modelled->append(rule.priority);
        }
        return;
    }
    // Renumber: model rules in list order, kept rules slotted by priority.
    struct Slot {
        int rule = -1;   // model rule index, or -1
        int kept = -1;   // kept rule index …
        int within = -1; // … and its cfRule
    };
    QList<Slot> order;
    for (qsizetype i = 0; i < rules.size(); ++i) {
        order.append(Slot{int(i), -1, -1});
    }
    struct Entry {
        int old;
        int kept;
        int within;
    };
    QList<Entry> entries;
    for (qsizetype k = 0; k < kept.size(); ++k) {
        for (qsizetype j = 0; j < kept.at(k)->priorities.size(); ++j) {
            entries.append(Entry{kept.at(k)->priorities.at(j), int(k), int(j)});
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.old < b.old; });
    for (const Entry &entry : std::as_const(entries)) {
        qsizetype at = order.size();
        for (qsizetype i = 0; i < order.size(); ++i) {
            const Slot &slot = order.at(i);
            if (slot.rule >= 0 && rules.at(slot.rule).priority > 0 && rules.at(slot.rule).priority > entry.old) {
                at = i;
                break;
            }
        }
        order.insert(at, Slot{-1, entry.kept, entry.within});
    }
    for (qsizetype i = 0; i < rules.size(); ++i) {
        modelled->append(0);
    }
    for (qsizetype n = 0; n < order.size(); ++n) {
        const Slot &slot = order.at(n);
        if (slot.rule >= 0) {
            (*modelled)[slot.rule] = int(n + 1);
        } else {
            (*keptOut)[slot.kept][slot.within] = int(n + 1);
        }
    }
}

// The cfRule elements directly inside a kept block get `priorities`, in
// order (rules without a priority attribute — x14 twins — are skipped).
QString withPriorities(const QString &xml, const QList<int> &old, const QList<int> &now)
{
    if (old == now) {
        return xml;
    }
    QString out = xml;
    int depth = -1;
    qsizetype next = 0;
    qsizetype pos = 0;
    while ((pos = out.indexOf(QLatin1Char('<'), pos)) >= 0 && next < now.size()) {
        const QChar kind = pos + 1 < out.size() ? out.at(pos + 1) : QChar();
        qsizetype end = pos + 1;
        char16_t quote = 0;
        for (; end < out.size(); ++end) {
            const char16_t c = out.at(end).unicode();
            if (quote != 0) {
                quote = c == quote ? 0 : quote;
            } else if (c == u'"' || c == u'\'') {
                quote = c;
            } else if (c == u'>') {
                break;
            }
        }
        if (kind == QLatin1Char('/')) {
            --depth;
        } else if (kind != QLatin1Char('?') && kind != QLatin1Char('!')) {
            ++depth;
            QString tag = out.mid(pos, end + 1 - pos);
            const QString name = tag.mid(1).section(QRegularExpression(QStringLiteral("[\\s/>]")), 0, 0);
            if (depth == 1 && name.section(QLatin1Char(':'), -1) == QLatin1String("cfRule")
                && tag.contains(QRegularExpression(QStringLiteral("\\spriority\\s*=")))) {
                tag = withAttribute(tag, QStringLiteral("priority"), QString::number(now.at(next++)));
                out.replace(pos, end + 1 - pos, tag);
                end = pos + tag.size() - 1;
            }
            if (out.at(end - 1) == QLatin1Char('/')) {
                --depth;
            }
        }
        pos = end + 1;
    }
    return out;
}

QString x14Block(const QString &sqref, const QString &rules)
{
    return QStringLiteral("<x14:conditionalFormatting xmlns:xm=\"%1\">").arg(QString::fromUtf16(kXmNs)) + rules
        + QStringLiteral("<xm:sqref>") + sqref + QStringLiteral("</xm:sqref></x14:conditionalFormatting>");
}

void fillFormats(const Sheet &sheet, DxfTable *dxfs, PatchManifest::SheetPatch *patch)
{
    const QList<CondFormatRule> &rules = sheet.condFormats();
    QList<const KeptRule *> kept;
    for (const KeptRule &rule : sheet.keptRules()) {
        if (isFormatKind(rule.kind)) {
            kept.append(&rule);
        }
    }
    if (rules.isEmpty() && kept.isEmpty()) {
        return;
    }
    QList<int> priorities;
    QList<QList<int>> keptPriorities;
    assignPriorities(rules, kept, &priorities, &keptPriorities);

    // The model's rules, grouped by sqref (first-seen order), each group
    // sorted by priority.
    struct Group {
        QString sqref;
        QList<QPair<int, QString>> rules; // priority, markup
    };
    QList<Group> mainGroups;
    QList<Group> x14Groups;
    const auto add = [](QList<Group> &groups, const QString &sqref, int priority, const QString &xml) {
        for (Group &group : groups) {
            if (group.sqref == sqref) {
                group.rules.append({priority, xml});
                return;
            }
        }
        groups.append(Group{sqref, {{priority, xml}}});
    };
    for (qsizetype i = 0; i < rules.size(); ++i) {
        const CondFormatRule &rule = rules.at(i);
        const QString sqref = sqrefText(rule.ranges());
        const QString id = rule.x14Id.isEmpty() ? ruleId(sheet, i, rule) : rule.x14Id;
        if (isX14Only(rule)) {
            add(x14Groups, sqref, priorities.at(i), QString::fromUtf8(x14RuleXml(rule, priorities.at(i), id)));
            continue;
        }
        const bool twin = needsX14Twin(rule);
        const int dxfId = usesDxf(rule) ? dxfs->idFor(rule) : -1;
        add(mainGroups, sqref, priorities.at(i),
            QString::fromUtf8(cfRuleXml(rule, priorities.at(i), dxfId, twin ? id : QString())));
        if (twin) {
            add(x14Groups, sqref, priorities.at(i), QString::fromUtf8(x14RuleXml(rule, priorities.at(i), id)));
        }
    }
    QList<Placed> main;
    QList<Placed> x14;
    for (Group &group : mainGroups) {
        std::stable_sort(group.rules.begin(), group.rules.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        QString xml = QStringLiteral("<conditionalFormatting sqref=\"%1\">").arg(group.sqref);
        for (const auto &[priority, markup] : std::as_const(group.rules)) {
            xml += markup;
        }
        main.append(Placed{xml + QStringLiteral("</conditionalFormatting>"), group.rules.first().first});
    }
    for (Group &group : x14Groups) {
        std::stable_sort(group.rules.begin(), group.rules.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        QString markup;
        for (const auto &[priority, rule] : std::as_const(group.rules)) {
            markup += rule;
        }
        x14.append(Placed{x14Block(group.sqref, markup), group.rules.first().first});
    }
    // The kept blocks, as written: only a moved sqref and renumbered
    // priorities change.
    for (qsizetype k = 0; k < kept.size(); ++k) {
        const KeptRule &rule = *kept.at(k);
        const bool x14Kind = rule.kind == KeptRule::Kind::CondFormatX14;
        const QString xml = withPriorities(withRanges(rule.xml, rule.ranges, x14Kind), rule.priorities, keptPriorities.at(k));
        const int first = keptPriorities.at(k).isEmpty() ? std::numeric_limits<int>::max()
                                                         : *std::min_element(keptPriorities.at(k).begin(), keptPriorities.at(k).end());
        (x14Kind ? x14 : main).append(Placed{xml, first});
    }
    const auto byPriority = [](const Placed &a, const Placed &b) { return a.priority < b.priority; };
    std::stable_sort(main.begin(), main.end(), byPriority);
    std::stable_sort(x14.begin(), x14.end(), byPriority);
    QString mainXml;
    for (const Placed &block : std::as_const(main)) {
        mainXml += block.xml;
    }
    patch->condFormatXml = mainXml.toUtf8();
    if (!x14.isEmpty()) {
        QString ext = QStringLiteral("<ext uri=\"%1\" xmlns:x14=\"%2\"><x14:conditionalFormattings>")
                          .arg(QString::fromUtf16(kCfExtUri), QString::fromUtf16(kX14Ns));
        for (const Placed &block : std::as_const(x14)) {
            ext += block.xml;
        }
        patch->extXml += (ext + QStringLiteral("</x14:conditionalFormattings></ext>")).toUtf8();
    }
}

} // namespace

namespace Rules {

QString withRanges(const QString &xml, const QList<CellRange> &ranges, bool x14)
{
    if (ranges.isEmpty()) {
        return xml; // a sqref the reader could not read stays as it was
    }
    if (!x14) {
        static const QRegularExpression sqref(QStringLiteral("\\ssqref\\s*=\\s*(\"([^\"]*)\"|'([^']*)')"));
        const QRegularExpressionMatch match = sqref.match(firstStartTag(xml));
        const QString old = match.captured(2).isNull() ? match.captured(3) : match.captured(2);
        if (match.hasMatch() && parseSqref(old) == ranges) {
            return xml;
        }
        return withAttribute(xml, QStringLiteral("sqref"), sqrefText(ranges));
    }
    // The element's own <xm:sqref> is its last child.
    static const QRegularExpression element(QStringLiteral("<((?:[\\w.-]+:)?sqref)>([^<]*)</\\1>"));
    QRegularExpressionMatch last;
    for (auto it = element.globalMatch(xml); it.hasNext();) {
        last = it.next();
    }
    if (!last.hasMatch() || parseSqref(last.captured(2).trimmed()) == ranges) {
        return xml;
    }
    QString out = xml;
    out.replace(last.capturedStart(2), last.capturedLength(2), sqrefText(ranges));
    return out;
}

} // namespace Rules

void fillRulesPatch(const Workbook &wb, const Sheet &sheet, DxfTable *dxfs, HiddenLists *lists,
                    PatchManifest::SheetPatch *patch, QStringList *warnings)
{
    fillFormats(sheet, dxfs, patch);
    fillValidationPatch(wb, sheet, lists, patch, warnings);
    // M90 (D-098): a kept rule's formulas follow structural edits like
    // cell formulas (structure_kept.cpp); one the engine could not parse
    // stays as written — say so.
    if (warnings == nullptr) {
        return;
    }
    int stale = 0;
    for (const KeptRule &rule : sheet.keptRules()) {
        stale += rule.staleFormulas ? 1 : 0;
    }
    if (stale > 0) {
        const QString message = QObject::tr("%n conditional-format or validation rule(s) on “%1” that QindaCalc keeps as "
                                            "Excel wrote them hold a formula QindaCalc cannot read; it still names the "
                                            "cells it named before rows or columns were inserted or deleted.",
                                            nullptr, stale)
                                    .arg(sheet.name());
        if (!warnings->contains(message)) {
            warnings->append(message);
        }
    }
}

} // namespace QindaOffice::Calc::IO
