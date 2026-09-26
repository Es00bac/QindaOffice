// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_types.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace QindaOffice::Base {

namespace {

struct TypeRow {
    FieldType type;
    const char *id;
    const char *label;
    const char *declared;
};

constexpr TypeRow kTypes[] = {
    {FieldType::AutoNumber, "autoNumber", "AutoNumber", "INTEGER"},
    {FieldType::Text, "text", "Short text", "TEXT"},
    {FieldType::LongText, "longText", "Long text", "LONGTEXT"},
    {FieldType::Integer, "integer", "Whole number", "INTEGER"},
    {FieldType::Number, "number", "Decimal number", "REAL"},
    {FieldType::Currency, "currency", "Currency", "CURRENCY"},
    {FieldType::YesNo, "yesNo", "Yes/No", "BOOLEAN"},
    {FieldType::Date, "date", "Date", "DATE"},
    {FieldType::DateTime, "dateTime", "Date & time", "DATETIME"},
    {FieldType::Other, "other", "Other (read-only)", "BLOB"},
};

const TypeRow &row(FieldType type)
{
    for (const TypeRow &r : kTypes) {
        if (r.type == type) {
            return r;
        }
    }
    return kTypes[1];
}

Parsed fail(const QString &message)
{
    return Parsed{false, {}, message};
}

Parsed value(const QVariant &v)
{
    return Parsed{true, v, {}};
}

QString stripGrouping(QString text)
{
    text.remove(QLatin1Char(','));
    text.remove(QLatin1Char(' '));
    text.remove(QChar(0x00A0));
    text.remove(QChar(0x202F));
    return text;
}

std::optional<QDate> parseDate(const QString &raw)
{
    const QString text = raw.trimmed();
    static const QRegularExpression iso(QStringLiteral("^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})$"));
    static const QRegularExpression us(QStringLiteral("^(\\d{1,2})[/.-](\\d{1,2})[/.-](\\d{2}|\\d{4})$"));
    QRegularExpressionMatch m = iso.match(text);
    if (m.hasMatch()) {
        const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        return d.isValid() ? std::optional<QDate>(d) : std::nullopt;
    }
    m = us.match(text);
    if (m.hasMatch()) {
        int year = m.captured(3).toInt();
        if (m.captured(3).size() == 2) {
            year += year < 70 ? 2000 : 1900;
        }
        int month = m.captured(1).toInt();
        int day = m.captured(2).toInt();
        if (month > 12 && day <= 12) {
            std::swap(month, day); // an unambiguous day/month/year date
        }
        const QDate d(year, month, day);
        return d.isValid() ? std::optional<QDate>(d) : std::nullopt;
    }
    const QLocale c = QLocale::c();
    for (const char *format : {"MMM d, yyyy", "MMMM d, yyyy", "d MMM yyyy", "d MMMM yyyy", "MMM d yyyy"}) {
        const QDate d = c.toDate(text, QString::fromLatin1(format));
        if (d.isValid()) {
            return d;
        }
    }
    return std::nullopt;
}

std::optional<QDateTime> parseDateTime(const QString &raw)
{
    QString text = raw.trimmed();
    text.replace(QLatin1Char('T'), QLatin1Char(' '));
    static const QRegularExpression split(
        QStringLiteral("^(\\S+)(?:\\s+(\\d{1,2}):(\\d{2})(?::(\\d{2}))?\\s*([AaPp][Mm])?)?$"));
    const QRegularExpressionMatch m = split.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const std::optional<QDate> date = parseDate(m.captured(1));
    if (!date) {
        return std::nullopt;
    }
    int hour = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
    const int minute = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
    const int second = m.captured(4).isEmpty() ? 0 : m.captured(4).toInt();
    const QString ampm = m.captured(5).toLower();
    if (!ampm.isEmpty()) {
        if (hour < 1 || hour > 12) {
            return std::nullopt;
        }
        hour = hour % 12 + (ampm == QStringLiteral("pm") ? 12 : 0);
    }
    const QTime time(hour, minute, second);
    if (!time.isValid()) {
        return std::nullopt;
    }
    return QDateTime(*date, time);
}

} // namespace

