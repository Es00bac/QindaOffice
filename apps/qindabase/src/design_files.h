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
// M80: `design` without `fields` in its binding and form layout — a form
// saved before its lookups left the designer's mirror (base_snapshot.h)
// still opens with its labels and order (base_service_form_controls.cpp).
[[nodiscard]] QVariantMap designWithoutFields(const QVariantMap &design, const QStringList &fields);

} // namespace QindaOffice::Apps::Base
