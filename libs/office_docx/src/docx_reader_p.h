// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to the .docx reader: the state shared by the body, table and
// object readers (docx_reader_body.cpp, docx_reader_table.cpp,
// docx_reader_objects.cpp). Not installed, not used outside office_docx.

#include "docx_model.h"
#include "docx_numbering.h"
#include "docx_styles.h"
#include "ooxml_theme.h"

#include <QHash>
#include <QMap>
#include <QPointer>
#include <QTextCursor>
#include <QTextList>

#include <array>

namespace QindaOffice::Docx::detail {

// Shared by every BodyReader of one document (body, headers, footers).
struct ReadEnv {
    std::shared_ptr<const Ooxml::Package> package;
    StyleSheet styles;
    Numbering numbering;
    Ooxml::Theme theme;
    WordParts *parts = nullptr;
    QHash<QString, QString> footnoteText; // w:footnote id → plain text
    int nextMedia = 1;
    int nextIsland = 1;
    QMap<QString, int> approximations; // "text box" → how many were shown approximately
};

// Table-style formatting a table cell hands to its paragraphs.
struct CellContext {
    Ooxml::XmlElementPtr pPr;
    Ooxml::XmlElementPtr rPr;
};

class BodyReader {
public:
    BodyReader(ReadEnv &env, QTextDocument *document, QString part);

    // Reads the block content of `container` (w:body, w:hdr, w:ftr) into the
    // reader's (empty) document.
    void readDocument(const Ooxml::XmlElement &container);
    // Section properties met on the way, in document order (paragraph-level
    // ones first, the body's own last).
    [[nodiscard]] const QList<const Ooxml::XmlElement *> &sections() const { return m_sections; }

    // Exposed for the table and object readers.
    struct ContainerState {
        bool blockFresh = true;      // the cursor's block is empty and not yet a w:p
        bool hasPending = false;     // a finished paragraph awaits its bottom margin
        QTextBlock pending;
        qreal pendingAfter = 0;
        QString previousStyle;
        bool previousContextual = false;
        bool previousAutoAfter = false; // its space after is Word's auto spacing
        int previousNumId = 0;          // the list it is an item of, or 0
        bool first = true;
        const CellContext *cell = nullptr;
        struct Field {
            QString instruction;
            bool inResult = false;
        };
        QList<Field> fields;         // complex fields may span paragraphs
        QStringList pendingAnchors;  // bookmarks waiting for the next text
        bool pageBreakPending = false;
        // AGENT-NOTE: Qt lays an EMPTY block that directly follows a table
        // out beside the table's last line — it takes no height (probed on
        // Qt 6.11). Word gives that paragraph a full line, so its height is
        // carried to whatever comes next: the next paragraph's top margin or
        // the next table's (tableTopMargin). Display only; markup unchanged.
        bool afterTable = false;
        qreal collapsedExtra = 0;
        qreal tableTopMargin = 0;
    };
    struct InlineState {
        QString href;
        QStringList simpleFields;
        QString insertAuthor;
        QString deleteAuthor;
        QDateTime revisionDate;
        QTextBlockFormat blockFormat; // for splitting at a page break
        QTextCharFormat markFormat;
        Ooxml::XmlElementPtr paragraphRunProperties; // effective, for runs
        bool blockHasContent = false;
    };

    void readBlocks(const Ooxml::XmlElement &container, QTextCursor &cursor, ContainerState &state);
    void insertText(QTextCursor &cursor, const QString &text, QTextCharFormat format, InlineState &inl,
                    ContainerState &state);
    void insertObject(QTextCursor &cursor, QTextCharFormat format, InlineState &inl, ContainerState &state);
    // Ends the pending paragraph: `keepAfter` gives it its own space after
    // (a table or the container's end follows); otherwise that space was
    // folded into the next paragraph's top margin.
    void finishParagraph(ContainerState &state, bool keepAfter);

    ReadEnv &env() { return m_env; }
    [[nodiscard]] const QString &part() const { return m_part; }
    [[nodiscard]] QTextDocument *document() const { return m_document; }

private:
    void readParagraph(const Ooxml::XmlElement &p, QTextCursor &cursor, ContainerState &state);
    void readInline(const Ooxml::XmlElement &parent, QTextCursor &cursor, InlineState &inl, ContainerState &state);
    void readRun(const Ooxml::XmlElement &r, QTextCursor &cursor, InlineState &inl, ContainerState &state);
    void readFieldChar(const Ooxml::XmlElement &fldChar, ContainerState &state);
    void pageBreak(QTextCursor &cursor, InlineState &inl, ContainerState &state);
    void footnoteMarker(QTextCursor &cursor, const QString &id, QTextCharFormat format, InlineState &inl,
                        ContainerState &state);
    void preservedText(QTextCursor &cursor, const Ooxml::XmlElement &element, const QString &display,
                       QTextCharFormat format, InlineState &inl, ContainerState &state);
    // Makes the cursor's block an item of the list Word would number it in,
    // starting a new QTextList where Word restarts the level.
    void joinList(int numId, int ilvl, const NumberingLevel &level, QTextCursor &cursor);
    [[nodiscard]] QString currentHref(const InlineState &inl, const ContainerState &state) const;
    [[nodiscard]] QString currentField(const InlineState &inl, const ContainerState &state) const;
    [[nodiscard]] bool inFieldInstruction(const ContainerState &state) const;

public:
    // Tables (docx_reader_table.cpp).
    void readTable(const Ooxml::XmlElement &tbl, QTextCursor &cursor, ContainerState &state);
    // Drawings, VML, OLE, math, AlternateContent (docx_reader_objects.cpp).
    // Returns false when `object` is not something it handles.
    bool readObject(const Ooxml::XmlElement &run, const Ooxml::XmlElement &object, QTextCursor &cursor,
                    const QTextCharFormat &format, InlineState &inl, ContainerState &state);
    void readMath(const Ooxml::XmlElement &math, QTextCursor &cursor, InlineState &inl, ContainerState &state);

private:
    ReadEnv &m_env;
    QTextDocument *m_document;
    QString m_part;
    QList<const Ooxml::XmlElement *> m_sections;
    // Comments whose w:commentRangeStart was read and whose end was not:
    // the runs in between carry their ids (kCommentIds, docx_comments.h).
    QStringList m_openComments;
    void commentRange(const Ooxml::XmlElement &marker);
    int m_footnoteNumber = 0;

    // Word keeps one counter set per abstract numbering definition (per
    // w:num when it overrides a start); a new QTextList starts whenever Word
    // would restart a level.
    struct ListCounters {
        std::array<int, 9> value{};
        std::array<QPointer<QTextList>, 9> list{};
    };
    QHash<QString, ListCounters> m_counters;
};

// Display helpers shared by the readers.
[[nodiscard]] QString formatListNumber(int value, const QString &numFmt);
[[nodiscard]] QImage placeholderImage(const QSizeF &size, const QString &label, const QString &body = QString());

} // namespace QindaOffice::Docx::detail