QString typeId(FieldType type)
{
    return QString::fromLatin1(row(type).id);
}

std::optional<FieldType> typeFromId(const QString &id)
{
    for (const TypeRow &r : kTypes) {
        if (id == QLatin1String(r.id)) {
            return r.type;
        }
    }
    return std::nullopt;
}

QString typeLabel(FieldType type)
{
    return QString::fromLatin1(row(type).label);
}

QList<FieldType> designableTypes()
{
    return {FieldType::AutoNumber, FieldType::Text,     FieldType::LongText, FieldType::Integer, FieldType::Number,
            FieldType::Currency,   FieldType::YesNo,    FieldType::Date,     FieldType::DateTime};
}

QString declaredType(FieldType type)
{
    return QString::fromLatin1(row(type).declared);
}

FieldType typeFromDeclared(const QString &declared, bool rowidPrimaryKey)
{
    const QString t = declared.trimmed().toUpper();
    if (t.contains(QLatin1String("BOOL")) || t == QLatin1String("BIT") || t == QLatin1String("YESNO")) {
        return FieldType::YesNo;
    }
    if (t.contains(QLatin1String("CURRENCY")) || t.contains(QLatin1String("MONEY"))) {
        return FieldType::Currency;
    }
    if (t.contains(QLatin1String("INT"))) {
        return rowidPrimaryKey ? FieldType::AutoNumber : FieldType::Integer;
    }
    if (t.contains(QLatin1String("DATETIME")) || t.contains(QLatin1String("TIMESTAMP"))) {
        return FieldType::DateTime;
    }
    if (t == QLatin1String("DATE")) {
        return FieldType::Date;
    }
    if (t.contains(QLatin1String("LONGTEXT")) || t.contains(QLatin1String("CLOB")) || t.contains(QLatin1String("MEMO"))) {
        return FieldType::LongText;
    }
    if (t.contains(QLatin1String("CHAR")) || t.contains(QLatin1String("TEXT")) || t.isEmpty()) {
        return FieldType::Text;
    }
    if (t.contains(QLatin1String("BLOB"))) {
        return FieldType::Other;
    }
    if (t.contains(QLatin1String("DATE")) || t.contains(QLatin1String("TIME"))) {
        return FieldType::DateTime;
    }
    // REAL / FLOAT / DOUBLE / NUMERIC / DECIMAL(10,2) …
    return FieldType::Number;
}

bool isNumeric(FieldType type)
{
    return type == FieldType::AutoNumber || type == FieldType::Integer || type == FieldType::Number
           || type == FieldType::Currency;
}

std::optional<qint64> parseMoney(const QString &raw, int decimals, QString *error)
{
    QString text = raw.trimmed();
    bool negative = false;
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        negative = true;
        text = text.mid(1, text.size() - 2).trimmed();
    }
    // Drop any currency symbol or code around the digits.
    static const QRegularExpression symbols(QStringLiteral("[^0-9.,\\-+\\s]"));
    text.remove(symbols);
    text = stripGrouping(text);
    if (text.startsWith(QLatin1Char('-'))) {
        negative = !negative;
        text.remove(0, 1);
    } else if (text.startsWith(QLatin1Char('+'))) {
        text.remove(0, 1);
    }
    static const QRegularExpression shape(QStringLiteral("^(\\d*)(?:\\.(\\d*))?$"));
    const QRegularExpressionMatch m = shape.match(text);
    if (text.isEmpty() || !m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty())) {
        if (error != nullptr) {
            *error = QStringLiteral("needs an amount");
        }
        return std::nullopt;
    }
    const QString whole = m.captured(1);
    QString fraction = m.captured(2);
    while (fraction.size() > decimals && fraction.endsWith(QLatin1Char('0'))) {
        fraction.chop(1);
    }
    if (fraction.size() > decimals) {
        if (error != nullptr) {
            *error = QStringLiteral("takes at most %1 decimal places").arg(decimals);
        }
        return std::nullopt;
    }
    fraction = fraction.leftJustified(decimals, QLatin1Char('0'));
    const QString digits = (whole.isEmpty() ? QStringLiteral("0") : whole) + fraction;
    if (digits.size() > 18) {
        if (error != nullptr) {
            *error = QStringLiteral("is too large");
        }
        return std::nullopt;
    }
    bool ok = false;
    const qint64 minor = digits.toLongLong(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = QStringLiteral("needs an amount");
        }
        return std::nullopt;
    }
    return negative ? -minor : minor;
}

