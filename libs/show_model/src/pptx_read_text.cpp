// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_read_text.h"

namespace QindaOffice::Show::Pptx {

namespace {

const QString kHyperlinkExtension = QStringLiteral("http://schemas.microsoft.com/office/drawing/2018/hyperlinkcolor");

TextRun effectiveRun(const PartContext &ctx, const RunProps &props)
{
    TextRun run;
    run.pointSize = props.size.value_or(18.0);
    run.bold = props.bold.value_or(false);
    run.italic = props.italic.value_or(false);
    run.underline = props.underline.value_or(QString());
    run.strike = props.strike.value_or(QString());
    run.caps = props.caps.value_or(QString());
    run.baseline = props.baseline.value_or(0);
    run.spacing = props.spacing.value_or(0);
    run.color = props.color.value_or(QColor(Qt::black));
    run.highlight = props.highlight.value_or(QColor());
    const QString latin = props.latin.value_or(QString());
    run.fontFamily = latin.isEmpty() && ctx.theme != nullptr ? ctx.theme->theme.minorLatin : latin;
    return run;
}

// PowerPoint draws a hyperlink in the theme's hyperlink colour, underlined,
// unless the link says to keep the text colour (Office 2019+).
void applyHyperlinkLook(const PartContext &ctx, const Ooxml::XmlElement *hlink, TextRun &run)
{
    if (hlink == nullptr || run.hyperlink.isEmpty()) {
        return;
    }
    bool keepTextColour = false;
    if (const Ooxml::XmlElement *ext = hlink->descendant(kHyperlinkExtension, u"hlinkClr")) {
        keepTextColour = ext->attr(u"val") == u"tx";
    }
    if (!keepTextColour && ctx.theme != nullptr) {
        Ooxml::XmlElement scheme(nsA(), QStringLiteral("schemeClr"));
        scheme.setAttribute(QString(), QStringLiteral("val"), QStringLiteral("hlink"));
        const QColor c = color(ctx, &scheme);
        if (c.isValid()) {
            run.color = c;
        }
        if (run.underline.isEmpty()) {
            run.underline = QStringLiteral("sng");
        }
    }
}

void readRuns(const PartContext &ctx, const Ooxml::XmlElement &paragraph, const RunProps &base, QList<TextRun> &runs)
{
    for (const Ooxml::XmlElement *e : paragraph.elements()) {
        if (e->ns == nsMc() && e->name == u"AlternateContent") {
            // Equations and other extensions: the Fallback's runs carry the text.
            const Ooxml::XmlElement *fallback = e->child(nsMc(), u"Fallback");
            if (fallback != nullptr) {
                readRuns(ctx, *fallback, base, runs);
            }
            continue;
        }
        if (e->ns != nsA()) {
            continue;
        }
        const Ooxml::XmlElement *rPr = e->child(nsA(), u"rPr");
        RunProps props = base;
        props.overlay(parseRunProps(ctx, rPr));
        if (e->name == u"r" || e->name == u"fld") {
            TextRun run = effectiveRun(ctx, props);
            if (const Ooxml::XmlElement *t = e->child(nsA(), u"t")) {
                run.text = t->text();
            }
            if (e->name == u"fld") {
                run.fieldType = e->attr(u"type");
                run.fieldId = e->attr(u"id");
            }
            const Ooxml::XmlElement *hlink = rPr != nullptr ? rPr->child(nsA(), u"hlinkClick") : nullptr;
            run.hyperlink = hyperlinkFrom(ctx, hlink);
            applyHyperlinkLook(ctx, hlink, run);
            runs.append(run);
        } else if (e->name == u"br") {
            if (runs.isEmpty()) {
                TextRun run = effectiveRun(ctx, props);
                runs.append(run);
            }
            runs.last().text += QChar(QChar::LineSeparator);
        }
    }
}

} // namespace

QList<TextParagraph> readTextBody(const PartContext &ctx, const Ooxml::XmlElement *txBody, const ListStyle &inherited,
                                  const RunProps &styleRun)
{
    QList<TextParagraph> paragraphs;
    if (txBody == nullptr) {
        return paragraphs;
    }
    const ListStyle own = parseListStyle(ctx, txBody->child(nsA(), u"lstStyle"));
    for (const Ooxml::XmlElement *p : txBody->elements(nsA(), u"p")) {
        const Ooxml::XmlElement *pPr = p->child(nsA(), u"pPr");
        const int level = pPr != nullptr ? qBound(0, pPr->attr(u"lvl").toInt(), 8) : 0;
        ParaProps eff = inherited.levels[level];
        eff.run.overlay(styleRun);
        eff.overlay(own.levels[level]);
        eff.overlay(parseParaProps(ctx, pPr));

        TextParagraph paragraph;
        paragraph.listLevel = level;
        paragraph.alignment = eff.alignment.value_or(int(Qt::AlignLeft));
        paragraph.marginLeft = eff.marginLeft.value_or(0);
        paragraph.indent = eff.indent.value_or(0);
        paragraph.lineSpacing = eff.lineSpacing.value_or(Spacing::percent(100));
        paragraph.spaceBefore = eff.spaceBefore.value_or(Spacing::points(0));
        paragraph.spaceAfter = eff.spaceAfter.value_or(Spacing::points(0));
        const int kind = eff.bulletKind.value_or(0);
        paragraph.bullet = kind != 0;
        if (kind == 1) {
            paragraph.bulletChar = eff.bulletChar.value_or(QStringLiteral("•"));
        } else if (kind == 2) {
            paragraph.autoNumber = eff.autoNumber.value_or(QStringLiteral("arabicPeriod"));
            paragraph.autoNumberStart = eff.autoNumberStart.value_or(1);
        }
        if (paragraph.bullet) {
            paragraph.bulletFont = eff.bulletFont.value_or(QString());
            paragraph.bulletColor = eff.bulletColor.value_or(QColor());
        }
        readRuns(ctx, *p, eff.run, paragraph.runs);
        RunProps end = eff.run;
        end.overlay(parseRunProps(ctx, p->child(nsA(), u"endParaRPr")));
        paragraph.endPointSize = end.size.value_or(18.0);
        if (paragraph.bullet) {
            const qreal size = eff.bulletSize.value_or(100.0);
            if (size < 0) {
                // Absolute points: as a percentage of the paragraph's first run.
                const qreal lead = paragraph.runs.isEmpty() ? paragraph.endPointSize : paragraph.runs.first().pointSize;
                paragraph.bulletSize = lead > 0 ? -size / lead * 100.0 : 100.0;
            } else {
                paragraph.bulletSize = size;
            }
        }
        paragraphs.append(paragraph);
    }
    return paragraphs;
}

} // namespace QindaOffice::Show::Pptx
