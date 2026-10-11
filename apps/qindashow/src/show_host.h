// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow window's host (conversion plan §5.3): the deck document,
// the slide canvas item it is handed once (attachCanvas), the sorter's
// slide list, the notes / transition / layout of the current slide, the
// Slide / Insert / Shape / Arrange command sets (the controllers own them
// as ActionEntries), find and replace across the deck, the presentation
// controller a running show drives, print/PDF and the assistant. Every
// dialog is a request signal the window answers through an invokable;
// the host holds no widgets.
//
// All model mutations arrive as old/new payloads from the canvas and are
// pushed as undo commands; slide-list changes go through SlideListCommand.
// Undo/redo refreshes canvas + sorter wholesale (deckRevision bumps).
//
// Files: show_host.cpp (document, canvas, current slide and its meta,
// status, undo wiring, edit commands, print), show_host_slides.cpp (deck
// operations, find/replace, the show, templates, PDF),
// show_host_menus.cpp (the menu tree, the context menu, the AI menu),
// show_host_ai.cpp (assistant, inline actions, deck generation),
// show_host_editing.cpp (M67, D-076: tables, chart data, the notes pane,
// the presenter view on one screen), show_host_show.cpp (M91, D-099: the
// show's start and end, actions leaving the show, the Slide Show tab).

#include "assistant_controller.h"
#include "document_host.h"
#include "presentation.h"
#include "presentation_controller.h"
#include "show_document.h"
#include "slide_canvas_item.h"

#include <QColor>
#include <QPointer>
#include <QTextDocument>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Ai {
class InlineActionFlow;
class ModelCatalog;
}

namespace QindaOffice::Apps::Show {

class ChartDataEditor;
class ShowActionSettings;
class ShowArrangeActions;
class ShowBackground;
class ShowComments;
class ShowDesignActions;
class ShowDesignTab;
class ShowEffectOptions;
class ShowExport;
class ShowMasterView;
class ShowMedia;
class ShowSections;
class ShowSlideLayouts;
class ShowSlideShow;
class ShowTableActions;
class ShowToolbox;

class ShowHost : public Quick::DocumentHost {
    Q_OBJECT
    // M75: the property types below are complete where moc needs them.
    Q_MOC_INCLUDE("show_background.h")
    Q_MOC_INCLUDE("show_comments.h")
    Q_MOC_INCLUDE("show_design_tab.h")
    Q_MOC_INCLUDE("show_master_view.h")
    Q_MOC_INCLUDE("show_media_actions.h")
    Q_MOC_INCLUDE("show_sections.h")
    Q_MOC_INCLUDE("show_slide_layouts.h")
    Q_MOC_INCLUDE("show_action_settings.h")
    Q_MOC_INCLUDE("show_export.h")
    Q_MOC_INCLUDE("show_slide_show.h")
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(int slideCount READ slideCount NOTIFY deckChanged)
    // Bumped on every deck change; the thumbnails repaint on it.
    Q_PROPERTY(int deckRevision READ deckRevision NOTIFY deckChanged)
    Q_PROPERTY(int currentSlide READ currentSlideRow WRITE selectSlideRow NOTIFY currentSlideChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY currentSlideChanged)
    Q_PROPERTY(int transitionIndex READ transitionIndex WRITE setTransitionIndex NOTIFY currentSlideChanged)
    Q_PROPERTY(int layoutIndex READ layoutIndex WRITE setLayoutIndex NOTIFY currentSlideChanged)
    Q_PROPERTY(QStringList transitionNames READ transitionNames CONSTANT)
    Q_PROPERTY(QStringList layoutNames READ layoutNames CONSTANT)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(bool reducedMotion READ reducedMotion CONSTANT)
    // Non-null while a show runs (the window shows the player windows).
    Q_PROPERTY(QindaOffice::Apps::Show::PresentationController *player READ player NOTIFY playerChanged)
    Q_PROPERTY(QindaOffice::Ai::AssistantController *assistant READ assistant CONSTANT)
    // The host owns the canvas item (the controllers need it from the first
    // line and the tests drive it without a window); the window seats it.
    Q_PROPERTY(QindaOffice::Apps::Show::SlideCanvasItem *canvas READ canvas CONSTANT)
    // M67: the Edit Data sheet of the selected chart.
    Q_PROPERTY(QindaOffice::Apps::Show::ChartDataEditor *chartData READ chartData CONSTANT)
    // M67: the show runs with the presenter view even on one screen
    // (Slide Show ▸ Presenter View); the player then stays hidden there.
    Q_PROPERTY(bool presenterView READ presenterView NOTIFY playerChanged)
    // M75 (D-084): the Design tab, Format Background, the Slide Master
    // view, sections, comments, audio and video.
    Q_PROPERTY(QindaOffice::Apps::Show::ShowDesignTab *design READ designTab CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowBackground *background READ background CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowMasterView *masterView READ masterView CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowSections *sections READ sections CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowComments *comments READ comments CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowMedia *media READ media CONSTANT)
    // M82 (D-091): New Slide on the deck's layouts, Layout, Reset, Reuse
    // Slides, a template's masters (show_slide_layouts.h).
    Q_PROPERTY(QindaOffice::Apps::Show::ShowSlideLayouts *layouts READ slideLayouts CONSTANT)
    // M91 (D-099): the Slide Show tab (custom shows, Set Up, timings,
    // rehearsal), Insert ▸ Action / Link and the action buttons, and the
    // video and picture exports.
    Q_PROPERTY(QindaOffice::Apps::Show::ShowSlideShow *slideShow READ slideShow CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowActionSettings *actionSettings READ actionSettings CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Show::ShowExport *exporter READ exporter CONSTANT)
public:
    ShowHost();
    ~ShowHost() override;

