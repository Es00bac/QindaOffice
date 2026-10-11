// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaNote window's host (plan §5.4): the notebook document, which
// page is open, the navigator model, the Ink and Page command sets, find /
// replace across the notebook, the print renderer and Export PDF. The QML
// window (qml/NoteWindow.qml) renders the page through a NotePageItem the
// host is handed at start-up (attachPageItem).
//
// AGENT-GUARD: the host holds section/page IDS, never pointers — the
// notebook's QVectors reallocate on every append/load and a stale pointer
// was use-after-free (and once wrote garbage-named page files).
//
// Files: note_host.cpp (document, pages, status), note_host_text.cpp
// (find/replace, Export PDF, print), note_host_live.cpp (live notes),
// note_host_structure.cpp (the Page menu: new/rename/delete/move,
// background), note_host_ink.cpp (the Ink menu: tools, pickers, tools.json),
// and M84 (D-093): note_host_format.cpp (formatted typing: Format and
// Insert commands), note_host_tags.cpp (Tags, Find Tags), note_host_pages.cpp
// (search, import, page templates, page colour, subpages, links).

#include "assistant_controller.h"
#include "document_host.h"
#include "json_settings.h"
#include "note_document.h"
#include "note_live_session.h"
#include "note_page_item.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class QTimer;

namespace QindaOffice::Ai {
class ModelCatalog;
class ProviderSettings;
}

namespace QindaOffice::Apps::Notes {

class NavigatorModel;
class NoteSearchModel;
class NoteTagSummary;

// AGENT-NOTE: not `final` — QML_ELEMENT derives a creation helper from it.
class NoteHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(NavigatorModel *navigator READ navigator CONSTANT)
    Q_PROPERTY(QString currentSectionId READ currentSectionId NOTIFY currentPageChanged)
    Q_PROPERTY(QString currentPageId READ currentPageId NOTIFY currentPageChanged)
    Q_PROPERTY(QString currentPageTitle READ currentPageTitle NOTIFY currentPageChanged)
    Q_PROPERTY(QStringList backgrounds READ backgrounds CONSTANT)
    Q_PROPERTY(QVariantList penColors READ penColors CONSTANT)
    Q_PROPERTY(QVariantList highlighterColors READ highlighterColors CONSTANT)
    Q_PROPERTY(QVariantList penWidths READ penWidths CONSTANT)
    Q_PROPERTY(QVariantList highlighterWidths READ highlighterWidths CONSTANT)
    Q_PROPERTY(QVariantList eraserSizes READ eraserSizes CONSTANT)
    // Settable twin of DocumentHost::preferencesPage: the window names its
    // page (NotePreferencesPage.qml) from QML.
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    // The assistant (note_host_ai.cpp): its panel binds to this; the live
    // editor's selection flows in through editorSelection.
    Q_PROPERTY(QindaOffice::Ai::AssistantController *assistant READ assistant CONSTANT)
    Q_PROPERTY(QString editorSelection READ editorSelection WRITE setEditorSelection NOTIFY editorSelectionChanged)
    // Live notes (note_host_live.cpp, D-057) and the AI providers dialog.
    Q_PROPERTY(QindaOffice::Apps::Notes::LiveNotesSession *live READ live CONSTANT)
    Q_PROPERTY(QindaOffice::Ai::ProviderSettings *providerSettings READ providerSettings CONSTANT)
    Q_MOC_INCLUDE("provider_settings.h")
    // The Session panel's view of the open page (M56, note_host_session.cpp):
    // {hasNotes, audioUrl, sections: [{heading, ms}], unclear: [{text,
    // guess, ms}], hasCards}.
    Q_PROPERTY(QVariantMap pageSession READ pageSession NOTIFY sessionChanged)
    // M84 (D-093): the formatting of the open text's selection for the
    // ribbon's font, size and colour widgets — {editable, family, size,
    // color, highlight, inTable}.
    Q_PROPERTY(QVariantMap textFormat READ textFormat NOTIFY textFormatChanged)
    // Tags (built-in and the user's custom ones): [{id, name, icon, color,
    // todo, highlight, custom}].
    Q_PROPERTY(QVariantList tagCatalog READ tagCatalog NOTIFY tagCatalogChanged)
    Q_PROPERTY(QStringList customTagIcons READ customTagIcons CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Notes::NoteTagSummary *tagSummary READ tagSummary CONSTANT)
    Q_PROPERTY(QindaOffice::Apps::Notes::NoteSearchModel *search READ search CONSTANT)
    Q_PROPERTY(QStringList importNameFilters READ importNameFilters CONSTANT)
    Q_PROPERTY(QStringList pageColorIds READ pageColorIds CONSTANT)
    Q_MOC_INCLUDE("note_search.h")
    Q_MOC_INCLUDE("note_tag_summary.h")
public:
    NoteHost();
    ~NoteHost() override;

