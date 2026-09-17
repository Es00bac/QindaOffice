// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_footnotes.h"

#include "page_geometry.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

namespace QindaOffice::Apps::Write {

namespace {

// The document-space y of a position, for the page lookup.
qreal documentYAt(const QTextDocument *document, int position)
{
    const QTextBlock block = document->findBlock(position);
    if (!block.isValid()) {
        return 0;
    }
    const QRectF rect = document->documentLayout()->blockBoundingRect(block);
    const QTextLayout *layout = block.layout();
    if (layout == nullptr) {
        return rect.top();
    }
    // Line-accurate, so a marker near a page break lands on the page it is
    // actually printed on rather than on the block's first page.
    const QTextLine line = layout->lineForTextPosition(position - block.position());
    return line.isValid() ? rect.top() + line.y() : rect.top();
}

} // namespace

QList<FootnoteRef> collectFootnotes(const QTextDocument *document, const PageGeometry &geometry)
{
    QList<FootnoteRef> refs;
    if (document == nullptr) {
        return refs;
    }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()
                || !fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
                continue;
            }
            FootnoteRef ref;
            ref.id = fragment.charFormat().property(kFootnoteIdProperty).toString();
            ref.position = fragment.position();
            ref.page = geometry.pageForDocY(documentYAt(document, ref.position)) + 1;
            // Numbering is document order, recomputed every time: inserting
            // a note in the middle renumbers the rest for free.
            ref.number = int(refs.size()) + 1;
            refs.append(ref);
        }
    }
    return refs;
}

QHash<int, QStringList> footnotesByPage(const QTextDocument *document,
                                        const PageGeometry &geometry,
                                        const QHash<QString, QString> &texts)
{
    QHash<int, QStringList> byPage;
    for (const FootnoteRef &ref : collectFootnotes(document, geometry)) {
        const QString text = texts.value(ref.id);
        if (text.isEmpty()) {
            continue;
        }
        byPage[ref.page].append(QStringLiteral("%1  %2").arg(ref.number).arg(text));
    }
    return byPage;
}

QString insertFootnoteMarker(QTextCursor cursor)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    QTextCharFormat marker;
    marker.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    marker.setProperty(kFootnoteIdProperty, id);
    // The visible glyph is a placeholder: collectFootnotes owns the real
    // number, and the view re-renders it from document order.
    cursor.insertText(QStringLiteral("*"), marker);
    // Stop the marker format bleeding into the next keystroke.
    QTextCharFormat plain;
    plain.setVerticalAlignment(QTextCharFormat::AlignNormal);
    plain.clearProperty(kFootnoteIdProperty);
    cursor.setCharFormat(plain);
    return id;
}

QString footnoteIdAt(const QTextDocument *document, int position)
{
    if (document == nullptr) {
        return {};
    }
    const QTextBlock block = document->findBlock(position);
    if (!block.isValid()) {
        return {};
    }
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid() || !fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
            continue;
        }
        // Inclusive of the character just after the marker, so clicking at
        // its right edge (where the caret lands) still finds it.
        if (position >= fragment.position()
            && position <= fragment.position() + fragment.length()) {
            return fragment.charFormat().property(kFootnoteIdProperty).toString();
        }
    }
    return {};
}

void renumberFootnoteMarkers(QTextDocument *document)
{
    if (document == nullptr) {
        return;
    }
    struct Marker {
        int position;
        int length;
        QString id;
        QString text;
    };
    QList<Marker> markers;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
                markers.append({fragment.position(), fragment.length(),
                                fragment.charFormat().property(kFootnoteIdProperty).toString(),
                                fragment.text()});
            }
        }
    }
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    // AGENT-GUARD: rewrite from the END backwards. Replacing a marker
    // changes the length of the text before every later position, so a
    // forward pass would edit at stale offsets after the first change.
    for (int i = int(markers.size()) - 1; i >= 0; --i) {
        const Marker &marker = markers.at(i);
        const QString wanted = QString::number(i + 1);
        if (marker.text == wanted) {
            continue;
        }
        cursor.setPosition(marker.position);
        cursor.setPosition(marker.position + marker.length, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        format.setProperty(kFootnoteIdProperty, marker.id);
        cursor.insertText(wanted, format);
    }
    cursor.endEditBlock();
}

void pruneOrphanFootnotes(const QTextDocument *document, QHash<QString, QString> *texts)
{
    if (document == nullptr || texts == nullptr) {
        return;
    }
    QSet<QString> live;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().hasProperty(kFootnoteIdProperty)) {
                live.insert(fragment.charFormat().property(kFootnoteIdProperty).toString());
            }
        }
    }
    // A note whose marker the user deleted would otherwise print forever
    // with no way to reach it.
    for (const QString &id : texts->keys()) {
        if (!live.contains(id)) {
            texts->remove(id);
        }
    }
}

} // namespace QindaOffice::Apps::Write