    [[nodiscard]] ShowDocument *showDocument() const { return m_document; }
    [[nodiscard]] SlideCanvasItem *canvas() const { return m_canvas; }
    [[nodiscard]] ShowToolbox *toolbox() const { return m_toolbox; }
    [[nodiscard]] ShowArrangeActions *arrangeActions() const { return m_arrange; }
    [[nodiscard]] ShowDesignActions *designActions() const { return m_design; }
    [[nodiscard]] Ai::AssistantController *assistant() const { return m_assistant; }
    [[nodiscard]] PresentationController *player() const { return m_player; }
    [[nodiscard]] ShowTableActions *tableActions() const { return m_tables; }
    [[nodiscard]] ChartDataEditor *chartData() const { return m_chartData; }
    [[nodiscard]] bool presenterView() const { return m_presenterView; }
    [[nodiscard]] ShowDesignTab *designTab() const { return m_designTab; }
    [[nodiscard]] ShowBackground *background() const { return m_background; }
    [[nodiscard]] ShowMasterView *masterView() const { return m_masterView; }
    [[nodiscard]] ShowSections *sections() const { return m_sections; }
    [[nodiscard]] ShowComments *comments() const { return m_comments; }
    [[nodiscard]] ShowMedia *media() const { return m_media; }
    [[nodiscard]] ShowSlideLayouts *slideLayouts() const { return m_layouts; }
    [[nodiscard]] ShowSlideShow *slideShow() const { return m_slideShow; }
    [[nodiscard]] ShowActionSettings *actionSettings() const { return m_actionSettings; }
    [[nodiscard]] ShowExport *exporter() const { return m_export; }
    // The window's in-place text editor (M91: Insert ▸ Link reads its selection).
    [[nodiscard]] QObject *editorItem() const { return m_editor; }
    // M75: the deck's slide at the current row (not the canvas's slide,
    // which in the Slide Master view is a master or layout); nullptr when
    // the deck has none.
    [[nodiscard]] ShowModel::Slide *currentSlide() const;
    [[nodiscard]] bool masterViewActive() const;
    // M75: a whole-deck change as ONE undo step named `label` — the deck
    // becomes `after`, the current slide stays by position. False (nothing
    // pushed) when `after` equals the deck.
    bool applyDeckChange(const ShowModel::Presentation &after, const QString &label);
    // Canvas, sorter and status again (the Slide Master view closing).
    void refreshSlides() { fullRefresh(); }

