// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The note page surface as a QtQuick item (plan §5.4): a fixed-width,
// growing page (height extends in page-height increments as content
// approaches the bottom, bounded at 64 page heights) painted in DOCUMENT
// units — a Tk.Viewport scales it (D-009: zoom is a view transform; ink
// stays in page coordinates). Ink comes from a Tk.StylusHandler (pen
// pressure and tilt, the eraser end, the mouse fallback, and one finger
// when `touchDrawing` is on and an ink tool is active — NotePage.qml opts
// the handler into touch; a second finger turns the gesture into the
// Viewport's pinch/pan and the finger's stroke is dropped). Text, checklist and image
// blocks are PAINTED under the ink (D-027: ink over typed notes is the
// point); one live editor at a time is a QML control the window places at
// `editorRect`. One QUndoStack (note_page_history.h) is the whole history.
//
// Files: note_page_item.cpp (state, paint, history glue),
// note_page_item_ink.cpp (stylus gestures, live eraser, cursor),
// note_page_item_blocks.cpp (blocks, tap-to-type, the editor contract),
// note_page_item_rich.cpp (M84: the formatted-text editor's document, its
// history and commands), note_page_item_tags.cpp (M84: tags, links, page
// colour).

#include "note_blocks.h"
#include "notebook.h"
#include "spatial_grid.h"
#include "stroke_builder.h"
#include "stroke_edit.h"

#include <qindatk/stylus_handler.h>

#include <QColor>
#include <QJsonObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QUndoStack;

namespace QindaTK {
class ClipboardAdapter;
class RichTextArea;
class RichTextDocument;
namespace RichText {
struct Selection;
}
}

