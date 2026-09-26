// SPDX-License-Identifier: GPL-3.0-or-later
#include "datasheet_source.h"

#include "datasheet_model.h"

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

DatasheetSource::DatasheetSource(DatasheetModel *model)
    : m_model(model)
{
}

QVariant DatasheetSource::value(const QString &rowId, const QString &columnId) const
{
    const int column = m_model->columnIndex(columnId);
    if (column < 0) {
        return {};
    }
    if (rowId == QLatin1String("new")) {
        const QVariant pending = m_model->m_pending.value(m_model->m_columns.at(column).name);
        return pending.isValid() ? pending.toString() : QVariant();
    }
    const int row = m_model->rowOf(rowId);
    return row < 0 ? QVariant() : QVariant(m_model->display(row, column));
}

QVariant DatasheetSource::editValue(const QString &rowId, const QString &columnId) const
{
    const int column = m_model->columnIndex(columnId);
    if (column < 0) {
        return {};
    }
    if (rowId == QLatin1String("new")) {
        return m_model->m_pending.value(m_model->m_columns.at(column).name).toString();
    }
    const int row = m_model->rowOf(rowId);
    return row < 0 ? QVariant() : QVariant(m_model->edit(row, column));
}

QVariantMap DatasheetSource::cellStyle(const QString &rowId, const QString &columnId) const
{
    Q_UNUSED(rowId);
    const int column = m_model->columnIndex(columnId);
    if (column < 0) {
        return {};
    }
    const Engine::ColumnInfo &c = m_model->m_columns.at(column);
    const bool lookup = !m_model->m_lookups.value(column).isEmpty();
    if (Engine::isNumeric(c.type) && !lookup) {
        return {{QStringLiteral("horizontalAlignment"), QStringLiteral("right")}, {QStringLiteral("numeric"), true}};
    }
    if (c.type == Engine::FieldType::YesNo) {
        return {{QStringLiteral("horizontalAlignment"), QStringLiteral("center")}};
    }
    return {};
}

bool DatasheetSource::commitChanges(const QString &label, const QVariantList &changes, QString *error)
{
    Q_UNUSED(label);
    return m_model->commit(changes, error);
}

} // namespace QindaOffice::Apps::Base
