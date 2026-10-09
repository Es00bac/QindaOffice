// SPDX-License-Identifier: GPL-3.0-or-later
// Cell hyperlinks (hyperlink.h, M66, D-075).

#include "hyperlink.h"

#include "eval.h"
#include "sheet.h"
#include "workbook.h"

namespace QindaOffice::Calc {

namespace {

// The range a reference node or a range-valued name stands for, resolved
// from `fromSheet`.
std::optional<LinkPlace> placeOfRef(const Workbook &book, qint32 fromSheet, const RangeRef &ref)
{
    const qint32 sheet = ref.sheet.isEmpty() ? fromSheet : book.sheetIndex(unquotedSheetName(ref.sheet));
    if (sheet < 0 || sheet >= book.sheetCount() || !ref.range.isValid()) {
        return std::nullopt;
    }
    return LinkPlace{sheet, ref.range};
}

} // namespace

QString hyperlinkTarget(const Hyperlink &link)
{
    if (!link.address.isEmpty()) {
        return link.address;
    }
    return link.location.isEmpty() ? QString() : QLatin1Char('#') + link.location;
}

void setHyperlinkTarget(Hyperlink *link, const QString &typed)
{
    const QString target = typed.trimmed();
    link->address.clear();
    link->location.clear();
    if (target.isEmpty()) {
        return;
    }
    // "#Sheet2!A1" is a place in this workbook (Excel's own spelling for a
    // typed address); everything else leaves it.
    if (target.startsWith(QLatin1Char('#'))) {
        link->location = target.mid(1);
        return;
    }
    if (!target.contains(QLatin1String("://")) && !target.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)
        && target.contains(QLatin1Char('@')) && !target.contains(QLatin1Char(' '))) {
        link->address = QStringLiteral("mailto:") + target;
        return;
    }
    if (target.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
        link->address = QStringLiteral("http://") + target;
        return;
    }
    link->address = target;
}

std::optional<LinkPlace> resolveLinkLocation(const Workbook &book, qint32 fromSheet, const QString &location)
{
    QString text = location.trimmed();
    if (text.startsWith(QLatin1Char('#'))) {
        text = text.mid(1);
    }
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const ParseResult parsed = parseFormula(text);
    if (!parsed.error.isEmpty() || !parsed.root) {
        return std::nullopt;
    }
    if (parsed.root->kind == Node::Kind::Ref) {
        return placeOfRef(book, fromSheet, parsed.root->ref);
    }
    if (parsed.root->kind != Node::Kind::Name) {
        return std::nullopt;
    }
    if (const auto named = book.namedRange(parsed.root->text)) {
        return placeOfRef(book, fromSheet, *named);
    }
    // A scoped or formula name whose body is a plain reference.
    const NodePtr body = book.nameFormula(parsed.root->text, fromSheet);
    if (body && body->kind == Node::Kind::Ref) {
        return placeOfRef(book, fromSheet, body->ref);
    }
    return std::nullopt;
}

QString hyperlinkFormulaTarget(const Workbook &book, qint32 sheet, const CellAddr &addr)
{
    if (sheet < 0 || sheet >= book.sheetCount()) {
        return {};
    }
    const Cell *cell = book.sheetAt(sheet).cellAt(addr);
    if (cell == nullptr || !cell->ast || cell->ast->kind != Node::Kind::Call
        || cell->ast->text.compare(QLatin1String("HYPERLINK"), Qt::CaseInsensitive) != 0
        || cell->ast->args.isEmpty()) {
        return {};
    }
    // Evaluated where the formula lives, so a location built from cells
    // ("#"&A1&"!B2") follows them.
    EvalContext ctx{&book, sheet, addr};
    const Value value = evalNode(cell->ast->args.first(), ctx);
    return isErr(value) ? QString() : toText(value).trimmed();
}

} // namespace QindaOffice::Calc
