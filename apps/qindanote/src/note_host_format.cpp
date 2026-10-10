// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's formatted typing (M84, D-093): OneNote's Basic Text, Styles,
// lists, alignment, links and tables as persistent ActionEntries over the
// open text block's QindaTK document (NotePageItem's rich half).
//
// AGENT-CONTRACT (AGENTS.md rule 5): the rich-text ids are the suite's
// shared ones (standard_actions.h "shared with any text host": formatBold…,
// insertTableAction, insertHyperlinkAction, tableInsertRow…) and the style
// commands are noteStyle_<id>; none is ever renamed or reused. Each is in
// the menu tree (note_host.cpp) and on the ribbon (NoteRibbonLayout.qml).
// They act on the open editor; with none open they are disabled — except
// Insert Table and Link, which start a new note as OneNote does.

#include "note_host.h"

#include "note_page_item.h"
#include "note_rich_style.h"
#include "standard_actions.h"

#include <qindatk/rich_text_area.h>
#include <qindatk/rich_text_format_state.h>

#include <QFileInfo>
#include <QUrl>

#include <utility>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;
using RichStyle::Style;
using RichStyle::TableEdit;
namespace ActionId = Quick::ActionId;

namespace {

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

struct StyleCommand {
    const char *suffix;
    Style style;
    const char *text;
    const char *shortcut;
};

// OneNote's Styles gallery, its order and its Ctrl+Alt+1…6 headings.
const StyleCommand kStyles[] = {
    {"normal", Style::Normal, QT_TRANSLATE_NOOP("NoteHost", "&Normal"), nullptr},
    {"title", Style::Title, QT_TRANSLATE_NOOP("NoteHost", "&Title"), nullptr},
    {"heading1", Style::Heading1, QT_TRANSLATE_NOOP("NoteHost", "Heading &1"), "Ctrl+Alt+1"},
    {"heading2", Style::Heading2, QT_TRANSLATE_NOOP("NoteHost", "Heading &2"), "Ctrl+Alt+2"},
    {"heading3", Style::Heading3, QT_TRANSLATE_NOOP("NoteHost", "Heading &3"), "Ctrl+Alt+3"},
    {"heading4", Style::Heading4, QT_TRANSLATE_NOOP("NoteHost", "Heading &4"), "Ctrl+Alt+4"},
    {"heading5", Style::Heading5, QT_TRANSLATE_NOOP("NoteHost", "Heading &5"), "Ctrl+Alt+5"},
    {"heading6", Style::Heading6, QT_TRANSLATE_NOOP("NoteHost", "Heading &6"), "Ctrl+Alt+6"},
    {"quote", Style::Quote, QT_TRANSLATE_NOOP("NoteHost", "&Quote"), nullptr},
    {"code", Style::Code, QT_TRANSLATE_NOOP("NoteHost", "&Code"), nullptr},
};

// OneNote's font sizes, for Grow and Shrink Font.
const qreal kSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36, 48, 72};

const char *const kTextIds[] = {ActionId::kFormatBold,        ActionId::kFormatItalic,
                                ActionId::kFormatUnderline,   ActionId::kFormatStrikeOut,
                                ActionId::kFormatSubscript,   ActionId::kFormatSuperscript,
                                ActionId::kFormatGrowFont,    ActionId::kFormatShrinkFont,
                                ActionId::kFormatTextColor,   ActionId::kFormatHighlight,
                                ActionId::kFormatClearFormat, ActionId::kFormatListBullet,
                                ActionId::kFormatListNumbered, ActionId::kFormatIndentMore,
                                ActionId::kFormatIndentLess,  ActionId::kFormatAlignLeft,
                                ActionId::kFormatAlignCenter, ActionId::kFormatAlignRight};

const char *const kTableIds[] = {ActionId::kTableInsertRowAbove, ActionId::kTableInsertRowBelow,
                                 ActionId::kTableInsertColumnLeft, ActionId::kTableInsertColumnRight,
                                 ActionId::kTableDeleteRow, ActionId::kTableDeleteColumn, "noteTableDeleteAction"};

} // namespace

