// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's Ink menu: the four tools, the pen/highlighter colour and width
// pickers and the eraser options as ONE set of ActionEntries shared by the
// Ink menu, the tool strip's buttons, the keyboard and the panel export
// (rule 5: one command = one entry). Per-tool state (last tool, colours,
// widths, eraser mode/size) is window-independent STATE, not a preference
// (D-026): tools.json in the app's state dir, written on change, read when
// the page item attaches.
//
// AGENT-NOTE: ink colours are DOCUMENT data — they persist into notebook
// files — not window chrome, so the colour literals below are allowed.

#include "note_host.h"

#include "note_page_item.h"

#include "json_settings.h"
#include "paths.h"
#include "preferences.h"

#include <QKeySequence>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;

namespace {

struct NamedColor {
    const char *id;
    const char *text;
    QColor color;
};

struct NamedWidth {
    const char *id;
    const char *text;
    qreal width;
};

const NamedColor kPenColors[] = {
    {"black", QT_TR_NOOP("Black"), QColor(0x00, 0x00, 0x00)},
    {"grey", QT_TR_NOOP("Grey"), QColor(0x7f, 0x7f, 0x7f)},
    {"blue", QT_TR_NOOP("Blue"), QColor(0x1c, 0x62, 0xd4)},
    {"red", QT_TR_NOOP("Red"), QColor(0xd2, 0x1c, 0x1c)},
    {"green", QT_TR_NOOP("Green"), QColor(0x1f, 0x9d, 0x3a)},
    {"orange", QT_TR_NOOP("Orange"), QColor(0xf2, 0x8c, 0x19)},
    {"purple", QT_TR_NOOP("Purple"), QColor(0x7d, 0x3a, 0xc1)},
    {"brown", QT_TR_NOOP("Brown"), QColor(0x8d, 0x5a, 0x2b)},
};

const NamedColor kHighlighterColors[] = {
    {"yellow", QT_TR_NOOP("Yellow"), QColor(0xff, 0xd2, 0x00)},
    {"green", QT_TR_NOOP("Green"), QColor(0x7e, 0xe7, 0x87)},
    {"pink", QT_TR_NOOP("Pink"), QColor(0xff, 0xa7, 0xc4)},
    {"blue", QT_TR_NOOP("Blue"), QColor(0x7e, 0xc8, 0xff)},
    {"orange", QT_TR_NOOP("Orange"), QColor(0xff, 0xbe, 0x6e)},
};

const NamedWidth kPenWidths[] = {
    {"fine", QT_TR_NOOP("Fine"), 1.5},   {"medium", QT_TR_NOOP("Medium"), 2.5},
    {"thick", QT_TR_NOOP("Thick"), 4.0}, {"bold", QT_TR_NOOP("Bold"), 6.0},
    {"marker", QT_TR_NOOP("Marker"), 10.0},
};

const NamedWidth kHighlighterWidths[] = {
    {"thin", QT_TR_NOOP("Thin"), 8.0},
    {"medium", QT_TR_NOOP("Medium"), 12.0},
    {"wide", QT_TR_NOOP("Wide"), 18.0},
    {"broad", QT_TR_NOOP("Broad"), 24.0},
};

// Eraser sizes are the disc RADIUS in page units.
const NamedWidth kEraserSizes[] = {
    {"small", QT_TR_NOOP("Small"), 6.0},
    {"medium", QT_TR_NOOP("Medium"), 12.0},
    {"large", QT_TR_NOOP("Large"), 24.0},
    {"huge", QT_TR_NOOP("Huge"), 40.0},
};

QString tr(const char *text)
{
    return QCoreApplication::translate("NoteHost", text);
}

template <size_t N>
QVariantList colorList(const NamedColor (&entries)[N])
{
    QVariantList list;
    for (const NamedColor &entry : entries) {
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(entry.id)},
                                {QStringLiteral("name"), tr(entry.text)},
                                {QStringLiteral("color"), entry.color}});
    }
    return list;
}