namespace QindaOffice::Apps::Notes {

enum class NoteTool { Pen, Highlighter, Eraser, Text };

namespace Tags {
struct TagDef;
}
namespace RichStyle {
enum class Style;
enum class TableEdit;
}

class NotePageItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::StylusHandler *stylus READ stylus WRITE setStylus NOTIFY stylusChanged)
    Q_PROPERTY(QString tool READ toolName WRITE setToolName NOTIFY presentationChanged)
    Q_PROPERTY(QColor penColor READ penColor WRITE setPenColor NOTIFY presentationChanged)
    Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth NOTIFY presentationChanged)
    Q_PROPERTY(QColor highlighterColor READ highlighterColor WRITE setHighlighterColor NOTIFY presentationChanged)
    Q_PROPERTY(qreal highlighterWidth READ highlighterWidth WRITE setHighlighterWidth NOTIFY presentationChanged)
    Q_PROPERTY(qreal eraserRadius READ eraserRadius WRITE setEraserRadius NOTIFY presentationChanged)
    Q_PROPERTY(QString eraserMode READ eraserModeName WRITE setEraserModeName NOTIFY presentationChanged)
    Q_PROPERTY(bool pressureEnabled READ pressureEnabled WRITE setPressureEnabled NOTIFY presentationChanged)
    Q_PROPERTY(qreal smoothing READ smoothing WRITE setStrokeSmoothing NOTIFY presentationChanged)
    // Preferences note/touchDrawing: a finger inks with the ink tools.
    Q_PROPERTY(bool touchDrawing READ touchDrawing WRITE setTouchDrawing NOTIFY presentationChanged)
    // The hosting viewport's zoom, for the eraser ring cursor only.
    Q_PROPERTY(qreal viewZoom READ viewZoom WRITE setViewZoom NOTIFY presentationChanged)
    Q_PROPERTY(bool hasPage READ hasPage NOTIFY pageChanged)
    Q_PROPERTY(QString background READ background WRITE setBackground NOTIFY pageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // The one live editor (note_page_item_blocks.cpp): -1 when none.
    Q_PROPERTY(int editingBlock READ editingBlock NOTIFY editingChanged)
    Q_PROPERTY(QString editingKind READ editingKind NOTIFY editingChanged)
    Q_PROPERTY(QString editingText READ editingText NOTIFY editingChanged)
    Q_PROPERTY(bool editingChecked READ editingChecked NOTIFY editingChanged)
    Q_PROPERTY(QRectF editorRect READ editorRect NOTIFY editingChanged)
    Q_PROPERTY(QFont pageFont READ pageFontValue CONSTANT)
    // AGENT-CONTRACT: the live editor's padding (NotePage.qml) equals the
    // painted text inset (note_blocks.h kTextPadding), so text does not jump
    // when a block opens or closes.
    Q_PROPERTY(qreal textPadding READ textPadding CONSTANT)
    // M84 (D-093): the open text block's formatted text — the document the
    // window's Tk.RichTextEditor edits (null for other kinds and when
    // nothing is open). The page item owns it and keeps its history.
    Q_PROPERTY(QindaTK::RichTextDocument *richDocument READ richDocument NOTIFY richDocumentChanged)
    Q_MOC_INCLUDE(<qindatk/rich_text_document.h>)
    // The page colour (M84): the paper the item paints.
    Q_PROPERTY(QColor paperColor READ paperColor NOTIFY pageChanged)
public:
    explicit NotePageItem(QQuickItem *parent = nullptr);
    ~NotePageItem() override;

    // ---- page ----
    void setPage(NotePage *page);
    // The SAME page moved in memory (its section's QVector grew or was
    // reordered): swap the pointer, keep the history and the editor.
    // AGENT-GUARD: any code that adds or moves pages while one is open must
    // call this (or setPage) — the old pointer dangles, and the next paint
    // reads freed memory (it crashed in the ink renderer).
    void rebindPage(NotePage *page)
    {
        m_page = page;
        update();
    }
    [[nodiscard]] NotePage *page() const { return m_page; }
    [[nodiscard]] bool hasPage() const { return m_page != nullptr; }
    [[nodiscard]] QString background() const;
    void setBackground(const QString &background);
    [[nodiscard]] QRectF pageRect() const;
    [[nodiscard]] qreal pageWidth() const { return m_pageWidth; }
    [[nodiscard]] QSizeF sheetSize() const { return {m_pageWidth, m_minPageHeight}; }
    void updatePageMetrics();

    // ---- tools ----
    [[nodiscard]] NoteTool tool() const { return m_tool; }
    void setTool(NoteTool tool);
    [[nodiscard]] QString toolName() const;
    void setToolName(const QString &name);
    [[nodiscard]] const QColor &penColor() const { return m_penColor; }
    void setPenColor(const QColor &color);
    [[nodiscard]] qreal penWidth() const { return m_penWidth; }
    void setPenWidth(qreal width);
    [[nodiscard]] const QColor &highlighterColor() const { return m_highlighterColor; }
    void setHighlighterColor(const QColor &color);
    [[nodiscard]] qreal highlighterWidth() const { return m_highlighterWidth; }
    void setHighlighterWidth(qreal width);
    [[nodiscard]] qreal eraserRadius() const { return m_eraserRadius; }
    void setEraserRadius(qreal radius);
    [[nodiscard]] Ink::EraseMode eraserMode() const { return m_eraserMode; }
    void setEraserMode(Ink::EraseMode mode);
    [[nodiscard]] QString eraserModeName() const;
    void setEraserModeName(const QString &name);
    [[nodiscard]] bool pressureEnabled() const { return m_pressureEnabled; }
    void setPressureEnabled(bool on);
    [[nodiscard]] qreal smoothing() const { return m_smoothing; }
    void setStrokeSmoothing(qreal factor);
    [[nodiscard]] bool touchDrawing() const { return m_touchDrawing; }
    void setTouchDrawing(bool on);
    [[nodiscard]] qreal viewZoom() const { return m_viewZoom; }
    void setViewZoom(qreal zoom);
    [[nodiscard]] QindaTK::StylusHandler *stylus() const { return m_stylus; }
    void setStylus(QindaTK::StylusHandler *handler);

    // ---- history ----
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] int historyIndex() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    void applyHistoryLayer(const Ink::StrokeLayer &layer);
    void applyHistoryBlocks(const QByteArray &blocksJson);
    void applyHistoryBackground(const QString &background);
    void applyHistoryContent(const QByteArray &blocksJson, const QString &title);
    void applyHistoryEnvelope(const Ink::PageEnvelope &envelope);
    bool restoreEnvelope(const Ink::PageEnvelope &envelope);

    // ---- blocks (note_page_item_blocks.cpp) ----
    [[nodiscard]] const QList<PageBlock> &blocks() const { return m_blocks; }
    Q_INVOKABLE void addTextBlock();
    Q_INVOKABLE void addCheckItem();
    // A rendered markdown block under the lowest block (assistant replies).
    Q_INVOKABLE void addMarkdownBlock(const QString &text);
    // The [mm:ss] time under `pos` (page coordinates) in a markdown block,
    // in ms, or -1 (M56: Ctrl+click replays the recording there).
    Q_INVOKABLE qint64 timestampAt(const QPointF &pos) const;
    void addImageBlock(const QByteArray &imageData);
    Q_INVOKABLE bool addImageFile(const QUrl &url);
    Q_INVOKABLE int blockAt(const QPointF &pos) const;
    // The Text tool's tap: toggles a checklist box, opens the block under
    // the point, or starts a new text block there.
    Q_INVOKABLE void tapAt(const QPointF &pos);
    Q_INVOKABLE void beginEdit(int index);
    Q_INVOKABLE void updateEdit(const QString &text);  // live typing (merged history)
    Q_INVOKABLE void commitEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void deleteBlock(int index);
    Q_INVOKABLE void toggleCheck(int index);
    Q_INVOKABLE void insertCheckItemAfter(int index);
    Q_INVOKABLE void indentCheckItem(int index, int delta);
    // Live notes (D-057): the markdown block they write, found by its uid.
    [[nodiscard]] int blockIndexForUid(const QString &uid) const;
    // Merges `fields` into block `index` as one undo step; false while
    // that block is open in the editor (the caller retries).
    bool setBlockFields(int index, const QJsonObject &fields, const QString &undoText);
    void appendMarkdownBlock(const QString &uid, const QString &text);
    // The assistant's edits (D-058, note_tools.cpp): the whole block list
    // at once, one undo step. Closes the editor first.
    bool replaceBlocks(const QJsonArray &blocks, const QString &undoText);
    // An assistant batch, including a rename, is one undo step. Refuses
    // unfinished typing/ink instead of committing it on somebody's behalf.
    bool replaceContent(const QJsonArray &blocks, const QString &title, const QString &undoText);
    [[nodiscard]] int editingBlock() const { return m_editingBlock; }
    [[nodiscard]] QString editingKind() const;
    [[nodiscard]] QString editingText() const { return m_editingText; }
    [[nodiscard]] bool editingChecked() const;
    [[nodiscard]] QRectF editorRect() const;
    [[nodiscard]] QFont pageFontValue() const { return pageFont(); }
    [[nodiscard]] qreal textPadding() const { return kTextPadding; }

    // ---- formatted text (note_page_item_rich.cpp, M84) ----
    [[nodiscard]] QindaTK::RichTextDocument *richDocument() const;
    [[nodiscard]] QindaTK::RichTextArea *richArea() const;
    // NotePage.qml hands over its editor's Tk.RichTextArea once.
    Q_INVOKABLE void attachRichArea(QQuickItem *area);
    // Puts the editor's caret where the tap that opened the block landed
    // (or at the end); NotePage.qml calls it once the editor shows.
    Q_INVOKABLE void placeRichCaret();
    // A QindaTK command (text.bold, paragraph.bulletList, …) on the open
    // block's selection; false when no formatted text is open.
    bool richCommand(const QString &id, const QVariantMap &args = {});
    [[nodiscard]] QVariantMap richCommandState(const QString &id) const;
    bool applyRichStyle(RichStyle::Style style);
    [[nodiscard]] int richStyleAtCaret() const; // RichStyle::Style, or -1
    bool insertRichLink(const QString &href, const QString &label);
    bool insertRichTable(int rows, int columns);
    bool editRichTable(RichStyle::TableEdit edit);
    [[nodiscard]] bool richCaretInTable() const;
    [[nodiscard]] QString richSelectedText() const;
    // The highlight of the selection's first character (invalid: none).
    [[nodiscard]] QColor richHighlight() const;
    // OneNote's indent: a list level in a list, the paragraph's indent
    // (18 pt a step) outside one.
    bool indentRich(int delta);
    // Several transactions as ONE undo step (RichEditCommand macro).
    void beginRichMacro();
    void endRichMacro();
    // RichEditCommand's replay (note_page_history.h): true when the live
    // document took it; false sends the command to its snapshot.
    bool replayRichEdit(QindaTK::RichTextDocument *document, int block, const QStringList &transactions,
                        bool undo, const QByteArray &expected, const QByteArray &target);

    // ---- tags, links, page colour (note_page_item_tags.cpp, M84) ----
    // Toggles `def` on the open block's selected paragraphs (or the open
    // checklist/markdown block); with nothing open, a new text block with
    // one tagged paragraph opens. One undo step.
    bool toggleTag(const Tags::TagDef &def);
    bool removeTags();
    // Marks the To Do tag `entry` of block `index` done or not.
    bool setTagDone(int index, int entry, bool done);
    // The To Do box under `pos`: {block, entry}, or {-1, -1}.
    [[nodiscard]] std::pair<int, int> todoBoxAt(const QPointF &pos) const;
    // Ticks (or unticks) the To Do box under `pos`; false when none is there.
    Q_INVOKABLE bool toggleTodoBoxAt(const QPointF &pos);
    // The link under `pos` (page coordinates) in a painted block, or "".
    Q_INVOKABLE QString linkAt(const QPointF &pos) const;
    [[nodiscard]] QColor paperColor() const;
    void setPageColor(const QColor &color);
    // Puts the editor on block `index`, at paragraph `paragraph` when given.
    void revealBlock(int index, const QString &paragraph);

    // Test seam: the stroke builder's stroke while inking.
    [[nodiscard]] bool isInking() const { return m_inking; }
    [[nodiscard]] bool hasActiveGesture() const { return m_inking || m_erasing; }

