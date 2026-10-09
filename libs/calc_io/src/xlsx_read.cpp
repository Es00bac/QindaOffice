// SPDX-License-Identifier: GPL-3.0-or-later
// The xlsx read half of the QXlsx <-> calc_engine bridge (split out of the
// old xlsx_bridge.cpp in M33): sheets, cells, shared-formula shifting,
// styles, geometry and merges. Style mapping and colour resolution live in
// xlsx_style.*. Since M61 (D-069) literal values come typed straight from
// the package (xlsx_read_cells), formulas lose Excel's _xlfn. prefixes on
// the way in (xlsx_formula), a formula the engine cannot parse stays a
// formula, and the workbook-level parts — defined names, the theme, the
// document properties — are read by xlsx_read_book.

#include "xlsx_bridge.h"

#include "xlsx_formula.h"
#include "xlsx_package.h"
#include "xlsx_parts.h"
#include "xlsx_read_cells.h"
#include "xlsx_read_features.h"
#include "xlsx_carry.h"
#include "xlsx_read_xml.h"
#include "xlsx_style.h"
#include "xlsx_text.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTemporaryDir>

#include "eval.h"

#include <xlsxcell.h>
#include <xlsxcelllocation.h>
#include <xlsxcellformula.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>
#include <xlsxworkbook.h>
#include <xlsxworksheet.h>
#include <xlsxzipreader_p.h>

#include <QXmlStreamReader>

#include <optional>

namespace QindaOffice::Calc::IO {

namespace {

// AGENT-NOTE: QXlsx's worksheet reader calls qWarning("no count") for every
// <mergeCells> element that omits the optional `count` attribute — which is
// normal (Open XML SDK writers omit it) and printed eleven times for one
// real workbook, so it was the only thing a user saw on the console when
// opening a file. The vendored library stays pristine: third_party/QXlsx is
// upstream at a pinned commit, and a local commit there would not survive a
// fresh `git submodule update`. So the importer silences exactly that one
// message for the duration of its own read and forwards everything else.
class QXlsxNoiseFilter {
public:
    QXlsxNoiseFilter() { s_previous = qInstallMessageHandler(&handle); }
    ~QXlsxNoiseFilter() { qInstallMessageHandler(s_previous); }
    QXlsxNoiseFilter(const QXlsxNoiseFilter &) = delete;
    QXlsxNoiseFilter &operator=(const QXlsxNoiseFilter &) = delete;

private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (type == QtWarningMsg && message == QLatin1String("no count")) {
            return;
        }
        if (s_previous != nullptr) {
            s_previous(type, context, message);
            return;
        }
        // AGENT-GUARD: never re-emit through qWarning() here — this handler
        // IS the installed one, so that would recurse forever. Format the
        // line the way the default handler does and write it out.
        const QByteArray line = qFormatLogMessage(type, context, message).toLocal8Bit();
        fprintf(stderr, "%s\n", line.constData());
    }

    static QtMessageHandler s_previous;
};

QtMessageHandler QXlsxNoiseFilter::s_previous = nullptr;

// QXlsx measures column widths in character units (~7 px + 5 padding per
// the plan) and row heights in points.
qreal charWidthToPx(qreal chars)
{
    return chars * 7.0 + 5.0;
}

qreal ptToPx(qreal pt)
{
    return pt * 96.0 / 72.0;
}

// The value of a literal cell when the package itself is not at hand
// (readXlsx on a caller's QXlsx::Document): QXlsx's cell type says how to
// read the string it kept. With the package, xlsx_read_cells types every
// cell instead (inline strings with every run, ISO dates, …).
Value valueFromCell(const std::shared_ptr<QXlsx::Cell> &cell, bool date1904)
{
    const QVariant v = cell->value();
    if (v.metaType().id() == QMetaType::QString) {
        const QString text = v.toString();
        switch (cell->cellType()) {
        case QXlsx::Cell::ErrorType:
            return errorFromFile(text);
        case QXlsx::Cell::NumberType:
        case QXlsx::Cell::CustomType: {
            // No t attribute: how Excel writes every number.
            bool ok = false;
            const double number = text.trimmed().toDouble(&ok);
            if (ok) {
                return number;
            }
            return text.isEmpty() ? Value() : Value(text);
        }
        default:
            return decodeXString(text);
        }
    }
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Double:
    case QMetaType::Int:
    case QMetaType::LongLong:
        return v.toDouble();
    case QMetaType::QDateTime:
    case QMetaType::QDate: {
        const QDate date = v.metaType().id() == QMetaType::QDate
            ? v.toDate()
            : v.toDateTime().date();
        double serial = serialFromDate(date);
        if (v.metaType().id() == QMetaType::QDateTime) {
            serial += v.toDateTime().time().msecsSinceStartOfDay() / 86400000.0;
        }
        // Normalize the 1904 date system to the 1900 serial space (plan
        // section 7.2).
        return date1904 ? serial + 1462.0 : serial;
    }
    default:
        if (cell->isDateTime()) {
            return serialFromDate(v.toDateTime().date())
                + v.toDateTime().time().msecsSinceStartOfDay() / 86400000.0 + (date1904 ? 1462.0 : 0.0);
        }
        return Value(std::monostate{});
    }
}

