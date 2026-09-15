// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Number-format compiler and formatter (plan section 7.2): Excel-style
// format codes with up to four sections (pos;neg;zero;text), digit tokens
// (0 # ?), decimal/thousands separators, percent, scientific, literals,
// escapes, colour names, and date/time patterns. Dates use the 1900 serial
// system (serial 0 = 1899-12-30).

#include "value.h"

namespace QindaOffice::Calc {

struct FormattedText {
    QString text;
    std::optional<Rgb> colour; // set when the section carries a colour token
};

class NumberFormat {
public:
    NumberFormat();
    explicit NumberFormat(const QString &code);

    [[nodiscard]] bool isValid() const { return m_valid; }
    [[nodiscard]] QString code() const { return m_code; }
    // General form: up to 11 significant characters, scientific beyond.
    [[nodiscard]] bool isGeneral() const;

    [[nodiscard]] FormattedText format(const Value &value) const;
    [[nodiscard]] FormattedText formatNumber(double number) const;
    [[nodiscard]] FormattedText formatText(const QString &text) const;

    // Built-in xlsx numFmtId 0-49 table lookup (plan section 7.2).
    [[nodiscard]] static QString builtinCode(int numFmtId);

private:
    struct Section {
        QString raw;
        bool isText = false;      // contains '@'
        bool isDate = false;      // contains date/time tokens
        bool hasThousands = false;
        int decimals = -1;        // -1 = integer (no '.'); else digits after '.'
        int integerDigits = 0;    // count of 0/#/? before the decimal point
        bool percent = false;
        bool scientific = false;
        QString colourToken;
        QString prefix;
        QString suffix;
        QString datePattern; // normalized pattern for date sections
    };
    void compile(const QString &code);

    QString m_code;
    bool m_valid = false;
    QVector<Section> m_sections; // 1..4
};

// Convenience used by TEXT() and the view.
[[nodiscard]] FormattedText formatWithCode(const Value &value, const QString &code);

} // namespace QindaOffice::Calc
