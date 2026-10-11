// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_alphabets.h"

#include <array>

namespace QindaOffice::Math {

namespace {

// Latin alphabets of the Mathematical Alphanumeric Symbols block: A–Z then
// a–z from each start (Unicode 15, U+1D400…).
enum Latin : int {
    LBold,
    LItalic,
    LBoldItalic,
    LScript,
    LBoldScript,
    LFraktur,
    LDoubleStruck,
    LBoldFraktur,
    LSans,
    LSansBold,
    LSansItalic,
    LSansBoldItalic,
    LMono,
    LCount
};
constexpr std::array<char32_t, LCount> kLatinStart{0x1D400, 0x1D434, 0x1D468, 0x1D49C, 0x1D4D0, 0x1D504, 0x1D538,
                                                  0x1D56C, 0x1D5A0, 0x1D5D4, 0x1D608, 0x1D63C, 0x1D670};

struct Hole {
    int alphabet;
    char32_t letter;
    char32_t replacement;
};
// Code points the block leaves reserved because Letterlike Symbols had them.
constexpr std::array<Hole, 24> kHoles{{
    {LItalic, U'h', 0x210E},       {LScript, U'B', 0x212C},       {LScript, U'E', 0x2130},
    {LScript, U'F', 0x2131},       {LScript, U'H', 0x210B},       {LScript, U'I', 0x2110},
    {LScript, U'L', 0x2112},       {LScript, U'M', 0x2133},       {LScript, U'R', 0x211B},
    {LScript, U'e', 0x212F},       {LScript, U'g', 0x210A},       {LScript, U'o', 0x2134},
    {LFraktur, U'C', 0x212D},      {LFraktur, U'H', 0x210C},      {LFraktur, U'I', 0x2111},
    {LFraktur, U'R', 0x211C},      {LFraktur, U'Z', 0x2128},      {LDoubleStruck, U'C', 0x2102},
    {LDoubleStruck, U'H', 0x210D}, {LDoubleStruck, U'N', 0x2115}, {LDoubleStruck, U'P', 0x2119},
    {LDoubleStruck, U'Q', 0x211A}, {LDoubleStruck, U'R', 0x211D}, {LDoubleStruck, U'Z', 0x2124},
}};

enum Greek : int { GBold, GItalic, GBoldItalic, GSansBold, GSansBoldItalic, GCount };
constexpr std::array<char32_t, GCount> kGreekStart{0x1D6A8, 0x1D6E2, 0x1D71C, 0x1D756, 0x1D790};
// The 58 letters of each Greek alphabet, in block order.
constexpr std::array<char32_t, 9> kGreekTail{0x2207, 0, 0x2202, 0x03F5, 0x03D1, 0x03F0, 0x03D5, 0x03F1, 0x03D6};

enum Digit : int { DBold, DDoubleStruck, DSans, DSansBold, DMono, DCount };
constexpr std::array<char32_t, DCount> kDigitStart{0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6};

int latinIndex(char32_t c)
{
    if (c >= U'A' && c <= U'Z') {
        return int(c - U'A');
    }
    if (c >= U'a' && c <= U'z') {
        return 26 + int(c - U'a');
    }
    return -1;
}

int greekIndex(char32_t c)
{
    if (c == 0x03F4) {
        return 17; // ϴ takes the slot of the unassigned U+03A2
    }
    if (c >= 0x0391 && c <= 0x03A9 && c != 0x03A2) {
        return int(c - 0x0391);
    }
    if (c >= 0x03B1 && c <= 0x03C9) {
        return 26 + int(c - 0x03B1);
    }
    for (size_t i = 0; i < kGreekTail.size(); ++i) {
        if (kGreekTail.at(i) != 0 && kGreekTail.at(i) == c) {
            return 25 + (i == 0 ? 0 : int(i) + 24);
        }
    }
    return -1;
}

char32_t greekAt(int index)
{
    if (index == 17) {
        return 0x03F4;
    }
    if (index < 25) {
        return 0x0391 + char32_t(index);
    }
    if (index == 25) {
        return 0x2207;
    }
    if (index <= 50) {
        return 0x03B1 + char32_t(index - 26);
    }
    return kGreekTail.at(size_t(index - 49));
}

int latinAlphabet(MathScript script, bool bold, bool italic)
{
    switch (script) {
    case MathScript::Roman:
        return bold && italic ? LBoldItalic : bold ? LBold : italic ? LItalic : -1;
    case MathScript::Script:
        return bold ? LBoldScript : LScript;
    case MathScript::Fraktur:
        return bold ? LBoldFraktur : LFraktur;
    case MathScript::DoubleStruck:
        return LDoubleStruck;
    case MathScript::SansSerif:
        return bold && italic ? LSansBoldItalic : bold ? LSansBold : italic ? LSansItalic : LSans;
    case MathScript::Monospace:
        return LMono;
    }
    return -1;
}

int greekAlphabet(MathScript script, bool bold, bool italic)
{
    if (script == MathScript::SansSerif && bold) {
        return italic ? GSansBoldItalic : GSansBold;
    }
    return bold && italic ? GBoldItalic : bold ? GBold : italic ? GItalic : -1;
}

int digitAlphabet(MathScript script, bool bold)
{
    switch (script) {
    case MathScript::Roman:
        return bold ? DBold : -1;
    case MathScript::DoubleStruck:
        return DDoubleStruck;
    case MathScript::SansSerif:
        return bold ? DSansBold : DSans;
    case MathScript::Monospace:
        return DMono;
    default:
        return bold ? DBold : -1;
    }
}

} // namespace

char32_t mathAlphanumeric(char32_t c, MathScript script, bool bold, bool italic)
{
    if (const int li = latinIndex(c); li >= 0) {
        const int alphabet = latinAlphabet(script, bold, italic);
        if (alphabet < 0) {
            return c;
        }
        for (const Hole &h : kHoles) {
            if (h.alphabet == alphabet && h.letter == c) {
                return h.replacement;
            }
        }
        return kLatinStart.at(size_t(alphabet)) + char32_t(li);
    }
    if (const int gi = greekIndex(c); gi >= 0) {
        const int alphabet = greekAlphabet(script, bold, italic);
        return alphabet < 0 ? c : kGreekStart.at(size_t(alphabet)) + char32_t(gi);
    }
    if (c >= U'0' && c <= U'9') {
        const int alphabet = digitAlphabet(script, bold);
        return alphabet < 0 ? c : kDigitStart.at(size_t(alphabet)) + (c - U'0');
    }
    if (italic && !bold && script == MathScript::Roman) {
        if (c == 0x0131) {
            return 0x1D6A4; // dotless i
        }
        if (c == 0x0237) {
            return 0x1D6A5; // dotless j
        }
    }
    return c;
}

bool italicByDefault(char32_t c)
{
    if (latinIndex(c) >= 0 || c == 0x0131 || c == 0x0237) {
        return true;
    }
    if (c >= 0x03B1 && c <= 0x03C9) {
        return true;
    }
    return c == 0x03D1 || c == 0x03D5 || c == 0x03D6 || c == 0x03F0 || c == 0x03F1 || c == 0x03F5;
}

bool plainLetter(char32_t c, PlainLetter *out)
{
    PlainLetter p;
    const auto styled = [&p](int alphabet) {
        static constexpr std::array<std::array<int, 3>, LCount> kStyle{{
            {int(MathScript::Roman), 1, 0},        {int(MathScript::Roman), 0, 1},
            {int(MathScript::Roman), 1, 1},        {int(MathScript::Script), 0, 0},
            {int(MathScript::Script), 1, 0},       {int(MathScript::Fraktur), 0, 0},
            {int(MathScript::DoubleStruck), 0, 0}, {int(MathScript::Fraktur), 1, 0},
            {int(MathScript::SansSerif), 0, 0},    {int(MathScript::SansSerif), 1, 0},
            {int(MathScript::SansSerif), 0, 1},    {int(MathScript::SansSerif), 1, 1},
            {int(MathScript::Monospace), 0, 0},
        }};
        const auto &s = kStyle.at(size_t(alphabet));
        p.script = MathScript(s.at(0));
        p.bold = s.at(1) != 0;
        p.italic = s.at(2) != 0;
    };
    bool found = false;
    for (const Hole &h : kHoles) {
        if (h.replacement == c) {
            p.base = h.letter;
            styled(h.alphabet);
            found = true;
        }
    }
    for (int a = 0; a < LCount && !found; ++a) {
        const char32_t start = kLatinStart.at(size_t(a));
        if (c >= start && c < start + 52) {
            const int i = int(c - start);
            p.base = i < 26 ? U'A' + char32_t(i) : U'a' + char32_t(i - 26);
            styled(a);
            found = true;
        }
    }
    for (int a = 0; a < GCount && !found; ++a) {
        const char32_t start = kGreekStart.at(size_t(a));
        if (c >= start && c < start + 58) {
            p.base = greekAt(int(c - start));
            p.script = a >= GSansBold ? MathScript::SansSerif : MathScript::Roman;
            p.bold = a != GItalic;
            p.italic = a == GItalic || a == GBoldItalic || a == GSansBoldItalic;
            found = true;
        }
    }
    for (int a = 0; a < DCount && !found; ++a) {
        const char32_t start = kDigitStart.at(size_t(a));
        if (c >= start && c < start + 10) {
            static constexpr std::array<MathScript, DCount> kScripts{MathScript::Roman, MathScript::DoubleStruck,
                                                                    MathScript::SansSerif, MathScript::SansSerif,
                                                                    MathScript::Monospace};
            p.base = U'0' + (c - start);
            p.script = kScripts.at(size_t(a));
            p.bold = a == DBold || a == DSansBold;
            found = true;
        }
    }
    if (!found && (c == 0x1D6A4 || c == 0x1D6A5)) {
        p.base = c == 0x1D6A4 ? char32_t(0x0131) : char32_t(0x0237);
        p.italic = true;
        found = true;
    }
    if (found && out != nullptr) {
        *out = p;
    }
    return found;
}

} // namespace QindaOffice::Math
