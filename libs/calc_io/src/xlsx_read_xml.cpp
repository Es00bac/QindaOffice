// SPDX-License-Identifier: GPL-3.0-or-later
// The orchestration of the per-sheet extras read straight from the package:
// conditional formats and validations (xlsx_rules.h, M78), the autofilter,
// comments and tables (xlsx_read_parts.cpp), the view, the print settings
// (xlsx_print.h), scenarios and data tables (xlsx_whatif.h); charts are
// xlsx_read_drawings.cpp's. Reads the NORMALIZED package, so relationship
// targets are already relative; element names are compared as local names
// because producers may prefix.

#include "xlsx_read_xml.h"

#include "xlsx_parts.h"
#include "xlsx_print.h"
#include "xlsx_rules.h"
#include "xlsx_whatif.h"

#include "sheet.h"
#include "workbook.h"

#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

void readSheetExtras(const QString &packagePath, qint32 sheetIndex, const ThemePalette &palette,
                     const QStringList &sourceDxfs, Sheet *sheet, RuleReadCounts *counts)
{
    QXlsx::ZipReader zip(packagePath);
    if (!zip.exists()) {
        return;
    }
    const QVector<QString> parts = sheetPartMap(zip);
    if (sheetIndex < 0 || sheetIndex >= parts.size() || parts.at(sheetIndex).isEmpty()) {
        return;
    }
    const QByteArray xml = zip.fileData(parts.at(sheetIndex));
    if (xml.isEmpty()) {
        return;
    }
    // M78 (D-087): every conditional-format and validation kind, the ones
    // the model cannot hold kept as written (xlsx_rules.h).
    RuleReadCounts sheetCounts;
    readSheetRules(xml, sourceDxfs, palette, sheet, &sheetCounts);
    if (counts != nullptr) {
        *counts += sheetCounts;
    }
    const AutoFilter filter = readAutoFilter(xml);
    if (filter.active) {
        sheet->setAutoFilter(filter);
    }
    const QHash<quint64, CellComment> comments = readComments(zip, parts.at(sheetIndex));
    if (!comments.isEmpty()) {
        sheet->setComments(comments);
    }
    const QList<TableSpec> tables = readTables(zip, parts.at(sheetIndex));
    if (!tables.isEmpty()) {
        sheet->setTables(tables);
    }
    readSheetViews(xml, sheet);
    readSheetPrint(xml, sheet);  // M78: every print setting (xlsx_print.h)
    readSheetWhatIf(xml, sheet); // M78: scenarios and data tables (xlsx_whatif.h)
    if (sheetCounts.keptFormats + sheetCounts.keptValidations > 0) {
        qInfo("xlsx: sheet %d: kept %d conditional-format and %d validation rule(s) as written",
              int(sheetIndex), sheetCounts.keptFormats, sheetCounts.keptValidations);
    }
}

} // namespace QindaOffice::Calc::IO
