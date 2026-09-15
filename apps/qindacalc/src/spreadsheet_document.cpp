// SPDX-License-Identifier: GPL-3.0-or-later
#include "spreadsheet_document.h"

#include "calc_undo.h"
#include "csv.h"
#include "number_format.h"

#include <QFile>
#include "xlsx_bridge.h"

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

SpreadsheetDocument::SpreadsheetDocument()
{
    m_workbook.addSheet(tr("Sheet1"));
}

bool SpreadsheetDocument::load(const QString &path, QString *error)
{
    CalcEng::Workbook fresh;
    bool ok = false;
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::readXlsxFile(path, &fresh, error);
    } else if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)
               || path.endsWith(QLatin1String(".tsv"), Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        const Csv::Table table = Csv::parse(file.readAll());
        fresh.addSheet(tr("Sheet1"));
        for (int r = 0; r < table.rows.size(); ++r) {
            for (int c = 0; c < table.rows.at(r).size(); ++c) {
                fresh.setCellInput(0, {r, c}, table.cellAt(r, c));
            }
        }
        ok = true;
    } else {
        if (error != nullptr) {
            *error = tr("Unsupported format (ods/xls arrive via the LibreOffice bridge in a later pass).");
        }
        return false;
    }
    if (!ok) {
        return false;
    }
    m_workbook = std::move(fresh);
    // A loaded document starts with empty history (like every editor).
    m_undoStack.clear();
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    emit structureChanged();
    return true;
}

bool SpreadsheetDocument::save(const QString &path, QString *error)
{
    bool ok = false;
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::writeXlsxFile(m_workbook, path, error);
    } else if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
        Csv::Table table;
        qint32 maxRow = 0;
        qint32 maxCol = 0;
        for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
            maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu));
            maxCol = qMax(maxCol, qint32(it.key() >> 32));
        }
        for (qint32 r = 0; r <= maxRow; ++r) {
            QStringList row;
            for (qint32 c = 0; c <= maxCol; ++c) {
                const CalcEng::Cell *cell = sheet.cellAt({r, c});
                row.append(cell == nullptr ? QString() : CalcEng::toText(cell->value));
            }
            table.rows.append(row);
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        file.write(Csv::serialize(table));
        ok = true;
    } else {
        if (error != nullptr) {
            *error = tr("Unsupported save format.");
        }
        return false;
    }
    if (!ok) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList SpreadsheetDocument::mimeTypes() const
{
    return {QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"),
            QStringLiteral("text/csv")};
}

QByteArray SpreadsheetDocument::recoverySnapshot() const
{
    return {}; // xlsx-in-memory snapshot arrives with the journal work; M3
               // documents the gap honestly (shell skips empty snapshots).
}

bool SpreadsheetDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Q_UNUSED(snapshot);
    if (error != nullptr) {
        *error = tr("Recovery snapshots are not available for spreadsheets yet.");
    }
    return false;
}

void SpreadsheetDocument::applyInput(const CalcEng::CellAddr &addr, const QString &input)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.setCellInput(m_activeSheet, addr, input);
    pushUndo(before, tr("Set cell"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::clearCells(const CalcEng::CellRange &range)
{
    if (!range.isValid()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    QSet<CalcEng::CellKey> cleared;
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            if (sheet.clearCell({row, col})) {
                const CalcEng::CellKey key(m_activeSheet, row, col);
                m_workbook.dependencies().updateCell(key, {});
                cleared.insert(key);
            }
        }
    }
    if (cleared.isEmpty()) {
        return;
    }
    m_workbook.recalc(cleared);
    pushUndo(before, tr("Clear cells"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::pasteCells(const CalcEng::CellAddr &at, const QStringList &rows)
{
    if (rows.isEmpty() || !at.isValid()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    qint32 row = at.row;
    for (const QString &line : rows) {
        qint32 col = at.col;
        const QStringList cells = line.split(QLatin1Char('\t'));
        for (const QString &input : cells) {
            if (!input.isEmpty()) {
                // setCellInput parses formulas ('=' prefix), numbers,
                // booleans and dates, falling back to text.
                m_workbook.setCellInput(m_activeSheet, {row, col}, input);
            }
            ++col;
        }
        ++row;
    }
    pushUndo(before, tr("Paste"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::restyleRange(const CalcEng::CellRange &range,
                                       const StylePatch &patch, const QString &undoText)
{
    if (!range.isValid()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            const CalcEng::CellAddr addr{row, col};
            const qint32 oldId = sheet.styleIdAt(addr);
            const CalcEng::CellStyle *old = oldId != 0 ? m_workbook.style(oldId) : nullptr;
            CalcEng::CellStyle style = old != nullptr ? *old : CalcEng::CellStyle{};
            if (patch.bold) {
                style.bold = *patch.bold;
            }
            if (patch.italic) {
                style.italic = *patch.italic;
            }
            if (patch.underline) {
                style.underline = *patch.underline;
            }
            if (patch.alignment) {
                style.alignment = *patch.alignment;
            }
            if (patch.numberFormat) {
                style.numberFormat = *patch.numberFormat;
            }
            if (patch.fontFamily) {
                style.fontFamily = *patch.fontFamily;
            }
            if (patch.fontSize) {
                style.fontSize = *patch.fontSize;
            }
            if (patch.fg) {
                style.fg = *patch.fg;
            }
            if (patch.bg) {
                style.bg = *patch.bg;
            }
            const qint32 id = m_workbook.addStyle(style);
            sheet.setStyleIdAt(addr, id);
        }
    }
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::insertSheet(const QString &name)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.addSheet(name);
    m_activeSheet = m_workbook.sheetCount() - 1;
    pushUndo(before, tr("Insert sheet"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::applySnapshot(const WorkbookSnapshot &snapshot)
{
    restoreSnapshot(&m_workbook, snapshot);
    m_activeSheet = qBound(qint32(0), snapshot.activeSheet, m_workbook.sheetCount() - 1);
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::pushUndo(const WorkbookSnapshot &before, const QString &text)
{
    m_undoStack.push(new WorkbookUndoCommand(this, before,
                                             takeSnapshot(m_workbook, m_activeSheet), text));
}

QString SpreadsheetDocument::inputTextAt(const CalcEng::CellAddr &addr) const
{
    const CalcEng::Cell *cell = m_workbook.sheetAt(m_activeSheet).cellAt(addr);
    if (cell == nullptr) {
        return QString();
    }
    return cell->hasFormula() ? QStringLiteral("=") + cell->formula
                              : CalcEng::toText(cell->value);
}

QString SpreadsheetDocument::displayTextAt(qint32 sheet, const CalcEng::CellAddr &addr) const
{
    const CalcEng::Sheet &s = m_workbook.sheetAt(sheet);
    const CalcEng::Cell *cell = s.cellAt(addr);
    if (cell == nullptr || CalcEng::isBlank(cell->value)) {
        return QString();
    }
    const CalcEng::CellStyle *style = cell->styleId >= 0 ? m_workbook.style(s.styleIdAt(addr))
                                                         : nullptr;
    const QString code = style != nullptr && !style->numberFormat.isEmpty()
        ? style->numberFormat
        : QStringLiteral("General");
    return CalcEng::NumberFormat(code).format(cell->value).text;
}

} // namespace QindaOffice::Apps::QindaCalc
