// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow toolbox (M13, on ActionEntry since M30): the shape-styling and
// insert-shape command sets as persistent entries (rule 5), shared between
// the shapes strip and the Insert/Shape menus. Style entries mutate the
// model through the canvas item so the old/new undo payloads keep
// flowing; their check marks re-read the selected shape. The colour and
// image dialogs are request signals the host relays to the window.

#include "action_model.h"
#include "presentation.h"

#include <QColor>
#include <QObject>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class SlideCanvasItem;

struct ShowStyleActions {
    Quick::ActionEntry *bold = nullptr;
    Quick::ActionEntry *italic = nullptr;
    Quick::ActionEntry *fillColor = nullptr;
    Quick::ActionEntry *strokeColor = nullptr;
    [[nodiscard]] QList<Quick::ActionEntry *> styleActions() const;
};

struct ShowInsertActions {
    Quick::ActionEntry *textBox = nullptr;
    Quick::ActionEntry *rectangle = nullptr;
    Quick::ActionEntry *ellipse = nullptr;
    Quick::ActionEntry *line = nullptr;
    Quick::ActionEntry *arrow = nullptr;
    Quick::ActionEntry *image = nullptr;
    [[nodiscard]] QList<Quick::ActionEntry *> all() const;
};

class ShowToolbox final : public QObject {
    Q_OBJECT
public:
    ShowToolbox(SlideCanvasItem *canvas, Quick::ActionModel *model, QObject *parent);

    [[nodiscard]] const ShowStyleActions &styleActions() const { return m_style; }
    [[nodiscard]] const ShowInsertActions &insertActions() const { return m_insert; }
    [[nodiscard]] QList<Quick::MenuNode> insertMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> styleMenuItems() const;
    [[nodiscard]] QStringList stripIds() const;
    void syncFromSelection();

signals:
    void imageInsertRequested();
    void fillColorRequested(const QColor &current);
    void strokeColorRequested(const QColor &current);

private:
    SlideCanvasItem *m_canvas;
    ShowStyleActions m_style;
    ShowInsertActions m_insert;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
