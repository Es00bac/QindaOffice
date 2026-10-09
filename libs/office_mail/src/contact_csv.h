// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Contact import from CSV (M64): the Outlook desktop export (the ANSI code
// page, US M/D/YYYY dates, "0/0/00" for none), Outlook.com (same column
// names, UTF-8), Google Contacts (both the "Given Name"/"E-mail 1 - Type"
// and the newer "First Name"/"E-mail 1 - Label" layouts, " ::: " lists,
// "* " marking the primary value) and Thunderbird's column names where
// they are plain aliases.
//
// Parsing is RFC 4180 (quoted fields with delimiters, doubled quotes and
// CR/LF inside; CRLF, LF or CR rows; ragged rows). The delimiter is the most
// frequent of , ; TAB in the header (Outlook uses the Windows list
// separator, ';' in much of Europe). Text: a UTF-8 or UTF-16 BOM is
// honoured; bytes that are not valid UTF-8 read as windows-1252.
// Columns are found by header name (case-insensitive, trimmed).
//
// AGENT-NOTE (columns the mapping does not know): a non-empty value in an
// unknown column is kept as a Contact::extra property named after the
// header (X-SPOUSE, X-MANAGER, X-ASSISTANT for Outlook's Spouse / Manager's
// Name / Assistant's Name, else "X-" + the header in upper case with
// non-alphanumerics as '-': "User 1" → X-USER-1), so nothing typed into
// Outlook is lost and it round-trips through vCard. Outlook's default
// values (Unspecified, Normal, False, 0/0/00) are not kept.
//
// A row with no name, no organization and no e-mail is skipped and
// counted (a blank line is not a row). Bounded like the vCard reader
// (ContactLimits): 64 MiB, 20 000 contacts, 1 MiB per field, 64 values per
// kind.
//
// Threading: pure function, any thread.

#include "contact.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace QindaOffice::Mail::ContactCsv {

struct ReadResult {
    QList<Contact> contacts;
    QStringList warnings;
    int skipped = 0;
};

[[nodiscard]] ReadResult read(const QByteArray &data);

} // namespace QindaOffice::Mail::ContactCsv
