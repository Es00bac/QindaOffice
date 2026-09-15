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
    recalc({CellKey(sheet, addr.row, addr.col)});
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
    recalc({CellKey(sheet, addr.row, addr.col)});
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
    m_dependencies.updateCell(key, collectTargets(ast, ctx));
    recalc({key});
}

void Workbook::recalc(const QSet<CellKey> &edited)
{
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
    m_names.insert(name.toUpper(), target);
}

std::optional<RangeRef> Workbook::namedRange(const QString &name) const
{
    const auto it = m_names.constFind(name.toUpper());
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
