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

class BordersController;
class FontController;
class FormatController;
class FormatPainter;
class ListController;
class NavigationModel;
class StylesController;
class PageTextControl;
class ParagraphController;
class TableLayoutController;
class SpellChecker;
class TableController;
class WriteReviewActions;
class WriteComments;
class WriteControls;
class WriteRevisions;
class WriteTypography;
class EnvelopesController;
class MailMergeController;
class NotesController;
class PictureLayoutController;
class SectionsController;
class GraphicsController;

class WriteHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(QStringList fontSizes READ fontSizes CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY formatChanged)
    Q_PROPERTY(QString fontSize READ fontSize NOTIFY formatChanged)
    Q_PROPERTY(QStringList symbols READ symbols CONSTANT)
    // {margin, rightMargin, pageWidth, firstLine, left, right} in document px: where
    // the ruler's markers sit for the cursor's paragraph.
    Q_PROPERTY(QVariantMap rulerMarkers READ rulerMarkers NOTIFY rulerChanged)
    // {width, height, left, top, right, bottom, headerDistance,
    // footerDistance, landscape, differentFirstPage, header, footer,
    // headerRich, footerRich} of the caret's section, plus (M88) its
    // sectionStart, section (1-based) and the document's sections
    Q_PROPERTY(QVariantMap pageSetup READ pageSetup NOTIFY pageSetupChanged)
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    Q_PROPERTY(QindaOffice::Ai::AssistantController *assistant READ assistant CONSTANT)
    // Review comments for the Comments pane (write_comments.h).
    Q_PROPERTY(QObject *comments READ commentsObject CONSTANT)
    // Word form fields: check boxes, lists, dates (write_controls.h).
    Q_PROPERTY(QObject *controls READ controlsObject CONSTANT)
    // Word's formatting dialogs and the border pen (M70, D-079):
    // write_borders.h, write_paragraph.h, write_font.h, table_layout.h.
    Q_PROPERTY(QObject *borders READ bordersObject CONSTANT)
    Q_PROPERTY(QObject *paragraphFormat READ paragraphObject CONSTANT)
    Q_PROPERTY(QObject *fontFormat READ fontObject CONSTANT)
    Q_PROPERTY(QObject *tableLayout READ tableLayoutObject CONSTANT)
    // Word's lists and styles (M73, D-082): write_lists.h, write_styles.h.
    Q_PROPERTY(QObject *lists READ listsObject CONSTANT)
    Q_PROPERTY(QObject *styles READ stylesObject CONSTANT)
    // Word's Format Painter and Navigation pane (M73): write_painter.h,
    // write_navigation.h.
    Q_PROPERTY(QObject *painter READ painterObject CONSTANT)
    Q_PROPERTY(QObject *navigation READ navigationObject CONSTANT)
    // Word's Mailings tab, envelopes and labels, endnotes and the Footnote
    // and Endnote dialog, picture wrapping and position (M77, D-086):
    // write_mailmerge.h, write_envelopes.h, write_notes.h, write_float.h.
    Q_PROPERTY(QObject *mailMerge READ mailMergeObject CONSTANT)
    Q_PROPERTY(QObject *envelopes READ envelopesObject CONSTANT)
    Q_PROPERTY(QObject *notes READ notesObject CONSTANT)
    Q_PROPERTY(QObject *pictureLayout READ pictureLayoutObject CONSTANT)
    // Word sections (M88, D-096): breaks, orientation, Link to Previous,
    // Different First Page / Odd & Even, Format Page Numbers
    // (write_section_commands.h).
    Q_PROPERTY(QObject *sections READ sectionsObject CONSTANT)
    Q_PROPERTY(QObject *graphics READ graphicsObject CONSTANT)
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
    [[nodiscard]] BordersController *bordersController() const { return m_borders; }
    [[nodiscard]] ParagraphController *paragraphController() const { return m_paragraph; }
    [[nodiscard]] FontController *fontController() const { return m_font; }
    [[nodiscard]] TableLayoutController *tableLayoutController() const { return m_tableLayout; }
    [[nodiscard]] ListController *listController() const { return m_lists; }
    [[nodiscard]] StylesController *stylesController() const { return m_styles; }
    [[nodiscard]] FormatPainter *formatPainter() const { return m_painter; }
    [[nodiscard]] NavigationModel *navigation() const { return m_navigation; }
    [[nodiscard]] MailMergeController *mailMergeController() const { return m_mailMerge; }
    [[nodiscard]] EnvelopesController *envelopesController() const { return m_envelopes; }
    [[nodiscard]] NotesController *notesController() const { return m_notes; }
    [[nodiscard]] PictureLayoutController *pictureLayoutController() const { return m_pictureLayout; }
    [[nodiscard]] GraphicsController *graphicsController() const { return m_graphics; }
    [[nodiscard]] SectionsController *sectionsController() const { return m_sections; }
    // A Word package opened in a window of its own, untitled and unsaved
    // (a merged document, an envelope, a label sheet — M77): the next
    // QindaWrite window this process creates adopts it (write_host_m77.cpp).
    void openDocumentWindow(const QByteArray &docx);
    // Prints `document` (laid out like this one) through the shell's print
    // dialog instead of this document, once.
    void printOther(std::shared_ptr<QTextDocument> document);

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
    // {width, height, left, top, right, bottom, headerDistance,
    // footerDistance} in document px, plus differentFirstPage, header,
    // footer (plain patterns; {page}/{pages}); M88: applyTo ("section",
    // "forward", "document"; the whole document by default) and
    // sectionStart ("nextPage", "continuous", "evenPage", "oddPage").
    // One undo step.
    Q_INVOKABLE void applyPageSetup(const QVariantMap &setup);
    // The selected picture for the Picture dialog: width/height and its
    // natural size (document px), alt text; empty without a picture.
    Q_INVOKABLE QVariantMap pictureInfo() const;
    // {width, height, altText} onto the selected picture, one undo step.
    Q_INVOKABLE bool applyPicture(const QVariantMap &picture);
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
    // Format > Picture, or a double-click on a picture.
    void pictureDialogRequested();
    // New Comment / Show Comments: the window shows or hides the Comments
    // pane (`compose`: shown with the new-comment field focused); it acts
    // only when the pane's state differs, so the checked sync cannot loop.
    void commentsPaneToggleRequested(bool visible, bool compose);
    void exportPdfRequested(const QString &suggestedPath);
    // A document openDocumentWindow() handed on (M77); the tests read it.
    void documentMade(const QByteArray &docx);
    void templateConsentRequested(const QString &path);
    void proposalRequested(const QString &original, const QString &proposal);

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void applyPreferences() override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;
    // The first page's paper (M88: each later page sets its own).
    [[nodiscard]] bool printPrefersLandscape() const override;
    [[nodiscard]] std::optional<QPageLayout> printPageLayout() const override;

