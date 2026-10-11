// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_bridge.h"

#include "shape_json_parts.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <cmath>
#include <optional>

namespace QindaOffice::Apps::Show {

namespace {

// The paragraph / run as the model had it, minus what the editor shows
// (M62): bullets, spacing, indents, underline, strike, baseline, caps,
// highlight, hyperlinks and fields survive an edit untouched.
constexpr int kParagraphStash = QTextFormat::UserProperty + 62;
constexpr int kRunStash = QTextFormat::UserProperty + 63;

QString stashParagraph(ShowModel::TextParagraph paragraph)
{
    paragraph.runs.clear();
    return QString::fromUtf8(QJsonDocument(ShowModel::Json::paragraphs({paragraph})).toJson(QJsonDocument::Compact));
}

QString stashRun(ShowModel::TextRun run)
{
    run.text.clear();
    ShowModel::TextParagraph holder;
    holder.runs = {run};
    return QString::fromUtf8(QJsonDocument(ShowModel::Json::paragraphs({holder})).toJson(QJsonDocument::Compact));
}

std::optional<ShowModel::TextParagraph> unstash(const QVariant &value)
{
    const QJsonArray array = QJsonDocument::fromJson(value.toString().toUtf8()).array();
    if (array.isEmpty()) {
        return std::nullopt;
    }
    return ShowModel::Json::paragraphs(array).first();
}

// AGENT-NOTE: the editor's TextEdit renders point sizes at the screen's
// logical dpi inside slide-point coordinates, while the renderer lays text
// out at 72 dpi (text_painter.h). Scaling the editor's sizes by 72/dpi
// shows the text at the size the slide paints it.
qreal screenScale()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen != nullptr && screen->logicalDotsPerInchY() > 0 ? screen->logicalDotsPerInchY() : 96.0;
    return dpi / 72.0;
}

QTextCharFormat charFormat(const ShowModel::TextRun &run, qreal scale);

} // namespace

QList<ShowModel::TextParagraph> paragraphsFromDocument(const QTextDocument &document)
{
    const qreal scale = screenScale();
    QList<ShowModel::TextParagraph> out;
    for (QTextBlock block = document.begin(); block != document.end(); block = block.next()) {
        const QTextBlockFormat blockFormat = block.blockFormat();
        // A block made by Enter carries its neighbour's stash: the new
        // paragraph continues the list, as PowerPoint's does.
        ShowModel::TextParagraph paragraph = unstash(blockFormat.property(kParagraphStash)).value_or(ShowModel::TextParagraph());
        paragraph.runs.clear();
        // QTextBlockFormat reports an unset alignment as AlignLeft: the
        // model's 0 (the default, left) is the same alignment.
        const auto horizontal = [](int a) {
            a &= int(Qt::AlignHorizontal_Mask);
            return a == 0 ? int(Qt::AlignLeft) : a;
        };
        const int alignment = int(blockFormat.alignment());
        if (horizontal(alignment) != horizontal(paragraph.alignment)) {
            paragraph.alignment = alignment;
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat format = fragment.charFormat();
            std::optional<ShowModel::TextParagraph> holder = unstash(format.property(kRunStash));
            ShowModel::TextRun run = holder && !holder->runs.isEmpty()
                ? holder->runs.first()
                : (paragraph.runs.isEmpty() ? ShowModel::TextRun() : paragraph.runs.last());
            run.text = fragment.text();
            run.bold = format.fontWeight() >= QFont::Bold;
            run.italic = format.fontItalic();
            const QColor shown = format.foreground().color();
            if (!holder || shown != (run.color.isValid() ? run.color : QColor(Qt::black))) {
                run.color = shown;
            }
            const qreal size = format.fontPointSize() > 0 ? format.fontPointSize() * scale : 18.0;
            if (!holder || qAbs(size - run.pointSize) > 0.05) {
                run.pointSize = std::round(size * 100.0) / 100.0;
            }
            // Qt 6.11's deprecated fontFamily() calls fontFamilies().first()
            // and aborts on an unset family; read the list defensively.
            const QStringList families = format.fontFamilies().toStringList();
            if (!families.isEmpty()) {
                run.fontFamily = families.first();
            } else if (!holder) {
                run.fontFamily.clear();
            }
            if (format.fontUnderline() != !run.underline.isEmpty()) {
                run.underline = format.fontUnderline() ? QStringLiteral("sng") : QString();
            }
            if (format.fontStrikeOut() != !run.strike.isEmpty()) {
                run.strike = format.fontStrikeOut() ? QStringLiteral("sngStrike") : QString();
            }
            paragraph.runs.append(run);
        }
        out.append(paragraph);
    }
    return out;
}

