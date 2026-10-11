// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QTextCharFormat>
#include <QTextCursor>
class QTextDocument;
namespace QindaOffice::Docx {
inline constexpr int kMathObject = QTextFormat::UserObject + 1;
// Install once, before connecting to the document layout's signals. The
// specialized Qt text layout keeps normal paragraph/table/picture behavior
// and gives equations their real ascent and descent. GUI thread only.
void installMathLayout(QTextDocument *document);
[[nodiscard]] bool isEquation(const QTextCharFormat &format);
// Select an equation at/just before the caret, or retain the exact selected
// equation. Null cursor if none. This never modifies the document.
[[nodiscard]] QTextCursor equationAt(const QTextCursor &cursor);
// An object retaining native OMML and the existing preservation metadata.
[[nodiscard]] QTextCharFormat equationFormat(const QString &xml, const QTextCharFormat &surrounding,
                                             const QString &sourcePart = {}, int serial = 0);
}