// Installs a formula read from the file, whatever the engine can make of it.
// AGENT-NOTE (D-069): a formula the parser cannot read — a structured table
// reference, the space intersection operator — stays a FORMULA: its text
// kept for the formula bar and the file, its value #NAME? (honest: the
// engine cannot compute it; Excel recomputes on open). It used to become the
// TEXT "=…", which the writer would now save as a string, losing it.
void installFileFormula(Workbook *out, qint32 sheet, const CellAddr &addr, const QString &text)
{
    const ParseResult parsed = parseFormula(text);
    const bool parses = parsed.error.isEmpty() && parsed.root;
    out->installFormula(sheet, addr, parses ? parsed.root : Node::makeError(Err::Name), text);
}

// The theme palette for the whole read, from xl/theme/theme1.xml when the
// package has one (writers may omit it; the Office default then applies).
ThemePalette paletteForPackage(const QString &path)
{
    QXlsx::ZipReader zip(path);
    if (!zip.exists()) {
        return ThemePalette::officeDefault();
    }
    const QByteArray xml = zip.fileData(QStringLiteral("xl/theme/theme1.xml"));
    if (xml.isEmpty()) {
        return ThemePalette::officeDefault();
    }
    return themePaletteFromXml(xml);
}

bool readXlsxImpl(QXlsx::Document &doc, Workbook *out, const ThemePalette &palette,
                  const QString &packagePath, QString *error)
{
    if (out == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("null workbook output");
        }
        return false;
    }
    const bool date1904 = doc.workbook()->isDate1904();
    const QStringList names = doc.sheetNames();
    if (names.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("workbook has no sheets");
        }
        return false;
    }
    // Literal values, typed from the package itself (xlsx_read_cells.h says
    // why QXlsx's own value model cannot be used for them).
    std::optional<QXlsx::ZipReader> package;
    QVector<QString> sheetParts;
    QStringList sharedStrings;
    // M66 (D-075): cell-format protection (QXlsx reads none) and which cm
    // values name a dynamic array.
    QVector<quint8> xfProtection;
    QSet<int> watchedXfs;
    QSet<int> dynamicCms;
    if (!packagePath.isEmpty()) {
        package.emplace(packagePath);
        sheetParts = sheetPartMap(*package);
        xfProtection = readXfProtection(*package);
        for (int xf = 0; xf < xfProtection.size(); ++xf) {
            if (xfProtection.at(xf) != 0) {
                watchedXfs.insert(xf);
            }
        }
        dynamicCms = readDynamicArrayCms(*package);
        QString sstPart = QStringLiteral("xl/sharedStrings.xml");
        for (const auto &[type, target] : readRels(*package, QStringLiteral("xl/_rels/workbook.xml.rels"))) {
            if (type.endsWith(QLatin1String("/sharedStrings"))) {
                sstPart = resolveTarget(QStringLiteral("xl"), target);
            }
        }
        sharedStrings = readSharedStrings(package->fileData(sstPart));
    }
    // One recalc for the whole workbook, at the end (Workbook::beginBulkLoad).
    out->beginBulkLoad();
    // AGENT-NOTE: style dedup is workbook-wide and keyed by QXlsx's own
    // formatKey, which covers EVERY attribute — the old hand-built key let
    // bold-only and italic-only cells collide, and a per-sheet table minted
    // duplicate styles for one format used on two sheets.
    QHash<QByteArray, qint32> styleIds;
    for (int sheetIndex = 0; sheetIndex < names.size(); ++sheetIndex) {
        if (!doc.selectSheet(sheetIndex)) {
            continue;
        }
        QXlsx::AbstractSheet *abstract = doc.sheet(names.at(sheetIndex));
        if (abstract == nullptr) {
            continue;
        }
        Sheet &sheet = out->addSheet(names.at(sheetIndex));
        sheet.setHidden(abstract->sheetState() != QXlsx::AbstractSheet::SS_Visible);
        if (abstract->sheetType() != QXlsx::AbstractSheet::ST_WorkSheet) {
            // AGENT-GUARD: a chart sheet (or dialog/macro sheet) is not a
            // Worksheet — the old static_cast read it as one. It stays a
            // sheet without cells so every later sheet keeps its index
            // (localSheetId, print names and the active tab count sheets by
            // position). A chart sheet shows its chart and is carried by the
            // save (M69, D-078); other kinds stay empty.
            if (abstract->sheetType() == QXlsx::AbstractSheet::ST_ChartSheet && !packagePath.isEmpty()) {
                readChartSheet(packagePath, out, sheetIndex, sheetParts.value(sheetIndex));
            } else {
                qInfo("xlsx: sheet %d is not a worksheet; kept empty", sheetIndex);
            }
            continue;
        }
        auto *worksheet = static_cast<QXlsx::Worksheet *>(abstract);
        const CellScan scan = package.has_value() && !sheetParts.value(sheetIndex).isEmpty()
            ? scanSheetCells(package->fileData(sheetParts.value(sheetIndex)), sharedStrings, watchedXfs)
            : CellScan();
        // The sheet's tables before its formulas (M69, D-078): a structured
        // reference to the table a formula sits in reads without the
        // table's name ([@Sales]), as Excel's formula bar shows it.
        const QList<TableSpec> sheetTables = package.has_value() && !sheetParts.value(sheetIndex).isEmpty()
            ? readTables(*package, sheetParts.value(sheetIndex))
            : QList<TableSpec>();
        const auto containingTable = [&sheetTables](const CellAddr &addr) {
            for (const TableSpec &table : sheetTables) {
                if (table.range.contains(addr)) {
                    return table.name;
                }
            }
            return QString();
        };
        const QHash<quint64, TypedCell> &typed = scan.literals;
        QElapsedTimer sheetTimer;
        sheetTimer.start();

        // Shared formulas: only the master cell carries text and the group's
        // ref; dependents carry only the si group index. Record masters by si
        // as they come up (row-major order puts masters first) and resolve
        // dependents through the map, shifting the master AST by the cell's
        // offset (plan section 7.2).
        QHash<int, QXlsx::CellReference> sharedMasters;
        // AGENT-GUARD: walk the cells that EXIST, row by row (shared-formula
        // masters come before their dependents) — not the sheet's
        // <dimension> element, which producers may omit or leave stale, and
        // not the rectangle out to the last cell: one stray cell far out
        // made that billions of empty lookups and the load hung (M69, found
        // by tst_calc_io_corpus). getFullCells lists them sorted.
        int maxRow = -1;
        int maxCol = -1;
        const QList<QXlsx::CellLocation> located = worksheet->getFullCells(&maxRow, &maxCol);
        QXlsx::CellRange dim = worksheet->dimension();
        if (maxRow >= 1 && maxCol >= 1) {
            dim = QXlsx::CellRange(1, 1, qMax(maxRow, dim.lastRow()), qMax(maxCol, dim.lastColumn()));
        }
        for (const QXlsx::CellLocation &location : located) {
            const int row = location.row;
            const int col = location.col;
            std::shared_ptr<QXlsx::Cell> cell = worksheet->cellAt(row, col);
            if (cell == nullptr) {
                continue;
            }
            const CellAddr addr{row - 1, col - 1};
            const QXlsx::CellFormula formula = cell->formula();
            // Excel's _xlfn./_xlpm. spellings become the engine's.
            const QString text = formulaFromFile(formula.formulaText(), containingTable(addr));
            if (!text.isEmpty()) {
                installFileFormula(out, sheetIndex, addr, text);
                if (formula.formulaType() == QXlsx::CellFormula::SharedType) {
                    sharedMasters.insert(formula.sharedIndex(),
                                         QXlsx::CellReference(row, col));
                }
            } else if (formula.formulaType() == QXlsx::CellFormula::SharedType) {
                // Reconstruct from the master. AGENT-GUARD: dependents
                // carry no ref attribute — reference() is a default
                // (-1,-1) range here, so the master must come from the
                // si map; the ref path is only a fallback for masters
                // that hid themselves from the row-major scan.
                QXlsx::CellReference master;
                if (sharedMasters.contains(formula.sharedIndex())) {
                    master = sharedMasters.value(formula.sharedIndex());
                } else {
                    master = formula.reference().topLeft();
                }
                std::shared_ptr<QXlsx::Cell> masterCell =
                    worksheet->cellAt(master.row(), master.column());
                if (masterCell == nullptr) {
                    continue;
                }
                const QString masterText = formulaFromFile(masterCell->formula().formulaText(),
                                                           containingTable({master.row() - 1, master.column() - 1}));
                if (masterText.isEmpty()) {
                    continue;
                }
                const qint32 dr = row - master.row();
                const qint32 dc = col - master.column();
                const ParseResult parsed = parseFormula(masterText);
                if (!parsed.error.isEmpty() || !parsed.root) {
                    // The master is beyond the engine: shift its TEXT so
                    // this cell keeps a formula of its own (D-069) —
                    // dropped before M61, which emptied filled columns
                    // such as VLOOKUP(…,$A:$C,…) on save.
                    installFileFormula(out, sheetIndex, addr, shiftedFormulaText(masterText, dr, dc));
                } else {
                    // Store the shifted text, not the master's (the
                    // formula bar and the xlsx export read the stored
                    // text); nodeText puts back the grouping brackets.
                    const NodePtr shifted = shiftNode(parsed.root, dr, dc);
                    out->installFormula(sheetIndex, addr, shifted, nodeText(shifted));
                }
            } else {
                Value value;
                if (const auto found = typed.constFind(addr.key()); found != typed.constEnd()) {
                    value = found->value;
                    // 1904 workbooks count days from 1904: dates move to
                    // the engine's 1900 serials (plan section 7.2).
                    if (date1904 && !found->isoDate && isNumber(value)
                        && cell->format().isDateTimeFormat()) {
                        value = std::get<double>(value) + 1462.0;
                    }
                } else {
                    value = valueFromCell(cell, date1904);
                }
                out->setCellValue(sheetIndex, addr, value);
            }
            const QXlsx::Format format = cell->format();
            // A format's protection is not in QXlsx's key: it joins it.
            const auto watched = scan.watchedCells.constFind(addr.key());
            const quint8 protection = watched != scan.watchedCells.constEnd() ? xfProtection.value(watched.value()) : 0;
            if (!format.isEmpty() || protection != 0) {
                const QByteArray key = format.formatKey() + QByteArrayLiteral("|p") + QByteArray::number(protection);
                if (!styleIds.contains(key)) {
                    CellStyle style = styleFromFormat(format, palette);
                    style.locked = (protection & 1) == 0;
                    style.hiddenFormula = (protection & 2) != 0;
                    styleIds.insert(key, out->addStyle(style));
                }
                sheet.setStyleIdAt(addr, styleIds.value(key));
            }
        }
        // Sizes from the part itself when it was scanned (M69, found by
        // tst_calc_io_corpus): QXlsx's columnWidth() adds an entry per
        // column it is asked about and walks them all, so a cell in column
        // XFD took minutes; its rowHeight() answers the sheet default for a
        // row with none, which stored — and saved back with customHeight, so
        // Excel stopped fitting those rows to their text — a height for
        // every row of the used range. Only rows with ht get one now.
        const bool scanned = package.has_value() && !sheetParts.value(sheetIndex).isEmpty();
        for (int col = qMax(1, dim.firstColumn()); col <= dim.lastColumn(); ++col) {
            const qreal width = scanned ? scannedColumnWidth(scan, col) : worksheet->columnWidth(col);
            if (width > 0) {
                sheet.setColumnWidth(col - 1, charWidthToPx(width));
            }
        }
        if (scanned) {
            for (auto it = scan.rowHeights.cbegin(); it != scan.rowHeights.cend(); ++it) {
                sheet.setRowHeight(it.key(), ptToPx(it.value()));
            }
        }
        for (int row = qMax(1, dim.firstRow()); !scanned && row <= dim.lastRow(); ++row) {
            const qreal height = worksheet->rowHeight(row);
            if (height > 0) {
                sheet.setRowHeight(row - 1, ptToPx(height));
            }
        }
        for (const QXlsx::CellRange &range : worksheet->mergedCells()) {
            sheet.addMerge({{range.firstRow() - 1, range.firstColumn() - 1},
                            {range.lastRow() - 1, range.lastColumn() - 1}});
        }
        // The features QXlsx has no public model for (conditional formats,
        // validations, autofilter, comments) read straight from the package.
        if (!packagePath.isEmpty()) {
            readSheetExtras(packagePath, sheetIndex, palette, &sheet);
            readSheetDrawings(packagePath, out, sheetIndex);
            if (!sheetParts.value(sheetIndex).isEmpty()) {
                readSheetFeatures(*package, sheetParts.value(sheetIndex), &sheet); // M66
            }
            markDynamicArrays(out, sheetIndex, scan, dynamicCms);
        }
        if (qEnvironmentVariableIsSet("QQO_XLSX_TRACE")) {
            qInfo("xlsx: sheet %d (%s) %lld ms, %lld cells", sheetIndex,
                  qPrintable(names.at(sheetIndex)), static_cast<long long>(sheetTimer.elapsed()),
                  static_cast<long long>(sheet.cells().size()));
        }
    }
    QElapsedTimer recalcTimer;
    recalcTimer.start();
    // AGENT-GUARD: endBulkLoad is mandatory, and it rebuilds the dependency
    // index before its one recalculation (M61). A formula installed while a
    // sheet it reads had not been read yet (Summary before Detail — the
    // common layout) recorded no edge for that reference; recalculating over
    // the load-time index ordered such cells by hash order.
    out->endBulkLoad();
    // View state that lives outside the sheet parts: the active tab and the
    // _xlnm print names, both from xl/workbook.xml. (QXlsx's activeSheet()
    // is unusable here: the read loop above selects every sheet in turn, so
    // it always reports the LAST one.)
    if (!packagePath.isEmpty()) {
        QXlsx::ZipReader zip(packagePath);
        if (zip.exists()) {
            const QByteArray workbookXml = zip.fileData(QStringLiteral("xl/workbook.xml"));
            QXmlStreamReader reader(workbookXml);
            while (!reader.atEnd()) {
                if (reader.readNext() == QXmlStreamReader::StartElement
                    && reader.name() == QLatin1String("workbookView")) {
                    const QString tab = reader.attributes()
                                            .value(QLatin1String("activeTab"))
                                            .toString();
                    if (!tab.isEmpty()) {
                        out->setActiveTab(tab.toInt());
                    }
                    break;
                }
            }
            readPrintNames(workbookXml, out);
        }
    }
    // Lists sourced from a range can only be resolved once every sheet,
    // and every formula in them, has a value.
    resolveValidationLists(out);
    if (qEnvironmentVariableIsSet("QQO_XLSX_TRACE")) {
        qInfo("xlsx: recalcAll %lld ms", static_cast<long long>(recalcTimer.elapsed()));
    }
    return true;
}

} // namespace