bool NoteHost::richCommand(const QString &command, const QVariantMap &args)
{
    return m_pageItem != nullptr && m_pageItem->richCommand(command, args);
}

void NoteHost::createFormatActions()
{
    Quick::ActionModel *model = actions();
    const auto toggle = [this, model](const char *action, const QString &text, const QKeySequence &key,
                                      const char *icon, const char *command) {
        ActionEntry *entry = model->add(id(action), text, key, id(icon));
        entry->setCheckable(true);
        connect(entry, &ActionEntry::triggered, this, [this, command] {
            if (!m_syncingFormat) {
                richCommand(id(command));
                syncFormatActions(); // the check follows the text, not the click
            }
        });
    };
    toggle(ActionId::kFormatBold, tr("&Bold"), QKeySequence::Bold, "bold", "text.bold");
    toggle(ActionId::kFormatItalic, tr("&Italic"), QKeySequence::Italic, "italic", "text.italic");
    toggle(ActionId::kFormatUnderline, tr("&Underline"), QKeySequence::Underline, "underline", "text.underline");
    // AGENT-NOTE: OneNote's keys for these three (Ctrl+-, Ctrl+=,
    // Ctrl+Shift+=) are the suite's Zoom Out and Zoom In (QKeySequence's
    // standard zoom keys); an ambiguous shortcut fires neither, so they
    // have none here — the Text tab and the Format menu carry them.
    toggle(ActionId::kFormatStrikeOut, tr("S&trikethrough"), {}, "strikethrough", "text.strike");
    toggle(ActionId::kFormatSubscript, tr("Su&bscript"), {}, "subscript", "text.subscript");
    toggle(ActionId::kFormatSuperscript, tr("Su&perscript"), {}, "superscript", "text.superscript");
    const auto grow = [this](int step) {
        const QVariantMap format = textFormat();
        const qreal now = format.value(QStringLiteral("size")).toReal();
        qreal next = now;
        for (const qreal size : kSizes) {
            if (step > 0 && size > now + 0.01) {
                next = size;
                break;
            }
            if (step < 0 && size < now - 0.01) {
                next = size;
            }
        }
        applyFontSize(next);
    };
    connect(model->add(id(ActionId::kFormatGrowFont), tr("&Grow Font"), QKeySequence(QStringLiteral("Ctrl+Shift+>")),
                       QStringLiteral("a-large-small")),
            &ActionEntry::triggered, this, [grow] { grow(1); });
    connect(model->add(id(ActionId::kFormatShrinkFont), tr("S&hrink Font"),
                       QKeySequence(QStringLiteral("Ctrl+Shift+<")), QStringLiteral("shrink")),
            &ActionEntry::triggered, this, [grow] { grow(-1); });
    connect(model->add(id(ActionId::kFormatTextColor), tr("Font &Colour…"), {}, QStringLiteral("baseline")),
            &ActionEntry::triggered, this, [this] {
                emit formatColorRequested(QStringLiteral("text"),
                                          textFormat().value(QStringLiteral("color")).value<QColor>());
            });
    // The highlighter button marks with the last colour, or takes it off.
    connect(model->add(id(ActionId::kFormatHighlight), tr("Text &Highlight Colour"),
                       QKeySequence(QStringLiteral("Ctrl+Alt+H")), QStringLiteral("highlighter")),
            &ActionEntry::triggered, this, [this] {
                const bool marked = m_pageItem != nullptr && m_pageItem->richHighlight().isValid();
                applyHighlight(marked ? QColor() : m_highlightColor);
            });
    connect(model->add(id(ActionId::kFormatClearFormat), tr("Clear &Formatting"), {},
                       QStringLiteral("remove-formatting")),
            &ActionEntry::triggered, this, [this] { richCommand(QStringLiteral("text.clearFormatting")); });

    for (const StyleCommand &command : kStyles) {
        ActionEntry *entry = model->add(QStringLiteral("noteStyle_") + id(command.suffix), tr(command.text),
                                        command.shortcut != nullptr ? QKeySequence(id(command.shortcut))
                                                                    : QKeySequence(),
                                        command.style == Style::Normal ? QStringLiteral("pilcrow")
                                        : command.style == Style::Title ? QStringLiteral("type-outline")
                                        : command.style == Style::Quote ? QStringLiteral("quote")
                                        : command.style == Style::Code  ? QStringLiteral("code")
                                        : command.style == Style::Heading1 ? QStringLiteral("heading-1")
                                        : command.style == Style::Heading2 ? QStringLiteral("heading-2")
                                        : command.style == Style::Heading3 ? QStringLiteral("heading-3")
                                                                           : QStringLiteral("heading"));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("noteStyle"));
        connect(entry, &ActionEntry::triggered, this, [this, style = command.style] {
            if (!m_syncingFormat && m_pageItem != nullptr) {
                m_pageItem->applyRichStyle(style);
                syncFormatActions();
            }
        });
    }

    toggle(ActionId::kFormatListBullet, tr("&Bullets"), QKeySequence(QStringLiteral("Ctrl+.")), "list",
           "paragraph.bulletList");
    toggle(ActionId::kFormatListNumbered, tr("&Numbering"), QKeySequence(QStringLiteral("Ctrl+/")), "list-ordered",
           "paragraph.numberedList");
    connect(model->add(id(ActionId::kFormatIndentMore), tr("&Increase Indent"),
                       QKeySequence(QStringLiteral("Alt+Shift+Right")), QStringLiteral("indent-increase")),
            &ActionEntry::triggered, this, [this] {
                if (m_pageItem != nullptr) {
                    m_pageItem->indentRich(1);
                }
            });
    connect(model->add(id(ActionId::kFormatIndentLess), tr("&Decrease Indent"),
                       QKeySequence(QStringLiteral("Alt+Shift+Left")), QStringLiteral("indent-decrease")),
            &ActionEntry::triggered, this, [this] {
                if (m_pageItem != nullptr) {
                    m_pageItem->indentRich(-1);
                }
            });
    const auto align = [this, model](const char *action, const QString &text, const QKeySequence &key,
                                     const char *icon, const char *value) {
        ActionEntry *entry = model->add(id(action), text, key, id(icon));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("noteAlign"));
        connect(entry, &ActionEntry::triggered, this, [this, value] {
            if (!m_syncingFormat) {
                richCommand(QStringLiteral("paragraph.align"), {{QStringLiteral("value"), id(value)}});
                syncFormatActions();
            }
        });
    };
    // OneNote: Ctrl+L / Ctrl+R; Ctrl+E is Search there (note_host_pages.cpp).
    align(ActionId::kFormatAlignLeft, tr("Align &Left"), QKeySequence(QStringLiteral("Ctrl+L")), "align-left", "left");
    align(ActionId::kFormatAlignCenter, tr("&Centre"), {}, "align-center", "center");
    align(ActionId::kFormatAlignRight, tr("Align &Right"), QKeySequence(QStringLiteral("Ctrl+R")), "align-right",
          "right");

    // Insert: tables and links.
    connect(model->add(id(ActionId::kInsertTable), tr("&Table…"), {}, QStringLiteral("table")),
            &ActionEntry::triggered, this, [this] { emit tableDialogRequested(); });
    connect(model->add(id(ActionId::kInsertHyperlink), tr("&Link…"), QKeySequence(QStringLiteral("Ctrl+K")),
                       QStringLiteral("link")),
            &ActionEntry::triggered, this, [this] {
                const QString selected = m_pageItem != nullptr ? m_pageItem->richSelectedText() : QString();
                const QString address =
                    m_pageItem != nullptr
                        ? m_pageItem->richCommandState(QStringLiteral("text.link")).value(QStringLiteral("value")).toString()
                        : QString();
                emit linkDialogRequested(selected, address);
            });
    const auto table = [this, model](const char *action, const QString &text, const char *icon, TableEdit edit) {
        connect(model->add(id(action), text, {}, id(icon)), &ActionEntry::triggered, this, [this, edit] {
            if (m_pageItem != nullptr) {
                m_pageItem->editRichTable(edit);
            }
        });
    };
    table(ActionId::kTableInsertRowAbove, tr("Insert Row &Above"), "between-horizontal-start", TableEdit::RowAbove);
    table(ActionId::kTableInsertRowBelow, tr("Insert Row &Below"), "rows-2", TableEdit::RowBelow);
    table(ActionId::kTableInsertColumnLeft, tr("Insert Column &Left"), "between-vertical-start",
          TableEdit::ColumnLeft);
    table(ActionId::kTableInsertColumnRight, tr("Insert Column &Right"), "columns-2", TableEdit::ColumnRight);
    table(ActionId::kTableDeleteRow, tr("Delete Ro&w"), "separator-horizontal", TableEdit::DeleteRow);
    table(ActionId::kTableDeleteColumn, tr("Delete Colum&n"), "separator-vertical", TableEdit::DeleteColumn);
    table("noteTableDeleteAction", tr("&Delete Table"), "trash-2", TableEdit::DeleteTable);
    // Edit ▸ Cut, Copy, Paste and Select All act on the formatted-text
    // editor (paste sanitised, NoteClipboard); the plain editors keep their
    // own keys, so these stay off while one of those is open.
    for (const auto &[action, command] : {std::pair{ActionId::kEditCut, "edit.cut"},
                                          std::pair{ActionId::kEditCopy, "edit.copy"},
                                          std::pair{ActionId::kEditPaste, "edit.paste"},
                                          std::pair{ActionId::kEditSelectAll, "edit.selectAll"}}) {
        connect(this->action(id(action)), &ActionEntry::triggered, this,
                [this, command] { richCommand(id(command)); });
    }
}

