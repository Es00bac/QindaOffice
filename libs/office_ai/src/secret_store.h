// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// API-key storage backed by QtKeychain (plan section 5.3): keys live in
// the platform secret service under service "qindaoffice"; a plain-file
// fallback is deliberately NOT provided (fail honestly instead).

#include <QObject>
#include <QString>

#include <functional>

namespace QKeychain {
class ReadPasswordJob;
class WritePasswordJob;
}

namespace QindaOffice::Ai {

class SecretStore : public QObject {
    Q_OBJECT
public:
    explicit SecretStore(QObject *parent = nullptr);

    // `done` (optional) runs when the keyring has answered.
    void store(const QString &key, const QString &value,
               const std::function<void(bool ok)> &done = {});
    void retrieve(const QString &key, const std::function<void(const QString &value)> &cb);
    void remove(const QString &key);
};

} // namespace QindaOffice::Ai
