// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_helper.h"

#include <QFileDialog>
#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>

namespace QindaOffice::Shell {

bool PrintHelper::print(QWidget *parent, const PrintRenderCallback &render)
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    QPainter painter(&printer);
    render(&printer, &painter);
    return true;
}

bool PrintHelper::printPreview(QWidget *parent, const PrintRenderCallback &render)
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintPreviewDialog dialog(&printer, parent);
    QObject::connect(&dialog, &QPrintPreviewDialog::paintRequested, &dialog,
                     [&render](QPrinter *printer) {
                         QPainter painter(printer);
                         render(printer, &painter);
                     });
    return dialog.exec() == QDialog::Accepted;
}

bool PrintHelper::exportPdf(QWidget *parent, const QString &path,
                            const PrintRenderCallback &render)
{
    Q_UNUSED(parent);
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    QPainter painter(&printer);
    // AGENT-GUARD: report honestly — an unwritable target previously
    // returned true while the render went to an inactive painter.
    if (!painter.isActive()) {
        return false;
    }
    render(&printer, &painter);
    return true;
}

} // namespace QindaOffice::Shell
