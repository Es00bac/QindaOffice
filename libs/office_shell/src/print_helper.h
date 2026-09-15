// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PrintHelper (plan section 5.2): QPrintDialog / QPrintPreviewDialog / PDF
// via QPrinter(PdfFormat). The app supplies a paginating render callback
// (the same loop the PDF exporter uses), keeping pagination identical
// across screen, PDF and paper by construction.

#include <functional>

class QPainter;
class QPrinter;
class QString;
class QWidget;

namespace QindaOffice::Shell {

// Renders the document across `printer`'s pages (QPainter already begun,
// positioned at the first page's origin; the callback must call
// printer.newPage() between pages itself).
using PrintRenderCallback = std::function<void(QPrinter *printer, QPainter *painter)>;

class PrintHelper {
public:
    // Print with the platform dialog. Returns false when cancelled.
    [[nodiscard]] static bool print(QWidget *parent, const PrintRenderCallback &render);
    [[nodiscard]] static bool printPreview(QWidget *parent, const PrintRenderCallback &render);
    // Direct PDF export through QPrinter.
    [[nodiscard]] static bool exportPdf(QWidget *parent, const QString &path,
                                        const PrintRenderCallback &render);
};

} // namespace QindaOffice::Shell
