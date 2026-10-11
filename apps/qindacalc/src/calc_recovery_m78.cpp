// SPDX-License-Identifier: GPL-3.0-or-later
// The M78 (D-087) parts of the crash-recovery payload (calc_recovery_m78.h).

#include "calc_recovery_m78.h"

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

QJsonArray rangeJson(const CellRange &range)
{
    return QJsonArray{range.topLeft.row, range.topLeft.col, range.bottomRight.row, range.bottomRight.col};
}

CellRange rangeFrom(const QJsonValue &value)
{
    const QJsonArray quad = value.toArray();
    return quad.size() == 4 ? CellRange{{quad.at(0).toInt(), quad.at(1).toInt()}, {quad.at(2).toInt(), quad.at(3).toInt()}}
                            : CellRange{};
}

QJsonArray rangesJson(const QList<CellRange> &ranges)
{
    QJsonArray out;
    for (const CellRange &range : ranges) {
        out.append(rangeJson(range));
    }
    return out;
}

QList<CellRange> rangesFrom(const QJsonValue &value)
{
    QList<CellRange> out;
    for (const QJsonValue &entry : value.toArray()) {
        if (const CellRange range = rangeFrom(entry); range.isValid()) {
            out.append(range);
        }
    }
    return out;
}

QJsonArray addrJson(const CellAddr &addr)
{
    return QJsonArray{addr.row, addr.col};
}

CellAddr addrFrom(const QJsonValue &value)
{
    const QJsonArray pair = value.toArray();
    return pair.size() == 2 ? CellAddr{pair.at(0).toInt(), pair.at(1).toInt()} : CellAddr{};
}

QJsonValue rgbJson(const std::optional<Rgb> &rgb)
{
    return rgb ? QJsonValue(QJsonArray{rgb->r, rgb->g, rgb->b}) : QJsonValue(QJsonValue::Null);
}

std::optional<Rgb> rgbFrom(const QJsonValue &value)
{
    const QJsonArray triple = value.toArray();
    if (triple.size() != 3) {
        return std::nullopt;
    }
    return Rgb{quint8(triple.at(0).toInt()), quint8(triple.at(1).toInt()), quint8(triple.at(2).toInt())};
}

} // namespace

QJsonObject pageSetupToJson(const Sheet::PageSetup &s)
{
    QJsonArray rowBreaks;
    for (const qint32 line : s.rowBreaks) {
        rowBreaks.append(line);
    }
    QJsonArray colBreaks;
    for (const qint32 line : s.colBreaks) {
        colBreaks.append(line);
    }
    return {{QStringLiteral("orientation"), s.orientation}, {QStringLiteral("fitToWidth"), s.fitToWidth},
            {QStringLiteral("fitToHeight"), s.fitToHeight}, {QStringLiteral("fitToPage"), s.fitToPage},
            {QStringLiteral("scale"), s.scale}, {QStringLiteral("paperSize"), s.paperSize},
            {QStringLiteral("firstPageNumber"), s.firstPageNumber}, {QStringLiteral("useFirstPageNumber"), s.useFirstPageNumber},
            {QStringLiteral("overThenDown"), s.overThenDown}, {QStringLiteral("blackAndWhite"), s.blackAndWhite},
            {QStringLiteral("draft"), s.draft}, {QStringLiteral("cellComments"), s.cellComments},
            {QStringLiteral("errors"), s.errors},
            {QStringLiteral("margins"), QJsonArray{s.marginLeft, s.marginRight, s.marginTop, s.marginBottom, s.marginHeader,
                                                   s.marginFooter}},
            {QStringLiteral("centerHorizontally"), s.centerHorizontally}, {QStringLiteral("centerVertically"), s.centerVertically},
            {QStringLiteral("gridLines"), s.gridLines}, {QStringLiteral("headings"), s.headings},
            {QStringLiteral("oddHeader"), s.oddHeader}, {QStringLiteral("oddFooter"), s.oddFooter},
            {QStringLiteral("evenHeader"), s.evenHeader}, {QStringLiteral("evenFooter"), s.evenFooter},
            {QStringLiteral("firstHeader"), s.firstHeader}, {QStringLiteral("firstFooter"), s.firstFooter},
            {QStringLiteral("differentOddEven"), s.differentOddEven}, {QStringLiteral("differentFirst"), s.differentFirst},
            {QStringLiteral("scaleWithDoc"), s.scaleWithDoc}, {QStringLiteral("alignWithMargins"), s.alignWithMargins},
            {QStringLiteral("rowBreaks"), rowBreaks}, {QStringLiteral("colBreaks"), colBreaks},
            {QStringLiteral("moreAreas"), rangesJson(s.morePrintAreas)}};
}

