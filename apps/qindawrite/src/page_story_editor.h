// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// In-place header and footer editing — Word's double-click on the header
// or footer band (D-068). One story is "open" at a time: the page view's
// editing control is switched onto that story's live QTextDocument (the
// one in WordParts the writer saves), so every formatting command, Undo
// and the clipboard work on it; the band is drawn on the page it was opened
// from, with its caret, while the body is dimmed. Esc, Close Header and
// Footer, or a double-click in the body switch the control back.
//
// Coordinates: "page" is relative to a page's top-left (document px);
// "story" is the story document's own, whose origin is the band's top-left
// (the text column's left edge; the header distance from the top, or the
// footer's bottom at the footer distance from the bottom).
//
// AGENT-GUARD: the control must be switched back to the body before the
// story document can go away (Page Setup replacing a header, a reload,
// different-first-page toggled): WriteDocument::storiesAboutToBeReplaced
// closes the editor first; m_story is a QPointer as a second line.

#include "docx_model.h"

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTextCursor>
#include <QTimer>

class QPainter;
class QTextDocument;

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class StoryEditor final : public QObject {
    Q_OBJECT
public:
    enum class Kind { None, Header, Footer };

    StoryEditor(WriteDocument *document, PageTextControl *control, QObject *parent = nullptr);
    ~StoryEditor() override;

    [[nodiscard]] Kind kind() const { return m_kind; }
    [[nodiscard]] bool isOpen() const { return m_kind != Kind::None; }
    // The 0-based page the open story is edited on.
    [[nodiscard]] int page() const { return m_page; }
    [[nodiscard]] QTextDocument *story() const { return m_story; }
    // Word sections (M88): the section of that page, the slot it shows, and
    // the section that keeps the story (itself, or the one it links to).
    [[nodiscard]] int section() const { return m_section; }
    [[nodiscard]] Docx::HeaderFooterSlot slot() const { return m_slot; }
    [[nodiscard]] int keeper() const { return m_keeper; }
    // "Header", "First Page Footer -Section 2-", … — the tab on the band;
    // "Same as Previous" while it shows a previous section's.
    [[nodiscard]] QString label() const;

    // Opens the header or footer page `page` (0-based) shows, creating an
    // empty one for a slot that has none. False without a document.
    bool open(Kind kind, int page);
    void close();

    // The open story's band on its page, in page coordinates.
    [[nodiscard]] QRectF bandRect() const;
    // Whether a point on (0-based) page `page` falls in the margin band
    // where the header (or footer) lives: above (below) its body.
    [[nodiscard]] Kind zoneAt(int page, const QPointF &pagePos) const;
    [[nodiscard]] QPointF pageToStory(const QPointF &pagePos) const;
    [[nodiscard]] QPointF storyToPage(const QPointF &storyPos) const;

    // The open story over an already painted page (`pageRect` in painter
    // coordinates): paper under it, its text with selection and caret, the
    // band's dashed boundary and its tab.
    void drawOpenStory(QPainter *painter, const QRectF &pageRect) const;

signals:
    void openChanged();
    // The page lay-out followed an edit (debounced).
    void settled();

private:
    void prepareStory();

    WriteDocument *m_document;
    PageTextControl *m_control;
    QPointer<QTextDocument> m_story;
    QTextCursor m_bodyCursor;
    Kind m_kind = Kind::None;
    int m_page = 0;
    int m_section = 0;
    int m_keeper = 0;
    Docx::HeaderFooterSlot m_slot = Docx::HeaderFooterSlot::Default;
    QTimer m_settle;
};

} // namespace QindaOffice::Apps::Write
