// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A desktop alert for a mail rule (M81, D-090): Outlook's "display a
// Desktop Alert" as a freedesktop notification (Desktop Notifications
// Specification 1.2, org.freedesktop.Notifications.Notify on the session
// bus), sent without a sound (the "suppress-sound" hint). Fire and forget:
// a session without a notification service just shows nothing.
//
// AGENT-NOTE: lives in office_mail because it already links Qt DBus
// privately; the app and its tests need no DBus. Tests install a sink
// (setSink) so nothing reaches the user's desktop.
// Threading: GUI thread.

#include <QString>

#include <functional>

namespace QindaOffice::Mail {

class DesktopNotifier {
public:
    using Sink = std::function<void(const QString &title, const QString &body)>;

    // False when no session bus is connected (the call itself is async).
    static bool notify(const QString &title, const QString &body);
    // Replaces the D-Bus call (tests); an empty sink restores it.
    static void setSink(Sink sink);
};

} // namespace QindaOffice::Mail
