// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// text/plain mail bodies as HTML for the reading pane and print (M64, D-072):
// the same QTextDocument path as sanitized HTML, so plain and HTML mail look,
// select and print alike.
//
// Pure function, any thread, linear in the input.

#include <QString>

namespace QindaOffice::Mail {

// Returns `text` as one HTML fragment, "<div style=\"white-space: pre-wrap;\">…</div>":
//  * everything HTML-escaped ("<script>" stays text); NUL becomes U+FFFD;
//  * line breaks (LF, CRLF, CR) and runs of spaces kept — pre-wrap, and
//    QTextDocument::toPlainText() of the result gives the text back with LF;
//  * quoted lines (starting with '>') greyed, their '>' kept;
//  * http:// and https:// URLs, www. hosts (as https://) and e-mail
//    addresses (as mailto:) become <a href> links. Trailing . , ; : ! ? '
//    and an unbalanced ) ] } are not part of a link.
QString plainTextToHtml(const QString &text);

} // namespace QindaOffice::Mail
