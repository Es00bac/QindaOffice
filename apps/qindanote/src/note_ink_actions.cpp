// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_ink_actions.h"

#include "note_page_view.h"
#include "note_window.h"

#include "json_settings.h"
#include "paths.h"
#include "wrapping_tool_bar.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

namespace QindaOffice::Apps::Notes {

namespace {

struct NamedColor {
    QLatin1String id;
    const char *text;
    QColor color;
};

struct NamedWidth {
    QLatin1String id;
    const char *text;
    qreal width;
};

// AGENT-NOTE: document data, not chrome (see the header). These persist
// into notebook files as stroke colours.
const QList<NamedColor> kPenColors = {
    {QLatin1String("black"), QT_TR_NOOP("Black"), QColor(0x00, 0x00, 0x00)},
    {QLatin1String("grey"), QT_TR_NOOP("Grey"), QColor(0x7f, 0x7f, 0x7f)},
    {QLatin1String("blue"), QT_TR_NOOP("Blue"), QColor(0x1c, 0x62, 0xd4)},
    {QLatin1String("red"), QT_TR_NOOP("Red"), QColor(0xd2, 0x1c, 0x1c)},
    {QLatin1String("green"), QT_TR_NOOP("Green"), QColor(0x1f, 0x9d, 0x3a)},
    {QLatin1String("orange"), QT_TR_NOOP("Orange"), QColor(0xf2, 0x8c, 0x19)},
    {QLatin1String("purple"), QT_TR_NOOP("Purple"), QColor(0x7d, 0x3a, 0xc1)},
    {QLatin1String("brown"), QT_TR_NOOP("Brown"), QColor(0x8d, 0x5a, 0x2b)},
};

const QList<NamedColor> kHighlighterColors = {
    {QLatin1String("yellow"), QT_TR_NOOP("Yellow"), QColor(0xff, 0xd2, 0x00)},
    {QLatin1String("green"), QT_TR_NOOP("Green"), QColor(0x7e, 0xe7, 0x87)},
    {QLatin1String("pink"), QT_TR_NOOP("Pink"), QColor(0xff, 0xa7, 0xc4)},
    {QLatin1String("blue"), QT_TR_NOOP("Blue"), QColor(0x7e, 0xc8, 0xff)},
    {QLatin1String("orange"), QT_TR_NOOP("Orange"), QColor(0xff, 0xbe, 0x6e)},
};

const QList<NamedWidth> kPenWidths = {
    {QLatin1String("fine"), QT_TR_NOOP("Fine 1.5"), 1.5},
    {QLatin1String("medium"), QT_TR_NOOP("Medium 2.5"), 2.5},
    {QLatin1String("thick"), QT_TR_NOOP("Thick 4"), 4.0},
    {QLatin1String("bold"), QT_TR_NOOP("Bold 6"), 6.0},
    {QLatin1String("marker"), QT_TR_NOOP("Marker 10"), 10.0},
};

const QList<NamedWidth> kHighlighterWidths = {
    {QLatin1String("thin"), QT_TR_NOOP("Thin 8"), 8.0},
    {QLatin1String("medium"), QT_TR_NOOP("Medium 12"), 12.0},
    {QLatin1String("wide"), QT_TR_NOOP("Wide 18"), 18.0},
    {QLatin1String("broad"), QT_TR_NOOP("Broad 24"), 24.0},
};

// Eraser sizes are the disc RADIUS in page units (the cursor ring is drawn
// at radius x zoom, so what you see is what one swipe eats).
const QList<NamedWidth> kEraserSizes = {
    {QLatin1String("small"), QT_TR_NOOP("Small 6"), 6.0},
    {QLatin1String("medium"), QT_TR_NOOP("Medium 12"), 12.0},
    {QLatin1String("large"), QT_TR_NOOP("Large 24"), 24.0},
    {QLatin1String("huge"), QT_TR_NOOP("Huge 40"), 40.0},
};

[[nodiscard]] QAction *make(QObject *parent, const QString &id, const QString &text)
{
    auto *action = new QAction(text, parent);
    // Stable objectName: the dbusmenu/global-menu activation identity
    // (AGENTS.md rule 5) — unnamed actions are unreachable from the panel.
    action->setObjectName(id);
    return action;
}

// AGENT-NOTE: painted swatch glyphs are DOCUMENT state (the ink colour and
// width the next stroke gets), not chrome — rule 3's no-palette-literals
// covers chrome only.
[[nodiscard]] QIcon colorSwatch(const QColor &color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(QRectF(2.5, 2.5, 11.0, 11.0));
    painter.end();
    return QIcon(pixmap);
}

[[nodiscard]] QIcon widthGlyph(qreal width)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    const qreal height = qBound(1.5, width, 12.0);
    painter.drawRoundedRect(QRectF(1.5, 8.0 - height / 2.0, 13.0, height), height / 2.0,
                            height / 2.0);
    painter.end();
    return QIcon(pixmap);
}

// Checks the action carrying `value`, or unchecks the whole group when the
// view's value is a custom one (an exclusive group cannot otherwise show
// "none of these").
void checkMatching(QActionGroup *group, const QList<QAction *> &actions, const QVariant &value)
{
    QAction *match = nullptr;
    for (QAction *action : actions) {
        if (action->data() == value) {
            match = action;
        }
    }
    if (match != nullptr) {
        match->setChecked(true);
        return;
    }
    group->setExclusive(false);
    for (QAction *action : actions) {
        action->setChecked(false);
    }
    group->setExclusive(true);
}

[[nodiscard]] QMenu *buildMenu(const QString &title, const QString &id, QWidget *parent,
                               const QList<QAction *> &actions)
{
    auto *menu = new QMenu(title, parent);
    menu->setObjectName(id);
    for (QAction *action : actions) {
        menu->addAction(action);
    }
    return menu;
}

} // namespace