signals:
    void pageEdited();
    void pageTitleChanged();
    void presentationChanged();
    void pageChanged();
    void historyChanged();
    void editingChanged();
    void stylusChanged();
    // The editor should take focus (a block was opened).
    void editorRequested();
    // M84: the open rich text document changed, or its selection's
    // formatting / caret context did (the host's checked states follow).
    void richDocumentChanged();
    void richStateChanged();
    // Ctrl+click on a link in the editor or on the page.
    void linkActivated(const QString &href);
    // Scroll `rect` (page coordinates) into view (a search hit on a picture).
    void revealRequested(const QRectF &rect);

protected:
    void paint(QPainter *painter) override;

private:
    // note_page_item_ink.cpp
    void onStylusBegan(const QindaTK::StylusSample &sample);
    void onStylusMoved(const QindaTK::StylusSample &sample);
    void onStylusEnded(const QindaTK::StylusSample &sample);
    void commitStroke();
    void onStylusCanceled();
    void beginEraseGesture(const QPointF &pagePos);
    void eraseAtPagePos(const QPointF &pagePos);
    void endEraseGesture();
    void beginTemporaryTool(NoteTool tool);
    void restoreTemporaryTool();
    void updateCursor();
    [[nodiscard]] QCursor eraserCursor() const;
    void repaintSegment();
    // note_page_item_rich.cpp
    void openRichDocument(const QJsonObject &block);
    void closeRichDocument();
    [[nodiscard]] QindaTK::RichText::Selection richSelection() const;
    void updateRichText(const QString &text);
    void onRichCommitted(const QString &transaction, const QString &label);
    void carryTodoAcrossSplit(QJsonObject *block, const QStringList &before);
    // note_page_item_blocks.cpp
    void relayoutBlocks();
    void writeBlocks(const QJsonArray &blocks, const QString &undoText, int mergeKey = -1);
    void appendBlock(const QJsonObject &block, const QString &undoText);
    // note_page_item.cpp
    void rebuildGrid();
    [[nodiscard]] qreal growHeight() const;
    void notifyHistory();

    NotePage *m_page = nullptr;
    NoteTool m_tool = NoteTool::Pen;
    qreal m_penWidth = 2.5;
    QColor m_penColor = Qt::black; // black reads correctly on light paper
    QColor m_highlighterColor{255, 210, 0};
    qreal m_highlighterWidth = 12.0;
    Ink::EraseMode m_eraserMode = Ink::EraseMode::Stroke;
    qreal m_eraserRadius = 12.0;
    bool m_pressureEnabled = true;
    qreal m_smoothing = 0.25;
    bool m_touchDrawing = true;
    bool m_touchGesture = false; // the current ink gesture is a finger's
    qreal m_viewZoom = 1.0;
    QindaTK::StylusHandler *m_stylus = nullptr;
    Ink::StrokeBuilder m_builder;
    bool m_inking = false;
    bool m_erasing = false;
    bool m_eraseChanged = false;
    Ink::StrokeLayer m_eraseSnapshot;
    bool m_temporaryTool = false;
    NoteTool m_toolBeforeTemporary = NoteTool::Pen;
    Ink::SpatialGrid m_grid{64.0};
    QUndoStack *m_history = nullptr;
    QList<PageBlock> m_blocks;
    int m_editingBlock = -1;
    QString m_editingText;
    QByteArray m_editBefore; // blocksJson when the editor opened
    // M84: the open text block's formatted text and its editor's area.
    QindaTK::RichTextDocument *m_richDocument = nullptr;
    QPointer<QindaTK::RichTextArea> m_richArea;
    QindaTK::ClipboardAdapter *m_clipboard = nullptr;
    QStringList m_richParagraphs;     // paragraph ids after the last commit
    std::pair<QString, int> m_pendingCaret{QString(), -1};
    int m_richMacro = 0;              // the open macro's serial, 0 when none
    int m_richMacroSerial = 0;
    bool m_replayingRich = false;
    qreal m_pageWidth = 760;
    qreal m_minPageHeight = 1004; // ~A4 ratio at the fixed width
    static constexpr qreal kMaxPages = 64;
};

} // namespace QindaOffice::Apps::Notes