template <size_t N>
QVariantList widthList(const NamedWidth (&entries)[N])
{
    QVariantList list;
    for (const NamedWidth &entry : entries) {
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(entry.id)},
                                {QStringLiteral("name"), tr(entry.text)},
                                {QStringLiteral("width"), entry.width}});
    }
    return list;
}

} // namespace

QVariantList NoteHost::penColors() const { return colorList(kPenColors); }
QVariantList NoteHost::highlighterColors() const { return colorList(kHighlighterColors); }
QVariantList NoteHost::penWidths() const { return widthList(kPenWidths); }
QVariantList NoteHost::highlighterWidths() const { return widthList(kHighlighterWidths); }
QVariantList NoteHost::eraserSizes() const { return widthList(kEraserSizes); }

void NoteHost::createInkActions()
{
    Quick::ActionModel *model = actions();
    const struct {
        const char *id;
        const char *text;
        const char *key;
        const char *icon;
        NoteTool tool;
    } tools[] = {
        {"notePenTool", QT_TR_NOOP("&Pen"), "P", "pen-tool", NoteTool::Pen},
        {"noteHighlighterTool", QT_TR_NOOP("&Highlighter"), "H", "highlighter", NoteTool::Highlighter},
        {"noteEraserTool", QT_TR_NOOP("&Eraser"), "E", "eraser", NoteTool::Eraser},
        {"noteTextTool", QT_TR_NOOP("&Text"), "T", "type", NoteTool::Text},
    };
    for (const auto &tool : tools) {
        ActionEntry *entry = model->add(QString::fromLatin1(tool.id), tr(tool.text),
                                        QKeySequence(QString::fromLatin1(tool.key)),
                                        QString::fromLatin1(tool.icon));
        entry->setRadioGroup(QStringLiteral("noteTool"));
        connect(entry, &ActionEntry::triggered, this, [this, t = tool.tool] {
            if (!m_applyingInk && m_pageItem != nullptr) {
                m_pageItem->setTool(t);
                saveToolState();
            }
        });
    }
    model->entry(QStringLiteral("notePenTool"))->setChecked(true);

    // Draw with Finger mirrors Preferences note/touchDrawing (the
    // preferences page writes the same key; applyPreferences syncs both).
    ActionEntry *touch = model->add(QStringLiteral("noteTouchDrawAction"), tr("Draw with &Finger"),
                                    {}, QStringLiteral("hand"));
    touch->setCheckable(true);
    touch->setChecked(Quick::Preferences::instance().noteTouchDrawing());
    connect(touch, &ActionEntry::toggled, this, [](bool on) {
        Quick::Preferences::instance().setNoteTouchDrawing(on);
    });

    for (const NamedColor &entry : kPenColors) {
        ActionEntry *action = model->add(QStringLiteral("notePenColor_") + QString::fromLatin1(entry.id),
                                         tr(entry.text));
        action->setRadioGroup(QStringLiteral("notePenColor"));
        connect(action, &ActionEntry::triggered, this,
                [this, color = entry.color] { if (!m_applyingInk) applyPenColor(color); });
    }
    connect(model->add(QStringLiteral("notePenColorAction"), tr("&Custom…"), {},
                       QStringLiteral("pipette")),
            &ActionEntry::triggered, this, [this] {
                emit customPenColorRequested(m_pageItem != nullptr ? m_pageItem->penColor()
                                                                   : QColor(Qt::black));
            });
    for (const NamedWidth &entry : kPenWidths) {
        ActionEntry *action = model->add(QStringLiteral("notePenWidth_") + QString::fromLatin1(entry.id),
                                         tr(entry.text) + QStringLiteral(" %1").arg(entry.width));
        action->setRadioGroup(QStringLiteral("notePenWidth"));
        connect(action, &ActionEntry::triggered, this,
                [this, width = entry.width] { if (!m_applyingInk) applyPenWidth(width); });
    }
    for (const NamedColor &entry : kHighlighterColors) {
        ActionEntry *action = model->add(
            QStringLiteral("noteHighlighterColor_") + QString::fromLatin1(entry.id), tr(entry.text));
        action->setRadioGroup(QStringLiteral("noteHighlighterColor"));
        connect(action, &ActionEntry::triggered, this,
                [this, color = entry.color] { if (!m_applyingInk) applyHighlighterColor(color); });
    }
    for (const NamedWidth &entry : kHighlighterWidths) {
        ActionEntry *action = model->add(
            QStringLiteral("noteHighlighterWidth_") + QString::fromLatin1(entry.id),
            tr(entry.text) + QStringLiteral(" %1").arg(entry.width));
        action->setRadioGroup(QStringLiteral("noteHighlighterWidth"));
        connect(action, &ActionEntry::triggered, this,
                [this, width = entry.width] { if (!m_applyingInk) applyHighlighterWidth(width); });
    }
    for (const auto &[id, text, mode] : QList<std::tuple<QString, QString, Ink::EraseMode>>{
             {QStringLiteral("noteEraserMode_stroke"), tr("Erase &Whole Strokes"), Ink::EraseMode::Stroke},
             {QStringLiteral("noteEraserMode_partial"), tr("Erase &Partially"), Ink::EraseMode::Partial}}) {
        ActionEntry *action = model->add(id, text);
        action->setRadioGroup(QStringLiteral("noteEraserMode"));
        connect(action, &ActionEntry::triggered, this, [this, mode] {
            if (!m_applyingInk && m_pageItem != nullptr) {
                m_pageItem->setEraserMode(mode);
                saveToolState();
            }
        });
    }
    for (const NamedWidth &entry : kEraserSizes) {
        ActionEntry *action = model->add(QStringLiteral("noteEraserSize_") + QString::fromLatin1(entry.id),
                                         tr(entry.text) + QStringLiteral(" %1").arg(entry.width));
        action->setRadioGroup(QStringLiteral("noteEraserSize"));
        connect(action, &ActionEntry::triggered, this,
                [this, radius = entry.width] { if (!m_applyingInk) applyEraserRadius(radius); });
    }
    m_applyingInk = true;
    model->setChecked(QStringLiteral("notePenColor_black"), true);
    model->setChecked(QStringLiteral("notePenWidth_medium"), true);
    model->setChecked(QStringLiteral("noteHighlighterColor_yellow"), true);
    model->setChecked(QStringLiteral("noteHighlighterWidth_medium"), true);
    model->setChecked(QStringLiteral("noteEraserMode_stroke"), true);
    model->setChecked(QStringLiteral("noteEraserSize_medium"), true);
    m_applyingInk = false;
}