Sheet::PageSetup pageSetupFromJson(const QJsonValue &value)
{
    Sheet::PageSetup s;
    if (value.isArray()) { // M37's [orientation, fitToWidth, fitToHeight]
        const QJsonArray legacy = value.toArray();
        if (legacy.size() == 3) {
            s.orientation = legacy.at(0).toInt();
            s.fitToWidth = legacy.at(1).toInt();
            s.fitToHeight = legacy.at(2).toInt();
            s.fitToPage = s.fitToWidth != 0 || s.fitToHeight != 0;
            if (!s.fitToPage) {
                s.fitToWidth = 1; // M37 wrote 0 for "no Fit to"; Excel's default is 1 × 1
                s.fitToHeight = 1;
            }
        }
        return s;
    }
    const QJsonObject o = value.toObject();
    if (o.isEmpty()) {
        return s;
    }
    s.orientation = o.value(QStringLiteral("orientation")).toInt();
    s.fitToWidth = o.value(QStringLiteral("fitToWidth")).toInt(1);
    s.fitToHeight = o.value(QStringLiteral("fitToHeight")).toInt(1);
    s.fitToPage = o.value(QStringLiteral("fitToPage")).toBool();
    s.scale = o.value(QStringLiteral("scale")).toInt(100);
    s.paperSize = o.value(QStringLiteral("paperSize")).toInt();
    s.firstPageNumber = o.value(QStringLiteral("firstPageNumber")).toInt(1);
    s.useFirstPageNumber = o.value(QStringLiteral("useFirstPageNumber")).toBool();
    s.overThenDown = o.value(QStringLiteral("overThenDown")).toBool();
    s.blackAndWhite = o.value(QStringLiteral("blackAndWhite")).toBool();
    s.draft = o.value(QStringLiteral("draft")).toBool();
    s.cellComments = o.value(QStringLiteral("cellComments")).toInt();
    s.errors = o.value(QStringLiteral("errors")).toInt();
    const QJsonArray margins = o.value(QStringLiteral("margins")).toArray();
    if (margins.size() == 6) {
        s.marginLeft = margins.at(0).toDouble();
        s.marginRight = margins.at(1).toDouble();
        s.marginTop = margins.at(2).toDouble();
        s.marginBottom = margins.at(3).toDouble();
        s.marginHeader = margins.at(4).toDouble();
        s.marginFooter = margins.at(5).toDouble();
    }
    s.centerHorizontally = o.value(QStringLiteral("centerHorizontally")).toBool();
    s.centerVertically = o.value(QStringLiteral("centerVertically")).toBool();
    s.gridLines = o.value(QStringLiteral("gridLines")).toBool();
    s.headings = o.value(QStringLiteral("headings")).toBool();
    s.oddHeader = o.value(QStringLiteral("oddHeader")).toString();
    s.oddFooter = o.value(QStringLiteral("oddFooter")).toString();
    s.evenHeader = o.value(QStringLiteral("evenHeader")).toString();
    s.evenFooter = o.value(QStringLiteral("evenFooter")).toString();
    s.firstHeader = o.value(QStringLiteral("firstHeader")).toString();
    s.firstFooter = o.value(QStringLiteral("firstFooter")).toString();
    s.differentOddEven = o.value(QStringLiteral("differentOddEven")).toBool();
    s.differentFirst = o.value(QStringLiteral("differentFirst")).toBool();
    s.scaleWithDoc = o.value(QStringLiteral("scaleWithDoc")).toBool(true);
    s.alignWithMargins = o.value(QStringLiteral("alignWithMargins")).toBool(true);
    for (const QJsonValue &line : o.value(QStringLiteral("rowBreaks")).toArray()) {
        s.rowBreaks.append(line.toInt());
    }
    for (const QJsonValue &line : o.value(QStringLiteral("colBreaks")).toArray()) {
        s.colBreaks.append(line.toInt());
    }
    s.morePrintAreas = rangesFrom(o.value(QStringLiteral("moreAreas")));
    return s;
}

QJsonObject scenariosToJson(const ScenarioSet &set)
{
    QJsonArray scenarios;
    for (const Scenario &scenario : set.scenarios) {
        QJsonArray cells;
        for (const CellAddr &cell : scenario.cells) {
            cells.append(addrJson(cell));
        }
        scenarios.append(QJsonObject{{QStringLiteral("name"), scenario.name}, {QStringLiteral("cells"), cells},
                                     {QStringLiteral("values"), QJsonArray::fromStringList(scenario.values)},
                                     {QStringLiteral("comment"), scenario.comment}, {QStringLiteral("user"), scenario.user},
                                     {QStringLiteral("locked"), scenario.locked}, {QStringLiteral("hidden"), scenario.hidden}});
    }
    return {{QStringLiteral("scenarios"), scenarios}, {QStringLiteral("current"), set.current},
            {QStringLiteral("shown"), set.shown}, {QStringLiteral("results"), rangesJson(set.resultCells)}};
}

