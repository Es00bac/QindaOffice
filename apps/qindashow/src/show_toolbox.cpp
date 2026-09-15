// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_toolbox.h"

#include "slide_canvas.h"
#include "standard_actions.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QToolBar>

namespace QindaOffice::Apps::Show {

QList<QAction *> ShowStyleActions::styleActions() const
{
    return {bold, italic, fillColor, strokeColor};
}

QList<QAction *> ShowInsertActions::all() const
{
    return {textBox, rectangle, ellipse, line, arrow, image};
}

namespace {

QAction *makeAction(QObject *parent, const char *id, const QString &text,
                    const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

ShowToolbox::ShowToolbox(SlideCanvas *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
    // Text styling reuses the suite-standard format command ids (same
    // conceptual commands as the Write/Calc toolbars).
    using namespace Shell::ActionId;
    m_style.bold = makeAction(this, kFormatBold, tr("&Bold"), QKeySequence::Bold,
                              "format-text-bold");
    m_style.italic = makeAction(this, kFormatItalic, tr("&Italic"), QKeySequence::Italic,
                                "format-text-italic");
    m_style.fillColor = makeAction(this, kShowFillColor, tr("&Fill Color…"),
                                   {}, "color-fill");
    m_style.strokeColor = makeAction(this, kShowStrokeColor, tr("&Stroke Color…"), {},
                                     "draw-brush");
    m_style.bold->setCheckable(true);
    m_style.italic->setCheckable(true);

    m_insert.textBox = makeAction(this, kShowInsertTextBox, tr("Text &Box"), {},
                                  "insert-text");
    m_insert.rectangle = makeAction(this, kShowInsertRectangle, tr("&Rectangle"));
    m_insert.ellipse = makeAction(this, kShowInsertEllipse, tr("&Ellipse"));
    m_insert.line = makeAction(this, kShowInsertLine, tr("&Line"));
    m_insert.arrow = makeAction(this, kShowInsertArrow, tr("A&rrow"));
    m_insert.image = makeAction(this, kShowInsertImage, tr("&Image…"), {}, "insert-image");

    connect(m_style.bold, &QAction::triggered, m_canvas, [this](bool on) {
        m_canvas->setSelectedTextBold(on);
    });
    connect(m_style.italic, &QAction::triggered, m_canvas, [this](bool on) {
        m_canvas->setSelectedTextItalic(on);
    });
    connect(m_style.fillColor, &QAction::triggered, this, [this] { pickColor(true); });
    connect(m_style.strokeColor, &QAction::triggered, this, [this] { pickColor(false); });

    connect(m_insert.textBox, &QAction::triggered, m_canvas, [this] {
        m_canvas->addShape(ShowModel::ShapeKind::TextBox, QRectF(60, 60, 260, 80));
    });
    connect(m_insert.rectangle, &QAction::triggered, m_canvas, [this] {
        m_canvas->addShape(ShowModel::ShapeKind::Rectangle, QRectF(80, 80, 220, 140));
    });
    connect(m_insert.ellipse, &QAction::triggered, m_canvas, [this] {
        m_canvas->addShape(ShowModel::ShapeKind::Ellipse, QRectF(80, 80, 220, 140));
    });
    connect(m_insert.line, &QAction::triggered, m_canvas, [this] {
        m_canvas->addShape(ShowModel::ShapeKind::Line, QRectF(60, 200, 300, 4));
    });
    connect(m_insert.arrow, &QAction::triggered, m_canvas, [this] {
        m_canvas->addShape(ShowModel::ShapeKind::Arrow, QRectF(60, 200, 300, 4));
    });
    connect(m_insert.image, &QAction::triggered, this,
            [this] { emit imageInsertRequested(); });

    connect(m_canvas, &SlideCanvas::selectionChanged, this,
            [this](bool) { syncFromSelection(); });
    connect(m_canvas, &SlideCanvas::slideEdited, this, [this] { syncFromSelection(); });
    syncFromSelection();
}

QToolBar *ShowToolbox::toolBar()
{
    if (m_toolBar != nullptr) {
        return m_toolBar;
    }
    auto *window = qobject_cast<QWidget *>(parent());
    m_toolBar = new QToolBar(tr("Shapes"), window);
    m_toolBar->setObjectName(QStringLiteral("shapeToolBar"));
    for (QAction *action : m_insert.all()) {
        m_toolBar->addAction(action);
    }
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_style.bold);
    m_toolBar->addAction(m_style.italic);
    m_toolBar->addAction(m_style.fillColor);
    m_toolBar->addAction(m_style.strokeColor);
    return m_toolBar;
}

void ShowToolbox::pickColor(bool fill)
{
    const ShowModel::Shape *shape =
        m_canvas->selectedItem() != nullptr ? m_canvas->selectedItem()->modelShape()
                                            : nullptr;
    const QColor initial = shape != nullptr ? (fill ? shape->fill : shape->stroke) : QColor();
    const QColor color = QColorDialog::getColor(initial.isValid() ? initial : Qt::white,
                                                qobject_cast<QWidget *>(parent()),
                                                fill ? tr("Fill Color") : tr("Stroke Color"));
    if (!color.isValid()) {
        return;
    }
    if (fill) {
        m_canvas->setSelectedFill(color);
    } else {
        m_canvas->setSelectedStroke(color);
    }
}

void ShowToolbox::syncFromSelection()
{
    const ShowModel::Shape *shape =
        m_canvas->selectedItem() != nullptr ? m_canvas->selectedItem()->modelShape()
                                            : nullptr;
    const bool enabled = shape != nullptr;
    for (QAction *action : m_style.styleActions()) {
        action->setEnabled(enabled);
    }
    if (shape == nullptr) {
        m_style.bold->setChecked(false);
        m_style.italic->setChecked(false);
        return;
    }
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

} // namespace QindaOffice::Apps::Show
