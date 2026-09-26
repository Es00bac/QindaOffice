// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DatasheetSource: the QindaTK grid's domain source for a datasheet
// (docs/editable-grid.md "Domain sources" in QindaTK). It reads the
// DatasheetModel's cached rows lazily and hands committed change sets back
// to the model, which writes them through base_engine.

#include <qindatk/editable_grid_model.h>

namespace QindaOffice::Apps::Base {

class DatasheetModel;

class DatasheetSource final : public QindaTK::GridCellSource, public QindaTK::GridCellSourceExtension {
public:
    explicit DatasheetSource(DatasheetModel *model);
    [[nodiscard]] QVariant value(const QString &rowId, const QString &columnId) const override;
    [[nodiscard]] QVariant editValue(const QString &rowId, const QString &columnId) const override;
    [[nodiscard]] QVariantMap cellStyle(const QString &rowId, const QString &columnId) const override;
    bool commitChanges(const QString &label, const QVariantList &changes, QString *error) override;

private:
    DatasheetModel *m_model;
};

} // namespace QindaOffice::Apps::Base