NoteInkActions::NoteInkActions(NoteWindow *window, NotePageView *view)
    : QObject(window)
    , m_window(window)
    , m_view(view)
{
    createToolActions();
    createPickerActions();
    m_state = std::make_unique<Core::JsonSettings>(
        Core::stateDirFor(Core::appLeaf()) + QStringLiteral("/tools.json"));
    (void)m_state->load(nullptr);
    loadState();
    connect(m_view, &NotePageView::presentationChanged, this, [this] {
        syncFromView();
        syncPopupButtons();
    });
    syncFromView();
}

NoteInkActions::~NoteInkActions() = default;

void NoteInkActions::createToolActions()
{
    m_toolGroup = new QActionGroup(this);
    const QList<std::tuple<QString, QString, NoteTool, QKeySequence, QString>> entries = {
        {tr("Pen"), QStringLiteral("notePenTool"), NoteTool::Pen,
         QKeySequence(QStringLiteral("P")), QStringLiteral("draw-freehand")},
        {tr("Highlighter"), QStringLiteral("noteHighlighterTool"), NoteTool::Highlighter,
         QKeySequence(QStringLiteral("H")), QStringLiteral("draw-highlight")},
        {tr("Eraser"), QStringLiteral("noteEraserTool"), NoteTool::Eraser,
         QKeySequence(QStringLiteral("E")), QStringLiteral("draw-eraser")},
        {tr("Text"), QStringLiteral("noteTextTool"), NoteTool::Text,
         QKeySequence(QStringLiteral("T")), QStringLiteral("insert-text")},
    };
    int i = 0;
    QAction *target = nullptr;
    for (const auto &[label, id, tool, shortcut, icon] : entries) {
        QAction *action = make(this, id, label);
        action->setCheckable(true);
        action->setShortcut(shortcut);
        action->setIcon(QIcon::fromTheme(icon));
        m_toolGroup->addAction(action);
        if (i == 0) {
            target = m_penTool = action;
        } else if (i == 1) {
            m_highlighterTool = action;
        } else if (i == 2) {
            m_eraserTool = action;
        } else {
            m_textTool = action;
        }
        ++i;
        connect(action, &QAction::triggered, this, [this, tool] {
            m_view->setTool(tool);
            saveState();
        });
    }
    target->setChecked(true);
}

