// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The page-painting contract shared by every printed form and report
// (D-046): a PagePainter paints ALL pages of one output onto any paged
// device — QPdfWriter for Export PDF, QPrinter for paper, the preview's
// temporary PDF — so screen, file and paper cannot drift.
//
// AGENT-CONTRACT (with office_quick's PrintRenderCallback,
// libs/office_quick/src/print_service.h): the painter is already active on
// the device, positioned on page 1; the PagePainter calls
// device->newPage() between pages and never ends the painter. QPrinter is
// a QPagedPaintDevice, so the app bridges with
//   PrintRenderCallback cb = asPrintCallback<QPrinter>(painter);
// books_io itself does not link office_quick.

#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QString>

#include <functional>

class QPagedPaintDevice;
class QPainter;

namespace QindaOffice::Books {

using PagePainter = std::function<void(QPagedPaintDevice *device, QPainter *painter)>;

template<class Printer>
[[nodiscard]] std::function<void(Printer *, QPainter *)> asPrintCallback(PagePainter painter)
{
    return [painter = std::move(painter)](Printer *printer, QPainter *qp) { painter(printer, qp); };
}

// US Letter where letter paper is the norm (US, Canada, Mexico,
// Philippines, Chile, Colombia …), A4 elsewhere.
[[nodiscard]] QPageSize defaultPageSize(const QLocale &locale);

// Renders `painter` to a PDF at `path` (overwrites). Zero device margins:
// the painters keep their own margins, identical on paper.
bool writePdf(const QString &path, const PagePainter &painter, const QPageSize &pageSize,
              QPageLayout::Orientation orientation = QPageLayout::Portrait, QString *error = nullptr);

} // namespace QindaOffice::Books
