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
    // The numbering a QTextList is written with. A Word list (docx_lists.h)
    // keeps its w:num while it is as read; otherwise it gets a new w:num —
    // over its source w:abstractNum with w:lvlOverride/w:startOverride when
    // only the starts changed, over a new w:abstractNum from its definition
    // when that changed or was made in Write. A Qt list (HTML, Markdown)
    // gets a new definition built from its format.
    Ref refFor(const QTextList *list, qreal indentWidth);
    // numbering.xml content, or null when the document has no numbering.
    [[nodiscard]] Ooxml::XmlElementPtr result() const;
    // A style's w:numPr names a list by the numId its paragraphs have in
    // QindaWrite (Modify Style ▸ Numbering, M73): the id that list was
    // written with; a numId no list has and the source lacks goes.
    void patchStyles(Ooxml::XmlElement &styles) const;

private:
    Ref wordRef(const QTextListFormat &format);
    [[nodiscard]] int freshNumId(int wanted);

    Ooxml::XmlElementPtr m_root;
    int m_nextAbstract = 0;
    int m_nextNum = 1;
    QHash<const QTextList *, Ref> m_refs;
    QSet<int> m_sourceNums;
    QSet<int> m_usedNums;
    QHash<QString, QString> m_sourceAbstracts; // abstractNumId → its markup as read
    QHash<int, int> m_wordNums;                // a Word list's numId → the one written
    QHash<QString, int> m_abstracts;           // definition key + markup → abstractNumId written
};

struct WriteEnv {
    const WordParts *parts = nullptr;
    Ooxml::PackageWriter writer;
    std::unique_ptr<Ooxml::RelationshipCopier> copier;
    StyleSheet sourceStyles;
    std::unique_ptr<NumberingBuilder> numbering;
    QString sourceMain;         // main part of the source package, if any
    QString sourceFootnotesPart;
    QString sourceEndnotesPart;
    QSet<QString> usedStyles;   // style ids the output references
    QHash<QByteArray, QString> mediaByHash; // content hash → part name
    QHash<QString, int> footnoteIds;        // note id → w:id
    QList<QString> footnoteOrder;
    int nextFootnoteId = 1;
    // Endnotes (M77): ids in reference order; while the referenced notes
    // are the ones read, unchanged, their ids stay and endnotes.xml is
    // copied (keepEndnotesPart).
    QHash<QString, int> endnoteIds;
    QList<QString> endnoteOrder;
    int nextEndnoteId = 1;
    bool keepEndnotesPart = false;
    int nextDocPr = 1;
    int nextRevision = 1;
    int nextBookmark = 1;
    QSet<QString> writtenControls; // content controls written once (a copy drops its w:id)
    bool layoutChanged = false;
    bool headersChanged = false;
    // Applied to every w:sectPr the body writer emits (paragraph-level ones
    // included): relationship ids, page layout and header references
    // (docx_writer.cpp). A paragraph-level section whose page differs from
    // the document's as read keeps its own page (an envelope, a label sheet).
    std::function<void(Ooxml::XmlElement &, const QString &)> sectionHook;

    [[nodiscard]] QString mediaPart(const QByteArray &bytes, const QString &extension);
};

// Writes the block content of one story (body, header, footer, cell) into
// `container` (w:body, w:hdr, w:ftr, w:tc).
class BodyWriter {
public:
    BodyWriter(WriteEnv &env, const QTextDocument *document, QString part);

    void writeFrame(const QTextFrame *frame, Ooxml::XmlElement &container);
    // The w:pPr / w:rPr written for a paragraph or a run (docx_style_edit.h:
    // a selection's formatting as a style's markup, M73).
    Ooxml::XmlElementPtr blockMarkup(const QTextBlock &block) { return paragraphProperties(block); }
    Ooxml::XmlElementPtr runMarkup(const QTextCharFormat &format) { return runProperties(format); }
    // Ends the comment ranges still open at the story's end (docx_comments.h).
    void closeComments(Ooxml::XmlElement &container);

private:
    // A frame's or a cell's blocks and tables in order, inside the
    // block-level content controls they carry (docx_writer_controls.cpp).
    void writeItems(const QList<QTextFrame::iterator> &items, Ooxml::XmlElement &container);
    // A w:sdt for control `key` appended to `parent`; returns its w:sdtContent.
    Ooxml::XmlElement &openControl(Ooxml::XmlElement &parent, const QString &key);
    // The keys of `keys` that name a control of this document (a run pasted
    // from another document keeps its content, not a control).
    [[nodiscard]] QStringList knownControls(const QVariant &keys) const;
    [[nodiscard]] static bool skipsBlock(const QTextBlock &block, bool requireParagraph);
    void writeBlock(const QTextBlock &block, Ooxml::XmlElement &container, bool requireParagraph);
    void writeTable(const QTextTable *table, Ooxml::XmlElement &container);
    void writeCell(const QTextTableCell &cell, Ooxml::XmlElement &tc);
    void writeRuns(const QTextBlock &block, Ooxml::XmlElement &p);
    Ooxml::XmlElementPtr runFor(const QTextFragment &fragment, const QString &text, bool deleted);
    Ooxml::XmlElementPtr pictureRun(const QTextImageFormat &format);
    Ooxml::XmlElementPtr preservedElement(const QTextCharFormat &format);
    Ooxml::XmlElementPtr paragraphProperties(const QTextBlock &block);
    Ooxml::XmlElementPtr runProperties(const QTextCharFormat &format);
    // w:numPr and, for a Word list paragraph, w:ind against its level
    // (docx_writer_paragraph.cpp, M73): a paragraph numbered by its style
    // or at its level's indents says nothing of its own, as in Word.
    // Returns true when it wrote the paragraph's indents.
    bool listProperties(Ooxml::XmlElement &pPr, const QTextBlock &block, const QString &styleId,
                        const std::function<bool(const char *)> &changed);

