// SPDX-License-Identifier: GPL-3.0-or-later
#include "workbook.h"

namespace QindaOffice::Calc {

Workbook::Workbook()
{
    // Style id 0 is the implicit default style so user styles start at 1;
    // the io bridge and the view treat 0 as "no explicit style".
    m_styles.append(CellStyle{});
}

QString Workbook::sheetName(qint32 index) const
{
    return index >= 0 && index < qint32(m_sheets.size()) ? m_sheets.at(size_t(index))->name() : QString();
}

qint32 Workbook::sheetIndex(const QString &name) const
{
    return m_sheetIndex.value(name, -1);
}

Sheet &Workbook::addSheet(const QString &name)
{
    auto sheet = std::make_unique<Sheet>(name);
    Sheet &ref = *sheet;
    m_sheetIndex.insert(name, qint32(m_sheets.size()));
    m_sheets.push_back(std::move(sheet));
    return ref;
}

Sheet &Workbook::insertSheet(qint32 index, const QString &name)
{
    const qint32 at = qBound(qint32(0), index, qint32(m_sheets.size()));
    auto sheet = std::make_unique<Sheet>(name);
    Sheet &ref = *sheet;
    m_sheets.insert(m_sheets.begin() + at, std::move(sheet));
    rebuildSheetIndex();
    return ref;
}

bool Workbook::removeSheetAt(qint32 index)
{
    // A workbook always has a sheet: removing the last one would leave the
    // view with nothing to show and every address unresolvable.
    if (index < 0 || index >= qint32(m_sheets.size()) || m_sheets.size() <= 1) {
        return false;
    }
    m_sheets.erase(m_sheets.begin() + index);
    rebuildSheetIndex();
    return true;
}

bool Workbook::renameSheetAt(qint32 index, const QString &name)
{
    if (index < 0 || index >= qint32(m_sheets.size()) || name.isEmpty()) {
        return false;
    }
    const qint32 clash = sheetIndex(name);
    if (clash >= 0 && clash != index) {
        return false; // sheet names are the addressing identity; keep unique
    }
    m_sheets.at(size_t(index))->setName(name);
    rebuildSheetIndex();
    return true;
}

bool Workbook::moveSheetTo(qint32 from, qint32 to)
{
    const qint32 count = qint32(m_sheets.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return false;
    }
    auto moved = std::move(m_sheets.at(size_t(from)));
    m_sheets.erase(m_sheets.begin() + from);
    m_sheets.insert(m_sheets.begin() + to, std::move(moved));
    rebuildSheetIndex();
    return true;
}

void Workbook::rebuildSheetIndex()
{
    m_sheetIndex.clear();
    for (size_t i = 0; i < m_sheets.size(); ++i) {
        m_sheetIndex.insert(m_sheets.at(i)->name(), qint32(i));
    }
}

bool Workbook::removeName(const QString &name)
{
    const QString spelled = m_nameSpelling.take(name.toUpper());
    return !spelled.isEmpty() && m_names.remove(spelled) > 0;
}

void Workbook::reindexAll()
{
    m_dependencies.clear();
    QSet<CellKey> all;
    for (qint32 s = 0; s < qint32(m_sheets.size()); ++s) {
        Sheet &sheet = *m_sheets.at(size_t(s));
        for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
            if (!cell.ast) {
                continue;
            }
            const CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
            const CellKey key(s, addr.row, addr.col);
            EvalContext ctx{this, s, addr};
            m_dependencies.updateCell(key, collectTargets(cell.ast, ctx));
            if (cell.isVolatile) {
                m_dependencies.addVolatile(key);
            }
            all.insert(key);
        }
    }
    recalc(all);
}

Sheet &Workbook::sheetAt(qint32 index)
{
    return *m_sheets.at(size_t(index));
}

const Sheet &Workbook::sheetAt(qint32 index) const
{
    return *m_sheets.at(size_t(index));
}

