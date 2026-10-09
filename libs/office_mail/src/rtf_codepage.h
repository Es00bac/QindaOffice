// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Windows code pages → text, shared by the RTF readers (rtf_text.cpp:
// \ansicpgN, \fcharsetN, \cpgN) and the Outlook .msg reader
// (msg_properties.cpp: PT_STRING8 values, PidTagHtml bytes).
//
// AGENT-NOTE: Qt 6's QStringDecoder only knows the Unicode encodings and
// Latin-1 by itself; every other code page comes from ICU through
// QStringDecoder(const char *name). Unknown or unavailable code pages fall
// back to windows-1252 and then to Latin-1, so decoding never fails — it may
// only be wrong for a code page this table does not map.

#include <QByteArray>
#include <QString>
#include <QStringDecoder>

namespace QindaOffice::Mail {

// Windows code page number for an RTF \fcharsetN value; 0 means "the
// document code page" (ANSI_CHARSET, DEFAULT_CHARSET, SYMBOL_CHARSET and
// unknown values).
int codePageForRtfCharset(int charset);

// A reusable decoder for one Windows code page. decode() resets the state
// first, so every call decodes a complete, independent byte run.
// Not thread-safe; cheap to keep per parser.
class CodePageDecoder {
public:
    explicit CodePageDecoder(int codePage = 1252);
    int codePage() const { return m_codePage; }
    QString decode(const QByteArray &bytes);

private:
    int m_codePage;
    bool m_latin1 = false;      // no usable decoder: Latin-1 fallback
    bool m_asciiSafe = true;    // ASCII bytes decode to themselves
    QStringDecoder m_decoder;
};

// One-shot convenience over CodePageDecoder.
QString decodeCodePage(const QByteArray &bytes, int codePage);

} // namespace QindaOffice::Mail
