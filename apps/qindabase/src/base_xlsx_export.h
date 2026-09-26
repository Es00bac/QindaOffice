// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// XLSX export for QindaBase (M49) through calc_io — see the .cpp.

#include "base_records.h"

namespace QindaOffice::Apps::Base {

// Storage rows (base_types.h values) under their columns, into one sheet.
::QindaOffice::Base::Result writeXlsx(const QString &path, const QString &sheetName,
                                      const QList<::QindaOffice::Base::ColumnInfo> &columns,
                                      const QList<QVariantList> &rows,
                                      const ::QindaOffice::Base::CurrencyFormat &currency);

} // namespace QindaOffice::Apps::Base
