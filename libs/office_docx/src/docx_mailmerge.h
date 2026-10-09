// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's mail merge settings (w:mailMerge in settings.xml, ECMA-376 Part 1
// §17.14.20) as QindaWrite models them (M77, D-086).
//
// A main document is a merge document when w:mailMerge names its type
// (letters, e-mail, envelopes, labels, a directory) and its data source:
// for the CSV files QindaWrite writes, w:dataType "textFile", the query
// "SELECT * FROM <path>" (plus Word's WHERE / ORDER BY when the recipient
// list is filtered or sorted) and w:dataSource, an external relationship
// to the file. Word re-opens such a document as a merge document: it first
// asks "Opening this document will run the following SQL command: SELECT *
// FROM … Data from your database will be placed in the document. Do you
// want to continue?", and asks for the data source when the path does not
// exist on that machine.
//
// While the settings are as read, the source's own w:mailMerge is written
// back unchanged (D-068 rule 3); a changed one is rebuilt from the model,
// keeping the source's children the model does not cover (w:odso,
// w:headerSource, w:checkErrors, …). Recipients the user left out (Edit
// Recipient List) are QindaWrite's own: a w:docVar, which Word keeps but
// does not apply.

#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QList>
#include <QMap>
#include <QString>

namespace QindaOffice::Docx {

// One row of Word's Filter Records: `field` `op` `value`, joined to the
// row before by `join` ("and" / "or").
struct MergeFilter {
    QString join = QStringLiteral("and");
    QString field;
    // equal, notEqual, less, greater, lessEqual, greaterEqual, blank,
    // notBlank, contains, notContains
    QString op = QStringLiteral("equal");
    QString value;
    bool operator==(const MergeFilter &) const = default;
};

// One level of Word's Sort Records.
struct MergeSort {
    QString field;
    bool descending = false;
    bool operator==(const MergeSort &) const = default;
};

struct MailMergeSettings {
    // w:mainDocumentType: formLetters, email, envelopes, mailingLabels,
    // catalog (a directory), fax. Empty: an ordinary document.
    QString documentType;
    QString dataType = QStringLiteral("textFile"); // w:dataType
    QString dataSource;  // the data source's local path (w:dataSource target)
    QString table;       // a workbook's sheet ("Sheet1"): the query's table
    QString query;       // w:query as read; rebuilt from the filters and sorts
    QList<MergeFilter> filters;
    QList<MergeSort> sorts;
    QString destination;   // w:destination: newDocument, printer, email
    QString addressField;  // w:addressFieldName (e-mail messages)
    QString mailSubject;   // w:mailSubject
    bool mailAsAttachment = false;
    bool viewMergedData = false; // w:viewMergedData: Preview Results on
    int activeRecord = 0;        // w:activeRecord, 1-based; 0 none
    // Match Fields (w:odso/w:fieldMapData): Word's address field ("First
    // Name", "Address 1", …) → the data source's column.
    QMap<QString, QString> fieldMap;
    // Records left out in Edit Recipient List (0-based, data source order).
    QList<int> excluded;

    [[nodiscard]] bool isMerge() const { return !documentType.isEmpty(); }
    bool operator==(const MailMergeSettings &) const = default;
};

// Word's own name for a merge type, and back: "letters", "email",
// "envelopes", "labels", "directory" ↔ formLetters, email, envelopes,
// mailingLabels, catalog.
[[nodiscard]] QString wordDocumentType(const QString &kind);
[[nodiscard]] QString mergeKind(const QString &wordType);

// The query Word runs for these settings: SELECT * FROM <source> with
// Word's WHERE ((`Field` = 'value') AND …) and ORDER BY `Field` [DESC].
[[nodiscard]] QString mergeQuery(const MailMergeSettings &settings);
// Reads the filters and sorts back from a query of that form; false (and
// nothing changed) for a query it does not recognise.
bool parseMergeQuery(const QString &query, QList<MergeFilter> *filters, QList<MergeSort> *sorts);

// The settings of `settings` (settings.xml's root; `settingsPart` resolves
// its relationships in `package`): w:mailMerge and the excluded records.
[[nodiscard]] MailMergeSettings readMailMerge(const Ooxml::Package &package, const QString &settingsPart,
                                              const Ooxml::XmlElement *settings);

// Rewrites settings.xml's merge state for `merge`: w:mailMerge rebuilt
// (or removed for an ordinary document) keeping the children of `source`
// (the w:mailMerge as read, or null) the model does not cover; the data
// source added as an external relationship of `settingsPart` in `writer`;
// the excluded-records w:docVar set or removed. Call only when `merge`
// differs from what was read.
void writeMailMerge(Ooxml::XmlElement &settings, const MailMergeSettings &merge, const Ooxml::XmlElement *source,
                    Ooxml::PackageWriter &writer, const QString &settingsPart);

// The w:docVar QindaWrite keeps the excluded records in.
inline constexpr char16_t kExcludedDocVar[] = u"QindaWriteMergeExcluded";

} // namespace QindaOffice::Docx
