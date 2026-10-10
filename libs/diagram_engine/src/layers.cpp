// SPDX-License-Identifier: GPL-3.0-or-later
// Layer and background-page rules (layers.h).
#include "layers.h"

#include <QSet>

namespace QindaOffice::Diagram {

namespace {

const Layer *layerAt(const Page &page, int index)
{
    return index >= 0 && index < page.layers.size() ? &page.layers.at(index) : nullptr;
}

// True when `test` holds for any known layer of `layers`; `none` when the
// item is on no (known) layer.
template<typename Test>
bool any(const Page &page, const QList<int> &layers, bool none, Test test)
{
    bool known = false;
    for (const int index : layers) {
        if (const Layer *layer = layerAt(page, index)) {
            known = true;
            if (test(*layer)) {
                return true;
            }
        }
    }
    return known ? false : none;
}

const QList<int> *layersOf(const Page &page, const QString &itemId)
{
    if (const Shape *s = page.shape(itemId)) {
        return &s->layers;
    }
    if (const Connector *c = page.connector(itemId)) {
        return &c->layers;
    }
    return nullptr;
}

} // namespace

bool layersVisible(const Page &page, const QList<int> &layers)
{
    return any(page, layers, true, [](const Layer &l) { return l.visible; });
}

bool layersPrint(const Page &page, const QList<int> &layers)
{
    return any(page, layers, true, [](const Layer &l) { return l.print; });
}

bool layersLocked(const Page &page, const QList<int> &layers)
{
    return any(page, layers, false, [](const Layer &l) { return l.locked; });
}

bool layersGlue(const Page &page, const QList<int> &layers)
{
    return any(page, layers, true, [](const Layer &l) { return l.glue; });
}

QColor layersColor(const Page &page, const QList<int> &layers)
{
    for (const int index : layers) {
        const Layer *layer = layerAt(page, index);
        if (layer != nullptr && layer->color.isValid() && layer->color.alpha() > 0) {
            return layer->color;
        }
    }
    return {};
}

bool itemVisible(const Page &page, const QString &itemId)
{
    const QList<int> *layers = layersOf(page, itemId);
    return layers != nullptr && layersVisible(page, *layers);
}

bool itemLocked(const Page &page, const QString &itemId)
{
    const QList<int> *layers = layersOf(page, itemId);
    return layers == nullptr || layersLocked(page, *layers);
}

QList<int> activeLayers(const Page &page)
{
    QList<int> out;
    for (int i = 0; i < page.layers.size(); ++i) {
        if (page.layers.at(i).active) {
            out.append(i);
        }
    }
    return out;
}

QStringList hiddenItems(const Page &page)
{
    QStringList out;
    if (page.layers.isEmpty()) {
        return out;
    }
    for (const Shape &s : page.shapes) {
        if (!layersVisible(page, s.layers)) {
            out.append(s.id);
        }
    }
    for (const Connector &c : page.connectors) {
        if (!layersVisible(page, c.layers)) {
            out.append(c.id);
        }
    }
    return out;
}

QVector<const Page *> backgroundChain(const Drawing &drawing, const Page &page)
{
    QVector<const Page *> chain;
    QSet<QString> seen{page.id};
    QString next = page.backPageId;
    while (!next.isEmpty() && !seen.contains(next)) {
        const Page *back = drawing.page(next);
        if (back == nullptr) {
            break;
        }
        seen.insert(next);
        chain.prepend(back);
        next = back->backPageId;
    }
    return chain;
}

} // namespace QindaOffice::Diagram
