// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared internals for the direct package readers (xlsx_read_xml,
// xlsx_read_parts, xlsx_read_drawings): address/range parsing, .rels reading,
// part resolution and colour-attribute resolution. Everything reads the
// NORMALIZED package (xlsx_package.h), so relationship targets are already
// relative; element names are compared as local names because producers may
// prefix (the Open XML SDK writes <x:worksheet>).

#include "xlsx_style.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>
#include <QXmlStreamAttributes>

#include <optional>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc::IO {

[[nodiscard]] std::optional<CellAddr> addrFromText(const QString &text);
[[nodiscard]] std::optional<CellRange> rangeFromText(const QString &text);

// id -> (type, target) for a .rels part. Missing part yields an empty map.
[[nodiscard]] QHash<QString, QPair<QString, QString>> readRels(QXlsx::ZipReader &zip,
                                                               const QString &relsPath);

// sheet index -> part path, from xl/workbook.xml + its rels.
[[nodiscard]] QVector<QString> sheetPartMap(QXlsx::ZipReader &zip);

// Resolves a relationship target against the directory of the part that owns
// the .rels (absolute "/xl/…" targets are honoured too).
[[nodiscard]] QString resolveTarget(const QString &ownerDir, const QString &target);

// A colour element's value (rgb / theme+tint / indexed), resolved.
[[nodiscard]] std::optional<Rgb> colorFromAttributes(const QXmlStreamAttributes &attributes,
                                                     const ThemePalette &palette);

} // namespace QindaOffice::Calc::IO