namespace {

QTextCharFormat charFormat(const ShowModel::TextRun &run, qreal scale)
{
    QTextCharFormat format;
    format.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(run.italic);
    format.setForeground(run.color.isValid() ? run.color : QColor(Qt::black));
    format.setFontPointSize(run.pointSize / scale);
    format.setFontUnderline(!run.underline.isEmpty());
    format.setFontStrikeOut(!run.strike.isEmpty());
    if (!run.fontFamily.isEmpty()) {
        format.setFontFamilies(QStringList{run.fontFamily});
    }
    format.setProperty(kRunStash, stashRun(run));
    return format;
}

} // namespace

QTextDocument *documentFromParagraphs(const QList<ShowModel::TextParagraph> &paragraphs, QObject *parent,
                                      const ShowModel::TextRun *emptyLook)
{
    const qreal scale = screenScale();
    auto *document = new QTextDocument(parent);
    QTextCursor cursor(document);
    bool first = true;
    for (const ShowModel::TextParagraph &paragraph : paragraphs) {
        QTextBlockFormat blockFormat;
        blockFormat.setAlignment(Qt::Alignment(paragraph.alignment));
        blockFormat.setLeftMargin(paragraph.marginLeft);
        blockFormat.setTextIndent(paragraph.bullet ? 0 : paragraph.indent);
        blockFormat.setProperty(kParagraphStash, stashParagraph(paragraph));
        if (first) {
            cursor.setBlockFormat(blockFormat);
            first = false;
        } else {
            cursor.insertBlock(blockFormat);
        }
        // M67: an empty paragraph types in the look it was given (a table
        // cell's region look, text_bridge.h).
        if (paragraph.runs.isEmpty() && emptyLook != nullptr) {
            const QTextCharFormat format = charFormat(*emptyLook, scale);
            cursor.setBlockCharFormat(format);
            cursor.setCharFormat(format);
        }
        for (const ShowModel::TextRun &run : paragraph.runs) {
            QString text = run.text;
            text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
            cursor.insertText(text, charFormat(run, scale));
        }
    }
    return document;
}

void setHyperlink(QTextDocument &document, int from, int to, const QString &link, const QColor &color)
{
    // The pieces first (one format each), then the changes: editing while
    // walking the fragments would invalidate them.
    struct Piece {
        int start;
        int end;
        QTextCharFormat format;
    };
    QList<Piece> pieces;
    for (QTextBlock block = document.begin(); block != document.end(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int start = qMax(from, fragment.position());
            const int end = qMin(to, fragment.position() + fragment.length());
            if (fragment.isValid() && start < end) {
                pieces.append({start, end, fragment.charFormat()});
            }
        }
    }
    const qreal scale = screenScale();
    for (const Piece &piece : std::as_const(pieces)) {
        std::optional<ShowModel::TextParagraph> holder = unstash(piece.format.property(kRunStash));
        ShowModel::TextRun run = holder && !holder->runs.isEmpty() ? holder->runs.first() : ShowModel::TextRun();
        if (!holder) {
            run.pointSize = piece.format.fontPointSize() > 0 ? piece.format.fontPointSize() * scale : run.pointSize;
            run.color = piece.format.foreground().color();
        }
        run.hyperlink = link;
        QTextCharFormat format = piece.format;
        if (!link.isEmpty()) {
            run.underline = QStringLiteral("sng");
            run.color = color;
            format.setFontUnderline(true);
            format.setForeground(color);
        }
        format.setProperty(kRunStash, stashRun(run));
        QTextCursor cursor(&document);
        cursor.setPosition(piece.start);
        cursor.setPosition(piece.end, QTextCursor::KeepAnchor);
        cursor.setCharFormat(format);
    }
}

QString hyperlinkAt(const QTextDocument &document, int position)
{
    const QTextBlock block = document.findBlock(position);
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.isValid() && fragment.contains(position)) {
            const std::optional<ShowModel::TextParagraph> holder = unstash(fragment.charFormat().property(kRunStash));
            return holder && !holder->runs.isEmpty() ? holder->runs.first().hyperlink : QString();
        }
    }
    return {};
}

} // namespace QindaOffice::Apps::Show
