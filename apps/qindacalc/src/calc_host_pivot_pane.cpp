// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's PivotTable Fields pane and dialogs (M74, D-083): the pane's
// model (every field with its checkbox, the four areas), the drags the pane
// sends back, and the state and answers of Insert ▸ PivotTable, Change Data
// Source, Move PivotTable and PivotTable Options. Value Field Settings,
// Field Settings, Filter and Group are calc_host_pivot_fields.cpp.

#include "calc_host.h"

#include "spreadsheet_item.h"

#include "parser.h"
#include "pivot_cache.h"
#include "sheet.h"

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

std::optional<PivotArea> areaFrom(const QString &name)
{
    if (name == QLatin1String("filters")) {
        return PivotArea::Filters;
    }
    if (name == QLatin1String("columns")) {
        return PivotArea::Columns;
    }
    if (name == QLatin1String("rows")) {
        return PivotArea::Rows;
    }
    if (name == QLatin1String("values")) {
        return PivotArea::Values;
    }
    return std::nullopt;
}

bool dateCode(const QString &code)
{
    return code.contains(QLatin1Char('y'), Qt::CaseInsensitive) || code.contains(QLatin1Char('d'), Qt::CaseInsensitive);
}

QString fieldKind(const PivotCacheField &field)
{
    if (field.grouped()) {
        return QStringLiteral("group");
    }
    bool numbers = false;
    for (const Value &item : field.items) {
        if (!isBlank(item) && !isNumber(item)) {
            return QStringLiteral("text");
        }
        numbers = numbers || isNumber(item);
    }
    return !numbers ? QStringLiteral("text") : (dateCode(field.numberFormat) ? QStringLiteral("date") : QStringLiteral("number"));
}

} // namespace

QVariantMap CalcHost::pivotPane() const
{
    const PivotRef ref = currentPivot();
    const PivotTable *pivot = pivotOf(*m_document->workbook(), ref);
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    if (pivot == nullptr || cache == nullptr) {
        return {{QStringLiteral("active"), false}};
    }
    if (!pivot->modelled) {
        return {{QStringLiteral("active"), false}, {QStringLiteral("readOnly"), true}, {QStringLiteral("note"), pivot->note},
                {QStringLiteral("name"), pivot->name}};
    }
    QVariantList fields;
    for (qsizetype f = 0; f < cache->fields.size(); ++f) {
        const bool shown = pivot->rowFields.contains(qint32(f)) || pivot->colFields.contains(qint32(f))
            || pivot->pageFields.contains(qint32(f)) || pivotAreaFields(*pivot, PivotArea::Values).contains(qint32(f));
        fields.append(QVariantMap{{QStringLiteral("index"), int(f)},
                                  {QStringLiteral("name"), cache->fields.at(f).name},
                                  {QStringLiteral("checked"), shown},
                                  {QStringLiteral("kind"), fieldKind(cache->fields.at(f))}});
    }
    const auto axis = [&](const QList<qint32> &list) {
        QVariantList out;
        for (qsizetype i = 0; i < list.size(); ++i) {
            const qint32 f = list.at(i);
            out.append(QVariantMap{{QStringLiteral("position"), int(i)},
                                   {QStringLiteral("field"), f},
                                   {QStringLiteral("caption"), f == kPivotValuesField ? tr("Σ Values")
                                                                                      : pivotFieldCaption(*cache, *pivot, f)}});
        }
        return out;
    };
    QVariantList values;
    for (qsizetype d = 0; d < pivot->dataFields.size(); ++d) {
        values.append(QVariantMap{{QStringLiteral("position"), int(d)},
                                  {QStringLiteral("field"), pivot->dataFields.at(d).field},
                                  {QStringLiteral("caption"), pivot->dataFields.at(d).name}});
    }
    return {{QStringLiteral("active"), true},
            {QStringLiteral("name"), pivot->name},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("filters"), axis(pivot->pageFields)},
            {QStringLiteral("columns"), axis(pivot->colFields)},
            {QStringLiteral("rows"), axis(pivot->rowFields)},
            {QStringLiteral("values"), values}};
}

void CalcHost::pivotToggleField(int field, bool on)
{
    changeCurrentPivot([field, on](PivotTable &p, const PivotCache &c) { CalcEng::pivotToggleField(p, c, field, on); },
                       on ? tr("Add Field") : tr("Remove Field"));
}

void CalcHost::pivotAddField(int field, const QString &area, int position)
{
    const auto to = areaFrom(area);
    if (!to.has_value()) {
        return;
    }
    changeCurrentPivot([field, to, position](PivotTable &p, const PivotCache &c) { pivotPlaceField(p, c, field, *to, position); },
                       tr("Add Field"));
}

