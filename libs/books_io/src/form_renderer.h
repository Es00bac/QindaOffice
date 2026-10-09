// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Paints a PrintableDocument (invoice, estimate, sales receipt, credit
// memo, statement, purchase order) as clean paginated pages: company block
// and logo, the title in the company's accent colour, bill-to (a purchase
// order's vendor and ship-to), the facts (number, date,
// terms, due), a highlighted amount, the line table with its header
// repeated on every page, totals, notes and payment instructions, footer
// with "Page x of y". White page, black ink, one accent (D-004).

#include "page_output.h"
#include "printable_document.h"

namespace QindaOffice::Books {

class FormRenderer {
public:
    // Lays out and paints every page (see page_output.h for the contract).
    static void paint(QPagedPaintDevice *device, QPainter *painter, const PrintableDocument &document);
    [[nodiscard]] static PagePainter painter(const PrintableDocument &document);
    // Number of pages the document takes on `pageSize` (portrait).
    [[nodiscard]] static int pageCount(const PrintableDocument &document, const QPageSize &pageSize);
    // Convenience: build + writePdf on the company's default paper.
    static bool writeDocumentPdf(CompanyFile &file, qint64 docId, const QString &path, QString *error = nullptr);
    static bool writeStatementPdf(CompanyFile &file, qint64 customerId, const QDate &from, const QDate &to,
                                  const QString &path, QString *error = nullptr);
};

} // namespace QindaOffice::Books
