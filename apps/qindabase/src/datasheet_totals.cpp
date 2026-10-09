// SPDX-License-Identifier: GPL-3.0-or-later
// DatasheetModel: the totals row (exact for tables — SQL sums over integer
// storage — and in memory for query results) and export rows.
#include "base_document.h"
#include "datasheet_model.h"

#include <cmath>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

void DatasheetModel::setTotalsVisible(bool visible)
{
    if (m_totalsVisible == visible) {
        return;
    }
    m_totalsVisible = visible;
    if (visible && m_totalFunctions.isEmpty()) {
        // Sensible first totals: a count on the first shown text field, sums
        // on money and plain numbers (not keys or lookups).
        QString countColumn;
        for (int i = 0; i < m_columns.size(); ++i) {
            const Engine::ColumnInfo &c = m_columns.at(i);
            const bool lookup = !m_lookups.value(i).isEmpty();
            if (countColumn.isEmpty() && c.type == Engine::FieldType::Text && !lookup) {
                countColumn = c.name;
            } else if ((c.type == Engine::FieldType::Currency || c.type == Engine::FieldType::Number) && !lookup) {
                m_totalFunctions.insert(c.name, QStringLiteral("sum"));
            }
        }
        if (countColumn.isEmpty() && !m_columns.isEmpty()) {
            countColumn = m_columns.first().name;
        }
        if (!countColumn.isEmpty()) {
            m_totalFunctions.insert(countColumn, QStringLiteral("count"));
        }
    }
    recomputeTotals();
}

void DatasheetModel::setTotal(const QString &field, const QString &function)
{
    if (function.isEmpty()) {
        m_totalFunctions.remove(field);
    } else {
        m_totalFunctions.insert(field, function);
    }
    recomputeTotals();
}

void DatasheetModel::recomputeTotals()
{
    m_totals.clear();
    Engine::BaseFile *f = file();
    if (!m_totalsVisible || f == nullptr) {
        emit totalsChanged();
        return;
    }
    const Engine::CurrencyFormat currency = f->currency();
    for (auto it = m_totalFunctions.cbegin(); it != m_totalFunctions.cend(); ++it) {
        const int column = columnIndex(it.key());
        if (column < 0) {
            continue;
        }
        const QString fn = it.value().toString();
        QString text;
        if (m_kind == QLatin1String("table")) {
            QString error;
            (void)Engine::Records::total(*f, m_name, it.key(), fn, m_filters, &text, &error, m_matchAny, m_condition);
        } else {
            // Query results total in memory: integers (money) exactly.
            qint64 isum = 0;
            double dsum = 0;
            int count = 0;
            bool integral = true;
            QVariant lo;
            QVariant hi;
            for (const QVariantList &row : std::as_const(m_values)) {
                const QVariant v = row.value(column);
                if (!v.isValid()) {
                    continue;
                }
                ++count;
                integral = integral && v.metaType().id() == QMetaType::LongLong;
                isum += v.toLongLong();
                dsum += v.toDouble();
                if (!lo.isValid() || QVariant::compare(v, lo) == QPartialOrdering::Less) {
                    lo = v;
                }
                if (!hi.isValid() || QVariant::compare(v, hi) == QPartialOrdering::Greater) {
                    hi = v;
                }
            }
            const Engine::FieldType type = m_columns.at(column).type;
            if (fn == QLatin1String("count")) {
                text = QString::number(count);
            } else if (fn == QLatin1String("sum") && count > 0) {
                text = Engine::displayText(type, integral ? QVariant::fromValue(isum) : QVariant(dsum), currency);
            } else if (fn == QLatin1String("average") && count > 0) {
                const double avg = (integral ? double(isum) : dsum) / count;
                text = type == Engine::FieldType::Currency
                           ? Engine::displayText(type, QVariant::fromValue(qint64(std::llround(avg))), currency)
                           : Engine::formatNumber(std::round(avg * 1e6) / 1e6);
            } else if (fn == QLatin1String("min") && count > 0) {
                text = Engine::displayText(type, lo, currency);
            } else if (fn == QLatin1String("max") && count > 0) {
                text = Engine::displayText(type, hi, currency);
            }
        }
        m_totals.insert(it.key(), QVariantMap{{QStringLiteral("function"), fn}, {QStringLiteral("text"), text}});
    }
    emit totalsChanged();
}

QStringList DatasheetModel::exportHeader() const
{
    QStringList out;
    for (const Engine::ColumnInfo &c : m_columns) {
        out.append(c.name);
    }
    return out;
}

QList<QStringList> DatasheetModel::exportRows() const
{
    QList<QStringList> out;
    for (int r = 0; r < m_values.size(); ++r) {
        QStringList row;
        for (int c = 0; c < m_columns.size(); ++c) {
            row.append(edit(r, c));
        }
        out.append(row);
    }
    return out;
}

} // namespace QindaOffice::Apps::Base
