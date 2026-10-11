// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost (M89, D-097): Formula AutoComplete — what the formula bar's
// QindaTK TextField lists in its suggestions (QindaTK D-314): functions with
// their arguments, defined names, tables and a table's columns, for the
// formula sheet and the cell being edited (formula_completion.h).
#include "calc_host.h"

#include "formula_completion.h"
#include "spreadsheet_document.h"

namespace QindaOffice::Apps::QindaCalc {

QVariantMap CalcHost::formulaCompletions(const QString &text, int position) const
{
    static const QHash<QString, QString> icons{{QStringLiteral("function"), QStringLiteral("sigma")},
                                               {QStringLiteral("name"), QStringLiteral("tag")},
                                               {QStringLiteral("table"), QStringLiteral("table")},
                                               {QStringLiteral("column"), QStringLiteral("columns-3")},
                                               {QStringLiteral("item"), QStringLiteral("hash")}};
    const CalcEng::CompletionContext found = CalcEng::formulaCompletions(
        *m_document->workbook(), m_document->activeSheet(), cursor(), text, position);
    QVariantList entries;
    for (const CalcEng::Completion &c : found.entries) {
        entries.append(QVariantMap{{QStringLiteral("label"), c.label},
                                   {QStringLiteral("detail"), c.detail},
                                   {QStringLiteral("iconName"), icons.value(c.kind)},
                                   {QStringLiteral("insert"), c.insert},
                                   {QStringLiteral("kind"), c.kind}});
    }
    return {{QStringLiteral("from"), found.from}, {QStringLiteral("to"), found.to}, {QStringLiteral("entries"), entries}};
}

} // namespace QindaOffice::Apps::QindaCalc
