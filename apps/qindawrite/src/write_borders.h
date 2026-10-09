// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's Borders button, Shading and the Borders and Shading dialog (M70,
// D-079). One command set, as in Word: inside a table it draws on the cells
// the selection covers (docx_table_format.h — the selected cells' own
// w:tcBorders, w:tblBorders for the whole table), elsewhere on the selected
// paragraphs (w:pBdr, docx_paragraph_format.h). The pen — line style, width
// and colour — is what the ribbon's border slots (Table Design ▸ Borders)
// last chose; Shading applies the shading slot's colour (cells, else
// paragraphs). A single-edge command toggles: an edge every selected cell
// or paragraph already draws with the pen is taken away, as Word's button
// does; one drawn otherwise takes the pen.
//
// Every command and the dialog's answer is one undo step on the control's
// current story (body, header or footer).

#include "action_model.h"

#include <QColor>
#include <QObject>
#include <QVariantMap>

namespace QindaOffice::Quick {
struct MenuNode;
}

class QTextCursor;

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

// The Borders and Shading dialog's question and answer for the paragraphs
// `cursor` selects, in any document: the page's story, or a style's sample
// in Modify Style (write_style_draft.h, M73). Not an edit block.
[[nodiscard]] QVariantMap paragraphBordersInfoAt(const QTextCursor &cursor);
void applyParagraphBordersAt(const QTextCursor &cursor, const QVariantMap &answer);

class BordersController final : public QObject {
    Q_OBJECT
    // The pen: a Word line style (w:val), a width in eighths of a point
    // (w:sz) and a colour; the shading slot's colour.
    Q_PROPERTY(int penStyleIndex READ penStyleIndex WRITE setPenStyleIndex NOTIFY penChanged)
    Q_PROPERTY(int penSizeIndex READ penSizeIndex WRITE setPenSizeIndex NOTIFY penChanged)
    Q_PROPERTY(QColor penColor READ penColor WRITE setPenColor NOTIFY penChanged)
    Q_PROPERTY(QColor shadingColor READ shadingColor WRITE setShadingColor NOTIFY penChanged)
    Q_PROPERTY(QStringList penStyles READ penStyleLabels CONSTANT)
    Q_PROPERTY(QStringList penSizes READ penSizeLabels CONSTANT)
public:
    BordersController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                      QObject *parent);

    // Format ▸ Borders (the edge commands) and Format ▸ Shading.
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QStringList borderIds() const;

    [[nodiscard]] int penStyleIndex() const { return m_penStyle; }
    void setPenStyleIndex(int index);
    [[nodiscard]] int penSizeIndex() const { return m_penSize; }
    void setPenSizeIndex(int index);
    [[nodiscard]] QColor penColor() const { return m_penColor; }
    void setPenColor(const QColor &color);
    [[nodiscard]] QColor shadingColor() const { return m_shading; }
    void setShadingColor(const QColor &color);
    [[nodiscard]] QStringList penStyleLabels() const;
    [[nodiscard]] QStringList penSizeLabels() const;
    // Word's names and sizes behind the labels (dialogs use the same lists).
    Q_INVOKABLE QStringList penStyleNames() const;
    Q_INVOKABLE QList<int> penSizeValues() const;

    // The shading slot: applies `color` (invalid: no colour) and remembers it.
    Q_INVOKABLE void applyShading(const QColor &color);

    // The Borders and Shading dialog. Question: {context: "table" |
    // "paragraph", applyTo, edges: {top|bottom|left|right|insideH|insideV:
    // {style, size, color}}, hasInsideH, hasInsideV, fill: "#rrggbb" or ""}.
    // Answer: {applyTo: "cell" | "table" | "paragraph", edges: {…}, fill,
    // fillChanged}. Edges absent from the answer are left alone.
    Q_INVOKABLE QVariantMap dialogInfo() const;
    Q_INVOKABLE bool applyDialog(const QVariantMap &answer);

signals:
    void penChanged();
    void dialogRequested();

private:
    void createActions(Quick::ActionModel *model);
    void applyEdges(int target);
    void syncFromCursor();

    PageTextControl *m_control;
    WriteDocument *m_document;
    QList<Quick::ActionEntry *> m_edges; // Docx::BorderTarget order
    Quick::ActionEntry *m_dialog = nullptr;
    Quick::ActionEntry *m_shade = nullptr;
    Quick::ActionEntry *m_noShade = nullptr;
    int m_penStyle = 0;
    int m_penSize = 1;
    QColor m_penColor = Qt::black;
    QColor m_shading = QColor(0xD9, 0xE2, 0xF3);
};

} // namespace QindaOffice::Apps::Write