void NoteHost::applyPenColor(const QColor &color)
{
    if (m_pageItem != nullptr) {
        m_pageItem->setPenColor(color);
        saveToolState();
    }
}

void NoteHost::applyHighlighterColor(const QColor &color)
{
    if (m_pageItem != nullptr) {
        m_pageItem->setHighlighterColor(color);
        saveToolState();
    }
}

void NoteHost::applyPenWidth(qreal width)
{
    if (m_pageItem != nullptr) {
        m_pageItem->setPenWidth(width);
        saveToolState();
    }
}

void NoteHost::applyHighlighterWidth(qreal width)
{
    if (m_pageItem != nullptr) {
        m_pageItem->setHighlighterWidth(width);
        saveToolState();
    }
}

void NoteHost::applyEraserRadius(qreal radius)
{
    if (m_pageItem != nullptr) {
        m_pageItem->setEraserRadius(radius);
        saveToolState();
    }
}

// Checks the entries matching the item's state; a custom value (the pen
// colour dialog) leaves its group unchecked, which the model allows.
void NoteHost::syncInkChecks()
{
    if (m_pageItem == nullptr) {
        return;
    }
    m_applyingInk = true;
    Quick::ActionModel *model = actions();
    switch (m_pageItem->tool()) {
    case NoteTool::Pen:
        model->setChecked(QStringLiteral("notePenTool"), true);
        break;
    case NoteTool::Highlighter:
        model->setChecked(QStringLiteral("noteHighlighterTool"), true);
        break;
    case NoteTool::Eraser:
        model->setChecked(QStringLiteral("noteEraserTool"), true);
        break;
    case NoteTool::Text:
        model->setChecked(QStringLiteral("noteTextTool"), true);
        break;
    }
    const auto checkColor = [model](const QString &prefix, const auto &table, const QColor &value) {
        bool any = false;
        for (const auto &entry : table) {
            if (entry.color == value) {
                model->setChecked(prefix + QString::fromLatin1(entry.id), true);
                any = true;
            }
        }
        if (!any) {
            for (const auto &entry : table) {
                model->setChecked(prefix + QString::fromLatin1(entry.id), false);
            }
        }
    };
    const auto checkWidth = [model](const QString &prefix, const auto &table, qreal value) {
        for (const auto &entry : table) {
            model->setChecked(prefix + QString::fromLatin1(entry.id), qFuzzyCompare(entry.width, value));
        }
    };
    checkColor(QStringLiteral("notePenColor_"), kPenColors, m_pageItem->penColor());
    checkColor(QStringLiteral("noteHighlighterColor_"), kHighlighterColors, m_pageItem->highlighterColor());
    checkWidth(QStringLiteral("notePenWidth_"), kPenWidths, m_pageItem->penWidth());
    checkWidth(QStringLiteral("noteHighlighterWidth_"), kHighlighterWidths, m_pageItem->highlighterWidth());
    checkWidth(QStringLiteral("noteEraserSize_"), kEraserSizes, m_pageItem->eraserRadius());
    model->setChecked(m_pageItem->eraserMode() == Ink::EraseMode::Partial
                          ? QStringLiteral("noteEraserMode_partial")
                          : QStringLiteral("noteEraserMode_stroke"),
                      true);
    m_applyingInk = false;
}

