// SPDX-License-Identifier: GPL-3.0-or-later
// Reading conditional formats, modelled and kept (xlsx_rules.h, M78,
// D-087): every cfRule of the 2007 markup and Excel 2010's x14 rules into
// CondFormatRule where the model holds the rule exactly — a 2007 data bar
// merged with its x14 twin —, and every other rule into Sheet::keptRules as
// the text it was read as. Validations are xlsx_read_validation.cpp's.
//
// AGENT-GUARD: a 2007 rule and its x14 twin (tied by {GUID}) are modelled
// together or kept together — a kept 2007 rule naming a twin the save
// rewrote would point at nothing, and a twin modelled on its own would be
// written twice.

#include "xlsx_rules.h"

#include "xlsx_rules_internal.h"

#include "xlsx_parts.h"

#include "sheet.h"
#include "workbook.h"

#include <QObject>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

using namespace Rules;

namespace {

enum class TwinState { Free, Modelled, Kept };

struct X14Rule {
    const RawNode *rule = nullptr;
    TwinState state = TwinState::Free;
    bool blockKept = false; // its whole x14:conditionalFormatting stays as written
};

struct Reading {
    const RuleXml &xml;
    QHash<QString, X14Rule> x14; // x14 cfRule id → the rule
    RuleContext context;
    QList<CondFormatRule> modelled;
    QList<KeptRule> kept;
    int keptRules = 0;
};

void keep(Reading &r, KeptRule::Kind kind, const QList<CellRange> &ranges, const QString &xml, const QList<int> &priorities,
          int rules, const QString &reason)
{
    KeptRule kept;
    kept.kind = kind;
    kept.ranges = ranges;
    kept.xml = xml;
    kept.priorities = priorities;
    kept.reason = reason;
    r.kept.append(kept);
    r.keptRules += rules;
}

void readBlock(Reading &r, const RawNode &block)
{
    const QList<CellRange> ranges = parseSqref(block.attr(u"sqref"));
    const QList<const RawNode *> rules = block.childrenNamed(u"cfRule");
    QList<int> allPriorities;
    for (const RawNode *rule : rules) {
        allPriorities.append(rule->attr(u"priority").toInt());
    }
    const auto keepTwins = [&r](const RawNode &rule) {
        for (const RawNode &ext : rule.child(u"extLst") ? rule.child(u"extLst")->children : QList<RawNode>()) {
            if (const RawNode *id = ext.child(u"id"); id != nullptr && r.x14.contains(id->text.trimmed())) {
                r.x14[id->text.trimmed()].state = TwinState::Kept;
            }
        }
    };
    const bool whole = ranges.isEmpty() || !onlyKnownAttributes(block, {"sqref"}) || rules.size() != block.children.size();
    if (whole) {
        for (const RawNode *rule : rules) {
            keepTwins(*rule);
        }
        keep(r, KeptRule::Kind::CondFormat, ranges, selfContained(r.xml.text, block, worksheetScope()), allPriorities,
             int(rules.size()), block.has(u"pivot") ? QStringLiteral("a PivotTable's format") : QStringLiteral("its range or shape"));
        return;
    }
    QString keptXml;
    QList<int> keptPriorities;
    int keptCount = 0;
    QHash<QString, QString> scope = worksheetScope();
    scope.insert(block.declared);
    for (const RawNode *rule : rules) {
        QString twin;
        if (const std::optional<CondFormatRule> modelled = modelCfRule(r.context, *rule, ranges, &twin)) {
            r.modelled.append(*modelled);
            if (!twin.isEmpty()) {
                r.x14[twin].state = TwinState::Modelled;
            }
            continue;
        }
        keepTwins(*rule);
        keptXml += selfContained(r.xml.text, *rule, scope);
        keptPriorities.append(rule->attr(u"priority").toInt());
        ++keptCount;
    }
    if (keptCount == rules.size()) {
        keep(r, KeptRule::Kind::CondFormat, ranges, selfContained(r.xml.text, block, worksheetScope()), allPriorities,
             keptCount, QStringLiteral("a rule kind or setting QindaCalc does not model"));
    } else if (keptCount > 0) {
        keep(r, KeptRule::Kind::CondFormat, ranges,
             startTag(r.xml.text, block) + keptXml + QStringLiteral("</") + block.qname + QLatin1Char('>'), keptPriorities,
             keptCount, QStringLiteral("a rule kind or setting QindaCalc does not model"));
    }
}

// An x14:conditionalFormatting the model cannot take apart: its sqref does
// not read, or it holds more than rules and their sqref.
bool x14BlockKept(const RawNode &block)
{
    const RawNode *sqref = block.child(u"sqref");
    return sqref == nullptr || parseSqref(sqref->text.trimmed()).isEmpty() || !onlyKnownAttributes(block, {})
        || block.childrenNamed(u"cfRule").size() + 1 != block.children.size();
}

void readX14Block(Reading &r, const RawNode &block)
{
    const RawNode *sqref = block.child(u"sqref");
    const QList<CellRange> ranges = sqref != nullptr ? parseSqref(sqref->text.trimmed()) : QList<CellRange>();
    const QList<const RawNode *> rules = block.childrenNamed(u"cfRule");
    const bool whole = x14BlockKept(block);
    QString keptXml;
    QList<int> keptPriorities;
    int keptCount = 0;
    int consumed = 0;
    QHash<QString, QString> scope = x14Scope(false);
    scope.insert(block.declared);
    for (const RawNode *rule : rules) {
        const QString id = rule->attr(u"id");
        const TwinState state = r.x14.value(id).state;
        if (state == TwinState::Modelled && !whole) {
            ++consumed;
            continue;
        }
        if (state == TwinState::Free && !whole && !ranges.isEmpty()) {
            if (const std::optional<CondFormatRule> modelled = modelX14CfRule(r.context, *rule, ranges)) {
                r.modelled.append(*modelled);
                ++consumed;
                continue;
            }
        }
        keptXml += selfContained(r.xml.text, *rule, scope);
        if (rule->has(u"priority")) {
            keptPriorities.append(rule->attr(u"priority").toInt());
        }
        ++keptCount;
    }
    if (keptCount == 0) {
        return;
    }
    if (consumed == 0) {
        keep(r, KeptRule::Kind::CondFormatX14, ranges, selfContained(r.xml.text, block, x14Scope(false)), keptPriorities,
             keptCount, QStringLiteral("an Excel 2010 rule QindaCalc does not model"));
        return;
    }
    keep(r, KeptRule::Kind::CondFormatX14, ranges,
         startTag(r.xml.text, block) + keptXml + (sqref != nullptr ? r.xml.text.mid(sqref->start, sqref->end - sqref->start) : QString())
             + QStringLiteral("</") + block.qname + QLatin1Char('>'),
         keptPriorities, keptCount, QStringLiteral("an Excel 2010 rule QindaCalc does not model"));
}

} // namespace