void CalcHost::pivotMoveField(const QString &fromArea, int fromPosition, const QString &toArea, int toPosition)
{
    const auto from = areaFrom(fromArea);
    const auto to = areaFrom(toArea);
    if (!from.has_value() || !to.has_value()) {
        return;
    }
    changeCurrentPivot(
        [from, to, fromPosition, toPosition](PivotTable &p, const PivotCache &c) {
            CalcEng::pivotMoveField(p, c, *from, fromPosition, *to, toPosition);
        },
        tr("Move Field"));
}

void CalcHost::pivotRemoveField(const QString &area, int position)
{
    const auto from = areaFrom(area);
    if (!from.has_value()) {
        return;
    }
    changeCurrentPivot([from, position](PivotTable &p, const PivotCache &) { CalcEng::pivotRemoveField(p, *from, position); },
                       tr("Remove Field"));
}

// "Data!$A$1:$E$7", "A1:E7" (the active sheet), "Table1", a defined name.
std::optional<PivotSource> CalcHost::pivotSourceFrom(const QString &text) const
{
    QString spec = text.trimmed();
    if (spec.startsWith(QLatin1Char('='))) {
        spec = spec.mid(1);
    }
    const Workbook &wb = *m_document->workbook();
    const ParseResult parsed = parseFormula(spec);
    if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
        const RangeRef &ref = parsed.root->ref;
        PivotSource source;
        source.sheet = ref.sheet.isEmpty() ? wb.sheetName(m_document->activeSheet()) : unquotedSheetName(ref.sheet);
        source.range = ref.range;
        const qint32 sheet = wb.sheetIndex(source.sheet);
        if (sheet < 0) {
            return std::nullopt;
        }
        if (ref.wholeCols || ref.wholeRows) {
            // A:E means the rows that hold data.
            qint32 last = 0;
            qint32 lastCol = 0;
            for (auto it = wb.sheetAt(sheet).cells().constBegin(); it != wb.sheetAt(sheet).cells().constEnd(); ++it) {
                last = qMax(last, qint32(it.key() & 0xffffffffu));
                lastCol = qMax(lastCol, qint32(it.key() >> 32));
            }
            if (ref.wholeCols) {
                source.range = {{0, ref.range.topLeft.col}, {last, ref.range.bottomRight.col}};
            } else {
                source.range = {{ref.range.topLeft.row, 0}, {ref.range.bottomRight.row, lastCol}};
            }
        }
        return source;
    }
    qint32 sheet = -1;
    if (wb.tableNamed(spec, &sheet) != nullptr || wb.namedRange(spec).has_value()) {
        return PivotSource{QString(), CellRange{}, spec};
    }
    return std::nullopt;
}

std::optional<QPair<qint32, CellAddr>> CalcHost::pivotPlaceFrom(const QString &text) const
{
    const ParseResult parsed = parseFormula(text.trimmed().startsWith(QLatin1Char('=')) ? text.trimmed().mid(1) : text.trimmed());
    if (!parsed.root || parsed.root->kind != Node::Kind::Ref) {
        return std::nullopt;
    }
    const RangeRef &ref = parsed.root->ref;
    const qint32 sheet = ref.sheet.isEmpty() ? m_document->activeSheet()
                                             : m_document->workbook()->sheetIndex(unquotedSheetName(ref.sheet));
    if (sheet < 0) {
        return std::nullopt;
    }
    return qMakePair(sheet, ref.range.topLeft);
}

QVariantMap CalcHost::pivotCreateState() const
{
    const Workbook &wb = *m_document->workbook();
    QString source;
    if (const TableSpec *table = sheet().tableCovering(cursor())) {
        source = table->name;
    } else if (const PivotTable *pivot = pivotOf(wb, currentPivot())) {
        const PivotCache *cache = pivotCacheOf(wb, pivot->cacheId);
        if (cache != nullptr) {
            source = !cache->source.name.isEmpty()
                ? cache->source.name
                : quotedSheetName(cache->source.sheet) + QLatin1Char('!') + addrText(cache->source.range.topLeft, true, true)
                    + QLatin1Char(':') + addrText(cache->source.range.bottomRight, true, true);
        }
    } else {
        const CellRange region = blockAroundSelection();
        source = quotedSheetName(sheet().name()) + QLatin1Char('!') + addrText(region.topLeft, true, true) + QLatin1Char(':')
            + addrText(region.bottomRight, true, true);
    }
    const PivotTable *pivot = pivotOf(wb, currentPivot());
    const CellAddr at = pivot != nullptr ? pivot->location : cursor();
    return {{QStringLiteral("source"), source},
            {QStringLiteral("location"), quotedSheetName(sheet().name()) + QLatin1Char('!') + addrText(at, true, true)},
            {QStringLiteral("newSheet"), pivot == nullptr}};
}

