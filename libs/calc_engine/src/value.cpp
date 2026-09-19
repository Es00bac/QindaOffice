// SPDX-License-Identifier: GPL-3.0-or-later
#include "value.h"

namespace QindaOffice::Calc {

QString errText(Err err)
{
    switch (err) {
    case Err::Null: return QStringLiteral("#NULL!");
    case Err::Div0: return QStringLiteral("#DIV/0!");
    case Err::Value: return QStringLiteral("#VALUE!");
    case Err::Ref: return QStringLiteral("#REF!");
    case Err::Name: return QStringLiteral("#NAME?");
    case Err::Num: return QStringLiteral("#NUM!");
    case Err::NA: return QStringLiteral("#N/A");
    case Err::Circular: return QStringLiteral("Err:522");
    }
    return QStringLiteral("#N/A");
}

Err errFromExport(const QString &text)
{
    if (text == QLatin1String("#NULL!")) return Err::Null;
    if (text == QLatin1String("#DIV/0!")) return Err::Div0;
    if (text == QLatin1String("#VALUE!")) return Err::Value;
    if (text == QLatin1String("#REF!")) return Err::Ref;
    if (text == QLatin1String("#NAME?")) return Err::Name;
    if (text == QLatin1String("#NUM!")) return Err::Num;
    if (text == QLatin1String("#N/A")) return Err::NA;
    return Err::NA;
}

bool isBlank(const Value &v) { return std::holds_alternative<std::monostate>(v); }
bool isErr(const Value &v) { return std::holds_alternative<Err>(v); }
bool isNumber(const Value &v) { return std::holds_alternative<double>(v); }
bool isText(const Value &v) { return std::holds_alternative<QString>(v); }
bool isBool(const Value &v) { return std::holds_alternative<bool>(v); }

std::optional<double> toNumber(const Value &v)
{
    if (const auto *d = std::get_if<double>(&v)) {
        return *d;
    }
    if (const auto *b = std::get_if<bool>(&v)) {
        return *b ? 1.0 : 0.0;
    }
    if (std::holds_alternative<std::monostate>(v)) {
        return 0.0;
    }
    if (const auto *s = std::get_if<QString>(&v)) {
        bool ok = false;
        const double d = s->toDouble(&ok);
        if (ok) {
            return d;
        }
        // Percent and grouped forms ("12%", "1,234") parse leniently.
        QString t = *s;
        t.remove(QLatin1Char('%'));
        t.remove(QLatin1Char(','));
        t.remove(QLatin1Char(' '));
        const double p = t.toDouble(&ok);
        if (ok && s->contains(QLatin1Char('%'))) {
            return p / 100.0;
        }
        if (ok) {
            return p;
        }
    }
    return std::nullopt;
}

std::optional<bool> toBool(const Value &v)
{
    if (const auto *b = std::get_if<bool>(&v)) {
        return *b;
    }
    if (const auto *d = std::get_if<double>(&v)) {
        return *d != 0.0;
    }
    if (std::holds_alternative<std::monostate>(v)) {
        return false;
    }
    if (const auto *s = std::get_if<QString>(&v)) {
        if (s->compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0) return true;
        if (s->compare(QLatin1String("FALSE"), Qt::CaseInsensitive) == 0) return false;
    }
    return std::nullopt;
}

QString toText(const Value &v)
{
    if (const auto *d = std::get_if<double>(&v)) {
        // General-form number text: up to 11 significant characters,
        // scientific beyond (plan section 7.2). Values without a fractional
        // part print as integers.
        if (qFuzzyIsNull(*d - std::round(*d)) && std::abs(*d) < 1e15) {
            return QString::number(qlonglong(std::llround(*d)));
        }
        return QString::number(*d, 'g', 11);
    }
    if (const auto *s = std::get_if<QString>(&v)) {
        return *s;
    }
    if (const auto *b = std::get_if<bool>(&v)) {
        return *b ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    }
    if (const auto *e = std::get_if<Err>(&v)) {
        return errText(*e);
    }
    return QString();
}

int compareValues(const Value &a, const Value &b)
{
    const auto rank = [](const Value &v) {
        if (std::holds_alternative<double>(v)) return 0;
        if (std::holds_alternative<QString>(v)) return 1;
        if (std::holds_alternative<bool>(v)) return 2;
        return -1; // blank sorts before numbers
    };
    const int ra = rank(a);
    const int rb = rank(b);
    if (ra != rb) {
        return ra < rb ? -1 : 1;
    }
    if (std::holds_alternative<double>(a)) {
        const double x = std::get<double>(a);
        const double y = std::get<double>(b);
        return (x < y) ? -1 : (x > y ? 1 : 0);
    }
    if (std::holds_alternative<QString>(a)) {
        return QString::compare(std::get<QString>(a), std::get<QString>(b));
    }
    if (std::holds_alternative<bool>(a)) {
        const bool x = std::get<bool>(a);
        const bool y = std::get<bool>(b);
        return (x == y) ? 0 : (x ? 1 : -1);
    }
    return 0;
}

QString colName(qint32 col)
{
    QString out;
    qint32 c = col;
    do {
        out.prepend(QLatin1Char(char('A' + (c % 26))));
        c = c / 26 - 1;
    } while (c >= 0);
    return out;
}

std::optional<qint32> colFromName(const QString &name)
{
    qint32 col = 0;
    for (const QChar ch : name) {
        if (ch < QLatin1Char('A') || ch > QLatin1Char('Z')) {
            return std::nullopt;
        }
        col = col * 26 + (ch.unicode() - 'A' + 1);
    }
    return col > 0 ? std::optional<qint32>(col - 1) : std::nullopt;
}

QString addrText(const CellAddr &addr, bool absRow, bool absCol)
{
    QString out = absCol ? QStringLiteral("$") : QString();
    out += colName(addr.col);
    if (absRow) {
        out += QLatin1Char('$');
    }
    out += QString::number(addr.row + 1);
    return out;
}

QString rangeRefText(const RangeRef &ref)
{
    QString out = ref.sheet.isEmpty() ? QString() : ref.sheet + QStringLiteral("!");
    if (ref.wholeCols) {
        return out + colName(ref.range.topLeft.col) + QStringLiteral(":")
               + colName(ref.range.bottomRight.col);
    }
    if (ref.wholeRows) {
        return out + QString::number(ref.range.topLeft.row + 1) + QStringLiteral(":")
               + QString::number(ref.range.bottomRight.row + 1);
    }
    out += addrText(ref.range.topLeft, ref.absRow0, ref.absCol0);
    if (ref.range.topLeft != ref.range.bottomRight) {
        out += QLatin1Char(':');
        out += addrText(ref.range.bottomRight, ref.absRow1, ref.absCol1);
    }
    return out;
}

QString unquotedSheetName(QString name)
{
    if (name.startsWith(QLatin1Char('\'')) && name.endsWith(QLatin1Char('\''))) {
        name = name.mid(1, name.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    return name;
}

} // namespace QindaOffice::Calc
