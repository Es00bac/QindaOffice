// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Page setup + header/footer persistence (M14). QTextDocument::setHtml
// drops unknown <meta> tags, so the page geometry, header and footer are
// carried in the HTML head and parsed back from the raw source on load
// (ODT/DOCX through the LibreOffice bridge still lose them - the
// documented limitation). {page}/{pages} tokens in header/footer text
// expand at render time so screen, print and PDF always agree.

#include <QSizeF>
#include <QString>

namespace QindaOffice::Apps::Write {

// Header/footer text drawn into the page margin on every page (screen,
// print and PDF share this so all outputs agree). {page}/{pages} tokens
// expand per page. margin is in document coordinates (96-dpi px).
struct PageDecoration {
    QString header;
    QString footer;
    qreal margin = 96;
};

struct PageSetup {
    QSizeF pageSize;
    qreal margin = 96;
    bool hasCustomPageSize = false;
    QString header;
    QString footer;
};

// Reads the three qindawrite-* metas from raw HTML; tolerant of absence
// (defaults) and of HTML-escaped content.
[[nodiscard]] PageSetup pageSetupFromHtml(const QString &html);

// Injects the three metas into an HTML document (before </head>); returns
// the input unchanged when no </head> exists.
[[nodiscard]] QString injectPageSetupMeta(const QString &html, const PageSetup &setup);

// Expands {page} and {pages} tokens for one rendered page.
[[nodiscard]] QString formatPageDecoration(const QString &pattern, int page, int pageCount);

} // namespace QindaOffice::Apps::Write
