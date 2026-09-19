// SPDX-License-Identifier: GPL-3.0-or-later
#include "spreadsheet_document.h"

#include "calc_recovery.h"
#include "calc_undo.h"
#include "csv.h"
#include "number_format.h"

#include "libre_office_converter.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

#include "xlsx_bridge.h"

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

SpreadsheetDocument::SpreadsheetDocument()
{
    m_workbook.addSheet(tr("Sheet1"));
    // AGENT-GUARD: dirty follows the undo stack's clean index, it is not a
    // latch. applySnapshot() used to force setDirty(true) on every undo, so
    // undoing all the way back to the saved state still asked "Save changes
    // to …?" on close and kept writing recovery snapshots for a document
    // that matched its file byte for byte.
    connect(&m_undoStack, &QUndoStack::cleanChanged, this,
            [this](bool clean) { setDirty(!clean); });
}

bool SpreadsheetDocument::loadCanRunOnWorker(const QString &path) const
{
    return path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".tsv"), Qt::CaseInsensitive);
}

bool SpreadsheetDocument::loadStaged(const QString &path, QString *error)
{
    // Worker half of the load (AGENT-CONTRACT in document.h): native reads
    // only, into the staging workbook — no signals, no GUI classes. The
    // engine and the xlsx bridge are QtCore/QImage territory and reentrant;
    // the QXlsx noise filter swaps the process message handler atomically
    // and forwards everything but its one message, so a concurrent GUI log
    // line is unaffected.
    CalcEng::Workbook fresh;
    bool ok = false;
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::readXlsxFile(path, &fresh, error);
    } else {
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
    }
    if (!ok) {
        return false;
    }
    m_staged = std::move(fresh);
    return true;
}

void SpreadsheetDocument::adoptStagedLoad(const QString &path)
{
    m_workbook = std::move(m_staged);
    // AGENT-GUARD: clamp the active-sheet index — the previous workbook may
    // have had more sheets than the loaded one, and every accessor indexes
    // m_sheets.at(m_activeSheet) (an out-of-range value aborts the process
    // in the structureChanged handler before the UI can react).
    m_activeSheet = 0;
    // A loaded document starts with empty history (like every editor).
    m_undoStack.clear();
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    emit structureChanged();
}

bool SpreadsheetDocument::load(const QString &path, QString *error)
{
    if (loadCanRunOnWorker(path)) {
        if (!loadStaged(path, error)) {
            return false;
        }
    } else {
        // Foreign spreadsheet formats (ods, xls, xlsm...) come in through the
        // headless LibreOffice bridge, converted to xlsx first. The .desktop
        // file has always declared these types, so refusing them here meant
        // double-clicking an .ods in the file manager launched QindaCalc just
        // to show an error.
        CalcEng::Workbook fresh;
        const QString converted =
            Core::convertBlocking(path, QStringLiteral("xlsx"),
                                  QStringLiteral(".qindacalc-lo"), error);
        if (converted.isEmpty()) {
            return false;
        }
        const bool ok = CalcEng::IO::readXlsxFile(converted, &fresh, error);
        QFile::remove(converted);
        if (!ok) {
            return false;
        }
        m_staged = std::move(fresh);
    }
    adoptStagedLoad(path);
    return true;
}

bool SpreadsheetDocument::save(const QString &path, QString *error)
{
    bool ok = false;
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::writeXlsxFile(m_workbook, path, error);
    } else if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        if (m_workbook.sheetCount() > 1) {
            // Fail honestly: CSV is one sheet. The save still succeeds (the
            // user asked for CSV), but it must not look lossless.
            emit saveWarning(tr("CSV holds one sheet: only “%1” was written; "
                                "the other %n sheet(s) were not saved.", nullptr,
                                m_workbook.sheetCount() - 1)
                                 .arg(m_workbook.sheetName(m_activeSheet)));
        }
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
        // Write the native xlsx into a scratch directory, convert it to the
        // requested format, then copy the result into place through
        // QSaveFile. AGENT-GUARD: copy, never rename — the intermediate
        // lives in a QTemporaryDir (usually tmpfs) while `path` is on the
        // user's filesystem, and a cross-device rename fails with EXDEV
        // after the original has already been removed (decisions D-010).
        QTemporaryDir temp;
        if (!temp.isValid()) {
            if (error != nullptr) {
                *error = tr("No temporary directory for the conversion.");
            }
            return false;
        }
        const QString intermediate = temp.path() + QStringLiteral("/workbook.xlsx");
        if (!CalcEng::IO::writeXlsxFile(m_workbook, intermediate, error)) {
            return false;
        }
        const QString suffix = QFileInfo(path).suffix().toLower();
        const QString converted =
            Core::convertBlocking(intermediate, suffix,
                                  QStringLiteral(".qindacalc-lo"), error);
        if (converted.isEmpty()) {
            return false;
        }
        QFile source(converted);
        if (!source.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = source.errorString();
            }
            return false;
        }
        const QByteArray bytes = source.readAll();
        source.close();
        QFile::remove(converted);
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()
            || !out.commit()) {
            if (error != nullptr) {
                *error = out.errorString();
            }
            return false;
        }
        ok = true;
    }
    if (!ok) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    // The current stack position IS the on-disk state now; undoing back here
    // must clear the dirty mark again.
    m_undoStack.setClean();
    setDirty(false);
    return true;
}

