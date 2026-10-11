// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QTextCharFormat>
#include <QTextCursor>
namespace QindaOffice::Apps::Write {
// Derives the format for ordinary typed text, removing the identity of
// equation/comment/note objects at the caret. Does not mutate the document.
[[nodiscard]] QTextCharFormat typingFormat(const QTextCursor &cursor);
}