void NoteHost::syncFormatActions()
{
    Quick::ActionModel *model = actions();
    const bool rich = m_pageItem != nullptr && m_pageItem->richDocument() != nullptr;
    const bool inTable = rich && m_pageItem->richCaretInTable();
    for (const char *action : kTextIds) {
        model->setEnabled(id(action), rich);
    }
    for (const StyleCommand &command : kStyles) {
        model->setEnabled(QStringLiteral("noteStyle_") + id(command.suffix), rich);
    }
    for (const char *action : kTableIds) {
        model->setEnabled(id(action), inTable);
    }
    for (const char *action : {ActionId::kEditCut, ActionId::kEditCopy, ActionId::kEditPaste, ActionId::kEditSelectAll}) {
        model->setEnabled(id(action), rich);
    }
    const bool havePage = currentPage() != nullptr;
    model->setEnabled(id(ActionId::kInsertTable), havePage && !inTable);
    model->setEnabled(id(ActionId::kInsertHyperlink), havePage);
    m_syncingFormat = true;
    const QindaTK::RichTextArea *area = rich ? m_pageItem->richArea() : nullptr;
    const QindaTK::RichTextFormatState *state = area != nullptr && area->document() == m_pageItem->richDocument()
        ? area->formatState() : nullptr;
    const auto check = [model](const char *action, bool on) { model->setChecked(id(action), on); };
    check(ActionId::kFormatBold, state != nullptr && state->bold() == Qt::Checked);
    check(ActionId::kFormatItalic, state != nullptr && state->italic() == Qt::Checked);
    check(ActionId::kFormatUnderline, state != nullptr && state->underline() == Qt::Checked);
    check(ActionId::kFormatStrikeOut, state != nullptr && state->strike() == Qt::Checked);
    check(ActionId::kFormatSubscript, state != nullptr && state->subscript() == Qt::Checked);
    check(ActionId::kFormatSuperscript, state != nullptr && state->superscript() == Qt::Checked);
    check(ActionId::kFormatListBullet, state != nullptr && state->list() == QLatin1String("bullet"));
    check(ActionId::kFormatListNumbered, state != nullptr && state->list() == QLatin1String("number"));
    const QString alignment = state != nullptr && !state->alignmentMixed() ? state->alignment() : QString();
    check(ActionId::kFormatAlignLeft, alignment == QLatin1String("left") || alignment == QLatin1String("start"));
    check(ActionId::kFormatAlignCenter, alignment == QLatin1String("center"));
    check(ActionId::kFormatAlignRight, alignment == QLatin1String("right") || alignment == QLatin1String("end"));
    const int style = rich ? m_pageItem->richStyleAtCaret() : -1;
    for (const StyleCommand &command : kStyles) {
        model->setChecked(QStringLiteral("noteStyle_") + id(command.suffix), int(command.style) == style);
    }
    m_syncingFormat = false;
    setRibbonContexts(inTable ? QStringList{QStringLiteral("noteTable")} : QStringList{});
    emit textFormatChanged();
}

