// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RTF → text for mail bodies (msg_reader.cpp, RTF-only Outlook messages):
// MS-OXRTFEX de-encapsulation of HTML (\fromhtml1) and of plain text
// (\fromtext), and best-effort plain text from any other RTF.
//
// AGENT-GUARD: the RTF is untrusted (it comes out of decompressRtf()).
// Parsing is iterative with a group-depth bound (kMaxRtfGroupDepth),
// \binN skips are clamped to the input, numeric parameters saturate, and
// malformed escapes are dropped; output can never exceed the input length
// in characters. Nothing here throws, asserts or reads out of bounds.

#include <QByteArray>
#include <QString>

namespace QindaOffice::Mail {

inline constexpr int kMaxRtfGroupDepth = 512;

enum class RtfBodyKind {
    None,   // empty or not RTF (no leading "{\rtf")
    Html,   // \fromhtml1 in the header: encapsulated HTML (MS-OXRTFEX 2.1.3.1)
    Text,   // \fromtext in the header: encapsulated plain text
    Rtf,    // any other RTF
};

// Classifies a document by the control words of its header, i.e. before
// the first nested group (the font table).
RtfBodyKind rtfEncapsulation(const QByteArray &rtf);

// MS-OXRTFEX 2.1.3: rebuilds the original HTML from \fromhtml1 RTF. Emits
// the text outside \htmlrtf…\htmlrtf0 and the contents of {\*\htmltagN …}
// groups; \par and \line become CRLF, \tab a TAB; \'hh decodes through the
// font / \ansicpg code page; \uN honours \ucN. False with `*error` set when
// the RTF is not \fromhtml1 or nests groups deeper than kMaxRtfGroupDepth.
bool rtfToHtml(const QByteArray &rtf, QString *html, QString *error);

// Plain text of an RTF document: \fromtext de-encapsulation, or the visible
// text of any other RTF (destinations such as the font table, pictures,
// headers and \* groups are skipped). Lines end in "\n". Empty when `rtf`
// is not RTF; a document nested too deeply yields the text read so far.
QString rtfToPlainText(const QByteArray &rtf);

} // namespace QindaOffice::Mail