void NoteInkActions::createPickerActions()
{
    m_penColorGroup = new QActionGroup(this);
    for (const NamedColor &entry : kPenColors) {
        QAction *action = make(this, QStringLiteral("notePenColor_") + entry.id, tr(entry.text));
        action->setCheckable(true);
        action->setData(entry.color);
        action->setIcon(colorSwatch(entry.color));
        m_penColorGroup->addAction(action);
        m_penColors.append(action);
        connect(action, &QAction::triggered, this,
                [this, color = entry.color] { applyPenColor(color); });
    }
    m_customColor = make(this, QStringLiteral("notePenColorAction"), tr("Custom…"));
    connect(m_customColor, &QAction::triggered, this, [this] {
        const QColor picked =
            QColorDialog::getColor(m_view->penColor(), m_window, tr("Pen Color"));
        if (picked.isValid()) {
            applyPenColor(picked);
        }
    });

    m_penWidthGroup = new QActionGroup(this);
    for (const NamedWidth &entry : kPenWidths) {
        QAction *action = make(this, QStringLiteral("notePenWidth_") + entry.id, tr(entry.text));
        action->setCheckable(true);
        action->setData(entry.width);
        action->setIcon(widthGlyph(entry.width));
        m_penWidthGroup->addAction(action);
        m_penWidths.append(action);
        connect(action, &QAction::triggered, this,
                [this, width = entry.width] { applyPenWidth(width); });
    }

    m_highlighterColorGroup = new QActionGroup(this);
    for (const NamedColor &entry : kHighlighterColors) {
        QAction *action =
            make(this, QStringLiteral("noteHighlighterColor_") + entry.id, tr(entry.text));
        action->setCheckable(true);
        action->setData(entry.color);
        action->setIcon(colorSwatch(entry.color));
        m_highlighterColorGroup->addAction(action);
        m_highlighterColors.append(action);
        connect(action, &QAction::triggered, this,
                [this, color = entry.color] { applyHighlighterColor(color); });
    }

    m_highlighterWidthGroup = new QActionGroup(this);
    for (const NamedWidth &entry : kHighlighterWidths) {
        QAction *action =
            make(this, QStringLiteral("noteHighlighterWidth_") + entry.id, tr(entry.text));
        action->setCheckable(true);
        action->setData(entry.width);
        action->setIcon(widthGlyph(entry.width));
        m_highlighterWidthGroup->addAction(action);
        m_highlighterWidths.append(action);
        connect(action, &QAction::triggered, this,
                [this, width = entry.width] { applyHighlighterWidth(width); });
    }

    m_eraserModeGroup = new QActionGroup(this);
    const QList<std::pair<QString, std::pair<QString, Ink::EraseMode>>> modes = {
        {QStringLiteral("noteEraserMode_stroke"),
         {tr("Erase Whole Strokes"), Ink::EraseMode::Stroke}},
        {QStringLiteral("noteEraserMode_partial"),
         {tr("Erase Partially"), Ink::EraseMode::Partial}},
    };
    for (const auto &[id, entry] : modes) {
        const auto &[label, mode] = entry;
        QAction *action = make(this, id, label);
        action->setCheckable(true);
        action->setData(int(mode));
        m_eraserModeGroup->addAction(action);
        m_eraserModes.append(action);
        connect(action, &QAction::triggered, this, [this, mode] { applyEraserMode(mode); });
    }

    m_eraserSizeGroup = new QActionGroup(this);
    for (const NamedWidth &entry : kEraserSizes) {
        QAction *action = make(this, QStringLiteral("noteEraserSize_") + entry.id, tr(entry.text));
        action->setCheckable(true);
        action->setData(entry.width);
        action->setIcon(widthGlyph(entry.width));
        m_eraserSizeGroup->addAction(action);
        m_eraserSizes.append(action);
        connect(action, &QAction::triggered, this,
                [this, size = entry.width] { applyEraserRadius(size); });
    }
}

void NoteInkActions::applyPenColor(const QColor &color)
{
    m_view->setPenColor(color);
    saveState();
}

void NoteInkActions::applyPenWidth(qreal width)
{
    m_view->setPenWidth(width);
    saveState();
}

void NoteInkActions::applyHighlighterColor(const QColor &color)
{
    m_view->setHighlighterColor(color);
    saveState();
}

void NoteInkActions::applyHighlighterWidth(qreal width)
{
    m_view->setHighlighterWidth(width);
    saveState();
}

void NoteInkActions::applyEraserMode(Ink::EraseMode mode)
{
    m_view->setEraserMode(mode);
    saveState();
}

void NoteInkActions::applyEraserRadius(qreal radius)
{
    m_view->setEraserRadius(radius);
    saveState();
}

