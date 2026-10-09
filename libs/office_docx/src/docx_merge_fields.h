// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's mail merge fields (M77, D-086): their codes, how Word shows them
// in a main document, and their values for one recipient.
//
// In the text a merge field is a run whose kFieldInstruction holds the
// field code — nested fields written inside it as Word's field-code view
// shows them, "IF { MERGEFIELD Country } = \"USA\" \"\" \"{ MERGEFIELD
// Country }\"" — and whose text is the result: «First_Name», «AddressBlock»,
// «GreetingLine», and for the rules Word draws without a result
// «Next Record», «Skip Record If…», «Next Record If…» (written back without
// a result, as Word writes them). Line breaks in a code (an Address Block's
// format) are '\n'.
//
// Evaluating: MERGEFIELD (with \b \f \* \@ \#), ADDRESSBLOCK and
// GREETINGLINE (Word's \f formats over the matched address fields), IF …
// THEN … ELSE (numbers compared as numbers, text case-sensitively, * and ?
// wildcards on = and <>), SKIPIF, NEXT, NEXTIF, MERGEREC and MERGESEQ.

#include "docx_merge_data.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace QindaOffice::Docx {

enum class MergeFieldKind { None, MergeField, AddressBlock, GreetingLine, If, SkipIf, Next, NextIf, MergeRec, MergeSeq };

// The kind of a field code ("MERGEFIELD First_Name \* MERGEFORMAT" →
// MergeField); None for other fields and for an IF that holds no merge field.
[[nodiscard]] MergeFieldKind mergeFieldKind(const QString &instruction);
[[nodiscard]] inline bool isMergeField(const QString &instruction)
{
    return mergeFieldKind(instruction) != MergeFieldKind::None;
}
// Rules Word writes with no result (NEXT, NEXTIF, SKIPIF).
[[nodiscard]] bool mergeFieldHasResult(MergeFieldKind kind);
// The text Word shows for the field in a main document: «Name»,
// «AddressBlock», «GreetingLine», «Next Record», «Next Record If…»,
// «Skip Record If…», «Merge Record #», «Merge Sequence #», «IF».
[[nodiscard]] QString mergeFieldLabel(const QString &instruction);
// The data source columns a code names (nested fields included; an
// Address Block or Greeting Line names none of its own).
[[nodiscard]] QStringList mergeFieldColumns(const QString &instruction);

// Codes as Word writes them.
[[nodiscard]] QString mergeFieldCode(const QString &column);
// A rule's condition: `column` `op` (=, <>, <, >, <=, >=) "value".
[[nodiscard]] QString ifCode(const QString &column, const QString &op, const QString &value, const QString &then,
                             const QString &otherwise);
[[nodiscard]] QString skipIfCode(const QString &column, const QString &op, const QString &value);
[[nodiscard]] QString nextIfCode(const QString &column, const QString &op, const QString &value);

// Word's Address Block and Greeting Line choices.
struct AddressBlockOptions {
    QString nameFormat;      // an \f name format (addressNameFormats())
    bool company = true;     // _COMPANY_
    bool postal = true;      // the street, city, state, postal code
    int country = 2;         // \c: 0 never, 1 always, 2 only when not `homeCountry`
    QString homeCountry = QStringLiteral("United States"); // \e
    bool formatByCountry = true; // \d
};
struct GreetingLineOptions {
    QString salutation = QStringLiteral("Dear"); // _BEFORE_ text ("Dear", "To", or none)
    QString nameFormat;                           // greetingNameFormats()
    QString punctuation = QStringLiteral(",");    // _AFTER_ text
    QString fallback = QStringLiteral("Dear Sir or Madam,"); // \e
};
// [{format, sample}] Word offers ("Joshua Randall Jr.", …).
[[nodiscard]] QList<std::pair<QString, QString>> addressNameFormats();
[[nodiscard]] QList<std::pair<QString, QString>> greetingNameFormats();
[[nodiscard]] QString addressBlockCode(const AddressBlockOptions &options);
[[nodiscard]] QString greetingLineCode(const GreetingLineOptions &options);

// Word's address fields (Match Fields): "Courtesy Title", "First Name", …
[[nodiscard]] QStringList addressFields();
// The column an address field reads: the user's match, else the first
// column whose name Word would match ("ZIP Code", "Postal Code", "Zip", …).
[[nodiscard]] QString matchedColumn(const QString &addressField, const MergeData &data,
                                    const QMap<QString, QString> &fieldMap);

// The data source column a MERGEFIELD name reads: the same name (any
// case), the name with '_' for its spaces (older Word codes say
// First_Name), or the same letters and digits; -1 for none.
[[nodiscard]] int mergeColumnIndex(const MergeData &data, const QString &name);

// One copy of the main document being merged: the recipients in merge
// order, the current one, and what the rules did.
struct MergeCursor {
    const MergeData *data = nullptr;
    QList<int> order;          // data source indices, in merge order
    int position = 0;          // index into `order`
    int sequence = 1;          // MERGESEQ: records merged so far, this one included
    QMap<QString, QString> fieldMap;
    bool skip = false;         // a SKIPIF held
    bool exhausted = false;    // a NEXT ran past the last recipient

    [[nodiscard]] int record() const { return !exhausted && position < order.size() ? order.at(position) : -1; }
    [[nodiscard]] QString value(const QString &column) const;
    // NEXT: the following recipient; past the last one, every field is empty.
    void next();
};

// The field's result for the cursor's recipient; the rules move the
// cursor or set `skip`. Fields that are not merge fields return `fallback`.
[[nodiscard]] QString evaluateMergeField(const QString &instruction, MergeCursor &cursor,
                                         const QString &fallback = QString());

} // namespace QindaOffice::Docx
