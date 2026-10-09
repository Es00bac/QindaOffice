// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MS-OXRTFCP: the compressed-RTF container Outlook stores in
// PidTagRtfCompressed (.msg files, TNEF). Decompression only — QindaMail
// never writes compressed RTF. msg_reader.cpp is the consumer; rtf_text.h
// turns the result into HTML or plain text.
//
// AGENT-GUARD: the blob is untrusted. RAWSIZE is capped at kMaxRtfRawSize,
// COMPSIZE must fit the blob, LZFu data must match its CRC, and the output
// may never exceed RAWSIZE; any violation returns false with a readable
// error instead of a partial or oversized result.

#include <QByteArray>
#include <QString>

namespace QindaOffice::Mail {

inline constexpr qint64 kMaxRtfRawSize = 64LL * 1024 * 1024;

// Decompresses an RTFCOMPRESSED blob (MS-OXRTFCP 2.2): COMPTYPE "LZFu"
// (compressed, CRC-checked) or "MELA" (stored). On success `*rtf` holds the
// raw RTF bytes; on failure it is cleared and `*error` (when non-null) says
// why.
bool decompressRtf(const QByteArray &compressed, QByteArray *rtf, QString *error);

// The MS-OXRTFCP 3.1.3.2 CRC-32 over `data` (the bytes after the 16-byte
// header): table-driven CRC-32 (polynomial 0xEDB88320) with initial value 0
// and no final XOR — NOT the zlib CRC-32.
quint32 rtfCompressedCrc(const QByteArray &data);

// The 207-byte dictionary prefix every LZFu stream starts from
// (MS-OXRTFCP 2.1.2.1); exposed for tests that build LZFu blobs.
QByteArray rtfCompressionPrefix();

} // namespace QindaOffice::Mail