    // Seats the canvas item under `container` (SlideCanvasView.qml's
    // Component.onCompleted) and names the TextEdit that shows the in-place
    // editor's document.
    Q_INVOKABLE void attachCanvas(QQuickItem *container);
    Q_INVOKABLE void attachEditor(QObject *textEdit);

    // ---- slides ----
    [[nodiscard]] int slideCount() const;
    [[nodiscard]] int deckRevision() const { return m_revision; }
    [[nodiscard]] int currentSlideRow() const { return m_currentRow; }
    // Selects a slide by deck position; everything that moves the user to
    // a slide goes through here.
    void selectSlideRow(int row);
    Q_INVOKABLE QString slideTitle(int row) const;
    Q_INVOKABLE void moveSlide(int from, int to);
    void newDeck();
    void addSlide();
    void duplicateSlide();
    void deleteSlide();
    void appendSlides(const QList<ShowModel::Slide> &slides);
    void replaceCurrentSlideText(const QString &text);
    // ---- the current slide's meta ----
    [[nodiscard]] QString notes() const;
    void setNotes(const QString &notes);
    [[nodiscard]] int transitionIndex() const;
    void setTransitionIndex(int index);
    [[nodiscard]] int layoutIndex() const;
    void setLayoutIndex(int index);
    [[nodiscard]] QStringList transitionNames() const;
    [[nodiscard]] QStringList layoutNames() const;
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] bool reducedMotion() const;
    // ---- the show ----
    // From the current slide (Shift+F5; the tests' entry point).
    Q_INVOKABLE void startShow();
    // M91 (D-099): From Beginning (F5), a custom show, a rehearsal.
    void startShow(const PresentationController::Options &options);
    Q_INVOKABLE void startShowFromBeginning();
    Q_INVOKABLE void endShow();
    // M91: an action's address or file (PresentationController::
    // openRequested). Web and e-mail addresses go to the desktop's URL
    // handler; a file asks first (fileConsentRequested) and a program is
    // refused; anything else is a notice (showNotice).
    Q_INVOKABLE void openActionTarget(const QString &address, bool file);
    // The presenter agreed to open `path`.
    Q_INVOKABLE void openFileConfirmed(const QString &path);
    // ---- tables, charts (show_host_editing.cpp) ----
    Q_INVOKABLE void insertTable(int rows, int columns);
    Q_INVOKABLE void splitCells(int columns, int rows);
    Q_INVOKABLE void applyTableShading(const QColor &color);
    Q_INVOKABLE void setTablePenColor(const QColor &color);
    Q_INVOKABLE bool applyChartData();
    // ---- find and replace (show_host_slides.cpp) ----
    Q_INVOKABLE bool findNext(const QString &needle, bool caseSensitive);
    Q_INVOKABLE bool findPrevious(const QString &needle, bool caseSensitive);
    Q_INVOKABLE int replaceAll(const QString &needle, const QString &replacement);
    // ---- dialog answers ----
    Q_INVOKABLE void insertImageFile(const QUrl &url);
    Q_INVOKABLE void applyFillColor(const QColor &color);
    Q_INVOKABLE void applyStrokeColor(const QColor &color);
    Q_INVOKABLE QVariantList masters() const;
    Q_INVOKABLE QStringList masterNames() const;
    Q_INVOKABLE void applyMasters(const QVariantList &masters);
    Q_INVOKABLE void applyMasterToSlide(int masterIndex);
    Q_INVOKABLE void exportPdfTo(const QUrl &url);
    Q_INVOKABLE void generateDeck(const QString &outline);
    Q_INVOKABLE void loadTemplateResolved(const QString &path, const QString &choice);
    Q_INVOKABLE void applyProposal(const QString &text);
    Q_INVOKABLE void regenerateProposal();
    Q_INVOKABLE void discardProposal();
    [[nodiscard]] Q_INVOKABLE QVariantMap contextMenuNode() const;

signals:
    void deckChanged();
    void currentSlideChanged();
    void playerChanged();
    void zoomRequested(qreal zoom);
    // Dialogs the window answers (see the invokables above).
    void findRequested();
    void replaceRequested();
    void assistantToggleRequested(bool visible);
    void contextMenuRequested(qreal x, qreal y);
    void fillColorRequested(const QColor &current);
    void strokeColorRequested(const QColor &current);
    void imageDialogRequested();
    void mastersRequested();
    void applyMasterRequested();
    void exportPdfRequested(const QString &suggestedPath);
    void generateDeckRequested();
    void templateConsentRequested(const QString &path);
    void proposalRequested(const QString &original, const QString &proposal);
    // M67 dialogs: Insert Table, Split Cells, cell shading, Edit Data.
    void insertTableRequested();
    void splitCellsRequested(int columns, int rows, int maxColumns, int maxRows);
    void tableShadingRequested(const QColor &current);
    void chartDataRequested();
    // M91: the show asks before opening a file; tells what it did not do.
    void fileConsentRequested(const QString &path);
    void showNotice(const QString &text);

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void applyPreferences() override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;

private:
    // show_host.cpp
    void fullRefresh();
    void switchSlide(int row);
    void wireCanvasUndo();
    void wireEditCommands();
    void updateSlideStatus();
    void markEdited();
    // show_host_slides.cpp
    [[nodiscard]] QList<int> matches(const QString &needle, bool caseSensitive) const;
    void loadTemplate(const QString &path);
    // show_host_menus.cpp
    void insertMenus();
    // show_host_ai.cpp
    void createAssistant();
    // show_host_editing.cpp
    void createEditing();
    [[nodiscard]] QList<Quick::MenuNode> chartMenuItems() const;
    // show_host_design.cpp (M75)
    void createDesign();
    // show_host_show.cpp (M91)
    void createSlideShow();
    void designMenus();
    void pushSlideList(const ShowModel::Presentation &before, const QString &label);

