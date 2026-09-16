// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_bridge.h"

#include "eval.h"
#include "number_format.h"

#include <xlsxcell.h>
#include <xlsxchart.h>
#include <xlsxcellformula.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>
#include <xlsxworkbook.h>
#include <xlsxworksheet.h>
#include <xlsxzipreader_p.h>

#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

// QXlsx measures column widths in character units (~7 px + 5 padding per
// the plan) and row heights in points.
qreal charWidthToPx(qreal chars)
{
    return chars * 7.0 + 5.0;
}

qreal pxToCharWidth(qreal px)
{
    return qMax(1.0, (px - 5.0) / 7.0);
}

qreal ptToPx(qreal pt)
{
    return pt * 96.0 / 72.0;
}

qreal pxToPt(qreal px)
{
    return px * 72.0 / 96.0;
}

CellStyle styleFromFormat(const QXlsx::Format &format)
{
    CellStyle style;
    // Built-in formats may carry only the numFmtId (empty format string).
    style.numberFormat = !format.numberFormat().isEmpty()
        ? format.numberFormat()
        : NumberFormat::builtinCode(format.numberFormatIndex());
    style.bold = format.fontBold();
    style.italic = format.fontItalic();
    style.fontSize = format.fontSize();
    style.fontFamily = format.fontName();
    if (format.fontColor().isValid()) {
        const QColor c = format.fontColor();
        style.fg = Rgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
    }
    return style;
}

QXlsx::Format formatFromStyle(const CellStyle &style, QXlsx::Format base = {})
{
    QXlsx::Format format = base;
    if (!style.numberFormat.isEmpty()
        && style.numberFormat.compare(QLatin1String("General"), Qt::CaseInsensitive) != 0) {
        // Built-in formats serialize reliably through their numFmtId; custom
        // codes go through the format-code path (font attributes persist on
        // both, but QXlsx 1.5.1 drops custom codes on write).
        static const QHash<QString, int> kBuiltin = {
            {QStringLiteral("0"), 1}, {QStringLiteral("0.00"), 2},
            {QStringLiteral("#,##0"), 3}, {QStringLiteral("#,##0.00"), 4},
            {QStringLiteral("0%"), 9}, {QStringLiteral("0.00%"), 10},
            {QStringLiteral("0.00E+00"), 11}, {QStringLiteral("mm-dd-yy"), 14},
            {QStringLiteral("d-mmm-yy"), 15}, {QStringLiteral("d-mmm"), 16},
            {QStringLiteral("mmm-yy"), 17}, {QStringLiteral("h:mm AM/PM"), 18},
            {QStringLiteral("h:mm:ss AM/PM"), 19}, {QStringLiteral("h:mm"), 20},
            {QStringLiteral("h:mm:ss"), 21}, {QStringLiteral("m/d/yy h:mm"), 22},
            {QStringLiteral("@"), 49},
        };
        const auto it = kBuiltin.constFind(style.numberFormat);
        if (it != kBuiltin.constEnd()) {
            format.setNumberFormatIndex(it.value());
        } else {
            format.setNumberFormat(style.numberFormat);
        }
    }
    format.setFontBold(style.bold);
    format.setFontItalic(style.italic);
    if (style.fontSize > 0) {
        format.setFontSize(style.fontSize);
    }
    if (!style.fontFamily.isEmpty()) {
        format.setFontName(style.fontFamily);
    }
    return format;
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
        // not user names and the engine has nothing to do with them.
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

} // namespace

