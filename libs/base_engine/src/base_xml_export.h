// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Access's XML interchange (M80, D-089 §8): a table or a datasheet as the
// pair Access's External Data ▸ XML File writes and imports — a data file
// (<dataroot> with one element per record) and its XML Schema with
// Access's own annotations (od:jetType, od:sqlSType, od:nonNullable,
// od:autoUnique, od:index), so Access's import brings the records back
// with their types, required fields and keys. Writing .accdb itself is not
// feasible (there is no open-source writer); XML, Excel and CSV are the
// way records go to Access.
//
// AGENT-CONTRACT (with base_service_io.cpp and tst_base_macro_report):
//  * Element names are the field names, escaped as Access escapes them:
//    every character an XML name cannot hold becomes _xHHHH_ ("first
//    gift" → first_x0020_gift).
//  * A Null field is left out of its record (minOccurs="0" allows it).
//  * Values: text as it is; whole numbers and AutoNumbers in decimal;
//    decimal numbers in shortest round-trip form; currency in units with
//    the database's decimals, exact from minor units ("25.00"); Yes/No as
//    1/0; dates and date-times as xsd:dateTime ("2026-03-14T00:00:00");
//    file fields as base64 (an OLE Object in Access).
//  * Types: autonumber/int, text(255)/nvarchar, memo/ntext, longinteger/int
//    (double when a value does not fit 32 bits — Access's Long Integer),
//    double/float, currency/money, yesno/bit, datetime/datetime,
//    oleobject/image; M86: hyperlink/ntext (Access's "display#address#"),
//    bigint/bigint (Large Number). The primary key, single-field indexes
//    and (M86) the Indexes dialog's indexes are od:index annotations.
//  * M86 (D-094): every field element also carries QindaBase's own type as
//    qb:type (namespace urn:qindaqt:qindabase, which Access ignores) and a
//    calculated field its expression as qb:expression, so the XML import
//    (base_xml_import.h) gives back the same design — Date apart from Date
//    & time, Long Text apart from Short Text, money, links.

#include "base_records.h"

#include <QByteArray>
#include <QDateTime>

namespace QindaOffice::Base {

struct AccessXml {
    QByteArray data;   // the <dataroot> file
    QByteArray schema; // its .xsd
};

// `name` is the table (or the query whose datasheet is exported);
// `design` (a table's, may be null) supplies the key and indexes;
// `schemaFile` is the .xsd's file name the data file points to.
[[nodiscard]] AccessXml accessXml(const QString &name, const QList<ColumnInfo> &columns,
                                  const QList<QVariantList> &rows, const CurrencyFormat &currency,
                                  const TableDef *design, const QString &schemaFile,
                                  const QDateTime &generated = QDateTime::currentDateTime());
// An XML name for a field or table name (Access's _xHHHH_ escaping), and
// back (M86).
[[nodiscard]] QString xmlName(const QString &name);
[[nodiscard]] QString nameFromXml(const QString &element);
inline constexpr char kQindaBaseXmlNamespace[] = "urn:qindaqt:qindabase";

} // namespace QindaOffice::Base
