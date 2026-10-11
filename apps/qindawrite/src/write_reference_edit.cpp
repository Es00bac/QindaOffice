// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_reference_edit.h"
#include "ooxml_namespaces.h"
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <algorithm>
namespace QindaOffice::Apps::Write {
namespace {
QString codeOf(const Docx::ReferenceSpan &span) { return span.field.value(QStringLiteral("code")).toString(); }
QString selected(const QTextDocument &document, int from, int to)
{
    QTextCursor c(const_cast<QTextDocument *>(&document)); c.setPosition(from); c.setPosition(to, QTextCursor::KeepAnchor);
    return c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n')).remove(QChar(0x200B));
}
QString switchValue(const QStringList &tokens, const QString &name)
{
    const qsizetype i = tokens.indexOf(name); return i < 0 ? QString() : tokens.value(i + 1);
}
}
bool referenceLocked(const QVariantMap &field)
{
    const auto begin = Ooxml::parseFragment(field.value(QStringLiteral("beginXml")).toString());
    if (!begin) return false;
    const QString value = begin->attribute(QString::fromUtf16(Ooxml::Ns::W), u"fldLock");
    return value == u"true" || value == u"1" || value == u"on";
}
bool simpleReferenceCode(const QString &code)
{
    const auto t = Docx::fieldTokens(code); const QString type = t.value(0).toUpper();
    if (type == u"BIBLIOGRAPHY") return t.size() == 1 || (t.size() == 3 && t.at(1) == u"\\l");
    if (t.size() < 2) return false;
    for (qsizetype i = 2; i < t.size(); ++i) {
        if ((type == u"REF" || type == u"PAGEREF") && t.at(i) == u"\\h") continue;
        if (t.at(i) == u"\\*" && t.value(i + 1).compare(QStringLiteral("MERGEFORMAT"), Qt::CaseInsensitive) == 0) { ++i; continue; }
        if (type == u"SEQ" && t.at(i) == u"\\*" && t.value(i + 1).compare(QStringLiteral("ARABIC"), Qt::CaseInsensitive) == 0) { ++i; continue; }
        if (type == u"SEQ" && t.at(i) == u"\\r" && t.value(i + 1).toInt() > 0) { ++i; continue; }
        if (type == u"CITATION" && (t.at(i) == u"\\l" || t.at(i) == u"\\p") && i + 1 < t.size()) { ++i; continue; }
        return false;
    }
    return type == u"CITATION" || type == u"SEQ" || type == u"REF" || type == u"PAGEREF";
}
QStringList citationOrder(const QTextDocument &document, const Docx::Bibliography &data)
{
    QStringList tags;
    for (const auto &span : Docx::referenceSpans(document)) {
        const auto tokens = Docx::fieldTokens(codeOf(span));
        if (tokens.value(0).compare(QStringLiteral("CITATION"), Qt::CaseInsensitive) == 0 && data.find(tokens.value(1)) && !tags.contains(tokens.value(1))) tags.append(tokens.value(1));
    }
    for (const auto &source : data.sources) if (!tags.contains(source.tag())) tags.append(source.tag());
    return tags;
}
QString bibliographyResult(const QTextDocument &document, const Docx::Bibliography &data)
{
    QStringList lines; const auto order = citationOrder(document, data);
    for (qsizetype i = 0; i < order.size(); ++i) if (const auto *source = data.find(order.at(i)))
        lines.append(Docx::bibliographyText(*source, data.style, int(i + 1)));
    return lines.join(QLatin1Char('\n'));
}
void insertReference(QTextCursor &cursor, const QString &code, const QString &result, QTextCharFormat format)
{
    Docx::clearReferenceFormat(format);
    format.setProperty(Docx::kReferenceFields, QVariantList{Docx::referenceField(code)});
    format.setProperty(Docx::kFieldInstruction, code);
    if (result.isEmpty()) format.setProperty(Docx::kEmptyReference, true);
    cursor.insertText(result.isEmpty() ? QString(QChar(0x200B)) : result, format);
}
void replaceReference(QTextDocument &document, const Docx::ReferenceSpan &span, const QString &text)
{
    QTextCursor c(&document); c.setPosition(span.start); c.setPosition(span.start + 1, QTextCursor::KeepAnchor);
    auto format = c.charFormat(); format.setProperty(Docx::kEmptyReference, text.isEmpty());
    c.setPosition(span.start); c.setPosition(span.end, QTextCursor::KeepAnchor);
    c.insertText(text.isEmpty() ? QString(QChar(0x200B)) : text, format);
}
int refreshReferences(QTextDocument &document, const Docx::Bibliography &data, qreal pageHeight, bool pagesOnly)
{
    int changed = 0;
    // Sequence/citation lengths move bookmark offsets; derive cross-references
    // in a second pass. Descending edits keep each pass's positions valid.
    for (int pass = pagesOnly ? 1 : 0; pass < 2; ++pass) {
        auto spans = Docx::referenceSpans(document); const auto bookmarks = Docx::bookmarkRanges(document);
        const auto order = citationOrder(document, data); QHash<QString, int> sequences;
        QList<QPair<Docx::ReferenceSpan, QString>> edits;
        for (const auto &span : spans) {
            const QString code = codeOf(span); const auto t = Docx::fieldTokens(code); const QString type = t.value(0).toUpper();
            if (!simpleReferenceCode(code) || (referenceLocked(span.field) && type != u"SEQ")) continue;
            QString value; bool supported = false;
            if (pass == 0 && type == u"SEQ") {
                int &number = sequences[t.value(1)];
                const QString restart = switchValue(t, QStringLiteral("\\r")); number = restart.isEmpty() ? number + 1 : restart.toInt();
                value = QString::number(number); supported = !referenceLocked(span.field);
            } else if (pass == 0 && data.style != u"preserved" && type == u"CITATION") {
                const auto *source = data.find(t.value(1));
                value = source ? Docx::citationText(*source, data.style, int(order.indexOf(source->tag()) + 1), switchValue(t, QStringLiteral("\\p"))) : QStringLiteral("Error! Source not found.");
                supported = true;
            } else if (pass == 0 && data.style != u"preserved" && type == u"BIBLIOGRAPHY") {
                value = bibliographyResult(document, data); supported = true;
            } else if (pass == 1 && (type == u"REF" || type == u"PAGEREF")) {
                const auto target = bookmarks.constFind(t.value(1)); supported = true;
                if (target == bookmarks.cend()) value = QStringLiteral("Error! Reference source not found.");
                else if (type == u"PAGEREF") value = QString::number(1 + qMax(0, int(document.documentLayout()->blockBoundingRect(document.findBlock(target->start)).top() / qMax(qreal(1), pageHeight))));
                else if (!pagesOnly) {
                    value = selected(document, target->start, target->end);
                } else supported = false;
            }
            if (supported && selected(document, span.start, span.end) != value) edits.append({span, value});
        }
        std::sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) { return a.first.start > b.first.start; });
        for (const auto &[span, text] : edits) { replaceReference(document, span, text); ++changed; }
    }
    return changed;
}
QVariantList referenceTargets(const QTextDocument &document)
{
    QVariantList out;
    for (auto ranges = Docx::bookmarkRanges(document); const auto &name : ranges.keys()) {
        if (name.startsWith(u"_QindaNumber_")) continue;
        const auto range = ranges.value(name);
        out.append(QVariantMap{{QStringLiteral("bookmark"), name}, {QStringLiteral("text"), selected(document, range.start, range.end).left(160)}});
    }
    return out;
}
bool followReference(QTextCursor &cursor, const QString &bookmark)
{
    const auto ranges = Docx::bookmarkRanges(*cursor.document());
    if (ranges.contains(bookmark)) { cursor.setPosition(ranges.value(bookmark).start); return true; }
    for (auto b = cursor.document()->begin(); b.isValid(); b = b.next()) for (auto it = b.begin(); !it.atEnd(); ++it)
        if (it.fragment().charFormat().anchorNames().contains(bookmark)) { cursor.setPosition(it.fragment().position()); return true; }
    return false;
}
} // namespace QindaOffice::Apps::Write