    // Opens and closes w:commentRangeStart/End as runs enter and leave
    // comments (kCommentIds), across paragraphs.
    void syncComments(Ooxml::XmlElement &parent, const QTextCharFormat &format);

    WriteEnv &m_env;
    const QTextDocument *m_document;
    QString m_part;
    QStringList m_openComments;
    QHash<QString, QString> m_controlTexts; // content control key → its text now
};

// Whether `element` (a w:tc, or a w:sdtContent in one) ends with a
// paragraph, looking into a closing block-level control.
[[nodiscard]] bool endsWithParagraph(const Ooxml::XmlElement &element);

// The schema order of w:pPr, w:rPr, w:tblPr, w:tcPr, w:sectPr children
// (ECMA-376 Part 1 §17.3.1.26, §17.3.2.28, §17.4.60, §17.4.70, §17.6.17):
// sorts `element`'s children into it; unknown names keep their place after
// the last known one.
void sortChildren(Ooxml::XmlElement &element);

// M70 (D-079) re-derivations of the Word values Qt's formats cannot hold
// (docx_writer_paragraph.cpp). `changed(key)` is the caller's snapshot
// comparison (true for content Word did not write).
using ChangedFn = std::function<bool(const char *)>;
void writeParagraphSpacing(Ooxml::XmlElement &pPr, const QTextBlockFormat &format, bool imported,
                           const ChangedFn &changed);
void writeParagraphExtras(Ooxml::XmlElement &pPr, const QTextBlockFormat &format, bool imported,
                          const ChangedFn &changed, const StyleSheet &styles, const QString &styleId);
void writeRunExtras(Ooxml::XmlElement &rPr, const QTextCharFormat &format, bool imported, const ChangedFn &changed,
                    bool insertMark);

// Table property re-derivations (docx_writer_table_props.cpp): w:tblPr
// against the table snapshot, w:trPr from the row's first cell, w:tcPr's
// borders and margins, and a cell's w:tcW from the column constraints.
void writeTableProperties(Ooxml::XmlElement &tblPr, const QTextTableFormat &format, const QVariantMap &snapshot,
                          bool imported);
void writeRowProperties(Ooxml::XmlElement &trPr, const QTextCharFormat &anchor);
void writeCellProperties(Ooxml::XmlElement &tcPr, const QTextTableCellFormat &format, bool imported);
void writeCellWidth(Ooxml::XmlElement &tcPr, const QTextTable *table, int column, int span,
                    const QList<qreal> &widthsPx);

// Footnotes and endnotes (docx_writer_notes.cpp): decides whether the
// source's endnotes part is kept (before the body is written), then writes
// footnotes.xml and endnotes.xml for the notes the body referenced.
struct NotesWritten {
    bool footnotes = false;
    bool endnotes = false;
};
void prepareEndnotes(WriteEnv &env, const QTextDocument &body);
NotesWritten writeNotes(WriteEnv &env);
// settings.xml (docx_writer_settings.cpp): the source's, kept consistent
// with what was written — default tab stop, note options and separators,
// even/odd headers, the mail merge (docx_mailmerge.h).
void writeSettings(WriteEnv &env, const Ooxml::XmlElementPtr &sourceSettings, const QString &sourceSettingsPart,
                   const NotesWritten &notes);
// Footnote/endnote options into a w:sectPr that has its own (or, for the
// closing section, when the options changed).
void patchSectionNoteOptions(const WordParts &parts, Ooxml::XmlElement &sectPr, bool closing);
// Declares the Microsoft extension namespaces a part uses as mc:Ignorable.
void markIgnorable(Ooxml::XmlElement &root);
[[nodiscard]] QString wordprocessingType(const char *suffix);

// Styles the writer may need in a document that lacks them (docx_writer_styles.cpp).
[[nodiscard]] Ooxml::XmlElementPtr builtInStyle(const QString &styleId);
// The built-ins QindaWrite offers when a document lacks them (M73).
[[nodiscard]] QStringList builtInStyleIds();
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
