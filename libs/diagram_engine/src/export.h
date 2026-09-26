// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Output (M50, D-050): PNG, SVG and PDF of a page or a selection, and the
// paged painter Print / Print Preview / Export PDF share. Everything is
// drawn by painter.h, so every output matches the canvas.
//
// Printing — and Export PDF, which goes through the same print callback —
// fits each diagram page on one sheet (aspect kept, centred inside the
// printable area, turned a quarter when the page and the sheet differ in
// orientation); a sheet never shows the screen grid.

#include "drawing.h"

#include <QImage>
#include <QSet>
#include <QString>

#include <functional>

class QPagedPaintDevice;
class QPainter;

namespace QindaOffice::Diagram {

using PagesPainter = std::function<void(QPagedPaintDevice *device, QPainter *painter)>;

// The region an export covers: the whole page, or the selection's bounds
// plus a margin.
[[nodiscard]] QRectF exportRegion(const Page &page, const QSet<QString> &ids, double margin = 12);

// Renders the region at `scale` device pixels per page unit.
[[nodiscard]] QImage renderImage(const Page &page, const QSet<QString> &ids, double scale, bool transparent = false);
bool exportPng(const QString &path, const Page &page, const QSet<QString> &ids, double scale,
               bool transparent = false, QString *error = nullptr);
bool exportSvg(const QString &path, const Page &page, const QSet<QString> &ids, QString *error = nullptr);
[[nodiscard]] QByteArray svgBytes(const Page &page, const QSet<QString> &ids);

// Paints `pages` (all when empty) one per sheet onto an active painter on a
// paged device (a QPrinter or QPdfWriter); see office_quick's
// PrintRenderCallback contract.
[[nodiscard]] PagesPainter pagesPainter(const Drawing &drawing, const QStringList &pageIds = {});

} // namespace QindaOffice::Diagram
