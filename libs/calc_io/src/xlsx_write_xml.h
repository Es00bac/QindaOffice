// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Part generators for the package pieces QXlsx has no model for: the
// comments part and the autoFilter element. The callers know the engine
// model; these produce the bytes.

#include "sheet_rules.h"

#include <QByteArray>

namespace QindaOffice::Calc::IO {

// xl/commentsN.xml for one sheet's comment map (keyed by packed CellAddr).
[[nodiscard]] QByteArray commentsPartXml(const QHash<quint64, CellComment> &comments);

// The <autoFilter> element for a sheet's filter model (empty when inactive).
[[nodiscard]] QByteArray autoFilterXml(const AutoFilter &filter);

} // namespace QindaOffice::Calc::IO