QVariantMap NoteHost::textFormat() const
{
    const bool rich = m_pageItem != nullptr && m_pageItem->richDocument() != nullptr;
    const QindaTK::RichTextArea *area = rich ? m_pageItem->richArea() : nullptr;
    if (area == nullptr || area->document() != m_pageItem->richDocument()) {
        return {{QStringLiteral("editable"), rich}, {QStringLiteral("family"), QString()},
                {QStringLiteral("size"), 11.0}, {QStringLiteral("color"), QColor(0x20, 0x20, 0x28)},
                {QStringLiteral("highlight"), QColor()}, {QStringLiteral("inTable"), false}};
    }
    const QindaTK::RichTextFormatState *state = area->formatState();
    return {{QStringLiteral("editable"), state->editable()},
            {QStringLiteral("family"), state->fontFamilyMixed() ? QString() : state->fontFamily()},
            {QStringLiteral("size"), state->fontSize()},
            {QStringLiteral("color"), state->color().isValid() ? state->color() : QColor(0x20, 0x20, 0x28)},
            {QStringLiteral("highlight"), m_pageItem->richHighlight()},
            {QStringLiteral("inTable"), m_pageItem->richCaretInTable()}};
}

void NoteHost::applyFontFamily(const QString &family)
{
    if (!family.trimmed().isEmpty()) {
        richCommand(QStringLiteral("text.fontFamily"), {{QStringLiteral("value"), family.trimmed()}});
    }
    emit editorFocusRequested();
}

