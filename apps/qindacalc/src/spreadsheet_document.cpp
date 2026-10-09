// SPDX-License-Identifier: GPL-3.0-or-later
#include "spreadsheet_document.h"

#include "calc_recovery.h"
#include "calc_undo.h"
#include "csv.h"
#include "number_format.h"

#include "libre_office_converter.h"
#include "recovery_journal.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
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
        || path.endsWith(QLatin1String(".xlsm"), Qt::CaseInsensitive)
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
    m_stagedWarnings.clear();
    // .xlsm is the same package with a VBA project (kept, never run: D-075).
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".xlsm"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::readXlsxFile(path, &fresh, error, &m_stagedWarnings);
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
    m_openWarnings = m_stagedWarnings; // what this file holds that a save cannot keep
    // The file's active tab wins (default 0); clamp against a workbook
    // whose sheet list differs from what the tab index assumed.
    m_activeSheet = qBound(qint32(0), m_workbook.activeTab(), m_workbook.sheetCount() - 1);
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
        // Foreign spreadsheet formats (ods, xls, ...) come in through the
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
        const bool ok = CalcEng::IO::readXlsxFile(converted, &fresh, error, &m_stagedWarnings);
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
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".xlsm"), Qt::CaseInsensitive)) {
        // What this save could not carry (a thread whose note was edited,
        // macros in an .xlsx) is said, never silent (D-069, D-075).
        QStringList warnings;
        ok = CalcEng::IO::writeXlsxFile(m_workbook, path, error, &warnings);
        if (ok && !warnings.isEmpty()) {
            emit saveWarning(warnings.join(QLatin1Char(' ')));
        }
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
            QStringLiteral("application/vnd.ms-excel.sheet.macroEnabled.12"),
            QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"),
            QStringLiteral("application/vnd.ms-excel"),
            QStringLiteral("text/csv")};
}

namespace {
// AGENT-NOTE: the journal payload is the JSON snapshot zlib-compressed
// behind this tag: a 100k-cell workbook's JSON passed the journal's 4 MiB
// text bound and was never journalled (known issue of 2026-09-19).
// Untagged JSON from older journals still restores.
const QByteArray kRecoveryTag = QByteArrayLiteral("QINDACALC-Z/1\n");
} // namespace

QByteArray SpreadsheetDocument::recoverySnapshot() const
{
    // The undo layer's snapshot already covers everything the app models
    // (cells, the style table including styles on empty cells, geometry,
    // merges, names, charts); calc_recovery serializes exactly that.
    return kRecoveryTag + qCompress(snapshotToJson(takeSnapshot(m_workbook, m_activeSheet)), 6);
}

qint64 SpreadsheetDocument::recoveryPayloadLimit() const
{
    return Core::RecoveryJournal::kMaxBinaryPayload;
}

bool SpreadsheetDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    QByteArray json = snapshot;
    if (snapshot.startsWith(kRecoveryTag)) {
        json = qUncompress(snapshot.mid(kRecoveryTag.size()));
        if (json.isEmpty()) {
            if (error != nullptr) {
                *error = tr("The recovery snapshot is damaged.");
            }
            return false;
        }
    }
    WorkbookSnapshot restored;
    if (!snapshotFromJson(json, &restored, error)) {
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

void SpreadsheetDocument::insertSheet(const QString &name)
{
    if (refuseStructureChange()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.addSheet(name);
    m_activeSheet = m_workbook.sheetCount() - 1;
    pushUndo(before, tr("Insert sheet"));
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
    noteSnapshotApplied(snapshot);
}

void SpreadsheetDocument::noteSnapshotApplied(const WorkbookSnapshot &snapshot)
{
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
    // Every cell of a legacy array formula shows it in braces, as Excel's
    // formula bar does (M69, D-078); committing the braces re-enters it.
    if (const auto block = m_workbook.arrayBlockAt(m_activeSheet, addr)) {
        const CalcEng::Cell *anchor = m_workbook.sheetAt(m_activeSheet).cellAt(block->topLeft);
        if (anchor != nullptr && !m_workbook.isFormulaHidden(m_activeSheet, block->topLeft)) {
            return QStringLiteral("{=") + anchor->formula + QLatin1Char('}');
        }
    }
    const CalcEng::Cell *cell = m_workbook.sheetAt(m_activeSheet).cellAt(addr);
    if (cell == nullptr) {
        return QString();
    }
    // A hidden formula on a protected sheet shows its value only (M66).
    if (cell->hasFormula() && m_workbook.isFormulaHidden(m_activeSheet, addr)) {
        return CalcEng::toText(cell->value);
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
