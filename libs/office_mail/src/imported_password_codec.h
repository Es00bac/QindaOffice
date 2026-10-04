// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

namespace QindaOffice::Mail::ImportedPassword {

// KWallet Password (entry type 1) is a QDataStream QString, not UTF-8 text.
// Accept one complete, bounded, non-empty UTF-16 string without NUL;
// no permissive byte fallback or lossy conversion.
[[nodiscard]] bool decode(const QByteArray &serialized, QString &password);
[[nodiscard]] bool validKey(const QString &key);

} // namespace QindaOffice::Mail::ImportedPassword