bool CalcHost::insertPivotTableFrom(const QVariantMap &choice)
{
    const std::optional<PivotSource> source = pivotSourceFrom(choice.value(QStringLiteral("source")).toString());
    if (!source.has_value()) {
        showStatusMessage(tr("Reference isn't valid: name a range (Sheet1!A1:D20) or a table."), 8000);
        return false;
    }
    const bool newSheet = choice.value(QStringLiteral("newSheet")).toBool();
    const auto place = newSheet ? std::optional<QPair<qint32, CellAddr>>(qMakePair(qint32(-1), CellAddr{2, 0}))
                                : pivotPlaceFrom(choice.value(QStringLiteral("location")).toString());
    if (!place.has_value()) {
        showStatusMessage(tr("Destination reference is not valid."), 8000);
        return false;
    }
    const PivotRef ref = m_document->insertPivotTable(*source, newSheet, place->first, place->second);
    if (!ref.isValid()) {
        return false;
    }
    if (m_grid != nullptr) {
        m_grid->setCursorCell(pivotOf(*m_document->workbook(), ref)->location);
    }
    emit sheetsChanged();
    refreshGrid();
    syncFromCursor();
    emit pivotChanged();
    emit pivotFieldListToggleRequested(actions()->entry(QStringLiteral("calcPivotFieldListAction"))->isChecked());
    return true;
}

bool CalcHost::changePivotSourceTo(const QString &text)
{
    const std::optional<PivotSource> source = pivotSourceFrom(text);
    if (!source.has_value()) {
        showStatusMessage(tr("Reference isn't valid: name a range (Sheet1!A1:D20) or a table."), 8000);
        return false;
    }
    const bool done = m_document->changePivotSource(currentPivot(), *source);
    refreshGrid();
    emit pivotChanged();
    return done;
}

bool CalcHost::movePivotTo(const QString &location)
{
    const PivotRef ref = currentPivot();
    const auto place = pivotPlaceFrom(location);
    if (!ref.isValid() || !place.has_value()) {
        showStatusMessage(tr("Destination reference is not valid."), 8000);
        return false;
    }
    const bool done = m_document->movePivot(ref, place->first, place->second);
    if (done && m_grid != nullptr) {
        m_grid->setCursorCell(place->second);
    }
    emit sheetsChanged();
    refreshGrid();
    syncFromCursor();
    emit pivotChanged();
    return done;
}

QVariantMap CalcHost::pivotOptionsState() const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    if (pivot == nullptr) {
        return {};
    }
    const PivotOptions &o = pivot->options;
    return {{QStringLiteral("name"), pivot->name},
            {QStringLiteral("showMissing"), o.showMissing && !o.missingCaption.isEmpty()},
            {QStringLiteral("missingCaption"), o.missingCaption},
            {QStringLiteral("rowGrand"), o.colGrandTotals}, // Excel's "for rows": the column of row totals
            {QStringLiteral("colGrand"), o.rowGrandTotals},
            {QStringLiteral("showHeaders"), o.showHeaders},
            {QStringLiteral("form"), int(o.form)}};
}

void CalcHost::applyPivotOptions(const QVariantMap &options)
{
    const QString name = options.value(QStringLiteral("name")).toString().trimmed();
    const Workbook &wb = *m_document->workbook();
    const PivotRef ref = currentPivot();
    for (qint32 s = 0; s < wb.sheetCount() && !name.isEmpty(); ++s) {
        for (qsizetype i = 0; i < wb.sheetAt(s).pivots().size(); ++i) {
            if (PivotRef{s, qint32(i)} != ref && wb.sheetAt(s).pivots().at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
                showStatusMessage(tr("A PivotTable called “%1” already exists.").arg(name), 8000);
                return;
            }
        }
    }
    changeCurrentPivot(
        [options, name](PivotTable &p, const PivotCache &) {
            if (!name.isEmpty()) {
                p.name = name;
            }
            p.options.missingCaption = options.value(QStringLiteral("showMissing")).toBool()
                ? options.value(QStringLiteral("missingCaption")).toString()
                : QString();
            p.options.colGrandTotals = options.value(QStringLiteral("rowGrand")).toBool();
            p.options.rowGrandTotals = options.value(QStringLiteral("colGrand")).toBool();
            p.options.showHeaders = options.value(QStringLiteral("showHeaders")).toBool();
            p.options.form = PivotForm(qBound(0, options.value(QStringLiteral("form")).toInt(), 2));
        },
        tr("PivotTable Options"));
}

} // namespace QindaOffice::Apps::QindaCalc
