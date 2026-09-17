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

class QCheckBox;

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
class TableController;
class WriteDocument;
class WriteReviewActions;
class WriteRevisions;
class WriteRuler;
class WriteTypography;

class WriteWindow final : public Shell::DocumentWindow {
    Q_OBJECT
public:
    WriteWindow();
    ~WriteWindow() override;

    [[nodiscard]] WriteDocument *writeDocument() const { return m_document; }
    [[nodiscard]] PageTextView *pageView() const { return m_view; }
    [[nodiscard]] FormatController *formatController() const { return m_format; }
    [[nodiscard]] TableController *tableController() const { return m_tables; }
    [[nodiscard]] FindReplaceBar *findReplaceBar() const { return m_findBar; }
    [[nodiscard]] WriteTypography *typography() const { return m_typography; }
    [[nodiscard]] WriteRuler *ruler() const { return m_ruler; }
    [[nodiscard]] WriteRevisions *revisions() const { return m_revisions; }
    [[nodiscard]] WriteReviewActions *reviewActions() const { return m_review; }

    void exportPdf();

protected:
    void insertApplicationMenus(QMenu *before) override;
    void insertFileMenuActions(QMenu *fileMenu) override;
    void addPreferencePages(Shell::PreferencesDialog *dialog) override;
    void applyPreferences() override;
    QString filterString() const override;
    QString saveFilterString() const override;

private:
    void wireEditingActions();
    // "12 words · page 2 of 5" in the permanent status field.
    void updateWriteStatus();
    void applyStyle(int styleId);

    void addSpellingActions(QMenu *menu, const QPointF &docPos);
    [[nodiscard]] Shell::PrintRenderCallback printRenderCallback() const;

    WriteDocument *m_document;
    PageTextView *m_view;
    FormatController *m_format;
    TableController *m_tables;
    FindReplaceBar *m_findBar;
    WriteTypography *m_typography = nullptr;
    WriteRuler *m_ruler = nullptr;
    WriteRevisions *m_revisions = nullptr;
    WriteReviewActions *m_review = nullptr;
    SpellChecker *m_spell = nullptr;
    QCheckBox *m_spellCheckBox = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantPanel *m_panel = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
};

} // namespace QindaOffice::Apps::Write
