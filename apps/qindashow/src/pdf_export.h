// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Deck PDF export (plan section 7.5): slide pages at the deck's point
// size with zero margins via QPdfWriter; optional trailing notes pages
// (portrait A4, slide image in the top 55%, notes below via
// QTextDocument::drawContents).

#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

#include <QString>

namespace QindaOffice::Apps::Show {

[[nodiscard]] bool exportDeckPdf(const ShowModel::Presentation &deck, const QString &path,
                                 bool withNotesPages, QString *error = nullptr);

} // namespace QindaOffice::Apps::Show
