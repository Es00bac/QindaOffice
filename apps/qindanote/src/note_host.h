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
// background), note_host_ink.cpp (the Ink menu: tools, pickers, tools.json).

#include "assistant_controller.h"
#include "document_host.h"
#include "json_settings.h"
#include "note_document.h"
#include "note_live_session.h"
#include "note_page_item.h"

#include <QColor>
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

signals:
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
    void providersRequested();

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
};

} // namespace QindaOffice::Apps::Notes