    [[nodiscard]] NoteDocument *noteDocument() const { return m_document; }
    [[nodiscard]] NavigatorModel *navigator() const { return m_navigator; }
    [[nodiscard]] NotePageItem *pageItem() const { return m_pageItem; }

    // The QML page item, once (NotePage.qml's Component.onCompleted).
    Q_INVOKABLE void attachPageItem(NotePageItem *item);
    // Navigator selection → the open page.
    Q_INVOKABLE void selectPage(const QString &sectionId, const QString &pageId);
    Q_INVOKABLE void selectRow(int row);
    [[nodiscard]] const QString &currentSectionId() const { return m_currentSectionId; }
    [[nodiscard]] const QString &currentPageId() const { return m_currentPageId; }
    [[nodiscard]] QString currentPageTitle() const;
    [[nodiscard]] NotePage *currentPage() const;
    [[nodiscard]] NoteSection *currentSection() const;
    // Writes the open page to disk now (debounced normally).
    Q_INVOKABLE void flushPersist();
    Q_INVOKABLE QVariantMap pageVersions() const;
    Q_INVOKABLE QVariantMap pageVersionPreview(const QString &id) const;
    Q_INVOKABLE QVariantMap savePageVersion();
    Q_INVOKABLE QVariantMap restorePageVersion(const QString &id);
    Q_INVOKABLE QVariantMap deletePageVersion(const QString &id);
    Q_INVOKABLE QVariantMap keepPageVersions(bool enabled);

