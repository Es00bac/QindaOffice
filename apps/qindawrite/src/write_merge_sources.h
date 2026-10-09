// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Where a mail merge's recipients come from (M77, D-086), as one table
// (Docx::MergeData): a delimited text file (office_docx), a workbook sheet
// (through calc_io, each cell's displayed text: a ZIP code keeps its
// leading zero when its number format shows it), or QindaMail's People.
//
// AGENT-CONTRACT (libs/office_mail contact.h toJson ⇄ peopleGroups /
// people below): People are read-only from QindaMail's contacts store,
// $XDG_DATA_HOME/qindaoffice/mail/contacts.json (schema
// qindaoffice.mail.contacts/1: {schema, contacts: [toJson(contact)]}); a
// group is one of QindaMail's contact groups (M76: `kind` "group", members
// "urn:uuid:<uid>" cards or "mailto:" addresses) or a contact category.
// QindaWrite never writes the store and does not link office_mail (its
// keyring and network stack are not Write's).

#include "docx_merge_data.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

namespace QindaOffice::Apps::Write::MergeSources {

// .csv / .txt / .tsv / .prn as delimited text; .xlsx / .xlsm through
// calc_io (`sheet`, or the first sheet holding data).
[[nodiscard]] Docx::MergeData read(const QString &path, const QString &sheet, QString *error);
// A workbook's sheets that hold data, in order.
[[nodiscard]] QStringList sheets(const QString &path);
[[nodiscard]] bool isWorkbook(const QString &path);

// QindaMail's People: the store's path, its groups with their sizes (""
// first: everyone but the groups; then the contact groups, then the
// categories), and a group's people as Word's address columns.
[[nodiscard]] QString peopleStorePath();
[[nodiscard]] QList<std::pair<QString, int>> peopleGroups(QString *error);
[[nodiscard]] Docx::MergeData people(const QString &group, QString *error);

// Type a New List's columns (Word's).
[[nodiscard]] QStringList newListFields();

} // namespace QindaOffice::Apps::Write::MergeSources