ScenarioSet scenariosFromJson(const QJsonObject &object)
{
    ScenarioSet set;
    for (const QJsonValue &entry : object.value(QStringLiteral("scenarios")).toArray()) {
        const QJsonObject item = entry.toObject();
        Scenario scenario;
        scenario.name = item.value(QStringLiteral("name")).toString();
        for (const QJsonValue &cell : item.value(QStringLiteral("cells")).toArray()) {
            scenario.cells.append(addrFrom(cell));
        }
        for (const QJsonValue &value : item.value(QStringLiteral("values")).toArray()) {
            scenario.values.append(value.toString());
        }
        scenario.comment = item.value(QStringLiteral("comment")).toString();
        scenario.user = item.value(QStringLiteral("user")).toString();
        scenario.locked = item.value(QStringLiteral("locked")).toBool(true);
        scenario.hidden = item.value(QStringLiteral("hidden")).toBool();
        if (!scenario.name.isEmpty() && scenario.cells.size() == scenario.values.size()) {
            set.scenarios.append(scenario);
        }
    }
    set.current = object.value(QStringLiteral("current")).toInt();
    set.shown = object.value(QStringLiteral("shown")).toInt(-1);
    set.resultCells = rangesFrom(object.value(QStringLiteral("results")));
    return set;
}

QJsonArray dataTablesToJson(const QList<DataTableSpec> &tables)
{
    QJsonArray out;
    for (const DataTableSpec &t : tables) {
        out.append(QJsonObject{{QStringLiteral("ref"), rangeJson(t.ref)}, {QStringLiteral("twoD"), t.twoD},
                               {QStringLiteral("rowInput"), t.rowInput}, {QStringLiteral("r1"), addrJson(t.r1)},
                               {QStringLiteral("r2"), addrJson(t.r2)}, {QStringLiteral("del1"), t.r1Deleted},
                               {QStringLiteral("del2"), t.r2Deleted}});
    }
    return out;
}

QList<DataTableSpec> dataTablesFromJson(const QJsonArray &array)
{
    QList<DataTableSpec> out;
    for (const QJsonValue &entry : array) {
        const QJsonObject o = entry.toObject();
        DataTableSpec t;
        t.ref = rangeFrom(o.value(QStringLiteral("ref")));
        t.twoD = o.value(QStringLiteral("twoD")).toBool();
        t.rowInput = o.value(QStringLiteral("rowInput")).toBool();
        t.r1 = addrFrom(o.value(QStringLiteral("r1")));
        t.r2 = addrFrom(o.value(QStringLiteral("r2")));
        t.r1Deleted = o.value(QStringLiteral("del1")).toBool();
        t.r2Deleted = o.value(QStringLiteral("del2")).toBool();
        if (t.ref.isValid()) {
            out.append(t);
        }
    }
    return out;
}

QJsonArray keptRulesToJson(const QList<KeptRule> &rules)
{
    QJsonArray out;
    for (const KeptRule &rule : rules) {
        QJsonArray priorities;
        for (const int priority : rule.priorities) {
            priorities.append(priority);
        }
        out.append(QJsonObject{{QStringLiteral("kind"), int(rule.kind)}, {QStringLiteral("ranges"), rangesJson(rule.ranges)},
                               {QStringLiteral("xml"), rule.xml}, {QStringLiteral("priorities"), priorities},
                               {QStringLiteral("reason"), rule.reason}, {QStringLiteral("staleFormulas"), rule.staleFormulas}});
    }
    return out;
}

QList<KeptRule> keptRulesFromJson(const QJsonArray &array)
{
    QList<KeptRule> out;
    for (const QJsonValue &entry : array) {
        const QJsonObject o = entry.toObject();
        KeptRule rule;
        rule.kind = KeptRule::Kind(qBound(0, o.value(QStringLiteral("kind")).toInt(), int(KeptRule::Kind::ValidationX14)));
        rule.ranges = rangesFrom(o.value(QStringLiteral("ranges")));
        rule.xml = o.value(QStringLiteral("xml")).toString();
        for (const QJsonValue &priority : o.value(QStringLiteral("priorities")).toArray()) {
            rule.priorities.append(priority.toInt());
        }
        rule.reason = o.value(QStringLiteral("reason")).toString();
        rule.staleFormulas = o.value(QStringLiteral("staleFormulas")).toBool();
        if (!rule.xml.isEmpty()) {
            out.append(rule);
        }
    }
    return out;
}

