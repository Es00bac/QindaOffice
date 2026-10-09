// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MS-PST primitives (M76, D-085): the CRC every page, block and header
// carries (MS-PST 5.3), the two block encodings Outlook writes (5.1
// "permute", the default "compressible encryption", and 5.2 "cyclic", the
// old "high encryption") and the page/block signature (5.5).
//
// AGENT-NOTE: the three 256-byte tables of MS-PST's mpbbCrypt are not typed
// in by hand. Only the decode table (mpbbI) and the cyclic middle table
// (mpbbS) are spelled out in pst_crypt.cpp; mpbbR is computed as the
// inverse of mpbbI, and tst_mail_pst checks the algebra the encodings rely
// on (mpbbI a permutation, mpbbS an involution, mpbbR[0..7] = 65 54 19 98
// 168 33 110 187 as MS-PST prints it), so a mistyped byte fails a test
// instead of silently garbling every encoded file.
//
// Threading: pure functions over immutable tables; any thread.

#include <QByteArray>
#include <QtGlobal>

namespace QindaOffice::Mail::Pst {

// MS-PST 5.3: CRC-32 with the reflected 0xEDB88320 polynomial, the caller's
// seed (0 for every structure in the file) and NO final inversion.
[[nodiscard]] quint32 crc32(const char *data, qsizetype size, quint32 seed = 0);
[[nodiscard]] inline quint32 crc32(const QByteArray &bytes) { return crc32(bytes.constData(), bytes.size()); }

// NDB_CRYPT_PERMUTE (bCryptMethod 0x01): `encode` true writes, false reads.
void permute(char *data, qsizetype size, bool encode);
// NDB_CRYPT_CYCLIC (0x02): its own inverse; `key` is the low 32 bits of the
// block's BID.
void cyclic(char *data, qsizetype size, quint32 key);

// MS-PST 5.5 ComputeSig: the wSig of a B-tree page or a block.
[[nodiscard]] quint16 signature(quint64 ib, quint64 bid);

// Read-only views of the tables, for tst_mail_pst's algebra checks.
[[nodiscard]] const quint8 *encodeTable();   // mpbbR
[[nodiscard]] const quint8 *middleTable();   // mpbbS
[[nodiscard]] const quint8 *decodeTable();   // mpbbI

} // namespace QindaOffice::Mail::Pst
