// SPDX-License-Identifier: GPL-3.0-or-later
// Word's Address Block and Greeting Line: their \f formats, the address
// fields they read (Match Fields, with Word's automatic matches), and the
// text they produce for one recipient.

#include "docx_merge_fields_p.h"

#include <QHash>
#include <QRegularExpression>

namespace QindaOffice::Docx {

namespace {

// Word's address fields and the column names it matches them to.
struct AddressField {
    const char *name;
    QStringList synonyms;
};

const QList<AddressField> &addressTable()
{
    static const QList<AddressField> table{
        {"Courtesy Title", {QStringLiteral("Courtesy Title"), QStringLiteral("Title"), QStringLiteral("Prefix"),
                            QStringLiteral("Salutation")}},
        {"First Name", {QStringLiteral("First Name"), QStringLiteral("FirstName"), QStringLiteral("First"),
                        QStringLiteral("Given Name"), QStringLiteral("Forename")}},
        {"Middle Name", {QStringLiteral("Middle Name"), QStringLiteral("MiddleName"), QStringLiteral("Middle")}},
        {"Last Name", {QStringLiteral("Last Name"), QStringLiteral("LastName"), QStringLiteral("Last"),
                       QStringLiteral("Surname"), QStringLiteral("Family Name")}},
        {"Suffix", {QStringLiteral("Suffix")}},
        {"Nickname", {QStringLiteral("Nickname"), QStringLiteral("Nick Name")}},
        {"Company", {QStringLiteral("Company"), QStringLiteral("Company Name"), QStringLiteral("Organization"),
                     QStringLiteral("Organisation"), QStringLiteral("Business")}},
        {"Address 1", {QStringLiteral("Address 1"), QStringLiteral("Address Line 1"), QStringLiteral("Address1"),
                       QStringLiteral("Street"), QStringLiteral("Street Address"), QStringLiteral("Address")}},
        {"Address 2", {QStringLiteral("Address 2"), QStringLiteral("Address Line 2"), QStringLiteral("Address2")}},
        {"City", {QStringLiteral("City"), QStringLiteral("Town"), QStringLiteral("Locality")}},
        {"State", {QStringLiteral("State"), QStringLiteral("Province"), QStringLiteral("Region"),
                   QStringLiteral("State/Province"), QStringLiteral("County")}},
        {"Postal Code", {QStringLiteral("Postal Code"), QStringLiteral("ZIP Code"), QStringLiteral("ZIP"),
                         QStringLiteral("Zip Code"), QStringLiteral("Postcode"), QStringLiteral("Post Code")}},
        {"Country or Region", {QStringLiteral("Country or Region"), QStringLiteral("Country"),
                               QStringLiteral("Country/Region")}},
        {"E-mail Address", {QStringLiteral("E-mail Address"), QStringLiteral("Email Address"), QStringLiteral("Email"),
                            QStringLiteral("E-mail"), QStringLiteral("Mail")}},
        {"Home Phone", {QStringLiteral("Home Phone"), QStringLiteral("Phone")}},
        {"Business Phone", {QStringLiteral("Business Phone"), QStringLiteral("Work Phone")}},
        {"Spouse Courtesy Title", {QStringLiteral("Spouse Courtesy Title"), QStringLiteral("Spouse Title")}},
        {"Spouse First Name", {QStringLiteral("Spouse First Name"), QStringLiteral("Spouse/Partner First Name")}},
        {"Spouse Last Name", {QStringLiteral("Spouse Last Name"), QStringLiteral("Spouse/Partner Last Name")}},
        {"Spouse Nickname", {QStringLiteral("Spouse Nickname")}},
    };
    return table;
}

QString simplified(QString text)
{
    static const QRegularExpression noise(QStringLiteral("[^\\p{L}\\p{N}]"));
    return text.remove(noise).toCaseFolded();
}

// The address field an \f token stands for.
QString tokenField(const QString &token)
{
    static const QHash<QString, QString> map{
        {QStringLiteral("_TITLE0_"), QStringLiteral("Courtesy Title")},
        {QStringLiteral("_FIRST0_"), QStringLiteral("First Name")},
        {QStringLiteral("_MIDDLE0_"), QStringLiteral("Middle Name")},
        {QStringLiteral("_INITIAL0_"), QStringLiteral("Middle Name")},
        {QStringLiteral("_LAST0_"), QStringLiteral("Last Name")},
        {QStringLiteral("_SUFFIX0_"), QStringLiteral("Suffix")},
        {QStringLiteral("_NICK0_"), QStringLiteral("Nickname")},
        {QStringLiteral("_TITLE1_"), QStringLiteral("Spouse Courtesy Title")},
        {QStringLiteral("_FIRST1_"), QStringLiteral("Spouse First Name")},
        {QStringLiteral("_LAST1_"), QStringLiteral("Spouse Last Name")},
        {QStringLiteral("_NICK1_"), QStringLiteral("Spouse Nickname")},
        {QStringLiteral("_COMPANY_"), QStringLiteral("Company")},
        {QStringLiteral("_STREET1_"), QStringLiteral("Address 1")},
        {QStringLiteral("_STREET2_"), QStringLiteral("Address 2")},
        {QStringLiteral("_CITY_"), QStringLiteral("City")},
        {QStringLiteral("_STATE_"), QStringLiteral("State")},
        {QStringLiteral("_POSTAL_"), QStringLiteral("Postal Code")},
        {QStringLiteral("_COUNTRY_"), QStringLiteral("Country or Region")}};
    return map.value(token);
}

QString addressValue(const QString &token, const MergeCursor &cursor)
{
    if (cursor.data == nullptr || cursor.record() < 0) {
        return {};
    }
    const QString column = matchedColumn(tokenField(token), *cursor.data, cursor.fieldMap);
    QString value = column.isEmpty() ? QString() : cursor.data->value(cursor.record(), column).trimmed();
    if (token == u"_INITIAL0_" && !value.isEmpty()) {
        value = value.left(1).toUpper() + QLatin1Char('.');
    }
    return value;
}

// Fills an \f format: <<…>> groups appear only when a token in them has a
// value (a group without data tokens — _BEFORE_ / _AFTER_ — always does);
// text outside groups is kept. `country` decides the _COUNTRY_ groups.
QString fillFormat(const QString &format, const MergeCursor &cursor, bool countryAllowed, bool *anyName)
{
    static const QRegularExpression token(QStringLiteral("_[A-Z]+[0-9]?_"));
    static const QStringList kNames{QStringLiteral("_TITLE0_"), QStringLiteral("_FIRST0_"), QStringLiteral("_LAST0_"),
                                    QStringLiteral("_NICK0_"), QStringLiteral("_MIDDLE0_"), QStringLiteral("_INITIAL0_"),
                                    QStringLiteral("_SUFFIX0_")};
    QString out;
    qsizetype i = 0;
    while (i < format.size()) {
        const qsizetype open = format.indexOf(QStringLiteral("<<"), i);
        if (open < 0) {
            out += format.mid(i);
            break;
        }
        out += format.mid(i, open - i);
        const qsizetype close = format.indexOf(QStringLiteral(">>"), open + 2);
        if (close < 0) {
            out += format.mid(open);
            break;
        }
        QString group = format.mid(open + 2, close - open - 2);
        i = close + 2;
        // Greeting Line's salutation and punctuation: always there, the
        // salutation followed by a space, the punctuation against the name.
        if (group.contains(QStringLiteral("_BEFORE_"))) {
            const QString text = group.remove(QStringLiteral("_BEFORE_")).trimmed();
            out += text.isEmpty() ? QString() : text + QLatin1Char(' ');
            continue;
        }
        if (group.contains(QStringLiteral("_AFTER_"))) {
            out += group.remove(QStringLiteral("_AFTER_")).trimmed();
            continue;
        }
        bool filled = false;
        bool data = false;
        for (const QRegularExpressionMatch &m : token.globalMatch(group)) {
            const QString name = m.captured(0);
            data = true;
            const QString value = name == u"_COUNTRY_" && !countryAllowed ? QString() : addressValue(name, cursor);
            filled = filled || !value.isEmpty();
            if (kNames.contains(name) && !value.isEmpty() && anyName != nullptr) {
                *anyName = true;
            }
            group.replace(name, value);
        }
        if (!data || filled) {
            out += group;
        }
    }
    return out;
}

} // namespace

QStringList addressFields()
{
    QStringList names;
    for (const AddressField &f : addressTable()) {
        names.append(QString::fromLatin1(f.name));
    }
    return names;
}

QString matchedColumn(const QString &addressField, const MergeData &data, const QMap<QString, QString> &fieldMap)
{
    if (const QString mapped = fieldMap.value(addressField); !mapped.isEmpty()) {
        return data.fieldIndex(mapped) >= 0 ? mapped : QString();
    }
    for (const AddressField &f : addressTable()) {
        if (QLatin1String(f.name) != addressField) {
            continue;
        }
        for (const QString &synonym : f.synonyms) {
            for (const QString &column : data.fields) {
                if (simplified(column) == simplified(synonym)) {
                    return column;
                }
            }
        }
    }
    return {};
}

QList<std::pair<QString, QString>> addressNameFormats()
{
    return {{QStringLiteral("<<_FIRST0_>>"), QStringLiteral("Joshua")},
            {QStringLiteral("<<_FIRST0_>><< _LAST0_>>"), QStringLiteral("Joshua Randall")},
            {QStringLiteral("<<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>"), QStringLiteral("Joshua Randall Jr.")},
            {QStringLiteral("<<_FIRST0_>><< _INITIAL0_>><< _LAST0_>><< _SUFFIX0_>>"), QStringLiteral("Joshua Q. Randall Jr.")},
            {QStringLiteral("<<_TITLE0_ >><<_LAST0_>>"), QStringLiteral("Mr. Randall")},
            {QStringLiteral("<<_TITLE0_ >><<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>"), QStringLiteral("Mr. Joshua Randall Jr.")},
            {QStringLiteral("<<_TITLE0_ >><<_FIRST0_>><< _INITIAL0_>><< _LAST0_>><< _SUFFIX0_>>"),
             QStringLiteral("Mr. Joshua Q. Randall Jr.")},
            {QStringLiteral("<<_NICK0_>><< _LAST0_>>"), QStringLiteral("Josh Randall")},
            {QStringLiteral("<<_TITLE0_ >><<_NICK0_>><< _LAST0_>><< _SUFFIX0_>>"), QStringLiteral("Mr. Josh Randall Jr.")},
            {QStringLiteral("<<_LAST0_>><<, _FIRST0_>>"), QStringLiteral("Randall, Joshua")}};
}

QList<std::pair<QString, QString>> greetingNameFormats()
{
    return {{QStringLiteral("<<_TITLE0_ >><<_LAST0_>>"), QStringLiteral("Mr. Randall")},
            {QStringLiteral("<<_FIRST0_>>"), QStringLiteral("Joshua")},
            {QStringLiteral("<<_NICK0_>>"), QStringLiteral("Josh")},
            {QStringLiteral("<<_FIRST0_>><< _LAST0_>>"), QStringLiteral("Joshua Randall")},
            {QStringLiteral("<<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>"), QStringLiteral("Joshua Randall Jr.")},
            {QStringLiteral("<<_TITLE0_ >><<_FIRST0_>><< _LAST0_>><< _SUFFIX0_>>"), QStringLiteral("Mr. Joshua Randall Jr.")},
            {QStringLiteral("<<_TITLE0_>><< and _TITLE1_>><< _LAST0_>>"), QStringLiteral("Mr. and Mrs. Randall")},
            {QStringLiteral("<<_FIRST0_>><< and _FIRST1_>>"), QStringLiteral("Joshua and Cynthia")}};
}

QString addressBlockCode(const AddressBlockOptions &options)
{
    QString format = options.nameFormat.isEmpty() ? addressNameFormats().at(5).first : options.nameFormat;
    format += QLatin1Char('\n');
    if (options.company) {
        format += QStringLiteral("<<_COMPANY_\n>>");
    }
    if (options.postal) {
        format += QStringLiteral("<<_STREET1_\n>><<_STREET2_\n>><<_CITY_>><<, _STATE_>><< _POSTAL_>>");
        if (options.country != 0) {
            format += QStringLiteral("<<\n_COUNTRY_>>");
        }
    }
    QString country = options.homeCountry;
    QString code = QStringLiteral("ADDRESSBLOCK \\f \"%1\" \\l 1033 \\c %2").arg(format).arg(options.country);
    if (options.country == 2) {
        code += QStringLiteral(" \\e \"%1\"").arg(country.replace(QLatin1Char('"'), QStringLiteral("\\\"")));
    }
    if (options.formatByCountry) {
        code += QStringLiteral(" \\d");
    }
    return code;
}

QString greetingLineCode(const GreetingLineOptions &options)
{
    const QString name = options.nameFormat.isEmpty() ? greetingNameFormats().first().first : options.nameFormat;
    QString format;
    if (!options.salutation.isEmpty()) {
        format += QStringLiteral("<<_BEFORE_ %1 >>").arg(options.salutation);
    }
    format += name;
    if (!options.punctuation.isEmpty()) {
        format += QStringLiteral("<<_AFTER_ %1>>").arg(options.punctuation);
    }
    QString fallback = options.fallback;
    QString code = QStringLiteral("GREETINGLINE \\f \"%1\" \\l 1033").arg(format);
    if (!fallback.isEmpty()) {
        code += QStringLiteral(" \\e \"%1\"").arg(fallback.replace(QLatin1Char('"'), QStringLiteral("\\\"")));
    }
    return code;
}

namespace detail {

QString formatAddressField(const FieldCode &code, const MergeCursor &cursor)
{
    const FieldToken *format = code.switchArgument(u"\\f");
    const bool greeting = code.type == u"GREETINGLINE";
    QString pattern = format != nullptr ? format->text
        : greeting ? QStringLiteral("<<_BEFORE_ Dear >><<_TITLE0_ >><<_LAST0_>><<_AFTER_ ,>>")
                   : addressBlockCode({}).section(QLatin1Char('"'), 1, 1);
    if (greeting) {
        bool anyName = false;
        const QString text = fillFormat(pattern, cursor, false, &anyName).simplified();
        if (!anyName) {
            const FieldToken *fallback = code.switchArgument(u"\\e");
            return fallback != nullptr ? fallback->text : QString();
        }
        // "Dear Mr. Randall ," → "Dear Mr. Randall,": the _AFTER_ group
        // keeps its punctuation against the name.
        static const QRegularExpression spaceBefore(QStringLiteral("\\s+([,:;!.])$"));
        return QString(text).replace(spaceBefore, QStringLiteral("\\1"));
    }
    bool countryAllowed = true;
    const FieldToken *countryRule = code.switchArgument(u"\\c");
    const int rule = countryRule != nullptr ? countryRule->text.toInt() : 2;
    if (rule == 0) {
        countryAllowed = false;
    } else if (rule == 2) {
        const FieldToken *home = code.switchArgument(u"\\e");
        const QString country = addressValue(QStringLiteral("_COUNTRY_"), cursor);
        countryAllowed = home == nullptr || country.compare(home->text, Qt::CaseInsensitive) != 0;
    }
    QStringList lines;
    for (const QString &line : fillFormat(pattern, cursor, countryAllowed, nullptr).split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            lines.append(line.trimmed());
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace detail

} // namespace QindaOffice::Docx
