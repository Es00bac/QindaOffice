// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QTextCharFormat>
#include <QTextCursor>
namespace QindaOffice::Apps::Write {
// Derives the format for ordinary typed text, removing the identity of
// equation/comment/note objects at the caret. Does not mutate the document.
[[nodiscard]] QTextCharFormat typingFormat(const QTextCursor &cursor);
// Sanitize an explicitly chosen format for newly inserted ordinary text.
// Keeps its text styling (for example an italic caption or an edited citation)
// while removing object identity; cursor supplies paragraph/table context.
[[nodiscard]] QTextCharFormat typingFormat(const QTextCursor &cursor, QTextCharFormat format);
}