bool readXlsx(QXlsx::Document &doc, Workbook *out, QString *error)
{
    // No package in hand (the document came from the caller): the stock
    // palette is the best available reading of its theme colours, and the
    // package-level extras cannot be reached.
    return readXlsxImpl(doc, out, ThemePalette::officeDefault(), QString(), error);
}

bool readXlsxFile(const QString &path, Workbook *out, QString *error, QStringList *warnings)
{
    // AGENT-GUARD: QXlsx cannot follow absolute part names in the package
    // relationships (a common shape from other producers); such a workbook
    // read as "no sheets". Hand it a relative copy (xlsx_package.h).
    QTemporaryDir scratch;
    const QString source = normalizedXlsxPackage(path, scratch, error);
    if (source.isEmpty()) {
        return false;
    }
    const QXlsxNoiseFilter quiet;
    QXlsx::Document doc(source);
    if (!doc.load()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1 as xlsx").arg(path);
        }
        return false;
    }
    if (out == nullptr) {
        return readXlsxImpl(doc, out, ThemePalette::officeDefault(), source, error);
    }
    QXlsx::ZipReader zip(source);
    // AGENT-GUARD: names BEFORE cells. installFormula() resolves a name to
    // its dependency targets at install time, so a name defined afterwards
    // leaves every formula that uses it evaluated against nothing (#NAME? at
    // best) and missing from the recalc index. readXlsx only appends sheets,
    // so seeding the name tables first is safe.
    readDefinedNames(zip.fileData(QStringLiteral("xl/workbook.xml")), out);
    // The theme and properties ride with the workbook to its next save.
    out->source = readWorkbookSource(zip);
    readWorkbookFeatures(zip, out); // structure lock, code name, VBA (M66)
    if (!readXlsxImpl(doc, out, paletteForPackage(source), source, error)) {
        return false;
    }
    // What the save will carry without modelling it (M66, D-075, D-068
    // rule 2), and what it cannot: said in the warnings the app shows.
    QStringList notes;
    out->source.carried = buildCarriedPackage(source, *out, &notes);
    if (warnings != nullptr) {
        *warnings = notes;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
