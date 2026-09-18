// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OOXML package normalisation for QXlsx. A relationship Target may be an
// absolute part name ("/xl/worksheets/sheet1.xml") — Open XML SDK,
// ClosedXML and several exporters write them that way — but QXlsx composes
// every target relative to its source part ("xl" + "/" + "/xl/…"), so such
// a workbook loads with no sheets, no styles and no shared strings.
// normalizedXlsxPackage() hands QXlsx a copy whose targets are relative;
// a package that is already relative is returned as-is.

#include <QString>

class QTemporaryDir;

namespace QindaOffice::Calc::IO {

// True when the package's .rels parts contain an absolute Target.
[[nodiscard]] bool xlsxPackageHasAbsoluteTargets(const QString &path);

// The path QXlsx should read: `path` itself, or a rewritten copy under
// `scratch` (which must outlive the QXlsx::Document). Empty with `error`
// when the package cannot be read or rewritten.
[[nodiscard]] QString normalizedXlsxPackage(const QString &path, QTemporaryDir &scratch,
                                            QString *error = nullptr);

// A relationship Target made relative to the directory of its source part
// ("xl/worksheets" + "/xl/drawings/drawing1.xml" → "../drawings/drawing1.xml").
// Relative and external targets come back unchanged.
[[nodiscard]] QString relativeTarget(const QString &sourceDir, const QString &target);

} // namespace QindaOffice::Calc::IO
