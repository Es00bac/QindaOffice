// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Input masks (M80, D-089): Access's Input Mask property — how a field's
// typed text is shaped before it is stored: "(999) 000-0000" for a phone
// number, "00000-9999" for a ZIP code, ">L<??????" for a capitalised name.
//
// AGENT-CONTRACT (with base_records.cpp and the designer's presets):
//  * A mask has Access's three sections, separated by ';': the mask, then
//    "0" (store the literal characters with the data) or "1"/empty (store
//    only what was typed), then the placeholder shown for an empty slot
//    (default '_'). "Password" is the special mask that stores the text as
//    typed and shows it as asterisks.
//  * Mask characters: 0 digit (required), 9 digit or space (optional),
//    # digit, space, + or - (optional), L letter (required), ? letter
//    (optional), A letter or digit (required), a letter or digit
//    (optional), & any character (required), C any character (optional),
//    > upper-cases and < lower-cases what follows, ! fills optional slots
//    from the right, \x is the literal x, "text" is literal text; every
//    other character (( ) - / . , : ; and space) is a literal.
//  * applyInputMask accepts the text with or without the literals ("303
//    555 1234", "(303) 555-1234", "3035551234" all give the same) and fails
//    with a reason when a required slot stays empty or text is left over.
//    Unfilled trailing optional slots and the literals after the last
//    filled slot are dropped (Access stores "12345-" for a ZIP mask without
//    the extension; QindaBase stores "12345").
//  * Masks apply where people type (datasheet cells, forms, paste), never
//    to imports or action queries — as in Access.

#include <QString>
#include <QVariantList>

#include <optional>

namespace QindaOffice::Base {

struct InputMask {
    struct Element {
        enum class Kind { Literal, Slot };
        Kind kind = Kind::Literal;
        QChar ch;              // the literal, or the slot's class character (0 9 # L ? A a & C)
        bool required = false;
        int letterCase = 0;    // 1 upper, -1 lower, 0 as typed
    };
    QString text;              // as written in the designer
    QList<Element> elements;
    bool storeLiterals = false;
    QChar placeholder = QLatin1Char('_');
    bool password = false;
    bool rightFill = false;

    [[nodiscard]] bool isEmpty() const { return text.trimmed().isEmpty(); }
    [[nodiscard]] int slotCount() const;
    // nullopt with `error` for a mask Access would refuse (an unclosed
    // "literal", a trailing \, no slot at all).
    static std::optional<InputMask> parse(const QString &text, QString *error = nullptr);
};

struct MaskResult {
    bool ok = true;
    QString text;  // what is stored (empty: nothing typed)
    QString error; // "needs 10 characters like (___) ___-____" (without the field name)
};

[[nodiscard]] MaskResult applyInputMask(const InputMask &mask, const QString &input);
// A stored value shown through its mask: typed characters only ("3035551234")
// get the literals back; anything that does not fit is shown as stored.
[[nodiscard]] QString maskedDisplay(const InputMask &mask, const QString &stored);
// The mask's shape with its placeholder for empty slots: "(___) ___-____".
[[nodiscard]] QString maskHint(const InputMask &mask);
// Access's Input Mask Wizard presets: [{id, label, mask, sample}].
[[nodiscard]] QVariantList inputMaskPresets();

} // namespace QindaOffice::Base
