// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdf_exporter.h"

#include "page_geometry.h"

#include <QAbstractTextDocumentLayout>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>

namespace QindaOffice::Apps::Write {

bool exportDocumentPdf(QTextDocument *document, const QString &path, QString *error)
{
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("no document");
        }
        return false;
    }
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(210, 297), QPageSize::Millimeter)); // A4
    writer.setResolution(96);
    writer.setCreator(QStringLiteral("QindaWrite"));
    writer.setTitle(QFileInfo(path).completeBaseName());

    QPainter painter(&writer);
    if (!painter.isActive()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1 for writing").arg(path);
        }
        return false;
    }
    const QSizeF paper = writer.pageLayout().paintRectPixels(96).size();
    renderDocumentPages(&painter, paper, document, [&writer] { writer.newPage(); });
    painter.end();
    return true;
}

void renderDocumentPages(QPainter *painter, const QSizeF &targetPixels, QTextDocument *document,
                         const std::function<void()> &newPage)
{
    // Probe-verified pattern (tst_write_probe_pagination): scale once, then
    // per page translate by -i*pageH, clip, draw; newPage between pages
    // (the painter transform persists across newPage on QPdfWriter/QPrinter).
    const QSizeF page = document->pageSize();
    painter->save();
    painter->scale(targetPixels.width() / page.width(), targetPixels.height() / page.height());
    const QAbstractTextDocumentLayout *layout = document->documentLayout();
    const int pages = qMax(1, int(std::ceil(layout->documentSize().height() / page.height())));
    for (int i = 0; i < pages; ++i) {
        if (i > 0 && newPage != nullptr) {
            newPage();
        }
        painter->save();
        painter->translate(0, -i * page.height());
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(0, i * page.height(), page.width(), page.height());
        const_cast<QAbstractTextDocumentLayout *>(layout)->draw(painter, context);
        painter->restore();
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Write
