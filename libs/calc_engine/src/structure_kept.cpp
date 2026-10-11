// SPDX-License-Identifier: GPL-3.0-or-later
// M90 (D-098): the formulas inside the rules kept as Excel wrote them
// (sheet_rules.h KeptRule) through inserted and deleted rows and columns —
// the M78 open item. A kept rule is Excel's XML, sliced from the file; its
// formulas are the text of <formula>, <formula1> / <formula2> and <xm:f>
// elements and the val of a cfvo, in Excel's spelling. Each is placed at
// the rule's anchor (the top-left cell of the first range of its sqref),
// moved and, when the edit takes the anchor's own row or column, shifted to
// the first cell that remains — exactly what moveRuleFormulas does for the
// modelled rules (structure_rules.cpp), so a kept rule and a modelled one
// follow an edit alike. A formula the engine cannot parse stays as written
// and marks the rule (KeptRule::staleFormulas, the save's warning).

#include "sheet.h"
#include "structure_internal.h"
#include "workbook.h"

#include <QRegularExpression>

namespace QindaOffice::Calc {

using namespace StructureMath;

namespace {

QString unescaped(QString text)
{
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&apos;"), QLatin1String("'"));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return text;
}

QString escaped(QString text)
{
    text.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    text.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    text.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    text.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return text;
}

// The pre-edit cell that becomes the top-left of what remains of `range`.
std::optional<CellAddr> survivingTopLeft(const CellRange &range, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    if (band.insert) {
        return range.topLeft;
    }
    const bool rows = editsRows(edit);
    const qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
    const qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
    const qint32 last = band.at + band.count - 1;
    if (lo >= band.at && hi <= last) {
        return std::nullopt;
    }
    CellAddr out = range.topLeft;
    if (lo >= band.at && lo <= last) {
        (rows ? out.row : out.col) = last + 1;
    }
    return out;
}

struct Mover {
    const StructureEdit &edit;
    bool owner;
    QString editedSheetName;
    CellAddr from;
    CellAddr to;
    bool stale = false;

    // One formula (Excel's spelling, unescaped) through the edit; the text
    // itself when nothing moved or it does not parse.
    QString move(const QString &formula, bool relative)
    {
        const QString body = formula.trimmed();
        if (body.isEmpty()) {
            return formula;
        }
        bool number = false;
        static_cast<void>(body.toDouble(&number));
        if (number) {
            return formula;
        }
        const ParseResult parsed = parseFormula(body);
        if (!parsed.error.isEmpty() || !parsed.root) {
            stale = stale || owner || body.contains(editedSheetName, Qt::CaseInsensitive);
            return formula;
        }
        NodePtr node = parsed.root;
        if (relative && to != from) {
            node = shiftNode(node, to.row - from.row, to.col - from.col);
        }
        node = adjustFormulaNode(node, edit, owner, editedSheetName);
        const QString before = nodeText(parsed.root);
        QString text = nodeText(node);
        if (text == before) {
            return formula;
        }
        // The engine reads Excel's future-function prefixes as part of the
        // name and prints them upper case; Excel writes them lower case.
        for (const char *prefix : {"_xlfn.", "_xlws.", "_xlpm."}) {
            text.replace(QString::fromLatin1(prefix).toUpper(), QString::fromLatin1(prefix));
        }
        return text;
    }
};

QString movedXml(const QString &xml, Mover &mover)
{
    // Element text: <formula>, <formula1>, <formula2>, <xm:f> (x14's
    // <x14:formula1> holds an <xm:f>, matched on its own).
    static const QRegularExpression element(QStringLiteral("<((?:[\\w.-]+:)?(?:formula[12]?|f))>([^<]*)</\\1>"));
    // A cfvo's val (a number, or a formula with no relative reference).
    static const QRegularExpression cfvo(QStringLiteral("(<(?:[\\w.-]+:)?cfvo\\b[^>]*\\bval=\")([^\"]*)(\")"));
    QString out;
    qsizetype copied = 0;
    for (auto it = element.globalMatch(xml); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString formula = unescaped(match.captured(2));
        const QString moved = mover.move(formula, true);
        out += QStringView(xml).mid(copied, match.capturedStart(2) - copied);
        out += moved == formula ? match.captured(2) : escaped(moved);
        copied = match.capturedEnd(2);
    }
    out += QStringView(xml).mid(copied);
    QString result;
    copied = 0;
    for (auto it = cfvo.globalMatch(out); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString formula = unescaped(match.captured(2));
        const QString moved = mover.move(formula, false);
        result += QStringView(out).mid(copied, match.capturedStart(2) - copied);
        result += moved == formula ? match.captured(2) : escaped(moved);
        copied = match.capturedEnd(2);
    }
    result += QStringView(out).mid(copied);
    return result;
}

} // namespace

void StructureMath::moveKeptRuleFormulas(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<KeptRule> rules = book.sheetAt(s).keptRules();
        bool touched = false;
        for (KeptRule &rule : rules) {
            if (rule.ranges.isEmpty()) {
                continue;
            }
            const bool owner = s == edit.sheet;
            const CellAddr from = rule.ranges.first().topLeft;
            std::optional<CellAddr> to = from;
            if (owner) {
                to.reset();
                for (const CellRange &range : std::as_const(rule.ranges)) {
                    if ((to = survivingTopLeft(range, edit))) {
                        break;
                    }
                }
            }
            if (!to.has_value()) {
                continue; // the rule goes with its cells
            }
            Mover mover{edit, owner, editedSheetName, from, *to};
            const QString xml = movedXml(rule.xml, mover);
            if (xml != rule.xml || mover.stale != rule.staleFormulas) {
                rule.xml = xml;
                rule.staleFormulas = rule.staleFormulas || mover.stale;
                touched = true;
            }
        }
        if (touched) {
            book.sheetAt(s).setKeptRules(rules);
        }
    }
}

} // namespace QindaOffice::Calc