void NoteInkActions::syncFromView()
{
    switch (m_view->tool()) {
    case NoteTool::Pen:
        m_penTool->setChecked(true);
        break;
    case NoteTool::Highlighter:
        m_highlighterTool->setChecked(true);
        break;
    case NoteTool::Eraser:
        m_eraserTool->setChecked(true);
        break;
    case NoteTool::Text:
        m_textTool->setChecked(true);
        break;
    }
    checkMatching(m_penColorGroup, m_penColors, m_view->penColor());
    checkMatching(m_penWidthGroup, m_penWidths, m_view->penWidth());
    checkMatching(m_highlighterColorGroup, m_highlighterColors, m_view->highlighterColor());
    checkMatching(m_highlighterWidthGroup, m_highlighterWidths, m_view->highlighterWidth());
    checkMatching(m_eraserModeGroup, m_eraserModes, int(m_view->eraserMode()));
    checkMatching(m_eraserSizeGroup, m_eraserSizes, m_view->eraserRadius());
}

void NoteInkActions::syncPopupButtons()
{
    if (m_colorButton == nullptr || m_widthButton == nullptr) {
        return;
    }
    const NoteTool tool = m_view->tool();
    const bool ink = tool == NoteTool::Pen || tool == NoteTool::Highlighter;
    m_colorButton->setEnabled(ink);
    m_widthButton->setEnabled(tool != NoteTool::Text);
    if (tool == NoteTool::Pen) {
        m_colorButton->setMenu(m_btnPenColor);
        m_colorButton->setIcon(colorSwatch(m_view->penColor()));
        m_colorButton->setToolTip(tr("Pen colour"));
        m_widthButton->setMenu(m_btnPenWidth);
        m_widthButton->setIcon(widthGlyph(m_view->penWidth()));
        m_widthButton->setToolTip(tr("Pen width"));
    } else if (tool == NoteTool::Highlighter) {
        m_colorButton->setMenu(m_btnHlColor);
        m_colorButton->setIcon(colorSwatch(m_view->highlighterColor()));
        m_colorButton->setToolTip(tr("Highlighter colour"));
        m_widthButton->setMenu(m_btnHlWidth);
        m_widthButton->setIcon(widthGlyph(m_view->highlighterWidth()));
        m_widthButton->setToolTip(tr("Highlighter width"));
    } else if (tool == NoteTool::Eraser) {
        m_colorButton->setMenu(nullptr);
        m_widthButton->setMenu(m_btnEraser);
        m_widthButton->setIcon(widthGlyph(m_view->eraserRadius()));
        m_widthButton->setToolTip(tr("Eraser size"));
    } else {
        m_colorButton->setMenu(nullptr);
        m_widthButton->setMenu(nullptr);
    }
}

void NoteInkActions::loadState()
{
    const QVariantMap map = m_state->valueMap();
    const QColor penColor = QColor::fromString(map.value(QStringLiteral("penColor")).toString());
    if (penColor.isValid()) {
        m_view->setPenColor(penColor);
    }
    if (map.contains(QStringLiteral("penWidth"))) {
        m_view->setPenWidth(map.value(QStringLiteral("penWidth")).toDouble());
    }
    const QColor highlighterColor =
        QColor::fromString(map.value(QStringLiteral("highlighterColor")).toString());
    if (highlighterColor.isValid()) {
        m_view->setHighlighterColor(highlighterColor);
    }
    if (map.contains(QStringLiteral("highlighterWidth"))) {
        m_view->setHighlighterWidth(map.value(QStringLiteral("highlighterWidth")).toDouble());
    }
    if (map.value(QStringLiteral("eraserMode")).toString() == QLatin1String("partial")) {
        m_view->setEraserMode(Ink::EraseMode::Partial);
    }
    if (map.contains(QStringLiteral("eraserSize"))) {
        m_view->setEraserRadius(map.value(QStringLiteral("eraserSize")).toDouble());
    }
    const QString tool = map.value(QStringLiteral("tool")).toString();
    if (tool == QLatin1String("highlighter")) {
        m_view->setTool(NoteTool::Highlighter);
    } else if (tool == QLatin1String("eraser")) {
        m_view->setTool(NoteTool::Eraser);
    } else if (tool == QLatin1String("text")) {
        m_view->setTool(NoteTool::Text);
    }
}

