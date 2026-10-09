// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to the .docx writer (docx_writer*.cpp): the package being built
// and the per-save bookkeeping every part writer shares.

#include "docx_model.h"
#include "docx_styles.h"
#include "ooxml_copier.h"
#include "ooxml_package.h"

#include <QHash>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

#include <functional>

namespace QindaOffice::Docx::detail {

// numId/ilvl decisions for the lists of a save (docx_writer_numbering.cpp).
class NumberingBuilder {
public:
    explicit NumberingBuilder(Ooxml::XmlElementPtr source);
    struct Ref {
        int numId = 0;
        int ilvl = 0;
    };
    // The numbering a QTextList is written with: its own when imported and
    // unchanged, otherwise a new definition built from its format.
    Ref refFor(const QTextList *list, qreal indentWidth);
    // numbering.xml content, or null when the document has no numbering.
    [[nodiscard]] Ooxml::XmlElementPtr result() const;

private:
    Ooxml::XmlElementPtr m_root;
    int m_nextAbstract = 0;
    int m_nextNum = 1;
    QHash<const QTextList *, Ref> m_refs;
};

struct WriteEnv {
    const WordParts *parts = nullptr;
    Ooxml::PackageWriter writer;
    std::unique_ptr<Ooxml::RelationshipCopier> copier;
    StyleSheet sourceStyles;
    std::unique_ptr<NumberingBuilder> numbering;
    QString sourceMain;         // main part of the source package, if any
    QString sourceFootnotesPart;
    QSet<QString> usedStyles;   // style ids the output references
    QHash<QByteArray, QString> mediaByHash; // content hash → part name
    QHash<QString, int> footnoteIds;        // note id → w:id
    QList<QString> footnoteOrder;
    int nextFootnoteId = 1;
    int nextDocPr = 1;
    int nextRevision = 1;
    int nextBookmark = 1;
    bool layoutChanged = false;
    bool headersChanged = false;
    // Applied to every w:sectPr the body writer emits (paragraph-level ones
    // included): relationship ids, page layout and header references
    // (docx_writer_parts.cpp, sectionProperties()).
    std::function<void(Ooxml::XmlElement &, const QString &)> sectionHook;

    [[nodiscard]] QString mediaPart(const QByteArray &bytes, const QString &extension);
};

// Writes the block content of one story (body, header, footer, cell) into
// `container` (w:body, w:hdr, w:ftr, w:tc).
class BodyWriter {
public:
    BodyWriter(WriteEnv &env, const QTextDocument *document, QString part);

    void writeFrame(const QTextFrame *frame, Ooxml::XmlElement &container);
    // Ends the comment ranges still open at the story's end (docx_comments.h).
    void closeComments(Ooxml::XmlElement &container);

private:
    void writeBlock(const QTextBlock &block, Ooxml::XmlElement &container, bool requireParagraph);
    void writeTable(const QTextTable *table, Ooxml::XmlElement &container);
    void writeCell(const QTextTableCell &cell, Ooxml::XmlElement &tc);
    void writeRuns(const QTextBlock &block, Ooxml::XmlElement &p);
    Ooxml::XmlElementPtr runFor(const QTextFragment &fragment, const QString &text, bool deleted);
    Ooxml::XmlElementPtr pictureRun(const QTextImageFormat &format);
    Ooxml::XmlElementPtr preservedElement(const QTextCharFormat &format);
    Ooxml::XmlElementPtr paragraphProperties(const QTextBlock &block);
    Ooxml::XmlElementPtr runProperties(const QTextCharFormat &format);

    // Opens and closes w:commentRangeStart/End as runs enter and leave
    // comments (kCommentIds), across paragraphs.
    void syncComments(Ooxml::XmlElement &parent, const QTextCharFormat &format);

    WriteEnv &m_env;
    const QTextDocument *m_document;
    QString m_part;
    QStringList m_openComments;
};

// The schema order of w:pPr, w:rPr, w:tblPr, w:tcPr, w:sectPr children
// (ECMA-376 Part 1 §17.3.1.26, §17.3.2.28, §17.4.60, §17.4.70, §17.6.17):
// sorts `element`'s children into it; unknown names keep their place after
// the last known one.
void sortChildren(Ooxml::XmlElement &element);

// Styles the writer may need in a document that lacks them (docx_writer_styles.cpp).
[[nodiscard]] Ooxml::XmlElementPtr builtInStyle(const QString &styleId);
[[nodiscard]] QString builtInStyleId(int writeStyleId);
// The styles part to write: `source` (cloned) or a new one for
// `defaultFont`, plus every used built-in the document lacks.
[[nodiscard]] Ooxml::XmlElementPtr stylesPart(const WriteEnv &env, const QFont &defaultFont,
                                              const Ooxml::XmlElementPtr &source);

// w: element helpers.
Ooxml::XmlElement &wChild(Ooxml::XmlElement &parent, const QString &name);
void setVal(Ooxml::XmlElement &element, const QString &value);
Ooxml::XmlElement &setChildVal(Ooxml::XmlElement &parent, const QString &name, const QString &value);
[[nodiscard]] QString twips(qreal px);

} // namespace QindaOffice::Docx::detail
