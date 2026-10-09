// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to the merge field code (docx_merge_tokens.cpp, docx_merge_fields.cpp,
// docx_merge_address.cpp): a field code split into Word's tokens.

#include "docx_merge_fields.h"

#include <QList>
#include <QString>

#include <utility>

namespace QindaOffice::Docx::detail {

struct FieldToken {
    enum Kind { Text, Quoted, Field, Switch };
    Kind kind = Text;
    QString text; // a Quoted token without its quotes; a Field token's code
};

struct FieldCode {
    QString type; // upper case
    QList<FieldToken> args;
    QList<std::pair<QString, FieldToken>> switches; // "\\f" → its argument (Text with empty text when none)

    [[nodiscard]] const FieldToken *switchArgument(QStringView name) const;
};

[[nodiscard]] QList<FieldToken> tokenizeField(const QString &code);
[[nodiscard]] FieldCode parseField(const QString &instruction);

// ADDRESSBLOCK / GREETINGLINE for the cursor's recipient (their \f format,
// \c / \e country rule, \e greeting for a recipient without a name).
[[nodiscard]] QString formatAddressField(const FieldCode &code, const MergeCursor &cursor);

} // namespace QindaOffice::Docx::detail
