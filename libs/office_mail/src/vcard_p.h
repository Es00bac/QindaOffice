// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by vcard_reader.cpp and vcard_writer.cpp (M64).
// Not part of the library's public surface; implemented in vcard_text.cpp.

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail::VCard::Detail {

// RFC 6350 §3.4 text escapes: "\" "," ";" and newlines (CRLF, CR, LF → \n).
[[nodiscard]] QString escapeText(const QString &text);
// The inverse: \n \N → LF (not in vCard 2.1, which has no newline escape
// and carries newlines through QUOTED-PRINTABLE), \\ \, \; \: → the
// character; an unknown escape keeps its backslash ("C:\temp" survives).
[[nodiscard]] QString unescapeText(const QString &text, bool v21);
// Splits on `separator` where it is not backslash-escaped; the parts keep
// their escapes (unescape each one afterwards).
[[nodiscard]] QStringList splitUnescaped(const QString &text, QChar separator);

// QUOTED-PRINTABLE (vCard 2.1): =XX → byte; a lone '=' at the end is a soft
// break; a malformed '=' is kept literally.
[[nodiscard]] QByteArray decodeQuotedPrintable(QByteArrayView raw);
// Base64 with whitespace removed; null on invalid input.
[[nodiscard]] QByteArray decodeBase64(QByteArrayView raw);
// Text from bytes: `charset` when given (and honoured) and known to Qt's
// codecs (ICU), else UTF-8 when valid, else windows-1252 (the ANSI code
// page Outlook writes), else Latin-1.
[[nodiscard]] QString decodeBytes(const QByteArray &bytes, const QString &charset, bool honourCharset);
// RFC 6868 parameter value caret encoding: ^n → LF, ^^ → ^, ^' → ".
[[nodiscard]] QString decodeCaret(const QString &value);
[[nodiscard]] QString encodeCaret(const QString &value);

// "JPEG", "jpg", "image/jpeg" → "image/jpeg"; "" when it is not a media type.
[[nodiscard]] QString mimeTypeForTypeParam(const QString &type);
// "image/jpeg" → "JPEG" (vCard 3.0 PHOTO;TYPE=); "" when unknown.
[[nodiscard]] QString typeParamForMimeType(const QString &mimeType);

// One content line (UTF-8, no line break) folded at 75 octets, never
// inside a UTF-8 sequence, each physical line ending in CRLF.
[[nodiscard]] QByteArray foldLine(const QByteArray &line);

// Same TYPE token set, ignoring order and "pref".
[[nodiscard]] bool sameTypeTokens(const QString &a, const QString &b);

} // namespace QindaOffice::Mail::VCard::Detail