    // Find / replace across the notebook (titles and typed text).
    Q_INVOKABLE bool findNext(const QString &needle, bool caseSensitive);
    Q_INVOKABLE bool findPrevious(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void addImageFile(const QUrl &url);
    Q_INVOKABLE void applyCustomPenColor(const QColor &color) { applyPenColor(color); }
    Q_INVOKABLE int replaceAll(const QString &needle, const QString &replacement);
    Q_INVOKABLE void exportPdfTo(const QUrl &url);

    // Structure commands answered by QML dialogs (note_host_structure.cpp).
    Q_INVOKABLE void createSection(const QString &name);
    Q_INVOKABLE void renamePage(const QString &sectionId, const QString &pageId, const QString &title);
    Q_INVOKABLE void renameSection(const QString &sectionId, const QString &name);
    Q_INVOKABLE void deletePageConfirmed();
    Q_INVOKABLE void deleteSectionConfirmed();
    Q_INVOKABLE void setBackground(const QString &background);
    [[nodiscard]] QStringList backgrounds() const;

    // Picker data for the tool strip (note_host_ink.cpp).
    [[nodiscard]] QVariantList penColors() const;
    [[nodiscard]] QVariantList highlighterColors() const;
    [[nodiscard]] QVariantList penWidths() const;
    [[nodiscard]] QVariantList highlighterWidths() const;
    [[nodiscard]] QVariantList eraserSizes() const;
    Q_INVOKABLE void applyPenColor(const QColor &color);
    Q_INVOKABLE void applyHighlighterColor(const QColor &color);
    Q_INVOKABLE void applyPenWidth(qreal width);
    Q_INVOKABLE void applyHighlighterWidth(qreal width);
    Q_INVOKABLE void applyEraserRadius(qreal radius);

    [[nodiscard]] Ai::AssistantController *assistant() const { return m_assistant; }
    [[nodiscard]] QString editorSelection() const { return m_editorSelection; }
    void setEditorSelection(const QString &text);
    // The current page's typed text (blocks), for the assistant's document
    // scope; ink is not text and is never sent.
    [[nodiscard]] QString currentPageText() const;

    // Live notes: the page side of a session (note_host_live.cpp).
    [[nodiscard]] LiveNotesSession *live() const { return m_live; }
    [[nodiscard]] Ai::ProviderSettings *providerSettings() const { return m_providerSettings; }
    // Opens (or reuses an empty current) page titled after `topic` and
    // appends the markdown block the notes go into.
    LiveTarget beginLiveNotes(const QString &topic);
    // The notes as they are on the page now ("" before the first update).
    [[nodiscard]] QString liveNotesText(const LiveTarget &target) const;
    [[nodiscard]] QString liveTranscript(const LiveTarget &target) const;
    // One undo step; false while the block is open in the editor.
    bool applyLiveNotes(const LiveTarget &target, const QString &notes);
    void storeLiveTranscript(const LiveTarget &target, const QString &transcript);
    void endLiveNotes(const LiveTarget &target, bool wroteNotes);

    // Session review and page commands (note_host_session.cpp, M56).
    [[nodiscard]] QVariantMap pageSession() const;
    [[nodiscard]] QString currentPageNotes() const;
    [[nodiscard]] QString recordingPathFor(const LiveTarget &target) const;
    void setLiveRecording(const LiveTarget &target, const QString &path);
    // Notes of the pages before the open one in its section (series memory).
    [[nodiscard]] QString earlierNotesInSection(int budget) const;
    // A new page right after the open one, holding one markdown block.
    QString addNotesPage(const QString &title, const QString &markdown);
    // Long notes → an index page plus one page per topic.
    void splitLiveNotes(const LiveTarget &target);
    Q_INVOKABLE void splitCurrentPage();
    Q_INVOKABLE bool resolveUnclear(const QString &annotation, const QString &replacement);
    Q_INVOKABLE bool exportMarkdownTo(const QUrl &url);
    // The open page as a Word document (note_word_export.h).
    Q_INVOKABLE bool exportWordTo(const QUrl &url);
    Q_INVOKABLE void openInWrite();
    Q_INVOKABLE bool exportAnkiTo(const QUrl &url);
    // Ctrl+click on a [12:40] in the notes, or a section in the panel.
    Q_INVOKABLE void playRecordingAt(qint64 ms) { emit playRecordingRequested(ms); }

    // ---- M84 (D-093): formatted typing, tags, search, import ----
    [[nodiscard]] QVariantMap textFormat() const;
    Q_INVOKABLE void applyFontFamily(const QString &family);
    Q_INVOKABLE void applyFontSize(qreal points);
    Q_INVOKABLE void applyTextColor(const QColor &color);
    // An invalid colour clears the highlight.
    Q_INVOKABLE void applyHighlight(const QColor &color);
    Q_INVOKABLE void applyLink(const QString &address, const QString &label);
    Q_INVOKABLE void insertTable(int rows, int columns);
    Q_INVOKABLE void linkFile(const QUrl &url);
    // Ctrl+click on a link: web pages and documents open in their apps.
    Q_INVOKABLE void openLink(const QString &href);
    // The open editor takes the keyboard back (after a ribbon widget).
    Q_INVOKABLE void focusEditor() { emit editorFocusRequested(); }
    [[nodiscard]] QVariantList tagCatalog() const;
    [[nodiscard]] QStringList customTagIcons() const;
    Q_INVOKABLE void applyTag(const QString &tagId);
    Q_INVOKABLE bool addCustomTag(const QString &name, const QString &icon, const QColor &color);
    Q_INVOKABLE void removeCustomTag(const QString &tagId);
    [[nodiscard]] NoteTagSummary *tagSummary() const { return m_tagSummary; }
    [[nodiscard]] NoteSearchModel *search() const { return m_search; }
    // A To Do tag ticked on any page (the open one through its history).
    bool setTagDone(const QString &sectionId, const QString &pageId, int block, int entry, bool done);
    // Opens a page and puts the caret in block `block` (paragraph when given).
    Q_INVOKABLE void navigateTo(const QString &sectionId, const QString &pageId, int block,
                                const QString &paragraph = {});
    [[nodiscard]] QStringList importNameFilters() const;
    Q_INVOKABLE bool importFile(const QUrl &url);
    [[nodiscard]] QStringList pageColorIds() const;
    Q_INVOKABLE void applyPageColor(const QColor &color);
    // A new page holding `blocks` after page `after` (the open one when
    // empty) in the open section; returns its id.
    QString addPageAfterCurrent(const QString &title, const QJsonArray &blocks,
                                const QString &background = QStringLiteral("blank"),
                                const QString &after = {});
    Q_INVOKABLE void createTagSummaryPage();

signals:
    void pageVersionsRequested();
    void currentPageChanged();
    // The window opens the matching dialog and answers through the
    // invokables above.
    void newSectionRequested();
    void renamePageRequested(const QString &sectionId, const QString &pageId, const QString &title);
    void renameSectionRequested(const QString &sectionId, const QString &name);
    void deletePageRequested(const QString &title);
    void deleteSectionRequested(const QString &name, int pages);
    void replaceRequested();
    void exportPdfRequested(const QString &suggestedPath);
    void findRequested();
    void navigatorToggleRequested(bool visible);
    void addImageRequested();
    void customPenColorRequested(const QColor &current);
    void editorSelectionChanged();
    // AI ▸ Assistant… toggles the panel; replies land through the editor.
    void assistantToggleRequested(bool visible);
    void assistantInsertRequested(const QString &text);
    void assistantReplaceRequested(const QString &text);
    // AI ▸ Live Notes toggles the live-notes panel; AI ▸ AI Providers…
    // opens the providers dialog.
    void liveNotesToggleRequested(bool visible);
    // Start with "ask before every session" on: the window opens the setup.
    void liveNotesSetupRequested();
    void providersRequested();
    // M56: the Session panel's data changed; play the recording from `ms`;
    // the export dialogs.
    void sessionChanged();
    void playRecordingRequested(qint64 ms);
    void sessionPanelToggleRequested(bool visible);
    void exportMarkdownRequested(const QString &suggestedPath);
    void exportWordRequested(const QString &suggestedPath);
    void exportAnkiRequested(const QString &suggestedPath);
    // M84: the dialogs and panes of formatted typing, tags, search, import.
    void linkDialogRequested(const QString &selected, const QString &address);
    void tableDialogRequested();
    void tagsDialogRequested(bool customize);
    void fileLinkRequested();
    void importRequested();
    // target: "text" | "highlight" | "page"
    void formatColorRequested(const QString &target, const QColor &current);
    void findTagsToggleRequested(bool visible);
    void searchRequested();
    void editorFocusRequested();
    void textFormatChanged();
    void tagCatalogChanged();

protected:
    [[nodiscard]] QString filterString() const override;
    void applyPreferences() override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;

private:
    // note_host.cpp
    bool findStep(const QString &needle, bool caseSensitive, bool backward);
    void rebuildNavigator();
    void openPage(const QString &sectionId, const QString &pageId);
    void persistCurrentPage();
    void updatePageStatus();
    void wireEditCommands();
    void insertMenus();
    void insertM84Menus();
    // note_host_structure.cpp
    void createStructureActions();
    void syncStructureEnablement();
    void newPage();
    void movePage(int delta);
    void stepPage(int delta);
    void duplicatePage();
    // note_host_ink.cpp
    void createInkActions();
    void syncInkChecks();
    void loadToolState();
    void saveToolState();
    // note_host_ai.cpp
    void createAssistant();
    // note_host_format.cpp (M84)
    void createFormatActions();
    void syncFormatActions();
    bool richCommand(const QString &id, const QVariantMap &args = {});
    // note_host_tags.cpp (M84)
    void createTagActions();
    void syncTagActions();
    // note_host_pages.cpp (M84)
    void createPageActions();
    void syncPageActions();
    void newPageFromTemplate(const QString &templateId);
    void changePageLevel(int delta);
    void applyPendingNavigation();
    // note_host_live.cpp
    void createLiveNotes();
    void syncLiveActions();
    [[nodiscard]] QString liveBlockField(const LiveTarget &target, const QString &field) const;
    bool writeLiveBlock(const LiveTarget &target, const QJsonObject &fields, bool undoable);

    NoteDocument *m_document = nullptr;
    NavigatorModel *m_navigator = nullptr;
    QPointer<NotePageItem> m_pageItem;
    QTimer *m_persistTimer = nullptr;
    QString m_currentSectionId;
    QString m_currentPageId;
    std::unique_ptr<Core::JsonSettings> m_toolState;
    bool m_applyingInk = false;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantController *m_assistant = nullptr;
    std::unique_ptr<Ai::DocumentContextProvider> m_aiContext;
    std::unique_ptr<Ai::EditApplier> m_aiApplier;
    std::unique_ptr<Ai::DocumentTools> m_aiTools; // D-058, note_tools.h
    QString m_editorSelection;
    LiveNotesSession *m_live = nullptr;
    Ai::ProviderSettings *m_providerSettings = nullptr;
    QTimer *m_sessionTimer = nullptr;
    // M84
    NoteTagSummary *m_tagSummary = nullptr;
    NoteSearchModel *m_search = nullptr;
    QTimer *m_tagRefresh = nullptr;
    bool m_syncingFormat = false;
    QColor m_highlightColor{255, 225, 77};
};

} // namespace QindaOffice::Apps::Notes
