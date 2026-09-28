// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The assistant's hands in a QindaWrite document (D-058, office_ai
// document_tools.h). The model sees the document as Markdown (headings,
// lists, emphasis, tables survive the round trip) with the selection and
// the cursor's paragraph, and edits it by replacing the whole document or
// one section, finding and replacing text, inserting Markdown, or
// replacing the selection. All of one reply's operations run inside ONE
// QTextCursor edit block, so Edit ▸ Undo reverts them together.
//
// AGENT-NOTE: rewritten parts come back from Markdown, so character styles
// the Markdown dialect cannot express (fonts, colours, custom paragraph
// styles) are not kept in the parts the assistant rewrites; find/replace
// keeps the formatting around the replaced words.

#include "document_tools.h"

class QTextDocument;

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class WriteTools final : public Ai::DocumentTools {
public:
    WriteTools(PageTextControl *control, WriteDocument *document)
        : m_control(control), m_document(document)
    {
    }

    [[nodiscard]] QString documentNoun() const override { return QStringLiteral("document"); }
    [[nodiscard]] QString snapshot(int budget) override;
    [[nodiscard]] QString operationGuide() const override;
    Ai::EditOutcome apply(const QJsonArray &edits) override;
    void undo() override;

private:
    PageTextControl *m_control;
    WriteDocument *m_document;
};

} // namespace QindaOffice::Apps::Write
