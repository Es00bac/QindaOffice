// SPDX-License-Identifier: GPL-3.0-or-later
// The xlsx read half of the QXlsx <-> calc_engine bridge (split out of the
// old xlsx_bridge.cpp in M33): sheets, cells, shared-formula shifting,
// styles, geometry, merges and defined names. Style mapping and colour
// resolution live in xlsx_style.*.

#include "xlsx_bridge.h"

#include "xlsx_package.h"
#include "xlsx_style.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTemporaryDir>

#include "eval.h"

#include <xlsxcell.h>
#include <xlsxcellformula.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>
#include <xlsxworkbook.h>
#include <xlsxworksheet.h>
#include <xlsxzipreader_p.h>

#include <QXmlStreamReader>

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

Value valueFromCell(const std::shared_ptr<QXlsx::Cell> &cell, bool date1904)
{
    const QVariant v = cell->value();
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Double:
    case QMetaType::Int:
    case QMetaType::LongLong:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString();
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

// AGENT-NOTE: QXlsx parses <definedName> into WorkbookPrivate::definedNamesList
// but exposes no public reader for it (AbstractOOXmlFile::d_ptr is protected),
// so the names are read straight out of xl/workbook.xml. ZipReader is QXlsx's
// own zip front end; it is a private header but a self-contained class, and
// the library is vendored and built here (third_party is ours to include).
// Writing goes the public way, through Workbook::defineName().
void readDefinedNames(const QString &path, Workbook *out)
{
    QXlsx::ZipReader zip(path);
    if (!zip.exists()) {
        return;
    }
    const QByteArray xml = zip.fileData(QStringLiteral("xl/workbook.xml"));
    if (xml.isEmpty()) {
        return;
    }
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("definedName")) {
            continue;
        }
        const QString name = reader.attributes().value(QLatin1String("name")).toString();
        const QString formula = reader.readElementText();
        if (name.isEmpty() || formula.isEmpty()) {
            continue;
        }
        // Print areas and similar built-ins are spelled _xlnm.*; they are
        // not user names and the engine has nothing to do with them (M37
        // reads them into per-sheet print fields).
        if (name.startsWith(QLatin1String("_xlnm"), Qt::CaseInsensitive)) {
            continue;
        }
        // The engine's own reference parser understands the xlsx spelling
        // (sheet qualifier, $ anchors, quoted sheet names).
        ParseResult parsed = parseFormula(formula);
        if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
            out->defineName(name, parsed.root->ref);
        }
    }
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