void NoteHost::applyFontSize(qreal points)
{
    if (points >= 1 && points <= 1638) {
        richCommand(QStringLiteral("text.fontSize"), {{QStringLiteral("value"), points}});
    }
    emit editorFocusRequested();
}

void NoteHost::applyTextColor(const QColor &color)
{
    if (color.isValid()) {
        richCommand(QStringLiteral("text.color"), {{QStringLiteral("value"), color.name(QColor::HexRgb)}});
    }
    emit editorFocusRequested();
}

void NoteHost::applyHighlight(const QColor &color)
{
    // An invalid or fully transparent colour takes the highlight off.
    const bool mark = color.isValid() && color.alpha() > 0;
    if (mark) {
        m_highlightColor = color;
    }
    richCommand(QStringLiteral("text.highlight"),
                {{QStringLiteral("value"), mark ? color.name(QColor::HexRgb) : QString()}});
    emit editorFocusRequested();
}

void NoteHost::applyLink(const QString &address, const QString &label)
{
    QString href = address.trimmed();
    if (href.isEmpty() || m_pageItem == nullptr) {
        return;
    }
    // A bare "example.org" is a web address, as OneNote reads it.
    if (QUrl(href).scheme().isEmpty() && !href.startsWith(QLatin1Char('/')) && !href.startsWith(QLatin1Char('#'))) {
        href.prepend(QStringLiteral("https://"));
    } else if (href.startsWith(QLatin1Char('/'))) {
        href = QUrl::fromLocalFile(href).toString();
    }
    if (m_pageItem->richDocument() == nullptr) {
        m_pageItem->addTextBlock(); // a link with nothing open starts a note
    }
    m_pageItem->insertRichLink(href, label);
    emit editorFocusRequested();
}

void NoteHost::insertTable(int rows, int columns)
{
    if (m_pageItem != nullptr) {
        m_pageItem->insertRichTable(rows, columns);
        emit editorFocusRequested();
    }
}

void NoteHost::linkFile(const QUrl &url)
{
    // Linked notes: a link from the page to a document (QindaWrite opens
    // a .docx, the browser a web page) — OneNote's Insert ▸ Link to File.
    if (!url.isValid() || url.isEmpty()) {
        return;
    }
    const QString name = url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName() : url.fileName();
    applyLink(url.toString(), name);
}

} // namespace QindaOffice::Apps::Notes
