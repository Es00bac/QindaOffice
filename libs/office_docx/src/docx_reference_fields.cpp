// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_reference_fields.h"
#include <QTextBlock>
#include <QUuid>
namespace QindaOffice::Docx {
QStringList fieldTokens(const QString &code)
{
    QStringList out; QString token; bool quoted = false;
    for (qsizetype i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('"')) quoted = !quoted;
        else if (c.isSpace() && !quoted) { if (!token.isEmpty()) { out.append(token); token.clear(); } }
        else token += c;
    }
    if (!token.isEmpty()) out.append(token);
    return out;
}
QString referenceType(const QString &code) { return fieldTokens(code).value(0).toUpper(); }
QVariantMap referenceField(const QString &code, const QString &beginXml)
{
    return {{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::Id128)},
            {QStringLiteral("code"), code.trimmed()}, {QStringLiteral("beginXml"), beginXml}};
}
QList<ReferenceSpan> referenceSpans(const QTextDocument &document)
{
    QList<ReferenceSpan> out; QHash<QString, qsizetype> indices;
    for (auto block = document.begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto f = it.fragment(); if (!f.isValid()) continue;
        const auto fields = f.charFormat().property(kReferenceFields).toList();
        for (qsizetype d = 0; d < fields.size(); ++d) {
            const auto field = fields.at(d).toMap(); const QString id = field.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) continue;
            const int previousEnd = indices.contains(id) ? out.at(indices.value(id)).end : -1;
            const bool adjacent = previousEnd == f.position() || (previousEnd + 1 == f.position() && document.characterAt(previousEnd) == QChar::ParagraphSeparator);
            // A copied result can retain its format id. A gap in that id's
            // result starts another occurrence, never a field spanning all
            // ordinary text between two copied records.
            if (!indices.contains(id) || !adjacent) { indices.insert(id, out.size()); out.append({field, f.position(), f.position() + f.length(), int(d)}); }
            else out[indices.value(id)].end = f.position() + f.length();
        }
    }
    return out;
}
QMap<QString, BookmarkRange> bookmarkRanges(const QTextDocument &document)
{
    QMap<QString, BookmarkRange> out;
    for (auto block = document.begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto f = it.fragment(); if (!f.isValid()) continue;
        for (const QString &name : f.charFormat().property(kBookmarkRanges).toStringList()) {
            if (!out.contains(name)) out.insert(name, {f.position(), f.position() + f.length()});
            else out[name].end = f.position() + f.length();
        }
    }
    return out;
}
void clearReferenceFormat(QTextCharFormat &format)
{
    format.clearProperty(kReferenceFields); format.clearProperty(kBookmarkRanges);
    format.clearProperty(kFieldInstruction); format.clearProperty(kEmptyReference);
    format.setAnchorNames({}); format.setAnchorHref({}); format.setAnchor(false);
}
} // namespace QindaOffice::Docx
