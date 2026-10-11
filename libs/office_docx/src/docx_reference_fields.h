// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "docx_properties.h"
#include <QTextDocument>
#include <QVariantMap>
namespace QindaOffice::Docx {
// AGENT-CONTRACT (M94, D-102): reader, writer and Write share these owned
// values. A field stack is outermost first. Each occurrence has its own id;
// its cached result can cross paragraph boundaries or contain nested fields.
inline constexpr int kReferenceFields = kDocxBase + 180; // QVariantList of maps
inline constexpr int kBookmarkRanges = kDocxBase + 181; // QStringList
inline constexpr int kTocOptions = kDocxBase + 182; // block QVariantMap
inline constexpr int kEmptyReference = kDocxBase + 184; // zero-width cached result
struct ReferenceSpan {
    QVariantMap field;
    int start = 0;
    int end = 0; // exclusive
    int depth = 0;
};
struct BookmarkRange { int start = 0; int end = 0; };
[[nodiscard]] QStringList fieldTokens(const QString &code);
[[nodiscard]] QString referenceType(const QString &code);
[[nodiscard]] QVariantMap referenceField(const QString &code, const QString &beginXml = QString());
[[nodiscard]] QList<ReferenceSpan> referenceSpans(const QTextDocument &document);
[[nodiscard]] QMap<QString, BookmarkRange> bookmarkRanges(const QTextDocument &document);
// Remove identity from newly typed text, not from edits inside a result.
void clearReferenceFormat(QTextCharFormat &format);
} // namespace QindaOffice::Docx
