// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_toolbox.h"

#include "menu_tree.h"
#include "slide_canvas_item.h"
#include "standard_actions.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

QList<ActionEntry *> ShowStyleActions::styleActions() const
{
    return {bold, italic, fillColor, strokeColor};
}

QList<ActionEntry *> ShowInsertActions::all() const
{
    return {textBox, rectangle, ellipse, line, arrow, image};
}

ShowToolbox::ShowToolbox(SlideCanvasItem *canvas, Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
    using namespace Quick::ActionId;
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    // Text styling reuses the suite-standard format command ids.
    m_style.bold = make(kFormatBold, tr("&Bold"), QKeySequence::Bold, "bold");
    m_style.italic = make(kFormatItalic, tr("&Italic"), QKeySequence::Italic, "italic");
    m_style.fillColor = make(kShowFillColor, tr("&Fill Colour…"), {}, "paint-bucket");
    m_style.strokeColor = make(kShowStrokeColor, tr("&Stroke Colour…"), {}, "brush");
    m_style.bold->setCheckable(true);
    m_style.italic->setCheckable(true);

    m_insert.textBox = make(kShowInsertTextBox, tr("Text &Box"), {}, "type");
    m_insert.rectangle = make(kShowInsertRectangle, tr("&Rectangle"), {}, "square");
    m_insert.ellipse = make(kShowInsertEllipse, tr("&Ellipse"), {}, "circle");
    m_insert.line = make(kShowInsertLine, tr("&Line"), {}, "minus");
    m_insert.arrow = make(kShowInsertArrow, tr("A&rrow"), {}, "arrow-right");
    m_insert.image = make(kShowInsertImage, tr("&Image…"), {}, "image");

    connect(m_style.bold, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_canvas->setSelectedTextBold(on);
        }
    });
    connect(m_style.italic, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_canvas->setSelectedTextItalic(on);
        }
    });
    connect(m_style.fillColor, &ActionEntry::triggered, this, [this] {
        const ShowModel::Shape *shape = m_canvas->selectedShape();
        emit fillColorRequested(shape != nullptr && shape->fill.isValid() ? shape->fill : QColor(Qt::white));
    });
    connect(m_style.strokeColor, &ActionEntry::triggered, this, [this] {
        const ShowModel::Shape *shape = m_canvas->selectedShape();
        emit strokeColorRequested(shape != nullptr && shape->stroke.isValid() ? shape->stroke : QColor(Qt::black));
    });
    const auto insert = [this](ActionEntry *entry, ShowModel::ShapeKind kind, const QRectF &rect) {
        connect(entry, &ActionEntry::triggered, this, [this, kind, rect] { m_canvas->addShape(kind, rect); });
    };
    insert(m_insert.textBox, ShowModel::ShapeKind::TextBox, QRectF(60, 60, 260, 80));
    insert(m_insert.rectangle, ShowModel::ShapeKind::Rectangle, QRectF(80, 80, 220, 140));
    insert(m_insert.ellipse, ShowModel::ShapeKind::Ellipse, QRectF(80, 80, 220, 140));
    insert(m_insert.line, ShowModel::ShapeKind::Line, QRectF(60, 200, 300, 4));
    insert(m_insert.arrow, ShowModel::ShapeKind::Arrow, QRectF(60, 200, 300, 4));
    connect(m_insert.image, &ActionEntry::triggered, this, &ShowToolbox::imageInsertRequested);

    connect(m_canvas, &SlideCanvasItem::selectionChanged, this, [this] { syncFromSelection(); });
    connect(m_canvas, &SlideCanvasItem::slideEdited, this, [this] { syncFromSelection(); });
    syncFromSelection();
}

QList<MenuNode> ShowToolbox::insertMenuItems() const
{
    QList<MenuNode> items;
    for (const ActionEntry *entry : m_insert.all()) {
        items.append(MenuNode::action(entry->id()));
    }
    return items;
}

QList<MenuNode> ShowToolbox::styleMenuItems() const
{
    QList<MenuNode> items;
    for (const ActionEntry *entry : m_style.styleActions()) {
        items.append(MenuNode::action(entry->id()));
    }
    return items;
}

QStringList ShowToolbox::stripIds() const
{
    QStringList ids;
    for (const ActionEntry *entry : m_insert.all()) {
        ids.append(entry->id());
    }
    ids.append(QStringLiteral("|"));
    for (const ActionEntry *entry : m_style.styleActions()) {
        ids.append(entry->id());
    }
    return ids;
}

void ShowToolbox::syncFromSelection()
{
    m_syncing = true;
    const ShowModel::Shape *shape = m_canvas->selectedShape();
    for (ActionEntry *entry : m_style.styleActions()) {
        entry->setEnabled(shape != nullptr);
    }
    if (shape == nullptr) {
        m_style.bold->setChecked(false);
        m_style.italic->setChecked(false);
    } else {
        bool allBold = !shape->paragraphs.isEmpty();
        bool allItalic = !shape->paragraphs.isEmpty();
        for (const ShowModel::TextParagraph &paragraph : shape->paragraphs) {
            for (const ShowModel::TextRun &run : paragraph.runs) {
                allBold = allBold && run.bold;
                allItalic = allItalic && run.italic;
            }
        }
        m_style.bold->setChecked(allBold);
        m_style.italic->setChecked(allItalic);
    }
    m_syncing = false;
}

} // namespace QindaOffice::Apps::Show