void NoteHost::loadToolState()
{
    if (m_pageItem == nullptr) {
        return;
    }
    if (m_toolState == nullptr) {
        m_toolState = std::make_unique<Core::JsonSettings>(
            Core::stateDirFor(Core::appLeaf()) + QStringLiteral("/tools.json"));
        static_cast<void>(m_toolState->load(nullptr));
    }
    const QVariantMap map = m_toolState->valueMap();
    const QColor penColor = QColor::fromString(map.value(QStringLiteral("penColor")).toString());
    if (penColor.isValid()) {
        m_pageItem->setPenColor(penColor);
    }
    if (map.contains(QStringLiteral("penWidth"))) {
        m_pageItem->setPenWidth(map.value(QStringLiteral("penWidth")).toDouble());
    }
    const QColor highlighter = QColor::fromString(map.value(QStringLiteral("highlighterColor")).toString());
    if (highlighter.isValid()) {
        m_pageItem->setHighlighterColor(highlighter);
    }
    if (map.contains(QStringLiteral("highlighterWidth"))) {
        m_pageItem->setHighlighterWidth(map.value(QStringLiteral("highlighterWidth")).toDouble());
    }
    m_pageItem->setEraserModeName(map.value(QStringLiteral("eraserMode")).toString());
    if (map.contains(QStringLiteral("eraserSize"))) {
        m_pageItem->setEraserRadius(map.value(QStringLiteral("eraserSize")).toDouble());
    }
    m_pageItem->setToolName(map.value(QStringLiteral("tool"), QStringLiteral("pen")).toString());
    syncInkChecks();
}

void NoteHost::saveToolState()
{
    if (m_toolState == nullptr || m_pageItem == nullptr) {
        return;
    }
    m_toolState->setValue(QStringLiteral("tool"), m_pageItem->toolName());
    m_toolState->setValue(QStringLiteral("penColor"), m_pageItem->penColor().name());
    m_toolState->setValue(QStringLiteral("penWidth"), m_pageItem->penWidth());
    m_toolState->setValue(QStringLiteral("highlighterColor"), m_pageItem->highlighterColor().name());
    m_toolState->setValue(QStringLiteral("highlighterWidth"), m_pageItem->highlighterWidth());
    m_toolState->setValue(QStringLiteral("eraserMode"), m_pageItem->eraserModeName());
    m_toolState->setValue(QStringLiteral("eraserSize"), m_pageItem->eraserRadius());
    // Degrade visibly: a state file that cannot be written costs the
    // remembered tool state, never the document.
    static_cast<void>(m_toolState->save(nullptr));
}

} // namespace QindaOffice::Apps::Notes
