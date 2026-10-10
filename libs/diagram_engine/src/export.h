// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Output (M50, D-050): PNG, SVG and PDF of a page or a selection, and the
// paged painter Print / Print Preview / Export PDF share. Everything is
// drawn by painter.h, so every output matches the canvas.
//
// Printing — and Export PDF, which goes through the same print callback —
// follows each page's print setup (M87, print_tiles.h): by default the page
// fitted on one sheet (aspect kept, centred inside the printable area,
// turned a quarter when the page and the sheet differ in orientation),
// else at a print zoom or fitted to sheets across and down, one sheet per
// tile. Background pages print under their pages; items on layers that do
// not print stay off paper (and off every export); a sheet never shows the
// screen grid.

#include "drawing.h"

#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class QPagedPaintDevice;
class QPainter;

namespace QindaOffice::Diagram {

using PagesPainter = std::function<void(QPagedPaintDevice *device, QPainter *painter)>;

// The region an export covers: the whole page, or the selection's bounds
// plus a margin.
[[nodiscard]] QRectF exportRegion(const Page &page, const QSet<QString> &ids, double margin = 12);

// Renders the region at `scale` device pixels per page unit; a whole page
// shows its `backgrounds` (layers.h backgroundChain) under it.
[[nodiscard]] QImage renderImage(const Page &page, const QSet<QString> &ids, double scale, bool transparent = false,
                                 const QVector<const Page *> &backgrounds = {});
bool exportPng(const QString &path, const Page &page, const QSet<QString> &ids, double scale,
               bool transparent = false, QString *error = nullptr, const QVector<const Page *> &backgrounds = {});
bool exportSvg(const QString &path, const Page &page, const QSet<QString> &ids, QString *error = nullptr,
               const QVector<const Page *> &backgrounds = {});
[[nodiscard]] QByteArray svgBytes(const Page &page, const QSet<QString> &ids,
                                  const QVector<const Page *> &backgrounds = {});

// Paints `pages` (every foreground page when empty) onto an active painter
// on a paged device (a QPrinter or QPdfWriter), each on as many sheets as
// its print setup takes; see office_quick's PrintRenderCallback contract.
[[nodiscard]] PagesPainter pagesPainter(const Drawing &drawing, const QStringList &pageIds = {});

} // namespace QindaOffice::Diagram
