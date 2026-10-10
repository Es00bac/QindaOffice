// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Access's XML import (M86, D-094): the reverse of M80's export
// (base_xml_export.h) — Access's ExportXML data file (<dataroot> with one
// element per record) and its schema, as Access's External Data ▸ XML File
// writes them and QindaBase's own export writes them.
//
// AGENT-CONTRACT (with BaseService::importXml and tst_base_xml_links):
//  * The schema is the .xsd the data names (xsi:noNamespaceSchemaLocation,
//    beside the data file), one embedded in the data file (Access's "embed
//    the schema" root <root><xsd:schema/><dataroot/></root>), or none — the
//    types then come from the text, as for CSV.
//  * Types: Access's od:jetType (autonumber, text, memo, byte, integer,
//    longinteger, bigint, single, double, decimal, currency, yesno,
//    datetime, oleobject, hyperlink, replicationid), required fields
//    (minOccurs="1" or od:nonNullable), the primary key and indexes
//    (od:index). QindaBase's own qb:type and qb:expression (M86 export)
//    win over the jet type, so a QindaBase export comes back exactly (Date
//    apart from Date & time, calculated fields as calculated fields).
//  * Values: xsd:dateTime → a date (all at midnight, or qb:type date) or a
//    date and time; yesno 1/0/true/false; base64 → a file field's bytes;
//    names unescaped from Access's _xHHHH_.
//  * The import runs through the Access import (base_access_import.h): the
//    same design rules, the same report of what came across, every change
//    returned for ONE undo step.

#include "base_access_import.h"

namespace QindaOffice::Base {

struct XmlTable {
    QString name;
    QStringList fields;
    qint64 records = 0;
};

struct AccessXmlDocument {
    bool ok = false;
    QString error;
    QString schemaSource; // "" (none), "embedded" or the .xsd's file name
    QList<XmlTable> tables;
    AccessSchema schema;        // tables with their columns, keys and indexes
    QHash<QString, CsvTable> data; // by table name, lower case: canonical text
    QHash<QString, QHash<QString, QString>> expressions; // table → field → qb:expression (lower case keys)
};

// Reads the data file at `path` (and its schema). `data`: the file's bytes,
// when the caller has them (tests); otherwise the file is read.
[[nodiscard]] AccessXmlDocument readAccessXml(const QString &path, const QByteArray &data = {});

// Imports `tables` (all when empty) of a read document; calculated fields
// come back as calculated fields. `changes` receives every step.
AccessImportReport importAccessXml(BaseFile &file, const AccessXmlDocument &document, const QStringList &tables,
                                   QList<Change> *changes);

} // namespace QindaOffice::Base
