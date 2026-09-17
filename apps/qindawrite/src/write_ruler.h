// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The horizontal ruler above the page (plan section 6.1 UI completeness).
//
// A word processor's ruler is not decoration: it is where the page's
// margins and the current paragraph's indents are both READ and SET. This
// one draws the page band at the view's own zoom and horizontal position
// (so the tick at 3 cm really is above the 3 cm point of the paper), and
// carries three draggable markers — first line, left indent, right indent —
// that write straight into the cursor block's QTextBlockFormat.
//
// Stock painting only: theme palette roles, no colour literals beyond the
// marker fill, which uses the highlight role (AGENTS.md rule 3).

#include <QWidget>

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;

class WriteRuler final : public QWidget {
    Q_OBJECT
public:
    WriteRuler(PageTextView *view, WriteDocument *document, QWidget *parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    // Test seam: the view-space x of the given document-space x.
    [[nodiscard]] qreal documentXToView(qreal docX) const;
    // Test seam: where the three markers currently sit, in document px.
    struct Markers {
        qreal firstLine = 0;
        qreal left = 0;
        qreal right = 0;
    };
    [[nodiscard]] Markers markers() const;

public slots:
    // Re-reads the cursor's block format and the page geometry.
    void refresh();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum class Handle { None, FirstLine, Left, Right };

    [[nodiscard]] QRectF pageBand() const;
    [[nodiscard]] Handle handleAt(const QPointF &pos) const;
    [[nodiscard]] QRectF handleRect(Handle handle) const;
    void applyDrag(const QPointF &pos);

    PageTextView *m_view;
    WriteDocument *m_document;
    Handle m_dragging = Handle::None;

    static constexpr int kHeight = 22;
    static constexpr qreal kHandlePx = 9.0;
    // One centimetre at 96 dpi; the ruler is metric because the page
    // defaults are (A4 at 96 dpi, PageGeometry).
    static constexpr qreal kPxPerCm = 96.0 / 2.54;
};

} // namespace QindaOffice::Apps::Write
