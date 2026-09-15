// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaWrite window: shared document frame hosting the PageTextView,
// with the Format (styles, character formats), Insert (page break) and
// Page Setup app menus (plan section 6.1 v1 scope; full format/insert
// richness lands with M5).

#include "document_window.h"

#include "assistant_panel.h"
#include "inline_action_flow.h"

#include "print_helper.h"

#include <QTextCharFormat>

namespace QindaOffice::Apps::Write {

namespace QindaOffice::Ai {
class AssistantPanel;
class InlineActionFlow;
class ModelCatalog;
}

class PageTextView;
class SpellChecker;
class WriteDocument;

class WriteWindow final : public Shell::DocumentWindow {
    Q_OBJECT
public:
    WriteWindow();
    ~WriteWindow() override;

    [[nodiscard]] WriteDocument *writeDocument() const { return m_document; }
    [[nodiscard]] PageTextView *pageView() const { return m_view; }

    void exportPdf();

protected:
    void insertApplicationMenus(QMenu *before) override;
    QString filterString() const override;
    QString saveFilterString() const override;

private:
    void applyStyle(int styleId);
    void applyCharFormat(const QTextCharFormat &format);

    void addSpellingActions(QMenu *menu, const QPointF &docPos);
    [[nodiscard]] Shell::PrintRenderCallback printRenderCallback() const;

    WriteDocument *m_document;
    PageTextView *m_view;
    SpellChecker *m_spell = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantPanel *m_panel = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
};

} // namespace QindaOffice::Apps::Write
