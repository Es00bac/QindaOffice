// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to Flash Fill (flash_fill.h, M78, D-087): the pieces a fill
// programme is made of. An Extractor names a part of one source cell in a
// way that carries to other rows (the second word, what follows the last
// '@', the first three characters of the first word …); a Piece is a
// literal or an extractor with a case change; a programme is a list of
// pieces whose outputs are joined. flash_fill_atoms.cpp tokenises, lists
// the extractors a row offers, applies them, prices them (the ranking's
// costs) and describes them; flash_fill.cpp searches for programmes.

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Calc::FlashFillInternal {

// How a source cell is cut into tokens.
enum class TokenClass {
    Word,    // runs of anything but whitespace ("O'Brien,", "4521")
    Alnum,   // runs of letters and digits
    Letters, // runs of letters
    Digits,  // runs of digits
};

enum class CaseMode { AsIs, Upper, Lower, Proper };

struct Extractor {
    enum class Kind {
        Whole,       // the whole cell
        Token,       // token `index` (from the end when fromEnd) of `tokenClass`
        Field,       // field `index` between `delimiter`s (trimmed of spaces when trimmed)
        AfterFirst,  // everything after the first `delimiter`
        BeforeLast,  // everything before the last `delimiter`
        Prefix,      // the first `length` characters of the cell
        Suffix,      // the last `length` characters
        TokenPrefix, // the first `length` characters of a token
        TokenSuffix, // the last `length` characters of a token
    };
    Kind kind = Kind::Whole;
    int column = 0;
    TokenClass tokenClass = TokenClass::Word;
    QChar delimiter;
    int index = 0;
    bool fromEnd = false;
    bool trimmed = false;
    int length = 0;
    bool operator==(const Extractor &) const = default;
};

struct Piece {
    bool literal = false;
    QString text;          // literal
    Extractor extractor;   // otherwise
    CaseMode caseMode = CaseMode::AsIs;
    bool operator==(const Piece &) const = default;
};

// An extractor a row offers, with what it gives there (before any case change).
struct Offer {
    Extractor extractor;
    QString value;
};

// Every extractor `sources` offer (non-empty values only). Long cells offer
// no character-count cuts past kMaxCut characters.
inline constexpr int kMaxCut = 64;
[[nodiscard]] QList<Offer> offers(const QStringList &sources);

// The extractor applied to another row; nullopt when the row lacks what it
// takes (no fourth word, no '@'). A cut longer than the text gives the
// whole text (Excel's "first three letters" of "Al" is "Al").
[[nodiscard]] std::optional<QString> extract(const Extractor &extractor, const QStringList &sources);

[[nodiscard]] QString applyCase(const QString &text, CaseMode mode);

// The ranking's price of a piece whose value on the first example is
// `value` (see flash_fill.cpp for the rule): every piece costs 1, plus its
// kind's cost; literals cost one per punctuation character, two per letter
// or digit.
[[nodiscard]] double pieceCost(const Piece &piece, const QString &value, const QStringList &sources);

// "the last word of Column A, in capitals" / "“, ”".
[[nodiscard]] QString describePiece(const Piece &piece, const QStringList &columnNames);

} // namespace QindaOffice::Calc::FlashFillInternal
