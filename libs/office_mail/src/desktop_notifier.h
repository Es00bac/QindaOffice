// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A desktop alert for a mail rule (M81, D-090): Outlook's "display a
// Desktop Alert" as a freedesktop notification (Desktop Notifications
// Specification 1.2, org.freedesktop.Notifications.Notify on the session
// bus), sent without a sound (the "suppress-sound" hint). Fire and forget:
// a session without a notification service just shows nothing.
//
// AGENT-CONTRACT: QindaTK owns optional D-Bus delivery and server availability.
// This adapter supplies mail-specific hints; tests install a sink so nothing
// reaches the user's desktop.
// Threading: GUI thread.

#include <QString>

#include <functional>

namespace QindaOffice::Mail {

class DesktopNotifier {
public:
    using Sink = std::function<void(const QString &title, const QString &body)>;

    // Initialize availability watching during MailService construction.
    static void prepare();
    // False when no notification server is available (delivery is async).
    static bool notify(const QString &title, const QString &body);
    // Replaces the D-Bus call (tests); an empty sink restores it.
    static void setSink(Sink sink);
};

} // namespace QindaOffice::Mail