    ShowDocument *m_document = nullptr;
    SlideCanvasItem *m_canvas = nullptr;
    QPointer<QObject> m_editor;
    QTextDocument *m_idleDocument = nullptr;
    ShowToolbox *m_toolbox = nullptr;
    ShowArrangeActions *m_arrange = nullptr;
    ShowDesignActions *m_design = nullptr;
    PresentationController *m_player = nullptr;
    ShowTableActions *m_tables = nullptr;
    ChartDataEditor *m_chartData = nullptr;
    bool m_presenterView = false;
    ShowDesignTab *m_designTab = nullptr;
    ShowBackground *m_background = nullptr;
    ShowMasterView *m_masterView = nullptr;
    ShowSections *m_sections = nullptr;
    ShowComments *m_comments = nullptr;
    ShowMedia *m_media = nullptr;
    ShowSlideLayouts *m_layouts = nullptr;
    ShowSlideShow *m_slideShow = nullptr;
    ShowActionSettings *m_actionSettings = nullptr;
    ShowExport *m_export = nullptr;
    ShowEffectOptions *m_effects = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantController *m_assistant = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
    std::unique_ptr<Ai::DocumentContextProvider> m_aiContext;
    std::unique_ptr<Ai::EditApplier> m_aiApplier;
    std::unique_ptr<Ai::DocumentTools> m_aiTools; // D-058, show_tools.h
    std::function<void()> m_proposalApply;
    std::function<void()> m_proposalRegenerate;
    QString m_pendingTemplate;
    ShowModel::Slide m_compoundBefore;
    int m_currentRow = -1;
    int m_revision = 0;
    bool m_compoundEdit = false;
    bool m_syncingMeta = false;
};

} // namespace QindaOffice::Apps::Show