bool readXlsxImpl(QXlsx::Document &doc, Workbook *out, const ThemePalette &palette, QString *error)
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
        QXlsx::Worksheet *worksheet = static_cast<QXlsx::Worksheet *>(doc.sheet(names.at(sheetIndex)));
        if (worksheet == nullptr) {
            continue;
        }
        Sheet &sheet = out->addSheet(names.at(sheetIndex));
        QElapsedTimer sheetTimer;
        sheetTimer.start();

        // Shared formulas: only the master cell carries text and the group's
        // ref; dependents carry only the si group index. Record masters by si
        // as they come up (row-major order puts masters first) and resolve
        // dependents through the map, shifting the master AST by the cell's
        // offset (plan section 7.2).
        QHash<int, QXlsx::CellReference> sharedMasters;
        // AGENT-GUARD: bound the walk by the cells that EXIST, not by the
        // sheet's <dimension> element — producers may omit it (QXlsx then
        // reports no columns at all), and a stale one would skip real
        // cells. getFullCells hands back the extent of the loaded table.
        int maxRow = -1;
        int maxCol = -1;
        static_cast<void>(worksheet->getFullCells(&maxRow, &maxCol));
        QXlsx::CellRange dim = worksheet->dimension();
        if (maxRow >= 1 && maxCol >= 1) {
            dim = QXlsx::CellRange(1, 1, qMax(maxRow, dim.lastRow()), qMax(maxCol, dim.lastColumn()));
        }
        for (int row = qMax(1, dim.firstRow()); row <= dim.lastRow(); ++row) {
            for (int col = qMax(1, dim.firstColumn()); col <= dim.lastColumn(); ++col) {
                std::shared_ptr<QXlsx::Cell> cell = worksheet->cellAt(row, col);
                if (cell == nullptr) {
                    continue;
                }
                const CellAddr addr{row - 1, col - 1};
                const QXlsx::CellFormula formula = cell->formula();
                const QString text = formula.formulaText();
                if (!text.isEmpty()) {
                    ParseResult parsed = parseFormula(text);
                    if (!parsed.error.isEmpty() || !parsed.root) {
                        out->setCellValue(sheetIndex, addr,
                                          QStringLiteral("=") + text);
                    } else {
                        // Volatility/dependency tracking happens in setCellInput;
                        // formulas install via the shared installer.
                        out->installFormula(sheetIndex, addr, parsed.root, text);
                    }
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
                    const QString masterText = masterCell->formula().formulaText();
                    if (masterText.isEmpty()) {
                        continue;
                    }
                    ParseResult parsed = parseFormula(masterText);
                    if (!parsed.error.isEmpty() || !parsed.root) {
                        continue;
                    }
                    const NodePtr shifted = shiftNode(parsed.root, row - master.row(),
                                                      col - master.column());
                    // Store the shifted text, not the master's (the formula
                    // bar and the xlsx export read the stored text).
                    out->installFormula(sheetIndex, addr, shifted,
                                        nodeText(shifted));
                } else {
                    out->setCellValue(sheetIndex, addr,
                                      valueFromCell(cell, date1904));
                }
                const QXlsx::Format format = cell->format();
                if (!format.isEmpty()) {
                    const QByteArray key = format.formatKey();
                    if (!styleIds.contains(key)) {
                        styleIds.insert(key, out->addStyle(styleFromFormat(format, palette)));
                    }
                    sheet.setStyleIdAt(addr, styleIds.value(key));
                }
            }
        }
        for (int col = qMax(1, dim.firstColumn()); col <= dim.lastColumn(); ++col) {
            const qreal width = worksheet->columnWidth(col);
            if (width > 0) {
                sheet.setColumnWidth(col - 1, charWidthToPx(width));
            }
        }
        for (int row = qMax(1, dim.firstRow()); row <= dim.lastRow(); ++row) {
            const qreal height = worksheet->rowHeight(row);
            if (height > 0) {
                sheet.setRowHeight(row - 1, ptToPx(height));
            }
        }
        for (const QXlsx::CellRange &range : worksheet->mergedCells()) {
            sheet.addMerge({{range.firstRow() - 1, range.firstColumn() - 1},
                            {range.lastRow() - 1, range.lastColumn() - 1}});
        }
        if (qEnvironmentVariableIsSet("QQO_XLSX_TRACE")) {
            qInfo("xlsx: sheet %d (%s) %lld ms, %lld cells", sheetIndex,
                  qPrintable(names.at(sheetIndex)), static_cast<long long>(sheetTimer.elapsed()),
                  static_cast<long long>(sheet.cells().size()));
        }
    }
    QElapsedTimer recalcTimer;
    recalcTimer.start();
    // AGENT-GUARD: a final full recalc is mandatory. installFormula evaluates
    // each cell as it loads, so a formula referencing a sheet that had not
    // been read yet (Summary before Detail — the common layout) captured a
    // stale 0/#REF! and no dependency edge could ever fix it afterwards.
    out->endBulkLoad();
    if (qEnvironmentVariableIsSet("QQO_XLSX_TRACE")) {
        qInfo("xlsx: recalcAll %lld ms", static_cast<long long>(recalcTimer.elapsed()));
    }
    return true;
}

} // namespace

bool readXlsx(QXlsx::Document &doc, Workbook *out, QString *error)
{
    // No package in hand (the document came from the caller): the stock
    // palette is the best available reading of its theme colours.
    return readXlsxImpl(doc, out, ThemePalette::officeDefault(), error);
}

bool readXlsxFile(const QString &path, Workbook *out, QString *error)
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
    // AGENT-GUARD: names BEFORE cells. installFormula() resolves a name to
    // its dependency targets at install time, so a name defined afterwards
    // leaves every formula that uses it evaluated against nothing (#NAME? at
    // best) and missing from the recalc index. readXlsx only appends sheets,
    // so seeding the name table first is safe.
    readDefinedNames(source, out);
    return readXlsxImpl(doc, out, paletteForPackage(source), error);
}

} // namespace QindaOffice::Calc::IO
