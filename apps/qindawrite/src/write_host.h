// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaWrite window's host (conversion plan §5.1): the paged text
// document, the editing control, the Format / Table / Insert / Review /
// Tools / Page command sets (the controllers own them as ActionEntries),
// find and replace, the ruler's markers, the print paginator and the
// assistant. The QML window (qml/WriteWindow.qml) shows the pages
// through a PageTextItem the host is handed once (attachPage). Every
// dialog is a request signal the window answers through an invokable;
// the host holds no widgets.
//
// Files: write_host.cpp (document, control, status, page setup, styles,
// print), write_host_menus.cpp (the menu tree, the context menu with
// spelling, templates), write_host_text.cpp (find/replace, ruler, the
// dialog answers), write_host_ai.cpp (the assistant, inline actions).

#include "assistant_controller.h"
#include "document_host.h"
#include "write_document.h"

#include "page_text_item.h"

#include <QColor>
#include <QPointer>
#include <QTextCursor>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Ai {
class InlineActionFlow;
class ModelCatalog;
}

namespace QindaOffice::Apps::Write {

class FormatController;
class PageTextControl;
class SpellChecker;
class TableController;
class WriteReviewActions;
class WriteRevisions;
class WriteTypography;

class WriteHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(QStringList fontSizes READ fontSizes CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY formatChanged)
    Q_PROPERTY(QString fontSize READ fontSize NOTIFY formatChanged)
    Q_PROPERTY(QStringList symbols READ symbols CONSTANT)
    // {margin, pageWidth, firstLine, left, right} in document px: where
    // the ruler's markers sit for the cursor's paragraph.
    Q_PROPERTY(QVariantMap rulerMarkers READ rulerMarkers NOTIFY rulerChanged)
    // {width, height, margin, header, footer}
    Q_PROPERTY(QVariantMap pageSetup READ pageSetup NOTIFY pageSetupChanged)
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    Q_PROPERTY(QindaOffice::Ai::AssistantController *assistant READ assistant CONSTANT)
public:
    WriteHost();
    ~WriteHost() override;

    [[nodiscard]] WriteDocument *writeDocument() const { return m_document; }
    [[nodiscard]] PageTextControl *control() const { return m_control; }
    [[nodiscard]] PageTextItem *pageItem() const { return m_page; }
    [[nodiscard]] FormatController *formatController() const { return m_format; }
    [[nodiscard]] TableController *tableController() const { return m_tables; }
    [[nodiscard]] WriteTypography *typography() const { return m_typography; }
    [[nodiscard]] WriteRevisions *revisions() const { return m_revisions; }
    [[nodiscard]] WriteReviewActions *reviewActions() const { return m_review; }
    [[nodiscard]] SpellChecker *spellChecker() const { return m_spell; }
    [[nodiscard]] Ai::AssistantController *assistant() const { return m_assistant; }

    // The QML page item, once (WritePage.qml's Component.onCompleted).
    Q_INVOKABLE void attachPage(PageTextItem *item);

    // ---- format strip ----
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QStringList fontSizes() const;
    [[nodiscard]] QString fontFamily() const;
    [[nodiscard]] QString fontSize() const;
    Q_INVOKABLE void applyFontFamily(const QString &family);
    Q_INVOKABLE void applyFontSize(const QString &size);
    Q_INVOKABLE void applyTextColor(const QColor &color);
    Q_INVOKABLE void applyHighlightColor(const QColor &color);
    Q_INVOKABLE void applyStyle(int styleId);