void readSheetRules(const QByteArray &sheetXml, const QStringList &sourceDxfs, const ThemePalette &palette,
                    Sheet *sheet, RuleReadCounts *counts)
{
    // Most sheets have neither: skip the second walk over their cells.
    if (!sheetXml.contains("onditionalFormatting") && !sheetXml.contains("ataValidation")) {
        return;
    }
    const RuleXml xml = scanRuleXml(sheetXml);
    Reading r{xml, {}, RuleContext{sourceDxfs, palette, {}}, {}, {}, 0};
    r.context.twin = [&r](const QString &id, const RawNode **twin) {
        const auto found = r.x14.constFind(id);
        if (found == r.x14.constEnd()) {
            return TwinLookup::Missing;
        }
        *twin = found->rule;
        return found->blockKept ? TwinLookup::Kept : TwinLookup::Usable;
    };
    for (const RawNode &block : xml.x14Formattings) {
        const bool blockKept = x14BlockKept(block);
        for (const RawNode *rule : block.childrenNamed(u"cfRule")) {
            if (rule->has(u"id")) {
                r.x14.insert(rule->attr(u"id"), X14Rule{rule, TwinState::Free, blockKept});
            }
        }
    }
    for (const RawNode &block : xml.conditionalFormattings) {
        readBlock(r, block);
    }
    for (const RawNode &block : xml.x14Formattings) {
        readX14Block(r, block);
    }
    // Excel's evaluation order is ascending priority over both markups.
    std::stable_sort(r.modelled.begin(), r.modelled.end(),
                     [](const CondFormatRule &a, const CondFormatRule &b) { return a.priority < b.priority; });
    sheet->setCondFormats(r.modelled);
    RuleReadCounts local;
    local.modelledFormats = int(r.modelled.size());
    local.keptFormats = r.keptRules;
    QList<KeptRule> kept = r.kept;
    readValidationRules(xml, sheet, &kept, &local);
    sheet->setKeptRules(kept);
    if (counts != nullptr) {
        *counts += local;
    }
}

QStringList readPackageDxfs(QXlsx::ZipReader &zip)
{
    QString stylesPart = QStringLiteral("xl/styles.xml");
    for (const auto &[type, target] : readRels(zip, QStringLiteral("xl/_rels/workbook.xml.rels"))) {
        if (type.endsWith(QLatin1String("/styles"))) {
            stylesPart = resolveTarget(QStringLiteral("xl"), target);
        }
    }
    return readSourceDxfs(zip.fileData(stylesPart));
}

QStringList keptRuleNotes(const Workbook &wb)
{
    QStringList notes;
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        if (const qsizetype kept = wb.sheetAt(s).keptRules().size(); kept > 0) {
            notes.append(QObject::tr("“%1” has %n conditional-format or validation rule(s) QindaCalc keeps as Excel "
                                     "wrote them but does not show.",
                                     nullptr, int(kept))
                             .arg(wb.sheetName(s)));
        }
    }
    return notes;
}

} // namespace QindaOffice::Calc::IO
