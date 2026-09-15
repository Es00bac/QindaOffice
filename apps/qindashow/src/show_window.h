// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow window (plan section 6.3): slide canvas in the center,
// a thumbnail sorter on the left (drag to reorder), and a notes/meta dock
// on the right (speaker notes, transition, layout). All model mutations
// arrive as old/new payloads from the canvas and are pushed as undo
// commands; slide-list changes (add/duplicate/delete/reorder) go through
// SlideListCommand. Undo/redo refreshes canvas + sorter wholesale.

#include "document_window.h"
#include "show_document.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QComboBox;
class QListWidget;
class QPlainTextEdit;

namespace QindaOffice::Ai {
class AssistantPanel;
class InlineActionFlow;
class ModelCatalog;
}

namespace QindaOffice::Apps::Show {

class SlideCanvas;

class ShowWindow final : public Shell::DocumentWindow {
    Q_OBJECT
public:
    ShowWindow();
    ~ShowWindow() override;

    [[nodiscard]] ShowDocument *showDocument() const { return m_document; }
    [[nodiscard]] SlideCanvas *canvas() const { return m_canvas; }

    void newDeck();
    void addSlide();
    void duplicateSlide();
    void deleteSlide();
    void startShow();
    void exportPdf();

    // AI seams (show_ai.cpp).
    [[nodiscard]] int currentSlideRow() const { return m_currentRow; }
    void appendSlides(const QList<ShowModel::Slide> &slides);
    void replaceCurrentSlideText(const QString &text);

protected:
    void insertApplicationMenus(QMenu *before) override;
    QString filterString() const override;

private:
    void rebuildSorter();
    void fullRefresh();
    void switchSlide(int row);
    void syncMetaWidgets();
    void pushSlideListCommand(const QList<ShowModel::Slide> &before);
    void wireCanvasUndo();

    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantPanel *m_panel = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
    ShowDocument *m_document;
    SlideCanvas *m_canvas;
    QListWidget *m_sorter;
    QPlainTextEdit *m_notes;
    QComboBox *m_transition;
    QComboBox *m_layout;
    int m_currentRow = -1;
    bool m_syncingMeta = false;
    bool m_sortSyncing = false;
};

} // namespace QindaOffice::Apps::Show
