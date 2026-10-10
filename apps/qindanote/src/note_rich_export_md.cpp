// SPDX-License-Identifier: GPL-3.0-or-later
// RichExport's Markdown half: GitHub Markdown with the page vocabulary
// (note_markdown_style.h), so an exported page reads back through Import ▸
// Markdown (note_rich_import.h) with its formatting and To Do boxes.

#include "note_rich_export.h"

#include "note_rich_style.h"
#include "note_rich_text.h"
#include "note_tags.h"

#include <qindatk/rich_text_document.h>

#include <QRegularExpression>

namespace QindaOffice::Apps::Notes::RichExport {

using namespace QindaTK::RichText;

namespace {

struct Look {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool code = false;
    bool highlight = false;
    bool sup = false;
    bool sub = false;
    QString href;
    friend bool operator==(const Look &, const Look &) = default;
};

Look lookOf(const QindaTK::RichTextDocument &doc, const CharFormat &direct)
{
    // The run's own look and its character style's — not the paragraph
    // style's (a Quote's italic is the "> ", not "*…*").
    const CharFormat f = effectiveCharFormat(doc.styleSheet(), direct, ParagraphFormat{});
    Look look;
    look.bold = f.weight >= 600;
    look.italic = f.italic == 1;
    look.underline = f.underline == 1;
    look.strike = f.strike == 1;
    look.code = f.family == QLatin1String("monospace") || f.family.contains(QLatin1String("Mono"))
        || f.family == QLatin1String("Consolas") || f.family.startsWith(QLatin1String("Courier"));
    look.highlight = f.background.isValid();
    look.sup = f.verticalAlign == VerticalAlign::Superscript;
    look.sub = f.verticalAlign == VerticalAlign::Subscript;
    look.href = f.href;
    return look;
}

QString escaped(const QString &text, bool inTable)
{
    QString out;
    for (const QChar c : text) {
        if (QStringLiteral("\\`*_[]~<>").contains(c) || (inTable && c == QLatin1Char('|'))) {
            out += QLatin1Char('\\');
        }
        out += c;
    }
    return out;
}

// Markers go INSIDE the run's outer whitespace ("** bold**" is no bold).
QString wrapped(const QString &text, const QString &open, const QString &close)
{
    const QString inner = text.trimmed();
    if (inner.isEmpty()) {
        return text;
    }
    const qsizetype lead = text.indexOf(inner);
    return text.left(lead) + open + inner + close + text.mid(lead + inner.size());
}

QString inlineMarkdown(const QindaTK::RichTextDocument &doc, const Paragraph &p, bool inTable)
{
    struct Run {
        Look look;
        QString text;
    };
    QList<Run> runs;
    int start = 0;
    for (const Span &span : p.spans) {
        const Look look = lookOf(doc, doc.charFormat(span.format));
        QString text = p.text.mid(start, span.length);
        start += span.length;
        text.remove(QChar(ObjectChar)); // pictures have no Markdown here
        if (text.isEmpty()) {
            continue;
        }
        if (!runs.isEmpty() && runs.last().look == look) {
            runs.last().text += text;
        } else {
            runs.append({look, text});
        }
    }
    QString out;
    for (const Run &run : std::as_const(runs)) {
        QString text = run.look.code ? run.text : escaped(run.text, inTable);
        text.replace(QChar(LineBreakChar), inTable ? QStringLiteral("<br>") : QStringLiteral("  \n"));
        if (run.look.code) {
            text = wrapped(text, QStringLiteral("`"), QStringLiteral("`"));
        } else {
            const bool spaced = text.trimmed().contains(QLatin1Char(' '));
            if (run.look.sup) {
                text = spaced ? wrapped(text, QStringLiteral("<sup>"), QStringLiteral("</sup>"))
                              : wrapped(text, QStringLiteral("^"), QStringLiteral("^"));
            }
            if (run.look.sub) {
                text = spaced ? wrapped(text, QStringLiteral("<sub>"), QStringLiteral("</sub>"))
                              : wrapped(text, QStringLiteral("~"), QStringLiteral("~"));
            }
            if (run.look.italic) {
                text = wrapped(text, QStringLiteral("*"), QStringLiteral("*"));
            }
            if (run.look.bold) {
                text = wrapped(text, QStringLiteral("**"), QStringLiteral("**"));
            }
            if (run.look.strike) {
                text = wrapped(text, QStringLiteral("~~"), QStringLiteral("~~"));
            }
            if (run.look.underline && run.look.href.isEmpty()) {
                text = wrapped(text, QStringLiteral("++"), QStringLiteral("++"));
            }
            if (run.look.highlight) {
                text = wrapped(text, QStringLiteral("=="), QStringLiteral("=="));
            }
        }
        if (!run.look.href.isEmpty()) {
            text = QStringLiteral("[%1](%2)").arg(text, run.look.href);
        }
        out += text;
    }
    return out;
}

QString cellMarkdown(const QindaTK::RichTextDocument &doc, const TableCell &cell)
{
    QStringList parts;
    for (const ParagraphPtr &p : cell.paragraphs) {
        parts.append(inlineMarkdown(doc, *p, true));
    }
    return parts.join(QStringLiteral("<br>"));
}

QString tableMarkdown(const QindaTK::RichTextDocument &doc, const Table &t)
{
    QStringList lines;
    for (int r = 0; r < t.rows.size(); ++r) {
        QStringList cells;
        for (const TableCell &cell : t.rows.at(r).cells) {
            cells.append(cellMarkdown(doc, cell));
        }
        lines.append(QStringLiteral("| %1 |").arg(cells.join(QStringLiteral(" | "))));
        if (r == 0) {
            lines.append(QStringLiteral("|%1").arg(QStringLiteral(" --- |").repeated(int(cells.size()))));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

QString badge(const QString &name)
{
    QString id = name.simplified();
    id.replace(QLatin1Char(' '), QLatin1Char('-'));
    static const QRegularExpression unsafe(QStringLiteral("[^\\w-]"));
    id.remove(unsafe);
    return QStringLiteral("[#%1]").arg(id);
}

QString markdown(const QJsonObject &block)
{
    if (!block.contains(QStringLiteral("rich"))) {
        return block.value(QStringLiteral("text")).toString();
    }
    const std::unique_ptr<QindaTK::RichTextDocument> doc = RichBlock::forBlock(block);
    const QJsonArray tags = block.value(QStringLiteral("tags")).toArray();
    struct Chunk {
        QString text;
        bool item = false; // list items and To Do lines stay together
    };
    QList<Chunk> chunks;
    QStringList code;
    const auto flushCode = [&] {
        if (!code.isEmpty()) {
            chunks.append({QStringLiteral("```\n%1\n```").arg(code.join(QLatin1Char('\n'))), false});
            code.clear();
        }
    };
    for (const Block &body : doc->blocks()) {
        if (body.table) {
            flushCode();
            chunks.append({tableMarkdown(*doc, *body.table), false});
            continue;
        }
        const Paragraph &p = *body.paragraph;
        const RichStyle::Style style = RichStyle::at(*doc, {p.id, 0});
        if (style == RichStyle::Style::Code) {
            QString text = p.text;
            text.remove(QChar(ObjectChar));
            code.append(text.replace(QChar(LineBreakChar), QLatin1Char('\n')));
            continue;
        }
        flushCode();
        const QString id = p.id.toString(QUuid::WithoutBraces);
        QString box;
        QStringList badges;
        for (const QJsonValue &value : tags) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("p")).toString() != id) {
                continue;
            }
            const Tags::TagDef def = Tags::fromEntry(entry);
            if (def.todo) {
                box = entry.value(QStringLiteral("done")).toBool() ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ");
            } else if (def.isValid()) {
                badges.append(badge(def.name));
            }
        }
        QString text = inlineMarkdown(*doc, p, false);
        if (!badges.isEmpty()) {
            text += QLatin1Char(' ') + badges.join(QLatin1Char(' '));
        }
        if (text.trimmed().isEmpty() && box.isEmpty()) {
            continue;
        }
        if (!p.format.listId.isEmpty()) {
            const ListDefinition *list = doc->list(p.format.listId);
            const bool ordered = list != nullptr && p.format.listLevel < list->levels.size()
                && isListKindOrdered(list->levels.at(p.format.listLevel).kind);
            const QString marker = !box.isEmpty() ? box : ordered ? QStringLiteral("1. ") : QStringLiteral("- ");
            chunks.append({QStringLiteral("  ").repeated(p.format.listLevel) + marker + text, true});
        } else if (!box.isEmpty()) {
            chunks.append({box + text, true});
        } else if (style == RichStyle::Style::Title) {
            chunks.append({QStringLiteral("# ") + text, false});
        } else if (style == RichStyle::Style::Quote) {
            chunks.append({QStringLiteral("> ") + text, false});
        } else if (p.format.headingLevel > 0) {
            chunks.append({QStringLiteral("#").repeated(qMin(p.format.headingLevel, 6)) + QLatin1Char(' ') + text, false});
        } else {
            chunks.append({text, false});
        }
    }
    flushCode();
    QString out;
    for (qsizetype i = 0; i < chunks.size(); ++i) {
        if (i > 0) {
            out += chunks.at(i).item && chunks.at(i - 1).item ? QStringLiteral("\n") : QStringLiteral("\n\n");
        }
        out += chunks.at(i).text;
    }
    return out;
}

} // namespace QindaOffice::Apps::Notes::RichExport