void NoteInkActions::saveState()
{
    if (m_state == nullptr) {
        return;
    }
    QString tool = QStringLiteral("pen");
    switch (m_view->tool()) {
    case NoteTool::Pen:
        break;
    case NoteTool::Highlighter:
        tool = QStringLiteral("highlighter");
        break;
    case NoteTool::Eraser:
        tool = QStringLiteral("eraser");
        break;
    case NoteTool::Text:
        tool = QStringLiteral("text");
        break;
    }
    m_state->setValue(QStringLiteral("tool"), tool);
    m_state->setValue(QStringLiteral("penColor"), m_view->penColor().name());
    m_state->setValue(QStringLiteral("penWidth"), m_view->penWidth());
    m_state->setValue(QStringLiteral("highlighterColor"), m_view->highlighterColor().name());
    m_state->setValue(QStringLiteral("highlighterWidth"), m_view->highlighterWidth());
    m_state->setValue(QStringLiteral("eraserMode"),
                      m_view->eraserMode() == Ink::EraseMode::Partial
                          ? QStringLiteral("partial")
                          : QStringLiteral("stroke"));
    m_state->setValue(QStringLiteral("eraserSize"), m_view->eraserRadius());
    // Degrade visibly: a state file that cannot be written costs the
    // remembered tool state, never the document.
    (void)m_state->save(nullptr);
}

QMenu *NoteInkActions::penColorMenu(QWidget *parent) const
{
    QMenu *menu = buildMenu(tr("Pen Colour"), QStringLiteral("noteInkPenColorMenu"), parent,
                            m_penColors);
    menu->addSeparator();
    menu->addAction(m_customColor);
    return menu;
}

QMenu *NoteInkActions::penWidthMenu(QWidget *parent) const
{
    return buildMenu(tr("Pen Width"), QStringLiteral("noteInkPenWidthMenu"), parent, m_penWidths);
}

QMenu *NoteInkActions::highlighterColorMenu(QWidget *parent) const
{
    return buildMenu(tr("Highlighter Colour"), QStringLiteral("noteInkHighlighterColorMenu"),
                     parent, m_highlighterColors);
}

QMenu *NoteInkActions::highlighterWidthMenu(QWidget *parent) const
{
    return buildMenu(tr("Highlighter Width"), QStringLiteral("noteInkHighlighterWidthMenu"),
                     parent, m_highlighterWidths);
}

QMenu *NoteInkActions::eraserMenu(QWidget *parent, bool withModes) const
{
    QMenu *menu = buildMenu(tr("Eraser"), QStringLiteral("noteInkEraserMenu"), parent,
                            withModes ? m_eraserModes : QList<QAction *>{});
    if (withModes) {
        menu->addSeparator();
    }
    for (QAction *action : m_eraserSizes) {
        menu->addAction(action);
    }
    return menu;
}

void NoteInkActions::populateInkMenu(QMenu *menu)
{
    for (QAction *action : {m_penTool, m_highlighterTool, m_eraserTool, m_textTool}) {
        menu->addAction(action);
    }
    menu->addSeparator();
    menu->addMenu(penColorMenu(menu));
    menu->addMenu(penWidthMenu(menu));
    menu->addMenu(highlighterColorMenu(menu));
    menu->addMenu(highlighterWidthMenu(menu));
    menu->addMenu(eraserMenu(menu, /*withModes=*/true));
}

void NoteInkActions::addToStrip(Shell::WrappingToolBar *strip)
{
    for (QAction *action : {m_penTool, m_highlighterTool, m_eraserTool, m_textTool}) {
        strip->addStripAction(action);
    }
    strip->addStripSeparator();
    // The two popup buttons hold the SAME QActions as the Ink menu
    // submenus (one command = one action); their menus and icons track the
    // active tool in syncPopupButtons().
    m_colorButton = new QToolButton(strip->strip());
    m_colorButton->setObjectName(QStringLiteral("noteInkColorButton"));
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    m_colorButton->setAutoRaise(true);
    strip->addStripWidget(m_colorButton);
    m_widthButton = new QToolButton(strip->strip());
    m_widthButton->setObjectName(QStringLiteral("noteInkWidthButton"));
    m_widthButton->setPopupMode(QToolButton::InstantPopup);
    m_widthButton->setAutoRaise(true);
    strip->addStripWidget(m_widthButton);
    m_btnPenColor = penColorMenu(m_colorButton);
    m_btnPenWidth = penWidthMenu(m_widthButton);
    m_btnHlColor = highlighterColorMenu(m_colorButton);
    m_btnHlWidth = highlighterWidthMenu(m_widthButton);
    m_btnEraser = eraserMenu(m_widthButton, /*withModes=*/false);
    syncPopupButtons();
}

} // namespace QindaOffice::Apps::Notes
