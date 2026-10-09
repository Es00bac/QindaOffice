// SPDX-License-Identifier: GPL-3.0-or-later
// Windows code page decoding for the RTF and .msg readers (rtf_codepage.h).

#include "rtf_codepage.h"

namespace QindaOffice::Mail {
namespace {

struct CodePageNames {
    int codePage;
    const char *primary;
    const char *alternative;   // tried when ICU lacks the primary name
};

// AGENT-NOTE: names are ICU converter aliases. CJK pages prefer the Windows
// variants (cp932 = windows-31j, cp949 = windows-949), which are supersets
// of the ISO/EUC tables Outlook-generated text actually uses.
constexpr CodePageNames kCodePages[] = {
    {437, "IBM437", "cp437"},
    {850, "IBM850", "cp850"},
    {852, "IBM852", "cp852"},
    {866, "IBM866", "cp866"},
    {874, "windows-874", "TIS-620"},
    {932, "windows-31j", "Shift_JIS"},
    {936, "GBK", "GB2312"},
    {949, "windows-949", "EUC-KR"},
    {950, "Big5", "windows-950"},
    {1250, "windows-1250", nullptr},
    {1251, "windows-1251", nullptr},
    {1252, "windows-1252", nullptr},
    {1253, "windows-1253", nullptr},
    {1254, "windows-1254", nullptr},
    {1255, "windows-1255", nullptr},
    {1256, "windows-1256", nullptr},
    {1257, "windows-1257", nullptr},
    {1258, "windows-1258", nullptr},
    {1361, "ibm-1361", "Johab"},
    {10000, "macintosh", "x-mac-roman"},
    {10007, "x-mac-cyrillic", nullptr},
    {20127, "US-ASCII", nullptr},
    {20866, "KOI8-R", nullptr},
    {21866, "KOI8-U", nullptr},
    {28591, "ISO-8859-1", nullptr},
    {28592, "ISO-8859-2", nullptr},
    {28593, "ISO-8859-3", nullptr},
    {28594, "ISO-8859-4", nullptr},
    {28595, "ISO-8859-5", nullptr},
    {28596, "ISO-8859-6", nullptr},
    {28597, "ISO-8859-7", nullptr},
    {28598, "ISO-8859-8", nullptr},
    {28599, "ISO-8859-9", nullptr},
    {28603, "ISO-8859-13", nullptr},
    {28605, "ISO-8859-15", nullptr},
    {50220, "ISO-2022-JP", nullptr},
    {50221, "ISO-2022-JP", nullptr},
    {50222, "ISO-2022-JP", nullptr},
    {51932, "EUC-JP", nullptr},
    {51936, "GB2312", nullptr},
    {51949, "EUC-KR", nullptr},
    {52936, "HZ-GB-2312", nullptr},
    {54936, "GB18030", nullptr},
    {65000, "UTF-7", nullptr},
};

const CodePageNames *namesFor(int codePage)
{
    for (const CodePageNames &entry : kCodePages) {
        if (entry.codePage == codePage) return &entry;
    }
    return nullptr;
}

} // namespace

int codePageForRtfCharset(int charset)
{
    switch (charset) {
    case 77: return 10000;    // MAC_CHARSET
    case 128: return 932;     // SHIFTJIS_CHARSET
    case 129: return 949;     // HANGUL_CHARSET
    case 130: return 1361;    // JOHAB_CHARSET
    case 134: return 936;     // GB2312_CHARSET
    case 136: return 950;     // CHINESEBIG5_CHARSET
    case 161: return 1253;    // GREEK_CHARSET
    case 162: return 1254;    // TURKISH_CHARSET
    case 163: return 1258;    // VIETNAMESE_CHARSET
    case 177: return 1255;    // HEBREW_CHARSET
    case 178: return 1256;    // ARABIC_CHARSET
    case 186: return 1257;    // BALTIC_CHARSET
    case 204: return 1251;    // RUSSIAN_CHARSET
    case 222: return 874;     // THAI_CHARSET
    case 238: return 1250;    // EASTEUROPE_CHARSET
    case 254: return 437;     // PC437_CHARSET
    case 255: return 850;     // OEM_CHARSET
    default: return 0;        // ANSI (0), DEFAULT (1), SYMBOL (2), unknown
    }
}

CodePageDecoder::CodePageDecoder(int codePage)
    : m_codePage(codePage)
{
    // AGENT-NOTE: UTF-16/32 and the code pages whose shift sequences are
    // written in ASCII (ISO-2022-JP, HZ, UTF-7) never take the ASCII fast path.
    switch (codePage) {
    case 1200: case 1201: case 12000: case 12001: case 50220: case 50221: case 50222: case 52936: case 65000:
        m_asciiSafe = false;
        break;
    default:
        break;
    }
    switch (codePage) {
    case 65001: m_decoder = QStringDecoder(QStringDecoder::Utf8); return;
    case 1200: m_decoder = QStringDecoder(QStringDecoder::Utf16LE); return;
    case 1201: m_decoder = QStringDecoder(QStringDecoder::Utf16BE); return;
    case 12000: m_decoder = QStringDecoder(QStringDecoder::Utf32LE); return;
    case 12001: m_decoder = QStringDecoder(QStringDecoder::Utf32BE); return;
    default: break;
    }
    if (const CodePageNames *names = namesFor(codePage)) {
        m_decoder = QStringDecoder(names->primary);
        if (!m_decoder.isValid() && names->alternative) m_decoder = QStringDecoder(names->alternative);
        if (m_decoder.isValid()) return;
    }
    // AGENT-NOTE: unknown code page — Outlook's own default is windows-1252.
    m_decoder = QStringDecoder("windows-1252");
    m_latin1 = !m_decoder.isValid();
}

QString CodePageDecoder::decode(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return {};
    if (m_latin1) return QString::fromLatin1(bytes);
    if (m_asciiSafe) {
        bool ascii = true;
        for (const char c : bytes) {
            if (uchar(c) >= 0x80) {
                ascii = false;
                break;
            }
        }
        if (ascii) return QString::fromLatin1(bytes);
    }
    m_decoder.resetState();
    return m_decoder.decode(bytes);
}

QString decodeCodePage(const QByteArray &bytes, int codePage)
{
    if (bytes.isEmpty()) return {};
    CodePageDecoder decoder(codePage);
    return decoder.decode(bytes);
}

} // namespace QindaOffice::Mail
