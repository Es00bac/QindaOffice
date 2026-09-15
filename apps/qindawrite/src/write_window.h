// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaWrite window: shared document frame hosting the PageTextView,
// the main toolbar (shell) plus the formatting toolbar and find/replace
// bar (M11 UI completeness), with the Format (styles + character/paragraph
// formats), Insert, Page Setup and AI app menus. All format commands are
// persistent actions owned by the FormatController and shared between
// menus and toolbar (action discipline, AGENTS.md rule 5).

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

class FindReplaceBar;
class FormatController;
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
    [[nodiscard]] FormatController *formatController() const { return m_format; }
    [[nodiscard]] FindReplaceBar *findReplaceBar() const { return m_findBar; }

    void exportPdf();

protected:
    void insertApplicationMenus(QMenu *before) override;
    void insertFileMenuActions(QMenu *fileMenu) override;
    QString filterString() const override;
    QString saveFilterString() const override;

private:
    void wireEditingActions();
    void applyStyle(int styleId);

    void addSpellingActions(QMenu *menu, const QPointF &docPos);
    [[nodiscard]] Shell::PrintRenderCallback printRenderCallback() const;

    WriteDocument *m_document;
    PageTextView *m_view;
    FormatController *m_format;
    FindReplaceBar *m_findBar;
    SpellChecker *m_spell = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantPanel *m_panel = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
};

} // namespace QindaOffice::Apps::Write
