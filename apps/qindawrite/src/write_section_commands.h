// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's section commands in QindaWrite (M88, D-096), each a persistent
// ActionEntry in the menus and on the ribbon and one undo step:
//  - Layout ▸ Breaks ▸ Section Break (Next Page / Continuous / Even Page /
//    Odd Page): the paragraph splits at the caret and the part before it
//    becomes a section of its own with the section's settings and its
//    headers (the part after links to them, Word's "Same as Previous");
//  - Layout ▸ Orientation ▸ Portrait / Landscape for the caret's section;
//  - Header & Footer: Link to Previous (per slot of the open header's
//    section), Different First Page (per section), Different Odd & Even
//    Pages (the document's, as in Word), Format Page Numbers… (format;
//    continue or start at N, per section);
//  - Backspace at the start of a section and Delete before its break take
//    the break away as Word does: the section joins the next one, whose
//    settings it takes, and the headers the next showed through its links
//    go with it; joining two paragraphs keeps the second's break.
// Page Setup's "Apply to" (this section, this point forward, whole
// document) is applySettings().

#include "action_model.h"
#include "docx_sections.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <functional>

class QKeyEvent;
class QTextBlock;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class PageTextItem;
class WriteDocument;

class SectionsController final : public QObject {
    Q_OBJECT
public:
    SectionsController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                       QObject *parent);

    // The page view, once: while a header or footer is open the commands
    // act on its section.
    void setPageItem(PageTextItem *item);

    // Section Break (Next Page), (Continuous), (Even Page), (Odd Page).
    [[nodiscard]] QList<Quick::MenuNode> breakItems() const;
    // Portrait, Landscape.
    [[nodiscard]] QList<Quick::MenuNode> orientationItems() const;
    // Format Page Numbers…, Different First Page, Different Odd & Even
    // Pages, Link to Previous.
    [[nodiscard]] QList<Quick::MenuNode> headerFooterItems() const;

    // The section the commands act on: the open header's or footer's, else
    // the caret's.
    [[nodiscard]] int currentSection() const;
    // A section break of `start` at the caret; false where none can go (in
    // a table, in a header).
    bool insertSectionBreak(Docx::SectionStart start);
    // Changes the settings of the sections `scope` names — "section" (the
    // current one), "forward" (a next-page break at the caret first, then
    // that section and every later one) or "document" — one undo step.
    void applySettings(const QString &scope, const std::function<void(Docx::SectionSettings &)> &change);

    // Format Page Numbers: {format, start (-1: continue), formats: [{format,
    // label}]} of the current section.
    Q_INVOKABLE QVariantMap pageNumberInfo() const;
    Q_INVOKABLE bool applyPageNumbers(const QVariantMap &answer);

    // Backspace / Delete across a section break (the control's key filter).
    bool handleKey(QKeyEvent *event);
    // Checked and enabled states after the caret, a story or the sections changed.
    void sync();

signals:
    void pageNumbersRequested();
    void message(const QString &text);

private:
    // Joins paragraph `first` with the one after it, the section breaks
    // handled as Word's paragraph marks are (handleKey).
    bool joinParagraphs(const QTextBlock &first);
    void setOrientation(bool landscape);
    void toggleStoryOption(const char *which);

    PageTextControl *m_control;
    WriteDocument *m_document;
    QPointer<PageTextItem> m_page;
    QList<Quick::ActionEntry *> m_breaks;
    Quick::ActionEntry *m_portrait = nullptr;
    Quick::ActionEntry *m_landscape = nullptr;
    Quick::ActionEntry *m_numbers = nullptr;
    Quick::ActionEntry *m_firstPage = nullptr;
    Quick::ActionEntry *m_oddEven = nullptr;
    Quick::ActionEntry *m_link = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
