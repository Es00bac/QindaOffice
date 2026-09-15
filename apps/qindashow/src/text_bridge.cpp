// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_bridge.h"

#include <QTextBlock>
#include <QTextDocument>

namespace QindaOffice::Apps::Show {

QList<ShowModel::TextParagraph> paragraphsFromDocument(const QTextDocument &document)
{

    QList<ShowModel::TextParagraph> out;
    for (QTextBlock block = document.begin(); block != document.end(); block = block.next()) {
        ShowModel::TextParagraph paragraph;
        paragraph.alignment = int(block.blockFormat().alignment());
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat format = fragment.charFormat();
            ShowModel::TextRun run;
            run.text = fragment.text();
            run.bold = format.fontWeight() >= QFont::Bold;
            run.italic = format.fontItalic();
            run.color = format.foreground().color();
            run.pointSize = format.fontPointSize() > 0 ? qRound(format.fontPointSize()) : 18;
            // Qt 6.11's deprecated fontFamily() calls fontFamilies().first()
            // and aborts on an unset family; read the list defensively.
            const QStringList families = format.fontFamilies().toStringList();
            run.fontFamily = families.isEmpty() ? QString() : families.first();
            paragraph.runs.append(run);
        }
        out.append(paragraph);
    }
    return out;
}


QTextDocument *documentFromParagraphs(const QList<ShowModel::TextParagraph> &paragraphs, QObject *parent)
{

    auto *document = new QTextDocument(parent);
    QTextCursor cursor(document);
    bool first = true;
    for (const ShowModel::TextParagraph &paragraph : paragraphs) {
        QTextBlockFormat blockFormat;
        blockFormat.setAlignment(Qt::Alignment(paragraph.alignment));
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
            format.setForeground(run.color);
            format.setFontPointSize(run.pointSize);
            if (!run.fontFamily.isEmpty()) {
                format.setFontFamilies(QStringList{run.fontFamily});
            }
            cursor.insertText(run.text, format);
        }
    }
    return document;
}


} // namespace QindaOffice::Apps::Show
