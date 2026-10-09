// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a QindaWrite formatting command works on (M70, D-079): the
// paragraphs a selection touches. Shared by the Borders, Paragraph, Tabs
// and Font commands so they agree with each other and with Word: a
// selection that ends at the very start of a paragraph does not take that
// paragraph along.

#include <QList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

[[nodiscard]] inline QList<QTextBlock> selectedBlocks(const QTextCursor &cursor)
{
    QList<QTextBlock> out;
    const QTextDocument *document = cursor.document();
    if (document == nullptr) {
        return out;
    }
    QTextBlock block = document->findBlock(cursor.selectionStart());
    QTextBlock last = document->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && last.position() == cursor.selectionEnd() && last != block) {
        last = last.previous();
    }
    for (; block.isValid(); block = block.next()) {
        out.append(block);
        if (block == last) {
            break;
        }
    }
    return out;
}

} // namespace QindaOffice::Apps::Write
