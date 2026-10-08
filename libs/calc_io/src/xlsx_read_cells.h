// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Typed literal cells, read straight from the package (M61, D-069).
//
// AGENT-NOTE: QXlsx's cell model cannot be trusted for values. It keeps
// every <v> without a t attribute — which is how Excel writes EVERY number —
// as a QString, so 5 loaded as the text "5" (ISTEXT TRUE, skipped by SUM);
// error cells come back as text; of an inline string's runs only the last
// one survives; shared strings gain their phonetic guide's text; and an
// ISO-8601 t="d" cell reads as 0. Formulas, styles and geometry still come
// from QXlsx: only literal values are taken from here.

#include "value.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>

namespace QindaOffice::Calc::IO {

struct TypedCell {
    Value value;
    // A t="d" cell already carries an absolute date (converted to a 1900
    // serial here); the 1904 date-system shift must not apply to it.
    bool isoDate = false;
};

// The shared string table: each <si>'s text with every rich run joined,
// phonetic guides (<rPh>) left out and _xHHHH_ escapes decoded.
[[nodiscard]] QStringList readSharedStrings(const QByteArray &sstXml);

// The literal (non-formula) cells of one worksheet part, keyed by
// CellAddr::key(). Cells without a value — styled blanks — are absent.
[[nodiscard]] QHash<quint64, TypedCell> readTypedCells(const QByteArray &sheetXml,
                                                      const QStringList &sharedStrings);

} // namespace QindaOffice::Calc::IO
