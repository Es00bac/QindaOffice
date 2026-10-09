// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// LabelSheet: the mailing labels a label report shows (M80, D-089 §7) —
// the engine's placements (base_labels.h) painted by ONE painter for the
// window (LabelPageItem), the printer and PDF (BaseHost's print callback),
// so the page on screen is the page that prints.
//
// AGENT-CONTRACT (with BaseHost::printRenderCallback/printPageLayout and
// qml/ReportView.qml):
//  * Geometry is the engine's, in points from the page's top-left corner;
//    paintPage scales it by the caller's pixels per point, and sizes fonts
//    by the painter's device resolution so a 10 pt line is 10 pt on paper.
//  * The print layout is the sheet's paper in full-page mode with no
//    margins (printPageLayout), because Avery's numbers are measured from
//    the paper's edge.
//  * Lines that do not fit a label shrink (down to 6 pt) and then elide;
//    nothing is drawn outside a label.
//
// AGENT-NOTE (D-004, AGENTS.md rule 3): a label page is paper — white,
// black ink — on every theme; it is the document surface, not chrome, so
// its colours are the one literal allowed.

#include "base_labels.h"

#include <QObject>
#include <QPageLayout>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class QPainter;

namespace QindaOffice::Apps::Base {

class LabelSheet : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by BaseService.")
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(int records READ records NOTIFY changed)
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString productLabel READ productLabel NOTIFY changed)
    Q_PROPERTY(QSizeF pageSize READ pageSize NOTIFY changed)
public:
    explicit LabelSheet(QObject *parent = nullptr);

    void setRun(const QString &name, const ::QindaOffice::Base::LabelRun &run, qreal fontSize);
    void clear();
    [[nodiscard]] int pageCount() const { return m_run.pageCount; }
    [[nodiscard]] int records() const { return m_run.records; }
    [[nodiscard]] bool truncated() const { return m_run.truncated; }
    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] QString productLabel() const;
    [[nodiscard]] QSizeF pageSize() const { return m_run.product.page; } // points
    [[nodiscard]] QPageLayout pageLayout() const;

    // Page `index` (0-based) at `pixelsPerPoint`, origin at the paper's corner.
    void paintPage(QPainter *painter, int index, qreal pixelsPerPoint) const;
    // The labels on page `index` as [{x, y, width, height, lines}] (points).
    Q_INVOKABLE QVariantList pageLabels(int index) const;

signals:
    void changed();

private:
    QString m_name;
    ::QindaOffice::Base::LabelRun m_run;
    qreal m_fontSize = 10;
};

// One page of a LabelSheet in the window, at 96 px per inch times `zoom`.
class LabelPageItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Apps::Base::LabelSheet *sheet READ sheet WRITE setSheet NOTIFY sheetChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
public:
    explicit LabelPageItem(QQuickItem *parent = nullptr);

    [[nodiscard]] LabelSheet *sheet() const { return m_sheet; }
    void setSheet(LabelSheet *sheet);
    [[nodiscard]] int page() const { return m_page; }
    void setPage(int page);
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    void paint(QPainter *painter) override;

signals:
    void sheetChanged();
    void pageChanged();
    void zoomChanged();

private:
    void updateSize();
    QPointer<LabelSheet> m_sheet;
    int m_page = 0;
    qreal m_zoom = 1;
};

} // namespace QindaOffice::Apps::Base
