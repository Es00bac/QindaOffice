// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "secret_backend.h"
#include <QDBusConnection>

namespace QindaOffice::Mail {

// D-067: preserve the primary backend and its errors. Only a missing read
// consults native imported KWallet Password records through Secret Service.
// Writes stay primary; remove reports Failed when a preserved import remains.
// Create/use/destroy on one event-loop thread. Keeps primary alive; destroying
// the wrapper cancels pending callbacks and closes its owned bus resources.
// Injectable connection is for a private synthetic test bus, never a global
// service override. Production uses the current session bus.
[[nodiscard]] std::shared_ptr<SecretBackend> withImportedPasswords(
    std::shared_ptr<SecretBackend> primary,
    const QDBusConnection &bus = QDBusConnection::sessionBus());

} // namespace QindaOffice::Mail
