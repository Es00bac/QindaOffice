// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sheet and workbook protection (M66, D-075): Excel's <sheetProtection>
// and <workbookProtection>, and the password check behind them.
//
// AGENT-NOTE: the password hash a file carries is kept VERBATIM — it is
// what Excel checks the owner's password against, and it cannot be
// recomputed without the password. Only a password set in QindaCalc makes
// a new hash, the way Excel 2013+ does (ISO/IEC 29500-1 §18.2.29 with
// MS-OFFCRYPTO's iteration order): H0 = SHA-512(salt ‖ UTF-16LE password),
// Hn+1 = SHA-512(Hn ‖ n as 32-bit little-endian), 100,000 rounds, a fresh
// random 16-byte salt, written base64 as algorithmName/hashValue/
// saltValue/spinCount. Protection here is what Excel's is: a guard against
// accidents, not encryption — the cells are stored in the clear.
//
// The permission flags keep the file's meaning: TRUE means the action is
// LOCKED while the sheet is protected (ECMA-376 names them that way; the
// defaults below are the schema's), so a flag absent from the file reads
// as the schema says and a save writes only what differs.

#include <QByteArray>
#include <QString>

namespace QindaOffice::Calc {

struct SheetProtection {
    bool enabled = false; // <sheetProtection sheet="1">
    // The password check, verbatim from the file (or made by setPassword).
    QString algorithmName;
    QString hashValue;
    QString saltValue;
    qint32 spinCount = 0;
    QString legacyHash; // the pre-2013 16-bit "password" attribute (hex)
    // Locked actions (schema defaults).
    bool objects = false;
    bool scenarios = false;
    bool formatCells = true;
    bool formatColumns = true;
    bool formatRows = true;
    bool insertColumns = true;
    bool insertRows = true;
    bool insertHyperlinks = true;
    bool deleteColumns = true;
    bool deleteRows = true;
    bool selectLockedCells = false;
    bool sort = true;
    bool autoFilter = true;
    bool pivotTables = true;
    bool selectUnlockedCells = false;
    bool operator==(const SheetProtection &) const = default;

    [[nodiscard]] bool hasPassword() const { return !hashValue.isEmpty() || !legacyHash.isEmpty(); }
};

struct WorkbookProtection {
    bool lockStructure = false; // sheets cannot be added, removed, renamed, moved, hidden
    bool lockWindows = false;
    bool lockRevision = false;
    QString algorithmName; // workbookAlgorithmName …
    QString hashValue;
    QString saltValue;
    qint32 spinCount = 0;
    QString legacyHash; // workbookPassword
    // The shared-workbook revision lock travels verbatim.
    QString revisionsAlgorithmName;
    QString revisionsHashValue;
    QString revisionsSaltValue;
    qint32 revisionsSpinCount = 0;
    QString revisionsLegacyHash;
    bool operator==(const WorkbookProtection &) const = default;

    [[nodiscard]] bool isProtected() const { return lockStructure || lockWindows || lockRevision; }
    [[nodiscard]] bool hasPassword() const { return !hashValue.isEmpty() || !legacyHash.isEmpty(); }
};

// A computed password hash, the four attributes Excel writes.
struct PasswordHash {
    QString algorithmName; // "SHA-512"
    QString hashValue;     // base64
    QString saltValue;     // base64
    qint32 spinCount = 0;
};

inline constexpr qint32 kExcelSpinCount = 100000;

// The SHA-512 hash of `password` with `salt` (see the note above). The
// test seam: setPassword below draws the salt at random.
[[nodiscard]] PasswordHash excelPasswordHash(const QString &password, const QByteArray &salt,
                                             qint32 spinCount = kExcelSpinCount);

// Excel's pre-2013 16-bit password verifier (the "password" attribute),
// upper-case hex.
[[nodiscard]] QString legacyPasswordHash(const QString &password);

// True when `password` opens a lock with these attributes: the iterated
// hash for SHA-1/-256/-384/-512 or MD5, else the legacy verifier, else
// (no password set) only the empty password.
[[nodiscard]] bool passwordMatches(const QString &password, const QString &algorithmName,
                                   const QString &hashValue, const QString &saltValue, qint32 spinCount,
                                   const QString &legacyHash);
[[nodiscard]] bool passwordMatches(const SheetProtection &protection, const QString &password);
[[nodiscard]] bool passwordMatches(const WorkbookProtection &protection, const QString &password);

// Sets (or, for an empty password, clears) the password with a fresh salt.
void setPassword(SheetProtection *protection, const QString &password);
void setPassword(WorkbookProtection *protection, const QString &password);

} // namespace QindaOffice::Calc