    // ---- dialog answers (write_host_text.cpp) ----
    [[nodiscard]] QStringList symbols() const;
    Q_INVOKABLE void insertTable(int rows, int columns);
    Q_INVOKABLE void insertColumns(int columns, int gutterPx);
    Q_INVOKABLE void insertImageFile(const QUrl &url);
    Q_INVOKABLE void insertSymbol(const QString &text);
    Q_INVOKABLE void insertHyperlink(const QString &url, const QString &label);
    Q_INVOKABLE void commitFootnote(const QString &id, const QString &text);
    [[nodiscard]] QVariantMap pageSetup() const;
    Q_INVOKABLE void applyPageSetup(qreal width, qreal height, qreal margin, const QString &header,
                                    const QString &footer);
    Q_INVOKABLE void exportPdfTo(const QUrl &url);
    Q_INVOKABLE void loadTemplateResolved(const QString &path, const QString &choice);
    // ---- find and replace ----
    Q_INVOKABLE bool findNext(const QString &needle, bool caseSensitive);
    Q_INVOKABLE bool findPrevious(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void replaceCurrent(const QString &needle, const QString &replacement, bool caseSensitive);
    Q_INVOKABLE int replaceAll(const QString &needle, const QString &replacement, bool caseSensitive);
    // ---- ruler ----
    [[nodiscard]] QVariantMap rulerMarkers() const;
    // `id` is "indent-first" | "indent-left" | "indent-right"; docX in
    // document px from the page's left edge.
    Q_INVOKABLE void applyRulerMarker(const QString &id, qreal docX);
    // ---- context menu (write_host_menus.cpp) ----
    // The node the window pops up: spelling suggestions for the word
    // under the pointer (when any), then the edit commands.
    [[nodiscard]] Q_INVOKABLE QVariantMap contextMenuNode() const;
    // ---- assistant (write_host_ai.cpp) ----
    Q_INVOKABLE void applyProposal(const QString &text);
    Q_INVOKABLE void regenerateProposal();
    Q_INVOKABLE void discardProposal();

signals:
    void formatChanged();
    void rulerChanged();
    void pageSetupChanged();
    // Preferences ▸ default zoom (the viewport applies it).
    void zoomRequested(qreal zoom);
    // Dialogs the window answers (see the invokables above).
    void findRequested(bool replace);
    void assistantToggleRequested(bool visible);
    void contextMenuRequested(qreal x, qreal y);
    void textColorRequested(const QColor &current);
    void highlightRequested();
    void tableDialogRequested();
    void columnsDialogRequested();
    void imageDialogRequested();
    void symbolRequested();
    void hyperlinkRequested(const QString &selected);
    void wordCountRequested(const QVariantMap &counts);
    void footnoteRequested(const QString &id, const QString &text);
    void pageSetupRequested();
    void exportPdfRequested(const QString &suggestedPath);
    void templateConsentRequested(const QString &path);
    void proposalRequested(const QString &original, const QString &proposal);

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void applyPreferences() override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;

private:
    // write_host.cpp
    void wireEditCommands();
    void updateWriteStatus();
    // write_host_menus.cpp
    void insertMenus();
    void createSpellingEntries();
    void prepareSpellingMenu(const QPointF &docPos);
    // write_host_text.cpp
    void createTextActions();
    bool findIn(const QString &needle, bool caseSensitive, bool backward);
    void loadTemplate(const QString &path);
    // write_host_ai.cpp
    void createAssistant();

    WriteDocument *m_document = nullptr;
    PageTextControl *m_control = nullptr;
    QPointer<PageTextItem> m_page;
    FormatController *m_format = nullptr;
    TableController *m_tables = nullptr;
    WriteTypography *m_typography = nullptr;
    WriteRevisions *m_revisions = nullptr;
    WriteReviewActions *m_review = nullptr;
    SpellChecker *m_spell = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantController *m_assistant = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
    std::unique_ptr<Ai::DocumentContextProvider> m_aiContext;
    std::unique_ptr<Ai::EditApplier> m_aiApplier;
    std::function<void()> m_proposalApply;
    std::function<void()> m_proposalRegenerate;
    QList<Quick::ActionEntry *> m_spellSuggestions;
    Quick::ActionEntry *m_spellIgnore = nullptr;
    QTextCursor m_spellWord;
    QString m_pendingTemplate;
};

} // namespace QindaOffice::Apps::Write
