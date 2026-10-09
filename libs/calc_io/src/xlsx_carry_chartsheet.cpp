// SPDX-License-Identifier: GPL-3.0-or-later
// Carry phase 4 (xlsx_carry_phases.h, M69, D-078): chart sheets.
//
// AGENT-NOTE: QXlsx can only write worksheets, and the post-pass names
// every sheet part by its position (xl/worksheets/sheet<N>.xml), so a chart
// sheet is written as an empty worksheet in its place first. Here the
// source chartsheet part is copied in whole — with its drawing, its chart
// and the chart's style parts, every byte as Excel wrote it (D-068 rules 2
// and 3) — and once every part is flushed the workbook relationship that
// named the placeholder is re-pointed at it and the placeholder goes. The
// chart's data references (<c:f>) are the only text that changes: they
// follow renamed sheets and inserted or removed rows and columns through
// the data sheets' edit journals, as a pivot cache's source does.

#include "xlsx_carry_phases.h"

#include "xlsx_carry_types.h"
#include "xlsx_carry_xml.h"
#include "xlsx_xml.h"

#include "lexer.h"
#include "parser.h"

#include <QObject>
#include <QRegularExpression>

namespace QindaOffice::Calc::IO::Carry {

namespace {

QString decoded(QString text)
{
    return text.replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&apos;"), QStringLiteral("'"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
}

QString encoded(QString text)
{
    return text.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

// A chart formula's sheet references through the edits since the load:
// Sheet1!$B$2:$B$13 names the sheet as it is called now and the cells where
// they went; a removed sheet or range is #REF!. Anything else — a constant,
// an external workbook's [1]Sheet1!A1 — stays as written.
QString movedReference(const Context &ctx, const QString &formula)
{
    const QVector<Token> tokens = lexFormula(formula);
    QString out;
    qsizetype copied = 0;
    for (qsizetype i = 0; i + 1 < tokens.size(); ++i) {
        const Token &sheet = tokens.at(i);
        const Token &ref = tokens.at(i + 1);
        const bool qualified = sheet.type == Token::Type::SheetName || sheet.type == Token::Type::QuotedSheetName;
        const bool external = i > 0
            && (tokens.at(i - 1).type == Token::Type::StructRef
                || (tokens.at(i - 1).type == Token::Type::Op && tokens.at(i - 1).text == QLatin1String("]")));
        if (!qualified || ref.type != Token::Type::Ref || external) {
            continue;
        }
        const SourceSheet *source = nullptr;
        for (const SourceSheet &candidate : ctx.sourceSheets()) {
            if (candidate.name.compare(sheet.text, Qt::CaseInsensitive) == 0) {
                source = &candidate;
            }
        }
        if (source == nullptr) {
            continue;
        }
        const DestSheet *dest = ctx.destForSource(source->part);
        const ParseResult parsed = parseFormula(ref.text);
        QString moved = QStringLiteral("#REF!");
        if (dest != nullptr && dest->patch != nullptr && parsed.root && parsed.root->kind == Node::Kind::Ref) {
            RangeRef range = parsed.root->ref;
            if (const auto mapped = mapSourceRange(dest->patch->source.steps, range.range)) {
                if (*mapped == range.range && dest->name == sheet.text) {
                    ++i; // nothing moved: the author's spelling stays, byte for byte
                    continue;
                }
                range.range = *mapped;
                range.sheet = dest->name;
                moved = rangeRefText(range);
            }
        }
        const qsizetype end = ref.pos + ref.text.size();
        if (moved != formula.mid(sheet.pos, end - sheet.pos)) {
            out += QStringView(formula).mid(copied, sheet.pos - copied);
            out += moved;
            copied = end;
        }
        ++i;
    }
    out += QStringView(formula).mid(copied);
    return out;
}

// Every <c:f> (and c15:f) of a chart part through movedReference; the
// bytes are untouched when nothing moved.
QByteArray movedChart(const Context &ctx, const QByteArray &xml)
{
    static const QRegularExpression kFormula(QStringLiteral("<((?:[\\w.-]+:)?f)>([^<]*)</\\1>"));
    const QString text = QString::fromUtf8(xml);
    QString out;
    qsizetype copied = 0;
    bool changed = false;
    auto it = kFormula.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString formula = decoded(match.captured(2));
        const QString moved = movedReference(ctx, formula);
        if (moved == formula) {
            continue;
        }
        out += QStringView(text).mid(copied, match.capturedStart(2) - copied);
        out += encoded(moved);
        copied = match.capturedEnd(2);
        changed = true;
    }
    if (!changed) {
        return xml;
    }
    out += QStringView(text).mid(copied);
    return out.toUtf8();
}

} // namespace

void carryChartSheets(Context &ctx, QList<ChartSheetSwap> *swaps)
{
    for (const DestSheet &dest : ctx.destSheets()) {
        if (dest.patch == nullptr || !dest.patch->chartSheet) {
            continue;
        }
        const QString source = dest.patch->source.part;
        if (source.isEmpty() || !ctx.source().contains(source)) {
            ctx.warn(QObject::tr("The chart sheet “%1” could not be kept: it was saved as an empty sheet.")
                         .arg(dest.name));
            continue;
        }
        const QString copied = ctx.copyPart(source);
        for (const QString &drawing : sourceTargets(ctx.source(), source, QStringLiteral("drawing"))) {
            for (const QString &chart : sourceTargets(ctx.source(), drawing, QStringLiteral("chart"))) {
                const QString destChart = ctx.copyPart(chart);
                const QByteArray data = ctx.copiedData(destChart);
                const QByteArray moved = movedChart(ctx, data);
                if (moved != data) {
                    ctx.replaceCopied(destChart, moved);
                }
            }
        }
        swaps->append({dest.part, copied, dest.relId});
    }
}

bool finishChartSheets(QHash<QString, QByteArray> *parts, const QList<ChartSheetSwap> &swaps, QString *error)
{
    if (swaps.isEmpty()) {
        return true;
    }
    const QString workbook = QStringLiteral("xl/workbook.xml");
    const QString relsPath = relsPathFor(workbook);
    QByteArray rels = parts->value(relsPath);
    QByteArray types = parts->value(QStringLiteral("[Content_Types].xml"));
    for (const ChartSheetSwap &swap : swaps) {
        bool found = false;
        rels = editStartTags(rels, QStringLiteral("Relationship"), [&swap, &found, &workbook](const QString &tag) {
            if (tagAttribute(tag, QStringLiteral("Id")) != swap.relId) {
                return tag;
            }
            found = true;
            const QString typed = withTagAttribute(
                tag, QStringLiteral("Type"),
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chartsheet"));
            return withTagAttribute(typed, QStringLiteral("Target"), Ooxml::relativeTargetFor(workbook, swap.chartSheet));
        });
        if (!found) {
            if (error != nullptr) {
                *error = QStringLiteral("carry: no workbook relationship %1 for a chart sheet").arg(swap.relId);
            }
            return false;
        }
        parts->remove(swap.placeholder);
        parts->remove(relsPathFor(swap.placeholder));
        const QRegularExpression override(QStringLiteral("<Override\\b[^>]*PartName=\"/%1\"[^>]*/>")
                                              .arg(QRegularExpression::escape(swap.placeholder)));
        types = QString::fromUtf8(types).remove(override).toUtf8();
    }
    parts->insert(relsPath, rels);
    parts->insert(QStringLiteral("[Content_Types].xml"), types);
    return true;
}

} // namespace QindaOffice::Calc::IO::Carry
