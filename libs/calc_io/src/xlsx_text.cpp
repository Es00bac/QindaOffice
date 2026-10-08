// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_text.h"

namespace QindaOffice::Calc::IO {

namespace {

bool isHex(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
        || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

// True when `text` holds an escape "_xHHHH_" at `at`.
bool escapeAt(const QString &text, qsizetype at)
{
    if (at + 7 > text.size() || text.at(at) != QLatin1Char('_') || text.at(at + 1) != QLatin1Char('x')
        || text.at(at + 6) != QLatin1Char('_')) {
        return false;
    }
    for (qsizetype i = at + 2; i < at + 6; ++i) {
        if (!isHex(text.at(i))) {
            return false;
        }
    }
    return true;
}

// Characters XML 1.0 cannot carry (tab, line feed are fine; a carriage
// return survives XML only as a reference, so Excel escapes it too).
bool needsEscape(QChar c)
{
    const ushort u = c.unicode();
    return (u < 0x20 && u != 0x09 && u != 0x0A) || u == 0xFFFE || u == 0xFFFF;
}

} // namespace

QString decodeXString(const QString &text)
{
    if (!text.contains(QLatin1String("_x"))) {
        return text;
    }
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (escapeAt(text, i)) {
            out += QChar(char16_t(text.mid(i + 2, 4).toUShort(nullptr, 16)));
            i += 6;
        } else {
            out += text.at(i);
        }
    }
    return out;
}

QString encodeXString(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (needsEscape(c)) {
            out += QStringLiteral("_x") + QString::number(c.unicode(), 16).rightJustified(4, QLatin1Char('0')).toUpper()
                + QLatin1Char('_');
        } else if (escapeAt(text, i)) {
            out += QStringLiteral("_x005F_"); // a literal escape: its underscore escaped
        } else {
            out += c;
        }
    }
    return out;
}

Err errorFromFile(const QString &text)
{
    const QString spelled = text.trimmed();
    for (const Err err : {Err::Null, Err::Div0, Err::Value, Err::Ref, Err::Name, Err::Num, Err::NA}) {
        if (spelled.compare(errText(err), Qt::CaseInsensitive) == 0) {
            return err;
        }
    }
    return Err::Value;
}

QString errorForFile(Err err)
{
    return err == Err::Circular ? QString() : errText(err);
}

} // namespace QindaOffice::Calc::IO