void condRuleExtrasToJson(const CondFormatRule &rule, QJsonObject *out)
{
    QJsonArray thresholds;
    for (const CfThreshold &t : rule.thresholds) {
        thresholds.append(QJsonObject{{QStringLiteral("type"), int(t.type)}, {QStringLiteral("value"), t.value},
                                      {QStringLiteral("gte"), t.gte}});
    }
    QJsonArray colors;
    for (const Rgb &c : rule.colors) {
        colors.append(rgbJson(c));
    }
    const DataBarStyle &bar = rule.bar;
    out->insert(QStringLiteral("more"), rangesJson(rule.moreRanges));
    out->insert(QStringLiteral("formula2"), rule.formula2);
    out->insert(QStringLiteral("rank"), rule.rank);
    out->insert(QStringLiteral("percent"), rule.percent);
    out->insert(QStringLiteral("bottom"), rule.bottom);
    out->insert(QStringLiteral("aboveAverage"), rule.aboveAverage);
    out->insert(QStringLiteral("equalAverage"), rule.equalAverage);
    out->insert(QStringLiteral("stdDev"), rule.stdDev);
    out->insert(QStringLiteral("period"), int(rule.period));
    out->insert(QStringLiteral("thresholds"), thresholds);
    out->insert(QStringLiteral("colors"), colors);
    out->insert(QStringLiteral("iconSet"), rule.iconSet);
    out->insert(QStringLiteral("reverse"), rule.reverse);
    out->insert(QStringLiteral("showValue"), rule.showValue);
    out->insert(QStringLiteral("bar"), QJsonObject{{QStringLiteral("min"), bar.minLength}, {QStringLiteral("max"), bar.maxLength},
                                                    {QStringLiteral("gradient"), bar.gradient}, {QStringLiteral("border"), rgbJson(bar.border)},
                                                    {QStringLiteral("negative"), rgbJson(bar.negativeColor)},
                                                    {QStringLiteral("negativeBorder"), rgbJson(bar.negativeBorder)},
                                                    {QStringLiteral("negativeSame"), bar.negativeSameAsPositive},
                                                    {QStringLiteral("axisColor"), rgbJson(bar.axisColor)},
                                                    {QStringLiteral("axis"), int(bar.axis)}, {QStringLiteral("direction"), int(bar.direction)},
                                                    {QStringLiteral("x14"), bar.x14}});
    out->insert(QStringLiteral("underline"), rule.format.underline ? QJsonValue(*rule.format.underline) : QJsonValue());
    out->insert(QStringLiteral("strikeout"), rule.format.strikeout ? QJsonValue(*rule.format.strikeout) : QJsonValue());
    out->insert(QStringLiteral("priority"), rule.priority);
    out->insert(QStringLiteral("dxfId"), rule.dxfId);
    out->insert(QStringLiteral("x14Id"), rule.x14Id);
}

