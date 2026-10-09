// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's styles in QindaWrite (M73, D-082), on office_docx's style model
// (docx_style_edit.h, docx_restyle.h):
//  - the Styles pane (Home ▸ Styles launcher, Alt+Ctrl+Shift+S): the
//    document's paragraph, character, linked and table styles and Word's
//    built-ins it lacks — a click applies one (a built-in joins the
//    document as Word adds it on first use);
//  - the Styles gallery: the document's quick styles (w:qFormat), sixteen
//    stable slots writeQuickStyle0–15 whose text follows the document;
//  - New Style from the selection, Modify Style (through M70's Font,
//    Paragraph, Tabs and Borders dialogs on a StyleDraft), Update Style to
//    Match Selection, Delete Style (its paragraphs fall back to its parent);
//  - character styles applied as w:rStyle, a linked style's character half
//    on a partial selection;
//  - Clear Formatting keeps the style; Enter at a paragraph's end gives the
//    next paragraph the style's "style for following paragraph".
// Each command is one undo step: the text's edits and, when the styles
// part changes, its swap (WriteDocument::setStylesRoot) in one edit block.
// The definition commands work on the body (disabled while a header or
// footer is open); applying a style works in any story.

#include "action_model.h"
#include "docx_restyle.h"
#include "ooxml_xml.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <memory>

class QKeyEvent;
class QTextCursor;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class StyleDraft;
class WriteDocument;

class StylesController final : public QObject {
    Q_OBJECT
    // The pane's rows: {id, name, type (paragraph, character, linked,
    // table), inDocument, inUse, quick, family, size, bold, italic, color}.
    Q_PROPERTY(QVariantList styles READ styles NOTIFY stylesChanged)
    // The style of the caret's paragraph (or run, for a character style).
    Q_PROPERTY(QString currentStyle READ currentStyle NOTIFY currentChanged)
    Q_PROPERTY(QObject *draft READ draftObject CONSTANT)
public:
    StylesController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);

    // Format ▸ Styles: the pane, the definition commands, the gallery.
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QVariantList styles() const { return m_rows; }
    [[nodiscard]] QString currentStyle() const { return m_current; }
    [[nodiscard]] StyleDraft *draft() const { return m_draft; }
    [[nodiscard]] QObject *draftObject() const;
    // The style id each gallery slot applies (tests).
    [[nodiscard]] QStringList quickStyleIds() const { return m_quick; }

    Q_INVOKABLE bool apply(const QString &id);
    // New Style from the selection / Modify Style: loads the draft and asks
    // the window for the dialog; commitDraft() is its OK.
    Q_INVOKABLE void newStyle();
    Q_INVOKABLE bool modify(const QString &id);
    Q_INVOKABLE bool commitDraft();
    Q_INVOKABLE bool updateToMatch(const QString &id);
    Q_INVOKABLE bool remove(const QString &id);
    [[nodiscard]] Q_INVOKABLE bool canRemove(const QString &id) const;
    // Format ▸ Clear Direct Formatting (Ctrl+Space's sibling).
    bool clearFormatting();
    // Enter at a paragraph's end: the next paragraph takes the style's
    // w:next (the control's key filter); true when it handled the key.
    bool handleKey(QKeyEvent *event);

signals:
    void stylesChanged();
    void currentChanged();
    void styleDialogRequested();
    void paneToggleRequested(bool visible);

private:
    void createActions(Quick::ActionModel *model);
    void refresh();
    void syncCurrent();
    [[nodiscard]] bool onBody() const;
    // The styles part with `id` in it (a built-in the document lacks added).
    [[nodiscard]] Ooxml::XmlElementPtr withEnsured(const Ooxml::XmlElementPtr &root, const QString &id) const;
    // Stores `root` as this command's styles part (inside its edit block)
    // when it differs from `original` or the document has none of its own.
    void commitRoot(const Ooxml::XmlElementPtr &root, const Ooxml::XmlElementPtr &original);
    // Runs `edit` as one undo step on the control's story.
    bool command(const std::function<bool(QTextCursor &)> &edit);
    [[nodiscard]] QString typeOf(const QString &id) const;
    // A table style on the table the caret is in (w:tblStyle).
    bool applyTableStyle(const QString &id);
    // The document's styles, parsed once until they change.
    [[nodiscard]] const Docx::DocumentStyles &documentStyles() const;

    PageTextControl *m_control;
    WriteDocument *m_document;
    StyleDraft *m_draft;
    QList<Quick::ActionEntry *> m_slots;
    QStringList m_quick;
    QVariantList m_rows;
    QString m_current;
    Quick::ActionEntry *m_pane = nullptr;
    Quick::ActionEntry *m_new = nullptr;
    Quick::ActionEntry *m_modify = nullptr;
    Quick::ActionEntry *m_update = nullptr;
    Quick::ActionEntry *m_delete = nullptr;
    QTimer m_refresh; // coalesces the document's change signals
    mutable std::unique_ptr<Docx::DocumentStyles> m_cache;
};

} // namespace QindaOffice::Apps::Write
