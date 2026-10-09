// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Printing one message (M64, D-072): File ▸ Print, Print Preview and
// Export as PDF of the message open in the reading pane, the way Outlook
// prints a memo — subject, From / Sent / To / Cc / Attachments, a rule, then
// the body as the pane shows it (formatted HTML, inline and attached
// pictures; remote pictures only when they are shown on screen).
//
// AGENT-CONTRACT (MailHost::printRenderCallback, office_quick PrintJob):
// the callback is a snapshot taken when the print starts — the message, its
// parsed source and the pictures fetched so far — and paginates the same
// QTextDocument for paper, preview and PDF (one renderer, D-031 plan 5.2).

#include "print_service.h"

#include <QString>

#include <memory>

class QTextDocument;

namespace QindaOffice::Apps::Mail {

class MailService;

class MessagePrinter {
public:
    // Empty callback when there is no such message.
    static Quick::PrintRenderCallback callback(MailService *service, const QString &messageId);
    // The document the callback paginates (tests): headers, rule, body.
    static std::unique_ptr<QTextDocument> document(MailService *service, const QString &messageId);
};

} // namespace QindaOffice::Apps::Mail
