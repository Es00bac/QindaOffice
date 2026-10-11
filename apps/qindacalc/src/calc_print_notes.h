// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "calc_print_internal.h"
#include "xlsx_print_assets.h"
namespace QindaOffice::Apps::QindaCalc::PrintDetail {
using NotePages = QList<QStringList>;
[[nodiscard]] NotePages endNotePages(const Canvas &canvas, const PrintContext &ctx, const CalcEng::PrintLayout &layout);
void paintEndNotes(const Canvas &canvas, const PrintContext &ctx, const CalcEng::PrintLayout &layout, const QStringList &lines);
void paintDisplayedNotes(const Canvas &canvas, const PrintContext &ctx, const QList<Line> &rows, const QList<Line> &cols,
                         const QList<qreal> &xs, const QList<qreal> &ys, const CalcEng::IO::LegacyPrintAssets &assets);
}
