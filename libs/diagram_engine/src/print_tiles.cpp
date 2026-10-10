// SPDX-License-Identifier: GPL-3.0-or-later
// Printing a page across sheets (print_tiles.h).
#include "print_tiles.h"

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

bool fitsOneSheet(const PrintSetup &p)
{
    return p.zoom <= 0 && p.sheetsAcross <= 1 && p.sheetsDown <= 1;
}

struct Grid {
    int across = 1;
    int down = 1;
    double scale = 1;   // device units per page unit
    QPointF offset;     // where the page's origin lands on the sheet canvas
};

// The sheets laid edge to edge make one canvas; the page sits on it at
// `scale`, centred when the setup says so.
Grid gridFor(const Page &page, QSizeF sheet, double dpi)
{
    const PrintSetup &p = page.print;
    const QSizeF size = page.size;
    Grid g;
    if (p.zoom > 0) {
        g.scale = p.zoom * dpi / 96.0;
        g.across = std::max(1, int(std::ceil(size.width() * g.scale / sheet.width() - 1e-6)));
        g.down = std::max(1, int(std::ceil(size.height() * g.scale / sheet.height() - 1e-6)));
    } else {
        g.across = std::max(1, p.sheetsAcross);
        g.down = std::max(1, p.sheetsDown);
        g.scale = std::min(g.across * sheet.width() / size.width(), g.down * sheet.height() / size.height());
    }
    if (p.center) {
        g.offset = QPointF((g.across * sheet.width() - size.width() * g.scale) / 2,
                           (g.down * sheet.height() - size.height() * g.scale) / 2);
    }
    return g;
}

} // namespace

QVector<PrintTile> printTiles(const Page &page, QSizeF sheet, double dpi)
{
    if (sheet.width() <= 0 || sheet.height() <= 0 || page.size.width() <= 0 || page.size.height() <= 0 || dpi <= 0) {
        return {};
    }
    const QRectF whole(QPointF(0, 0), page.size);
    if (fitsOneSheet(page.print)) {
        // M50's rule: fitted on one sheet, turned a quarter when the page
        // and the sheet differ in orientation.
        PrintTile t;
        t.region = whole;
        t.turned = (page.size.width() > page.size.height()) != (sheet.width() > sheet.height())
                && std::abs(page.size.width() - page.size.height()) > 1;
        const QSizeF laid = t.turned ? page.size.transposed() : page.size;
        t.scale = std::min(sheet.width() / laid.width(), sheet.height() / laid.height());
        t.offset = page.print.center ? QPointF((sheet.width() - laid.width() * t.scale) / 2,
                                               (sheet.height() - laid.height() * t.scale) / 2)
                                     : QPointF();
        return {t};
    }
    const Grid g = gridFor(page, sheet, dpi);
    QVector<PrintTile> tiles;
    for (int j = 0; j < g.down; ++j) {
        for (int i = 0; i < g.across; ++i) {
            PrintTile t;
            t.scale = g.scale;
            t.region = QRectF((i * sheet.width() - g.offset.x()) / g.scale, (j * sheet.height() - g.offset.y()) / g.scale,
                              sheet.width() / g.scale, sheet.height() / g.scale);
            // A sheet the drawing does not reach is not printed.
            if (t.region.intersects(whole)) {
                tiles.append(t);
            }
        }
    }
    return tiles;
}

QVector<QRectF> pageBreakTiles(const Page &page)
{
    if (fitsOneSheet(page.print)) {
        return {};
    }
    const QSizeF paper = page.print.paper.isEmpty() ? page.size : page.print.paper;
    const QSizeF printable(paper.width() - kPrintMargin * 2, paper.height() - kPrintMargin * 2);
    if (printable.width() <= 1 || printable.height() <= 1) {
        return {};
    }
    // At 96 device units per inch a device unit is a page unit at 100 %.
    const QVector<PrintTile> tiles = printTiles(page, printable, 96);
    QVector<QRectF> out;
    const QRectF whole(QPointF(0, 0), page.size);
    for (const PrintTile &t : tiles) {
        out.append(t.region.intersected(whole));
    }
    return out.size() > 1 ? out : QVector<QRectF>();
}

} // namespace QindaOffice::Diagram
