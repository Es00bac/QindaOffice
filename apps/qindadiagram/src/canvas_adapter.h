// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DiagramCanvasAdapter: the current page of a DiagramService seen through
// QindaTK's canvas boundary (docs/canvas.md#document-adapter, the pattern
// QindaTK's SlideCanvasDocument set), so QindaDiagram uses Tk.CanvasView,
// ToolController, SpatialTransform, SnapModel, SelectionOverlay and
// NavigatorView unchanged (D-050).
//
// AGENT-NOTE (why the objects are invisible): every diagram item becomes a
// PICK PROXY — a shape an image object with a transparent pixel (picked on
// its rotated box), a connector a polyline along its route (picked on the
// line). The pixels come from DiagramPageView underneath the transparent
// canvas, drawn by diagram_engine's painter, so the canvas, print and
// export are one painter and a drag previews live with connectors following.
// The canvas still draws hover and selection outlines of the proxies.
//
// AGENT-CONTRACT: commit() accepts what a canvas gesture can mean for a
// diagram — shape moves, resizes and rotations (projected to rect +
// rotation), connector translations and removals — as ONE DrawingDocument
// undo step through DiagramService::applyGeometry. Inserts, layer, page,
// guide and artboard edits are refused.

#include "diagram_service.h"

#include <qindatk/canvas_document.h>

#include <QHash>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Diagram {

class DiagramCanvasAdapter : public QindaTK::CanvasDocumentAdapter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Apps::Diagram::DiagramService *service READ service WRITE setService NOTIFY serviceChanged)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
public:
    explicit DiagramCanvasAdapter(QObject *parent = nullptr);

    [[nodiscard]] DiagramService *service() const { return m_service; }
    void setService(DiagramService *service);

    // Canvas object ids <-> diagram item ids (stable while the item exists).
    Q_INVOKABLE QStringList itemIds(const QVariantList &canvasIds) const;
    Q_INVOKABLE QVariantList canvasIds(const QStringList &itemIds) const;
    Q_INVOKABLE QString itemId(const QVariant &canvasId) const;

    // CanvasDocumentAdapter
    [[nodiscard]] quint64 revision() const override { return m_revision; }
    [[nodiscard]] QString documentId() const override;
    [[nodiscard]] QRectF pageRect() const override;
    [[nodiscard]] QRectF contentBounds() const override;
    [[nodiscard]] QColor pageColor() const override { return QColor(Qt::transparent); }
    [[nodiscard]] const QindaTK::CanvasObject *object(QindaTK::CanvasObjectId id) const override;
    [[nodiscard]] const QindaTK::CanvasLayer *layer(QindaTK::CanvasLayerId id) const override;
    [[nodiscard]] QindaTK::CanvasTiledImagePtr image(QindaTK::CanvasImageId id) const override;
    [[nodiscard]] QVector<QindaTK::CanvasObjectId> candidates(const QRectF &region) const override;
    [[nodiscard]] QindaTK::CanvasLayerId activeLayer() const override { return m_layer.id; }
    QindaTK::CanvasObjectId reserveObjectId() override { return m_nextId++; }
    qint64 reserveZ() override { return qint64(m_objects.size()); }
    bool commit(const QindaTK::CanvasEdit &edit) override;

    // A shape's canvas transform, and its inverse projection (shear dropped:
    // a diagram shape is a rotated rectangle).
    [[nodiscard]] static QTransform objectTransform(const QRectF &rect, double rotation);
    static void projectTransform(const QRectF &localRect, const QTransform &transform, QRectF *rect, double *rotation);

signals:
    void serviceChanged();
    void changed();

private:
    void rebuild();
    QindaTK::CanvasObjectId idFor(const QString &itemId);

    QPointer<DiagramService> m_service;
    quint64 m_revision = 1;
    QindaTK::CanvasObjectId m_nextId = 1;
    QindaTK::CanvasLayer m_layer;
    QindaTK::CanvasTiledImagePtr m_blank;
    QVector<QindaTK::CanvasObject> m_objects;
    QHash<QindaTK::CanvasObjectId, int> m_index;
    QHash<QString, QindaTK::CanvasObjectId> m_canvasIds;
    QHash<QindaTK::CanvasObjectId, QString> m_itemIds;
};

} // namespace QindaOffice::Apps::Diagram