void condRuleExtrasFromJson(const QJsonObject &in, CondFormatRule *rule)
{
    rule->moreRanges = rangesFrom(in.value(QStringLiteral("more")));
    rule->formula2 = in.value(QStringLiteral("formula2")).toString();
    rule->rank = in.value(QStringLiteral("rank")).toInt(10);
    rule->percent = in.value(QStringLiteral("percent")).toBool();
    rule->bottom = in.value(QStringLiteral("bottom")).toBool();
    rule->aboveAverage = in.value(QStringLiteral("aboveAverage")).toBool(true);
    rule->equalAverage = in.value(QStringLiteral("equalAverage")).toBool();
    rule->stdDev = in.value(QStringLiteral("stdDev")).toInt();
    rule->period = CondFormatRule::Period(qBound(0, in.value(QStringLiteral("period")).toInt(), 9));
    rule->thresholds.clear();
    for (const QJsonValue &entry : in.value(QStringLiteral("thresholds")).toArray()) {
        const QJsonObject t = entry.toObject();
        rule->thresholds.append({CfThreshold::Type(qBound(0, t.value(QStringLiteral("type")).toInt(), int(CfThreshold::Type::AutoMax))),
                                 t.value(QStringLiteral("value")).toString(), t.value(QStringLiteral("gte")).toBool(true)});
    }
    rule->colors.clear();
    for (const QJsonValue &entry : in.value(QStringLiteral("colors")).toArray()) {
        rule->colors.append(rgbFrom(entry).value_or(Rgb{}));
    }
    if (in.contains(QStringLiteral("iconSet"))) {
        rule->iconSet = in.value(QStringLiteral("iconSet")).toString();
    }
    rule->reverse = in.value(QStringLiteral("reverse")).toBool();
    rule->showValue = in.value(QStringLiteral("showValue")).toBool(true);
    const QJsonObject bar = in.value(QStringLiteral("bar")).toObject();
    if (!bar.isEmpty()) {
        rule->bar.minLength = bar.value(QStringLiteral("min")).toInt(10);
        rule->bar.maxLength = bar.value(QStringLiteral("max")).toInt(90);
        rule->bar.gradient = bar.value(QStringLiteral("gradient")).toBool(true);
        rule->bar.border = rgbFrom(bar.value(QStringLiteral("border")));
        rule->bar.negativeColor = rgbFrom(bar.value(QStringLiteral("negative"))).value_or(Rgb{255, 0, 0});
        rule->bar.negativeBorder = rgbFrom(bar.value(QStringLiteral("negativeBorder")));
        rule->bar.negativeSameAsPositive = bar.value(QStringLiteral("negativeSame")).toBool();
        rule->bar.axisColor = rgbFrom(bar.value(QStringLiteral("axisColor"))).value_or(Rgb{});
        rule->bar.axis = DataBarStyle::Axis(qBound(0, bar.value(QStringLiteral("axis")).toInt(), 2));
        rule->bar.direction = DataBarStyle::Direction(qBound(0, bar.value(QStringLiteral("direction")).toInt(), 2));
        rule->bar.x14 = bar.value(QStringLiteral("x14")).toBool();
    }
    if (in.value(QStringLiteral("underline")).isBool()) {
        rule->format.underline = in.value(QStringLiteral("underline")).toBool();
    }
    if (in.value(QStringLiteral("strikeout")).isBool()) {
        rule->format.strikeout = in.value(QStringLiteral("strikeout")).toBool();
    }
    rule->priority = in.value(QStringLiteral("priority")).toInt();
    rule->dxfId = in.value(QStringLiteral("dxfId")).toInt(-1);
    rule->x14Id = in.value(QStringLiteral("x14Id")).toString();
}

void validationExtrasToJson(const ValidationRule &rule, QJsonObject *out)
{
    out->insert(QStringLiteral("more"), rangesJson(rule.moreRanges));
    out->insert(QStringLiteral("op"), int(rule.op));
    out->insert(QStringLiteral("formula1"), rule.formula1);
    out->insert(QStringLiteral("formula2"), rule.formula2);
    out->insert(QStringLiteral("style"), int(rule.style));
    out->insert(QStringLiteral("errorTitle"), rule.errorTitle);
    out->insert(QStringLiteral("promptTitle"), rule.promptTitle);
    out->insert(QStringLiteral("prompt"), rule.prompt);
    out->insert(QStringLiteral("showError"), rule.showError);
    out->insert(QStringLiteral("showInput"), rule.showInput);
    out->insert(QStringLiteral("allowBlank"), rule.allowBlank);
    out->insert(QStringLiteral("dropdown"), rule.inCellDropdown);
    out->insert(QStringLiteral("x14"), rule.x14);
}

void validationExtrasFromJson(const QJsonObject &in, ValidationRule *rule)
{
    rule->moreRanges = rangesFrom(in.value(QStringLiteral("more")));
    rule->op = ValidationRule::Operator(qBound(0, in.value(QStringLiteral("op")).toInt(), int(ValidationRule::Operator::LessThanOrEqual)));
    rule->formula1 = in.value(QStringLiteral("formula1")).toString();
    rule->formula2 = in.value(QStringLiteral("formula2")).toString();
    rule->style = ValidationRule::Style(qBound(0, in.value(QStringLiteral("style")).toInt(), 2));
    rule->errorTitle = in.value(QStringLiteral("errorTitle")).toString();
    rule->promptTitle = in.value(QStringLiteral("promptTitle")).toString();
    rule->prompt = in.value(QStringLiteral("prompt")).toString();
    rule->showError = in.value(QStringLiteral("showError")).toBool(true);
    rule->showInput = in.value(QStringLiteral("showInput")).toBool(true);
    rule->allowBlank = in.value(QStringLiteral("allowBlank")).toBool(true);
    rule->inCellDropdown = in.value(QStringLiteral("dropdown")).toBool(true);
    rule->x14 = in.value(QStringLiteral("x14")).toBool();
    static_cast<void>(prepareValidationRule(*rule));
}

} // namespace QindaOffice::Apps::QindaCalc
