// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Format ▸ Paragraph and Format ▸ Tabs (M70, D-079): Word's Paragraph
// dialog — alignment, outline level, indents, spacing before and after
// (with Auto), line spacing, "don't add space between paragraphs of the
// same style", and the Line and Page Breaks switches (widow/orphan control,
// keep with next, keep lines together, page break before) — and the Tabs
// dialog (stops with alignment and leader, default stops). Applies to the
// selected paragraphs of the control's current story as one undo step,
// through office_docx's Word model (docx_paragraph_format.h), and asks
// QindaWrite's pagination (write_pagination.h) to lay the keep rules out
// again.

#include "action_model.h"

#include <QObject>
#include <QVariantMap>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class ParagraphController final : public QObject {
    Q_OBJECT
public:
    ParagraphController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                        QObject *parent);

    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QString paragraphId() const;
    [[nodiscard]] QString tabsId() const;

    // The Paragraph dialog. Lengths in document px (the dialog shows
    // inches or centimetres), spacing in points. Keys: alignment ("left",
    // "center", "right", "justify"), outline (0–8, 9 body text), left,
    // right, special ("none", "firstLine", "hanging"), by, before,
    // beforeAuto, after, afterAuto, lineRule ("single", "oneHalf",
    // "double", "atLeast", "exactly", "multiple"), lineAt (points, or the
    // multiple), contextual, widow, keepNext, keepLines, pageBreakBefore.
    // The question describes the first selected paragraph; the answer
    // carries only what the user changed (`changed`: the key list), so
    // mixed selections keep what was not touched.
    Q_INVOKABLE QVariantMap paragraphInfo() const;
    Q_INVOKABLE bool applyParagraph(const QVariantMap &answer);

    // The Tabs dialog: {stops: [{position (twips from the text column),
    // alignment ("left", "center", "right", "decimal", "bar"), leader
    // ("none", "dot", "hyphen", "underscore")}], defaultStop (px)}.
    Q_INVOKABLE QVariantMap tabsInfo() const;
    Q_INVOKABLE bool applyTabs(const QVariantMap &answer);

signals:
    void paragraphDialogRequested();
    void tabsDialogRequested();

private:
    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_paragraph = nullptr;
    Quick::ActionEntry *m_tabs = nullptr;
};

} // namespace QindaOffice::Apps::Write