bool readXlsx(QXlsx::Document &doc, Workbook *out, QString *error)
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
    for (int sheetIndex = 0; sheetIndex < names.size(); ++sheetIndex) {
        if (!doc.selectSheet(sheetIndex)) {
            continue;
        }
        QXlsx::Worksheet *worksheet = static_cast<QXlsx::Worksheet *>(doc.sheet(names.at(sheetIndex)));
        if (worksheet == nullptr) {
            continue;
        }
        Sheet &sheet = out->addSheet(names.at(sheetIndex));
        QHash<QString, qint32> styleIds;

        // Shared formulas: only the master cell carries text and the group's
        // ref; dependents carry only the si group index. Record masters by si
        // as they come up (row-major order puts masters first) and resolve
        // dependents through the map, shifting the master AST by the cell's
        // offset (plan section 7.2).
        QHash<int, QXlsx::CellReference> sharedMasters;
        const QXlsx::CellRange dim = worksheet->dimension();
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
                // Import every cell that carries a real format record; the
                // dedup key must cover all imported attributes, not just the
                // number-format string (bold-only and italic-only cells
                // shared a key before and cross-contaminated styles).
                const QXlsx::Format format = cell->format();
                if (!format.isEmpty()) {
                    const CellStyle imported = styleFromFormat(format);
                    const QString key = imported.numberFormat + QLatin1Char('\x1f')
                        + (imported.bold ? QLatin1String("B") : QString())
                        + (imported.italic ? QLatin1String("I") : QString())
                        + (imported.underline ? QLatin1String("U") : QString())
                        + QString::number(imported.fontSize) + QLatin1Char('\x1f')
                        + imported.fontFamily + QLatin1Char('\x1f')
                        + (imported.fg.has_value()
                               ? QString::number((*imported.fg).r * 65536
                                                     + (*imported.fg).g * 256
                                                     + (*imported.fg).b)
                               : QString());
                    if (!styleIds.contains(key)) {
                        styleIds.insert(key, out->addStyle(imported));
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
    }
    // AGENT-GUARD: a final full recalc is mandatory. installFormula evaluates
    // each cell as it loads, so a formula referencing a sheet that had not
    // been read yet (Summary before Detail — the common layout) captured a
    // stale 0/#REF! and no dependency edge could ever fix it afterwards.
    out->recalcAll();
    return true;
}

bool readXlsxFile(const QString &path, Workbook *out, QString *error)
{
    QXlsx::Document doc(path);
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
    readDefinedNames(path, out);
    return readXlsx(doc, out, error);
}

std::unique_ptr<QXlsx::Document> writeXlsx(const Workbook &wb, QString *error)
{
    Q_UNUSED(error);
    auto doc = std::make_unique<QXlsx::Document>();
    QHash<qint32, QXlsx::Format> formats;
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const Sheet &sheet = wb.sheetAt(s);
        if (s == 0 && !doc->sheetNames().isEmpty()) {
            doc->renameSheet(doc->sheetNames().first(), sheet.name());
        } else {
            doc->addSheet(sheet.name());
        }
        doc->selectSheet(sheet.name());
        QXlsx::Worksheet *worksheet =
            static_cast<QXlsx::Worksheet *>(doc->sheet(sheet.name()));
        for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
            const quint64 key = it.key();
            const CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
            const Cell &cell = it.value();
            const qint32 styleId = sheet.styleIdAt(addr);
            const QXlsx::Format format =
                styleId != 0 && wb.style(styleId) != nullptr
                ? formatFromStyle(*wb.style(styleId))
                : QXlsx::Format();
            const int row = addr.row + 1;
            const int col = addr.col + 1;
            // AGENT-NOTE: value writes go through Document (it registers the
            // cell format into the workbook style table; Worksheet::write
            // drops it). Formulas use the Worksheet API - QXlsx has no
            // Document::writeFormula - so formula cells keep the default
            // format in v1 (documented limitation). The engine's evaluated
            // value is written as the cached result: consumers that do not
            // recalculate on load (LibreOffice's default for xlsx) still
            // show correct data.
            if (cell.hasFormula()) {
                const double cached = isNumber(cell.value) ? std::get<double>(cell.value) : 0.0;
                worksheet->writeFormula(row, col, QStringLiteral("=") + cell.formula,
                                        QXlsx::Format(), cached);
            } else if (isNumber(cell.value)) {
                doc->write(row, col, std::get<double>(cell.value), format);
            } else if (isText(cell.value)) {
                doc->write(row, col, std::get<QString>(cell.value), format);
            } else if (isBool(cell.value)) {
                doc->write(row, col, std::get<bool>(cell.value), format);
            } else if (isErr(cell.value)) {
                doc->write(row, col, errText(std::get<Err>(cell.value)), format);
            }
        }
        for (const CellRange &merge : sheet.merges()) {
            doc->mergeCells(QXlsx::CellRange(merge.topLeft.row + 1, merge.topLeft.col + 1,
                                             merge.bottomRight.row + 1,
                                             merge.bottomRight.col + 1));
        }
        for (const qint32 col : sheet.sizedColumns()) {
            worksheet->setColumnWidth(col + 1, col + 1, pxToCharWidth(sheet.columnWidth(col)));
        }
        for (const qint32 row : sheet.sizedRows()) {
            worksheet->setRowHeight(row + 1, row + 1, pxToPt(sheet.rowHeight(row)));
        }
    }
    // Floating charts (plan section 7.2): one chart part per spec, anchored
    // at its cell. QXlsx renders clustered bars for StackedBar (its chart
    // API has no grouping control) - a documented export limitation.
    for (const ChartSpec &chart : wb.charts) {
        const CellRange range = parseChartRange(chart.rangeText);
        if (!range.isValid() || chart.sheet < 0 || chart.sheet >= wb.sheetCount()) {
            continue;
        }
        doc->selectSheet(wb.sheetName(chart.sheet));
        auto *worksheet = static_cast<QXlsx::Worksheet *>(doc->sheet(wb.sheetName(chart.sheet)));
        if (worksheet == nullptr) {
            continue;
        }
        QXlsx::Chart *qxChart =
            worksheet->insertChart(chart.anchorRow + 1, chart.anchorCol + 1,
                                   QSize(chart.widthPx, chart.heightPx));
        if (qxChart == nullptr) {
            continue;
        }
        qxChart->addSeries(QXlsx::CellRange(range.topLeft.row + 1, range.topLeft.col + 1,
                                            range.bottomRight.row + 1, range.bottomRight.col + 1),
                           worksheet, chart.headerH, chart.headerV);
        QXlsx::Chart::ChartType type = QXlsx::Chart::CT_BarChart;
        switch (chart.kind) {
        case ChartSpec::Kind::Line: type = QXlsx::Chart::CT_LineChart; break;
        case ChartSpec::Kind::Area: type = QXlsx::Chart::CT_AreaChart; break;
        case ChartSpec::Kind::Pie: type = QXlsx::Chart::CT_PieChart; break;
        case ChartSpec::Kind::Scatter: type = QXlsx::Chart::CT_ScatterChart; break;
        case ChartSpec::Kind::Bar:
        case ChartSpec::Kind::StackedBar: type = QXlsx::Chart::CT_BarChart; break;
        }
        qxChart->setChartType(type);
        qxChart->setChartTitle(chart.title);
    }
    // Named ranges: the canonical text form is already the xlsx spelling
    // ("Sheet1!$A$1:$A$5"), so the workbook's own defineName takes it as is.
    const QHash<QString, RangeRef> &names = wb.namedRanges();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        static_cast<void>(doc->workbook()->defineName(it.key(), rangeRefText(it.value())));
    }
    return doc;
}

bool writeXlsxFile(const Workbook &wb, const QString &path, QString *error)
{
    std::unique_ptr<QXlsx::Document> doc = writeXlsx(wb, error);
    if (doc == nullptr) {
        return false;
    }
    if (!doc->saveAs(path)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
