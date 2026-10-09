// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Hand-written formula lexer (plan section 7.2). Canonical storage text is
// en-US: ',' argument separator, '.' decimal; ';' is accepted on input as an
// alternate separator and normalized away by the parser.

#include <QString>
#include <QVector>

namespace QindaOffice::Calc {

struct Token {
    enum class Type {
        Number,
        String,
        Identifier, // function name, named range, TRUE/FALSE
        ErrorLit,   // #NULL! #DIV/0! ... #N/A
        Ref,        // A1, $A$1, ABC123
        SheetName,  // identifier immediately followed by '!'
        QuotedSheetName, // 'My Sheet'!
        // A structured reference with its table name and balanced brackets,
        // as one token: Table1[Sales], [@Sales], Table1[[#This Row],[Q1]]
        // (M69, D-078). What is inside the brackets is column text, never
        // an operator, a function or an address.
        StructRef,
        Op,         // : , ; % ^ * / + - & = < > <= >= <> ( ) { } ! @
        End,
    };
    Type type = Type::End;
    QString text;
    int pos = 0;
};

// Lexes the whole input. Never fails: unknown bytes become Op tokens, which
// the parser rejects with a positioned error.
[[nodiscard]] QVector<Token> lexFormula(const QString &input);

} // namespace QindaOffice::Calc
