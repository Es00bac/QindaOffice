// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "managesieve_provider.h"
#include <functional>
namespace QindaOffice::Mail {
// The public toolkit client owns TLS/authentication/literals. This adapter
// owns only the mail application's managed vacation block and explicit activation.
void startToolkitVacation(QObject *owner, const ManageSieveProvider::Server &server,
                          const QString &secret, bool write, const AutoReply &reply,
                          const QStringList &addresses, std::function<void(QVariantMap)> done);
}
