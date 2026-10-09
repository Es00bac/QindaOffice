// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Post-pass work for a sheet's M66 features (D-075) in a QXlsx-written
// worksheet part: dynamic-array anchors get cm="1", the protection, the
// hyperlinks (with their external relationships), outline levels and
// hidden flags on <row>/<col>, the outline depth in <sheetFormatPr>, and
// <sheetPr> (code name, summary placement, fit-to-page). Every edit lands
// at its schema position; a missing anchor fails the save rather than
// writing a broken package (D-012).

#include "xlsx_manifest.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

// QXlsx writes an empty sheet's data as <sheetData/>; the post-pass inserts
// after </sheetData>, so the element is opened first.
void openSheetData(QByteArray *sheetXml);

[[nodiscard]] bool applySheetFeatures(QHash<QString, QByteArray> *parts, const PatchManifest::SheetPatch &patch,
                                      QString *error);

// The xf index each listed cell uses in the written sheet → the cell's
// protection bits (bit 1 unlocked, bit 2 formula hidden).
void collectXfProtection(const QByteArray &sheetXml, const QHash<QString, quint8> &cells, QHash<int, quint8> *xfs);

// Adds <protection> (and applyProtection) to the listed xfs of styles.xml.
[[nodiscard]] bool applyXfProtection(QByteArray *stylesXml, const QHash<int, quint8> &xfs);

} // namespace QindaOffice::Calc::IO