QString formatMoney(qint64 minor, int decimals, const QString &symbol, bool grouping)
{
    const bool negative = minor < 0;
    // AGENT-GUARD: unsigned magnitude so INT64_MIN does not overflow.
    const quint64 magnitude = negative ? quint64(0) - quint64(minor) : quint64(minor);
    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    QString whole = QString::number(magnitude / scale);
    if (grouping) {
        for (int i = int(whole.size()) - 3; i > 0; i -= 3) {
            whole.insert(i, QLatin1Char(','));
        }
    }
    QString out = symbol + whole;
    if (decimals > 0) {
        out += QLatin1Char('.') + QString::number(magnitude % scale).rightJustified(decimals, QLatin1Char('0'));
    }
    return negative ? QLatin1Char('-') + out : out;
}

QString formatNumber(double value)
{
    if (std::isfinite(value) && std::floor(value) == value && std::fabs(value) < 1e15) {
        return QString::number(qint64(value));
    }
    return QString::number(value, 'g', 15);
}

Parsed parseInput(FieldType type, const QString &raw, const CurrencyFormat &currency)
{
    const QString text = raw.trimmed();
    if (text.isEmpty()) {
        return value({});
    }
    switch (type) {
    case FieldType::Text:
        if (raw.size() > kShortTextLimit) {
            return fail(QStringLiteral("holds at most %1 characters (use Long text)").arg(kShortTextLimit));
        }
        return value(raw);
    case FieldType::LongText:
        return value(raw);
    case FieldType::AutoNumber:
    case FieldType::Integer: {
        bool ok = false;
        const qint64 n = stripGrouping(text).toLongLong(&ok);
        return ok ? value(QVariant::fromValue(n)) : fail(QStringLiteral("needs a whole number"));
    }
    case FieldType::Number: {
        bool ok = false;
        const double d = QLocale::c().toDouble(stripGrouping(text), &ok);
        return ok && std::isfinite(d) ? value(d) : fail(QStringLiteral("needs a number"));
    }
    case FieldType::Currency: {
        QString error;
        const std::optional<qint64> minor = parseMoney(text, currency.decimals, &error);
        return minor ? value(QVariant::fromValue(*minor)) : fail(error);
    }
    case FieldType::YesNo: {
        const QString t = text.toLower();
        if (t == QLatin1String("yes") || t == QLatin1String("true") || t == QLatin1String("1") || t == QLatin1String("y")
            || t == QLatin1String("on") || t == QLatin1String("x") || t == QLatin1String("checked")) {
            return value(QVariant::fromValue<qint64>(1));
        }
        if (t == QLatin1String("no") || t == QLatin1String("false") || t == QLatin1String("0") || t == QLatin1String("n")
            || t == QLatin1String("off") || t == QLatin1String("unchecked")) {
            return value(QVariant::fromValue<qint64>(0));
        }
        return fail(QStringLiteral("needs Yes or No"));
    }
    case FieldType::Date: {
        const std::optional<QDate> d = parseDate(text);
        if (d) {
            return value(d->toString(Qt::ISODate));
        }
        const std::optional<QDateTime> dt = parseDateTime(text);
        return dt ? value(dt->date().toString(Qt::ISODate)) : fail(QStringLiteral("needs a date (yyyy-mm-dd)"));
    }
    case FieldType::DateTime: {
        const std::optional<QDateTime> dt = parseDateTime(text);
        return dt ? value(dt->toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                  : fail(QStringLiteral("needs a date and time (yyyy-mm-dd hh:mm)"));
    }
    case FieldType::Other:
        return fail(QStringLiteral("cannot be edited here"));
    }
    return fail(QStringLiteral("has an unknown type"));
}

Parsed parseValue(FieldType type, const QVariant &input, const CurrencyFormat &currency)
{
    if (!input.isValid() || input.isNull()) {
        return value({});
    }
    const int id = input.metaType().id();
    if (id == QMetaType::Bool) {
        if (type == FieldType::YesNo) {
            return value(QVariant::fromValue<qint64>(input.toBool() ? 1 : 0));
        }
        return parseInput(type, input.toBool() ? QStringLiteral("1") : QStringLiteral("0"), currency);
    }
    if (id == QMetaType::Double || id == QMetaType::Float) {
        const double d = input.toDouble();
        if (!std::isfinite(d)) {
            return fail(QStringLiteral("needs a number"));
        }
        if (type == FieldType::Number) {
            return value(d);
        }
        if (type == FieldType::Currency) {
            const double scaled = d * std::pow(10.0, currency.decimals);
            const double rounded = std::round(scaled);
            if (std::fabs(scaled - rounded) > 1e-6 || std::fabs(rounded) > 9e17) {
                return fail(QStringLiteral("takes at most %1 decimal places").arg(currency.decimals));
            }
            return value(QVariant::fromValue(qint64(rounded)));
        }
        if (type == FieldType::Integer || type == FieldType::AutoNumber || type == FieldType::YesNo) {
            if (std::floor(d) != d || std::fabs(d) > 9e18) {
                return fail(QStringLiteral("needs a whole number"));
            }
            return type == FieldType::YesNo ? parseInput(type, QString::number(qint64(d)), currency)
                                            : value(QVariant::fromValue(qint64(d)));
        }
        return parseInput(type, formatNumber(d), currency);
    }
    if (id == QMetaType::Int || id == QMetaType::LongLong || id == QMetaType::UInt || id == QMetaType::ULongLong) {
        const qint64 n = input.toLongLong();
        if (type == FieldType::Currency) {
            return parseInput(type, QString::number(n), currency);
        }
        if (type == FieldType::Number) {
            return value(double(n));
        }
        if (type == FieldType::Integer || type == FieldType::AutoNumber) {
            return value(QVariant::fromValue(n));
        }
        return parseInput(type, QString::number(n), currency);
    }
    return parseInput(type, input.toString(), currency);
}

QString displayText(FieldType type, const QVariant &stored, const CurrencyFormat &currency)
{
    if (!stored.isValid() || stored.isNull()) {
        return {};
    }
    const int id = stored.metaType().id();
    const bool integral = id == QMetaType::LongLong || id == QMetaType::Int;
    switch (type) {
    case FieldType::Currency:
        if (integral) {
            return formatMoney(stored.toLongLong(), currency.decimals, currency.symbol);
        }
        if (id == QMetaType::Double) {
            // A REAL in a currency column (a foreign file): show it as units.
            return currency.symbol + QString::number(stored.toDouble(), 'f', currency.decimals);
        }
        return stored.toString();
    case FieldType::YesNo:
        if (integral || id == QMetaType::Double) {
            return stored.toDouble() != 0 ? QStringLiteral("Yes") : QStringLiteral("No");
        }
        return stored.toString();
    case FieldType::Number:
        return id == QMetaType::Double ? formatNumber(stored.toDouble()) : stored.toString();
    case FieldType::Other:
        if (id == QMetaType::QByteArray) {
            return QStringLiteral("(binary, %1 bytes)").arg(stored.toByteArray().size());
        }
        return stored.toString();
    default:
        if (id == QMetaType::Double) {
            return formatNumber(stored.toDouble());
        }
        if (id == QMetaType::QByteArray) {
            return QStringLiteral("(binary, %1 bytes)").arg(stored.toByteArray().size());
        }
        return stored.toString();
    }
}

QString editText(FieldType type, const QVariant &stored, const CurrencyFormat &currency)
{
    if (type == FieldType::Currency && stored.isValid() && !stored.isNull()
        && (stored.metaType().id() == QMetaType::LongLong || stored.metaType().id() == QMetaType::Int)) {
        return formatMoney(stored.toLongLong(), currency.decimals, QString(), false);
    }
    return displayText(type, stored, currency);
}

} // namespace QindaOffice::Base
