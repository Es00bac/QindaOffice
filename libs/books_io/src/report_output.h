// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ReportTable → PDF / paper (a PagePainter) and → CSV. The table is the
// report; nothing here recomputes a number (report_table.h contract).

#include "page_output.h"
#include "report_table.h"

#include <QColor>
#include <QLocale>

namespace QindaOffice::Books {

class CompanyFile;

struct ReportStyle {
    QLocale locale = QLocale(QLocale::English, QLocale::UnitedStates);
    QColor accent = QColor(0x1F, 0x5F, 0xBF);
};

class ReportPdf {
public:
    static void paint(QPagedPaintDevice *device, QPainter *painter, const ReportTable &table,
                      const ReportStyle &style);
    [[nodiscard]] static PagePainter painter(const ReportTable &table, const ReportStyle &style);
    // True when the columns do not fit a portrait page of `pageSize`.
    [[nodiscard]] static bool prefersLandscape(const ReportTable &table, const QPageSize &pageSize,
                                               const ReportStyle &style);
    // The company's locale, accent and paper; landscape when needed.
    static bool writeReportPdf(CompanyFile &file, const ReportTable &table, const QString &path,
                               QString *error = nullptr);
    [[nodiscard]] static ReportStyle styleFor(CompanyFile &file);
};

// Header row of column titles, then one line per row. Money as plain
// "-1234.56" (Money::toPlainString), numbers/percent as exact decimals,
// dates ISO 8601, labels without indentation. RFC 4180, CRLF, UTF-8.
[[nodiscard]] QByteArray reportToCsv(const ReportTable &table);
// A cell's machine value as reportToCsv writes it.
[[nodiscard]] QString reportCellPlain(const ReportCell &cell, const QString &currency);

} // namespace QindaOffice::Books
