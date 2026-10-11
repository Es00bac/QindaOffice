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

namespace QindaOffice::Base {
class BaseFile;
}

namespace QindaOffice::Apps::Base {

// Validates `design` against the provider's schema first; false with
// `error` when the design no longer fits or the file cannot be written.
bool writeDesignFile(const QString &path, const QVariantMap &design, QindaTK::LocalDatabaseProvider *provider,
                     QString *error);
// M89 (D-097, base_service_form_layout.cpp): the design the form object
// `definition` over `table` opens with, bound to every column of `mirror`
// (its form mirror): its saved design brought to the table as it is now,
// laid out with positioned controls once when it was saved before M89
// (base_form_layout.h). `notice` says what had to change; {} with `error`
// when the table cannot be bound.
[[nodiscard]] QVariantMap formDesignFor(const QindaOffice::Base::BaseFile &file, const QVariantMap &definition,
                                        const QString &table, QindaTK::LocalDatabaseProvider *mirror,
                                        QString *notice, QString *error);

} // namespace QindaOffice::Apps::Base