QStringList SpreadsheetDocument::mimeTypes() const
{
    // Must stay in step with data/org.qindaqt.QindaCalc.desktop's MimeType
    // list (shell rule 2): the desktop only offers this app for types it can
    // actually open. ods/xls go through the LibreOffice bridge.
    return {QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"),
            QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"),
            QStringLiteral("application/vnd.ms-excel"),
            QStringLiteral("text/csv")};
}

QByteArray SpreadsheetDocument::recoverySnapshot() const
{
    // The undo layer's snapshot already covers everything the app models
    // (cells, the style table including styles on empty cells, geometry,
    // merges, names, charts); calc_recovery serializes exactly that.
    return snapshotToJson(takeSnapshot(m_workbook, m_activeSheet));
}

bool SpreadsheetDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    WorkbookSnapshot restored;
    if (!snapshotFromJson(snapshot, &restored, error)) {
        return false;
    }
    restoreSnapshot(&m_workbook, restored);
    m_activeSheet = qBound(qint32(0), restored.activeSheet, m_workbook.sheetCount() - 1);
    // Recovered content is by definition unsaved: start a fresh history whose
    // clean position is "saved", which this state is not.
    m_undoStack.clear();
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
    return true;
}

void SpreadsheetDocument::applyInput(const CalcEng::CellAddr &addr, const QString &input)
{
    // AGENT-GUARD: validation is enforced HERE, on the one path every UI
    // edit takes (in-cell editor, formula bar, paste of a single cell, AI
    // apply). Checking it in the view would leave the other paths open.
    if (const CalcEng::ValidationRule *rule =
            m_workbook.sheetAt(m_activeSheet).validationAt(addr);
        rule != nullptr && !CalcEng::validationAccepts(*rule, input)) {
        emit inputRejected(addr, CalcEng::validationDescription(*rule));
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.setCellInput(m_activeSheet, addr, input);
    pushUndo(before, tr("Set cell"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::applyInputs(const QList<QPair<CalcEng::CellAddr, QString>> &edits,
                                     const QString &undoText)
{
    if (edits.isEmpty()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    for (const auto &[addr, input] : edits) {
        m_workbook.setCellInput(m_activeSheet, addr, input);
    }
    pushUndo(before, undoText);
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
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    QSet<CalcEng::CellKey> cleared;
    qint32 row = at.row;
    for (const QString &line : rows) {
        qint32 col = at.col;
        const QStringList cells = line.split(QLatin1Char('\t'));
        for (const QString &input : cells) {
            if (!input.isEmpty()) {
                // setCellInput parses formulas ('=' prefix), numbers,
                // booleans and dates, falling back to text.
                m_workbook.setCellInput(m_activeSheet, {row, col}, input);
            } else if (sheet.clearCell({row, col})) {
                // An empty source cell clears the target (Excel semantics):
                // paste over existing data must be idempotent with the copy.
                const CalcEng::CellKey key(m_activeSheet, row, col);
                m_workbook.dependencies().updateCell(key, {});
                cleared.insert(key);
            }
            ++col;
        }
        ++row;
    }
    if (!cleared.isEmpty()) {
        m_workbook.recalc(cleared);
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
            if (patch.wrap) {
                style.wrap = *patch.wrap;
            }
            if (patch.borders) {
                style.borders = *patch.borders;
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

void SpreadsheetDocument::addChart(const CalcEng::ChartSpec &spec)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.charts.append(spec);
    pushUndo(before, tr("Insert chart"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::defineName(const QString &name, const CalcEng::RangeRef &target)
{
    if (name.isEmpty() || !target.range.isValid()) {
        return;
    }
    CalcEng::RangeRef pinned = target;
    if (pinned.sheet.isEmpty()) {
        pinned.sheet = m_workbook.sheetName(m_activeSheet);
    }
    // Absolute on both corners: a name addresses one fixed block, and the
    // canonical text form (used for xlsx export and the recovery payload)
    // then reads "Sheet1!$A$1:$A$5" the way every other spreadsheet writes it.
    pinned.absRow0 = pinned.absCol0 = pinned.absRow1 = pinned.absCol1 = true;
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.defineName(name, pinned);
    pushUndo(before, tr("Define name"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::applySnapshot(const WorkbookSnapshot &snapshot)
{
    restoreSnapshot(&m_workbook, snapshot);
    m_activeSheet = qBound(qint32(0), snapshot.activeSheet, m_workbook.sheetCount() - 1);
    // Dirty is not set here: QUndoStack::cleanChanged drives it, so undoing
    // back to the saved position clears the mark instead of latching it on.
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