Value Workbook::parseLiteral(const QString &input, bool *isFormula, NodePtr *ast,
                             QString *formulaText)
{
    *isFormula = false;
    ast->reset();
    if (input.startsWith(QLatin1Char('='))) {
        *isFormula = true;
        *formulaText = input.mid(1);
        ParseResult parsed = parseFormula(*formulaText);
        if (!parsed.error.isEmpty() || !parsed.root) {
            // Unparseable formulas stay visible as text (fail honestly).
            *isFormula = false;
            ast->reset();
            return input;
        }
        *ast = parsed.root;
        return Value();
    }
    if (input.startsWith(QLatin1Char('\''))) {
        return input.mid(1);
    }
    if (input.compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (input.compare(QLatin1String("FALSE"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    if (input.endsWith(QLatin1Char('%'))) {
        bool ok = false;
        const double pct = input.left(input.size() - 1).toDouble(&ok);
        if (ok) {
            return pct / 100.0;
        }
    }
    bool ok = false;
    const double num = input.toDouble(&ok);
    if (ok && !input.isEmpty()) {
        return num;
    }
    static const QStringList kDatePatterns = {
        QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy/MM/dd"),
    };
    for (const QString &pattern : kDatePatterns) {
        const QDate date = QDate::fromString(input, pattern);
        if (date.isValid()) {
            return serialFromDate(date);
        }
    }
    return input;
}

bool Workbook::setCellInput(qint32 sheet, const CellAddr &addr, const QString &input)
{
    bool isFormula = false;
    NodePtr ast;
    QString formulaText;
    const Value literal = parseLiteral(input, &isFormula, &ast, &formulaText);
    Cell &cell = sheetAt(sheet).ensureCell(addr);
    if (isFormula && ast) {
        installFormula(sheet, addr, ast, formulaText);
        return true;
    }
    // Literal: drop any formula state and dependency edges.
    cell.formula.clear();
    cell.ast.reset();
    cell.isVolatile = false;
    cell.value = literal;
    m_dependencies.updateCell(CellKey(sheet, addr.row, addr.col), {});
    if (!m_bulkLoading) {
        recalc({CellKey(sheet, addr.row, addr.col)});
    }
    return true;
}

void Workbook::setCellValue(qint32 sheet, const CellAddr &addr, const Value &value)
{
    Cell &cell = sheetAt(sheet).ensureCell(addr);
    cell.formula.clear();
    cell.ast.reset();
    cell.isVolatile = false;
    cell.value = value;
    m_dependencies.updateCell(CellKey(sheet, addr.row, addr.col), {});
    if (!m_bulkLoading) {
        recalc({CellKey(sheet, addr.row, addr.col)});
    }
}

void Workbook::installFormula(qint32 sheet, const CellAddr &addr, const NodePtr &ast,
                              const QString &formula)
{
    Cell &cell = sheetAt(sheet).ensureCell(addr);
    cell.formula = formula;
    cell.ast = ast;
    cell.isVolatile = containsVolatileCall(ast);
    const CellKey key(sheet, addr.row, addr.col);
    EvalContext ctx{this, sheet, addr};
    // AGENT-GUARD: addVolatile must come after updateCell — updateCell calls
    // removeCell, which clears the volatile registration. Missing this made
    // TODAY()/NOW()/RAND() go stale on every edit (they were only re-registered
    // by the app's undo path).
    m_dependencies.updateCell(key, collectTargets(ast, ctx));
    if (cell.isVolatile) {
        m_dependencies.addVolatile(key);
    }
    if (!m_bulkLoading) {
        recalc({key});
    }
}

void Workbook::endBulkLoad()
{
    m_bulkLoading = false;
    // AGENT-GUARD (M61, D-069): rebuild the dependency index, not just the
    // values. A formula installed during the load resolved its sheet
    // references against the sheets that existed THEN — Summary reading
    // Detail, which is read later, recorded no edge at all — so recalcAll
    // over that index ordered such cells by hash order: a real workbook
    // opened with a different set of #DIV/0! cells on every run, and editing
    // Detail afterwards never reached Summary.
    reindexAll();
}

void Workbook::recalc(const QSet<CellKey> &edited)
{
    // Values the data bars measure against are moving; drop their extents.
    m_dataBarExtents.clear();
    QSet<CellKey> dirty = m_dependencies.dependentsOf(edited);
    dirty |= m_dependencies.volatileCells();
    dirty |= edited;
    QSet<CellKey> cycles;
    const QVector<CellKey> order = topoOrder(dirty, m_dependencies, &cycles);
    for (const CellKey &key : order) {
        Sheet &sheet = sheetAt(key.sheet());
        Cell *cell = sheet.cellAt({key.row(), key.col()});
        if (cell == nullptr || !cell->ast) {
            continue;
        }
        EvalContext ctx{this, key.sheet(), {key.row(), key.col()}};
        cell->value = evalNode(cell->ast, ctx);
    }
    for (const CellKey &key : cycles) {
        Cell *cell = sheetAt(key.sheet()).cellAt({key.row(), key.col()});
        if (cell != nullptr) {
            cell->value = Err::Circular;
        }
    }
}

void Workbook::recalcAll()
{
    QSet<CellKey> all;
    for (qint32 s = 0; s < qint32(m_sheets.size()); ++s) {
        const Sheet &sheet = *m_sheets.at(size_t(s));
        for (const auto &[key, cell] : sheet.cells().asKeyValueRange()) {
            if (cell.ast) {
                // AGENT-GUARD: CellAddr::key() packs COL in the high dword
                // and ROW in the low one. This used to read them the other
                // way round, so every formula cell off the diagonal was
                // marked dirty at a transposed address that holds no cell —
                // recalcAll() then skipped it, which is exactly the stale
                // forward cross-sheet reference it exists to fix.
                all.insert(CellKey(s, qint32(key & 0xffffffffu), qint32(key >> 32)));
            }
        }
    }
    recalc(all);
}

qint32 Workbook::addStyle(const CellStyle &style)
{
    m_styles.append(style);
    return qint32(m_styles.size()) - 1;
}

const CellStyle *Workbook::style(qint32 id) const
{
    return id >= 0 && id < m_styles.size() ? &m_styles.at(id) : nullptr;
}

void Workbook::defineName(const QString &name, const RangeRef &target)
{
    removeName(name); // a redefinition may change the spelling's case
    m_names.insert(name, target);
    m_nameSpelling.insert(name.toUpper(), name);
}

void Workbook::setDefinedNames(QList<DefinedName> names)
{
    m_definedIndex.clear();
    for (qsizetype i = 0; i < names.size(); ++i) {
        DefinedName &entry = names[i];
        if (!entry.ast && !entry.formula.isEmpty()) {
            const ParseResult parsed = parseFormula(entry.formula);
            if (parsed.error.isEmpty()) {
                entry.ast = parsed.root;
            }
        }
        m_definedIndex[entry.name.toUpper()].append(i);
    }
    m_definedNames = std::move(names);
}

NodePtr Workbook::nameFormula(const QString &name, qint32 sheet) const
{
    const auto found = m_definedIndex.constFind(name.toUpper());
    if (found == m_definedIndex.constEnd()) {
        return {};
    }
    // Excel's precedence: the name scoped to the sheet the formula lives on
    // hides a workbook-wide one of the same name.
    const auto bodyOf = [](const DefinedName &entry) {
        return entry.ast ? entry.ast : Node::makeError(Err::Name);
    };
    for (const qsizetype index : found.value()) {
        if (m_definedNames.at(index).sheet == sheet) {
            return bodyOf(m_definedNames.at(index));
        }
    }
    if (m_nameSpelling.contains(name.toUpper())) {
        return {}; // a plain workbook range: namedRange() answers
    }
    for (const qsizetype index : found.value()) {
        if (m_definedNames.at(index).sheet < 0) {
            return bodyOf(m_definedNames.at(index));
        }
    }
    return {};
}

QString Workbook::cellFormula(qint32 sheet, qint32 row, qint32 col) const
{
    if (sheet < 0 || sheet >= qint32(m_sheets.size())) {
        return {};
    }
    const Cell *cell = m_sheets.at(size_t(sheet))->cellAt({row, col});
    return cell != nullptr && cell->ast ? cell->formula : QString();
}

std::optional<RangeRef> Workbook::namedRange(const QString &name) const
{
    const auto it = m_names.constFind(m_nameSpelling.value(name.toUpper()));
    if (it != m_names.constEnd()) {
        return it.value();
    }
    return std::nullopt;
}

Value Workbook::cellValue(qint32 sheet, qint32 row, qint32 col) const
{
    if (sheet < 0 || sheet >= qint32(m_sheets.size()) || row < 0 || col < 0) {
        return Err::Ref;
    }
    const Cell *cell = m_sheets.at(size_t(sheet))->cellAt({row, col});
    return cell != nullptr ? cell->value : Value(std::monostate{});
}

CondFormat Workbook::condFormatFor(qint32 sheet, const CellAddr &addr) const
{
    if (sheet < 0 || sheet >= qint32(m_sheets.size())) {
        return {};
    }
    return condFormatForHost(m_sheets.at(size_t(sheet))->condFormats(), sheet, addr, *this,
                             m_dataBarExtents);
}

QVector<QPair<CellAddr, Value>> Workbook::nonBlankCellsIn(qint32 sheet, const CellRange &range) const
{
    QVector<QPair<CellAddr, Value>> out;
    if (sheet < 0 || sheet >= qint32(m_sheets.size()) || !range.isValid()) {
        return out;
    }
    const QHash<quint64, Cell> &cells = m_sheets.at(size_t(sheet))->cells();
    // Iterate the hash once when the range is large; direct lookups when small.
    const qint64 count = range.cellCount();
    if (count < 4096) {
        for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
            for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                const Cell *cell = m_sheets.at(size_t(sheet))->cellAt({r, c});
                if (cell != nullptr && !isBlank(cell->value)) {
                    out.append({{r, c}, cell->value});
                }
            }
        }
        return out;
    }
    for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
        const quint64 key = it.key();
        const CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
        if (range.contains(addr) && !isBlank(it.value().value)) {
            out.append({addr, it.value().value});
        }
    }
    return out;
}

} // namespace QindaOffice::Calc
