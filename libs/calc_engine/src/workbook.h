// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "chart_spec.h"
#include "dependencies.h"
#include "eval.h"
#include "sheet.h"

#include <memory>
#include <vector>

namespace QindaOffice::Calc {

// The spreadsheet document (plan section 7.2): ordered sheets, the shared
// cell-style table, named ranges, the dependency index and the recalc
// engine. Implements EvalHost so formulas evaluate against it directly.
//
// Input parsing (plan section 7.2): leading '=' starts a formula; leading '
// forces text; otherwise the text parses as a locale number/percent/date or
// boolean, falling back to text.
class Workbook final : public EvalHost {
public:
    Workbook();

    [[nodiscard]] qint32 sheetCount() const override { return qint32(m_sheets.size()); }
    [[nodiscard]] QString sheetName(qint32 index) const override;
    [[nodiscard]] qint32 sheetIndex(const QString &name) const override;
    Sheet &addSheet(const QString &name);
    [[nodiscard]] Sheet &sheetAt(qint32 index);
    [[nodiscard]] const Sheet &sheetAt(qint32 index) const;

    // Parses and installs cell input; returns false when the input is a
    // formula that failed to parse (it is stored as text instead).
    bool setCellInput(qint32 sheet, const CellAddr &addr, const QString &input);
    // Installs an already-parsed formula (used by calc_io; shared formulas
    // arrive shifted from the master). Takes the canonical text for storage.
    void installFormula(qint32 sheet, const CellAddr &addr, const NodePtr &ast,
                        const QString &formula);
    // Installs a pre-parsed literal (number/text/bool/error) without parsing.
    void setCellValue(qint32 sheet, const CellAddr &addr, const Value &value);

    // Recalculates the transitive dependents of `edited` plus all volatile
    // cells in Kahn topological order; cells on a cycle get Err::Circular.
    void recalc(const QSet<CellKey> &edited);

    // Style table shared by the sheets.
    [[nodiscard]] qint32 addStyle(const CellStyle &style);
    [[nodiscard]] const CellStyle *style(qint32 id) const;

    // Named ranges (simple refs in v1).
    void defineName(const QString &name, const RangeRef &target);
    [[nodiscard]] std::optional<RangeRef> namedRange(const QString &name) const;

    // EvalHost implementation.
    [[nodiscard]] Value cellValue(qint32 sheet, qint32 row, qint32 col) const override;
    [[nodiscard]] QVector<QPair<CellAddr, Value>> nonBlankCellsIn(
        qint32 sheet, const CellRange &range) const override;

    [[nodiscard]] DependencyGraph &dependencies() { return m_dependencies; }

    // Floating charts anchored on the sheets (plan section 7.2); exported
    // to xlsx, dropped with a warning on import (QXlsx has no chart read).
    QList<ChartSpec> charts;

private:
    static Value parseLiteral(const QString &input, bool *isFormula, NodePtr *ast,
                              QString *formulaText);

    std::vector<std::unique_ptr<Sheet>> m_sheets;
    QHash<QString, qint32> m_sheetIndex;
    QVector<CellStyle> m_styles;
    QHash<QString, RangeRef> m_names;
    DependencyGraph m_dependencies;
};

} // namespace QindaOffice::Calc