private:
    // write_host.cpp
    void wireEditCommands();
    void updateWriteStatus();
    // write_host_menus.cpp
    void insertMenus();
    // Edit Header / Edit Footer / Page Number / Number of Pages / Close
    // Header and Footer (page_story_editor.h); ids writeEditHeaderAction …
    [[nodiscard]] QList<Quick::MenuNode> headerFooterMenuItems();
    [[nodiscard]] QList<Quick::MenuNode> commentMenuItems();
    [[nodiscard]] QObject *commentsObject() const;
    [[nodiscard]] QObject *controlsObject() const;
    [[nodiscard]] QObject *bordersObject() const;
    [[nodiscard]] QObject *paragraphObject() const;
    [[nodiscard]] QObject *fontObject() const;
    [[nodiscard]] QObject *tableLayoutObject() const;
    [[nodiscard]] QObject *listsObject() const;
    [[nodiscard]] QObject *stylesObject() const;
    [[nodiscard]] QObject *painterObject() const;
    [[nodiscard]] QObject *navigationObject() const;
    // write_host_m77.cpp
    void createMailings();
    void adoptPendingDocument();
    [[nodiscard]] QList<Quick::MenuNode> mailingsMenu() const;
    [[nodiscard]] QObject *mailMergeObject() const;
    [[nodiscard]] QObject *envelopesObject() const;
    [[nodiscard]] QObject *notesObject() const;
    [[nodiscard]] QObject *pictureLayoutObject() const;
    [[nodiscard]] QObject *sectionsObject() const;
    [[nodiscard]] QObject *graphicsObject() const;
    void insertPageField(bool total);
    void syncStoryActions();
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
    const QStringList m_fontFamilies;
    QPointer<PageTextItem> m_page;
    FormatController *m_format = nullptr;
    TableController *m_tables = nullptr;
    WriteTypography *m_typography = nullptr;
    WriteRevisions *m_revisions = nullptr;
    WriteComments *m_comments = nullptr;
    WriteControls *m_controls = nullptr;
    BordersController *m_borders = nullptr;
    ParagraphController *m_paragraph = nullptr;
    FontController *m_font = nullptr;
    TableLayoutController *m_tableLayout = nullptr;
    ListController *m_lists = nullptr;
    StylesController *m_styles = nullptr;
    FormatPainter *m_painter = nullptr;
    NavigationModel *m_navigation = nullptr;
    MailMergeController *m_mailMerge = nullptr;
    EnvelopesController *m_envelopes = nullptr;
    NotesController *m_notes = nullptr;
    PictureLayoutController *m_pictureLayout = nullptr;
    SectionsController *m_sections = nullptr;
    GraphicsController *m_graphics = nullptr;
    std::shared_ptr<QTextDocument> m_printOverride;
    WriteReviewActions *m_review = nullptr;
    SpellChecker *m_spell = nullptr;
    Ai::ModelCatalog *m_catalog = nullptr;
    Ai::AssistantController *m_assistant = nullptr;
    Ai::InlineActionFlow *m_inline = nullptr;
    std::unique_ptr<Ai::DocumentContextProvider> m_aiContext;
    std::unique_ptr<Ai::EditApplier> m_aiApplier;
    std::unique_ptr<Ai::DocumentTools> m_aiTools; // D-058, write_tools.h
    std::function<void()> m_proposalApply;
    std::function<void()> m_proposalRegenerate;
    QList<Quick::ActionEntry *> m_spellSuggestions;
    Quick::ActionEntry *m_spellIgnore = nullptr;
    Quick::ActionEntry *m_closeStory = nullptr;
    Quick::ActionEntry *m_picture = nullptr;
    QTextCursor m_spellWord;
    QString m_pendingTemplate;
};

} // namespace QindaOffice::Apps::Write
