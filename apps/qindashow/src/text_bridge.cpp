// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_bridge.h"

#include "shape_json_parts.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QTextBlock>
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

QTextDocument *documentFromParagraphs(const QList<ShowModel::TextParagraph> &paragraphs, QObject *parent)
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
        for (const ShowModel::TextRun &run : paragraph.runs) {
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
            QString text = run.text;
            text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
            cursor.insertText(text, format);
        }
    }
    return document;
}

} // namespace QindaOffice::Apps::Show
