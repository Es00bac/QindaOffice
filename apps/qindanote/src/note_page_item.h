// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The note page surface as a QtQuick item (plan §5.4): a fixed-width,
// growing page (height extends in page-height increments as content
// approaches the bottom, bounded at 64 page heights) painted in DOCUMENT
// units — a Tk.Viewport scales it (D-009: zoom is a view transform; ink
// stays in page coordinates). Ink comes from a Tk.StylusHandler (pen
// pressure and tilt, the eraser end, the mouse fallback; touch never
// reaches it — the palm-rejection baseline). Text, checklist and image
// blocks are PAINTED under the ink (D-027: ink over typed notes is the
// point); one live editor at a time is a QML control the window places at
// `editorRect`. One QUndoStack (note_page_history.h) is the whole history.
//
// Files: note_page_item.cpp (state, paint, history glue),
// note_page_item_ink.cpp (stylus gestures, live eraser, cursor),
// note_page_item_blocks.cpp (blocks, tap-to-type, the editor contract).

#include "note_blocks.h"
#include "notebook.h"
#include "spatial_grid.h"
#include "stroke_builder.h"
#include "stroke_edit.h"

#include <qindatk/stylus_handler.h>

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class QUndoStack;

namespace QindaOffice::Apps::Notes {

enum class NoteTool { Pen, Highlighter, Eraser, Text };

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
public:
    explicit NotePageItem(QQuickItem *parent = nullptr);
    ~NotePageItem() override;

    // ---- page ----
    void setPage(NotePage *page);
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
    [[nodiscard]] qreal viewZoom() const { return m_viewZoom; }
    void setViewZoom(qreal zoom);
    [[nodiscard]] QindaTK::StylusHandler *stylus() const { return m_stylus; }
    void setStylus(QindaTK::StylusHandler *handler);

    // ---- history ----
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    void applyHistoryLayer(const Ink::StrokeLayer &layer);
    void applyHistoryBlocks(const QByteArray &blocksJson);
    void applyHistoryBackground(const QString &background);

    // ---- blocks (note_page_item_blocks.cpp) ----
    [[nodiscard]] const QList<PageBlock> &blocks() const { return m_blocks; }
    Q_INVOKABLE void addTextBlock();
    Q_INVOKABLE void addCheckItem();
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
    [[nodiscard]] int editingBlock() const { return m_editingBlock; }
    [[nodiscard]] QString editingKind() const;
    [[nodiscard]] QString editingText() const { return m_editingText; }
    [[nodiscard]] bool editingChecked() const;
    [[nodiscard]] QRectF editorRect() const;
    [[nodiscard]] QFont pageFontValue() const { return pageFont(); }

    // Test seam: the stroke builder's stroke while inking.
    [[nodiscard]] bool isInking() const { return m_inking; }

signals:
    void pageEdited();
    void presentationChanged();
    void pageChanged();
    void historyChanged();
    void editingChanged();
    void stylusChanged();
    // The editor should take focus (a block was opened).
    void editorRequested();

protected:
    void paint(QPainter *painter) override;

private:
    // note_page_item_ink.cpp
    void onStylusBegan(const QindaTK::StylusSample &sample);
    void onStylusMoved(const QindaTK::StylusSample &sample);
    void onStylusEnded(const QindaTK::StylusSample &sample);
    void commitStroke();
    void beginEraseGesture(const QPointF &pagePos);
    void eraseAtPagePos(const QPointF &pagePos);
    void endEraseGesture();
    void beginTemporaryTool(NoteTool tool);
    void restoreTemporaryTool();
    void updateCursor();
    [[nodiscard]] QCursor eraserCursor() const;
    void repaintSegment();
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
    qreal m_pageWidth = 760;
    qreal m_minPageHeight = 1004; // ~A4 ratio at the fixed width
    static constexpr qreal kMaxPages = 64;
};

} // namespace QindaOffice::Apps::Notes
