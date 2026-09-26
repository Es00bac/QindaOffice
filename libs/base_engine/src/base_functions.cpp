// SPDX-License-Identifier: GPL-3.0-or-later
// SQL functions QindaBase registers on its own connection.
//
// AGENT-CONTRACT (plans and queries, D-049 §3): these exist only on the
// engine's connection. A schema plan may call qb_convert; a saved query's
// compiled SQL never does (other SQLite tools must be able to run it).
//   qb_convert(value, fromTypeId, toTypeId) → the value in the new type's
//     storage form through parseInput (so currency stays exact), or the
//     value unchanged when it does not convert (counted by the preview).
#include "base_file.h"

#include <sqlite3.h>

#include <cmath>

namespace QindaOffice::Base {

namespace {

QVariant argument(sqlite3_value *v)
{
    switch (sqlite3_value_type(v)) {
    case SQLITE_INTEGER:
        return QVariant::fromValue<qint64>(sqlite3_value_int64(v));
    case SQLITE_FLOAT:
        return sqlite3_value_double(v);
    case SQLITE_TEXT:
        return QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_value_text(v)), sqlite3_value_bytes(v));
    case SQLITE_BLOB:
        return QByteArray(static_cast<const char *>(sqlite3_value_blob(v)), sqlite3_value_bytes(v));
    default:
        return {};
    }
}

void result(sqlite3_context *context, const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        sqlite3_result_null(context);
        return;
    }
    switch (value.metaType().id()) {
    case QMetaType::LongLong:
    case QMetaType::Int:
        sqlite3_result_int64(context, value.toLongLong());
        return;
    case QMetaType::Double:
        sqlite3_result_double(context, value.toDouble());
        return;
    case QMetaType::QByteArray: {
        const QByteArray bytes = value.toByteArray();
        sqlite3_result_blob64(context, bytes.constData(), sqlite3_uint64(bytes.size()), SQLITE_TRANSIENT);
        return;
    }
    default: {
        const QByteArray utf8 = value.toString().toUtf8();
        sqlite3_result_text64(context, utf8.constData(), sqlite3_uint64(utf8.size()), SQLITE_TRANSIENT, SQLITE_UTF8);
        return;
    }
    }
}

void convertFunction(sqlite3_context *context, int, sqlite3_value **argv)
{
    const auto *file = static_cast<const BaseFile *>(sqlite3_user_data(context));
    const QVariant input = argument(argv[0]);
    const std::optional<FieldType> from =
        typeFromId(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_value_text(argv[1]))));
    const std::optional<FieldType> to =
        typeFromId(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_value_text(argv[2]))));
    if (!input.isValid() || !from || !to || *from == *to) {
        result(context, input);
        return;
    }
    const CurrencyFormat currency = file->currency();
    // Currency leaves as units (exact text), everything else as its edit
    // text; the target type parses it like typed input.
    QString text = editText(*from, input, currency);
    if (*from == FieldType::YesNo && isNumeric(*to)) {
        text = input.toLongLong() != 0 ? QStringLiteral("1") : QStringLiteral("0");
    }
    if (*to == FieldType::YesNo && isNumeric(*from)) {
        text = input.toDouble() != 0 ? QStringLiteral("1") : QStringLiteral("0");
    }
    const int storage = input.metaType().id();
    if ((*to == FieldType::Integer || *to == FieldType::AutoNumber)
        && (*from == FieldType::Currency || *from == FieldType::Number)
        && (storage == QMetaType::LongLong || storage == QMetaType::Double)) {
        // Whole units, rounded half away from zero.
        const double units = *from == FieldType::Currency ? input.toDouble() / std::pow(10.0, currency.decimals)
                                                          : input.toDouble();
        result(context, QVariant::fromValue(qint64(std::llround(units))));
        return;
    }
    if (*to == FieldType::Date && *from == FieldType::DateTime) {
        text = text.left(10);
    }
    const Parsed parsed = parseInput(*to, text, currency);
    result(context, parsed.ok ? parsed.value : input);
}

} // namespace

// Called by BaseFile once its connection is open.
void registerBaseFunctions(BaseFile *file)
{
    sqlite3_create_function_v2(file->db().handle(), "qb_convert", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, file,
                               convertFunction, nullptr, nullptr, nullptr);
}

} // namespace QindaOffice::Base
