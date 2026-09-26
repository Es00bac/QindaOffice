// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Writing a QindaTK form/report design as a session-document file that
// Tk.DocumentManager opens with a fresh identity and a clean history
// (qindatk.local-data-design version 2 with its `session` header). Used by
// the form and report halves of BaseService.

#include <QString>
#include <QVariantMap>

namespace QindaTK {
class LocalDatabaseProvider;
}

namespace QindaOffice::Apps::Base {

// Validates `design` against the provider's schema first; false with
// `error` when the design no longer fits or the file cannot be written.
bool writeDesignFile(const QString &path, const QVariantMap &design, QindaTK::LocalDatabaseProvider *provider,
                     QString *error);

} // namespace QindaOffice::Apps::Base
