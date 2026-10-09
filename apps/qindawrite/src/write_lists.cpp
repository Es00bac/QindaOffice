// SPDX-License-Identifier: GPL-3.0-or-later
// ListController (write_lists.h): Word's list commands, their menus and
// check marks, and the list keys. The dialogs' questions and answers are
// in write_lists_dialogs.cpp.

#include "write_lists.h"

#include "docx_list_edit.h"
#include "docx_list_labels.h"
#include "docx_lists.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "standard_actions.h"
#include "write_document.h"
#include "write_selection.h"

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>

#include <algorithm>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// Word's Bullet Library: the glyph as Word stores it, in Word's font.
struct BulletPick {
    char16_t glyph;
    const char *font;
    const char *label;
};
constexpr BulletPick kBullets[] = {{0xF0B7, "Symbol", "●"},   {u'o', "Courier New", "○"},
                                   {0xF0A7, "Wingdings", "■"}, {0xF075, "Wingdings", "◆"},
                                   {0xF0D8, "Wingdings", "➢"}, {0xF0FC, "Wingdings", "✓"},
                                   {0x2013, "", "–"}};

// Word's Numbering Library.
struct NumberPick {
    const char *format;
    const char *text;
    const char *jc;
    const char *label;
};
constexpr NumberPick kNumbers[] = {{"decimal", "%1.", "left", "1. 2. 3."},      {"decimal", "%1)", "left", "1) 2) 3)"},
                                   {"upperRoman", "%1.", "right", "I. II. III."}, {"upperLetter", "%1.", "left", "A. B. C."},
                                   {"lowerLetter", "%1)", "left", "a) b) c)"},    {"lowerLetter", "%1.", "left", "a. b. c."},
                                   {"lowerRoman", "%1.", "right", "i. ii. iii."}};

constexpr const char *kMultilevelLabels[] = {"1. 1.1. 1.1.1.", "1) a) i)", "➢ ▪ • bullets",
                                             "Article I. / Section 1.01 / (a)"};

MenuNode a(const ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}

// The level text of a library number at `level` ("%1." → "%2." at level 2).
QString atLevel(const QString &text, int level)
{
    QString out = text;
    return out.replace(QStringLiteral("%1"), QStringLiteral("%%1").arg(level + 1));
}

} // namespace

ListController::ListController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                               QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    createActions(model);
    connect(m_control, &PageTextControl::cursorPositionChanged, this, &ListController::sync);
    connect(m_control, &PageTextControl::documentChanged, this, &ListController::sync);
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(0);
    connect(&m_syncTimer, &QTimer::timeout, this, &ListController::sync);
    connect(m_document->textDocument(), &QTextDocument::contentsChanged, &m_syncTimer, qOverload<>(&QTimer::start));
    sync();
}

void ListController::createActions(Quick::ActionModel *model)
{
    const auto make = [model](const QString &id, const QString &text, const char *icon = nullptr) {
        return model->add(id, text, {}, icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    m_bulletToggle = model->entry(QString::fromLatin1(Quick::ActionId::kFormatListBullet));
    m_numberToggle = model->entry(QString::fromLatin1(Quick::ActionId::kFormatListNumbered));
    connect(m_bulletToggle, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            toggleBullets();
        }
    });
    connect(m_numberToggle, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            toggleNumbering();
        }
    });
    for (int i = 0; i < int(std::size(kBullets)); ++i) {
        ActionEntry *entry = make(QStringLiteral("writeBullet%1").arg(i), tr("Bullet %1").arg(QString::fromUtf8(kBullets[i].label)));
        connect(entry, &ActionEntry::triggered, this, [this, i] { applyBullet(i); });
        m_bullets.append(entry);
    }
    for (int i = 0; i < int(std::size(kNumbers)); ++i) {
        ActionEntry *entry = make(QStringLiteral("writeNumbering%1").arg(i), tr("Numbering %1").arg(QString::fromLatin1(kNumbers[i].label)));
        connect(entry, &ActionEntry::triggered, this, [this, i] { applyNumbering(i); });
        m_numbers.append(entry);
    }
    for (int i = 0; i < Docx::kMultilevelPresets; ++i) {
        ActionEntry *entry = make(QStringLiteral("writeMultilevel%1").arg(i), tr("List %1").arg(QString::fromUtf8(kMultilevelLabels[i])));
        connect(entry, &ActionEntry::triggered, this, [this, i] { applyMultilevel(i); });
        m_multilevels.append(entry);
    }
    for (int level = 0; level < 9; ++level) {
        ActionEntry *entry = make(QStringLiteral("writeListLevel%1").arg(level + 1), tr("Level &%1").arg(level + 1));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writeListLevel"));
        connect(entry, &ActionEntry::triggered, this, [this, level] {
            if (!m_syncing) {
                setLevel(level);
            }
        });
        m_levels.append(entry);
    }
    m_bulletNone = make(QStringLiteral("writeBulletNoneAction"), tr("&No Bullets"));
    m_numberNone = make(QStringLiteral("writeNumberingNoneAction"), tr("&No Numbering"));
    m_defineBullet = make(QStringLiteral("writeDefineBulletAction"), tr("Define New &Bullet…"), "list");
    m_defineNumber = make(QStringLiteral("writeDefineNumberAction"), tr("Define New &Number Format…"), "list-ordered");
    m_defineMultilevel = make(QStringLiteral("writeDefineMultilevelAction"), tr("Define New &Multilevel List…"), "list-tree");
    m_restart = make(QStringLiteral("writeRestartNumberingAction"), tr("&Restart at 1"), "list-restart");
    m_continue = make(QStringLiteral("writeContinueNumberingAction"), tr("&Continue Numbering"), "list-plus");
    m_setValue = make(QStringLiteral("writeSetNumberingValueAction"), tr("Set Numbering &Value…"), "hash");
    m_indents = make(QStringLiteral("writeAdjustListIndentsAction"), tr("Adjust List &Indents…"), "list-indent-increase");
    connect(m_bulletNone, &ActionEntry::triggered, this, [this] { removeNumbering(); });
    connect(m_numberNone, &ActionEntry::triggered, this, [this] { removeNumbering(); });
    connect(m_defineBullet, &ActionEntry::triggered, this, &ListController::defineBulletRequested);
    connect(m_defineNumber, &ActionEntry::triggered, this, &ListController::defineNumberRequested);
    connect(m_defineMultilevel, &ActionEntry::triggered, this, &ListController::defineMultilevelRequested);
    connect(m_restart, &ActionEntry::triggered, this, [this] { restartNumbering(); });
    connect(m_continue, &ActionEntry::triggered, this, [this] { continueNumbering(); });
    connect(m_setValue, &ActionEntry::triggered, this, &ListController::numberingValueRequested);
    connect(m_indents, &ActionEntry::triggered, this, &ListController::listIndentsRequested);
}

QList<MenuNode> ListController::menuItems() const
{
    QList<MenuNode> bullets;
    for (const ActionEntry *entry : m_bullets) {
        bullets.append(a(entry));
    }
    bullets += {MenuNode::separator(), a(m_bulletNone), a(m_defineBullet)};
    QList<MenuNode> numbers;
    for (const ActionEntry *entry : m_numbers) {
        numbers.append(a(entry));
    }
    numbers += {MenuNode::separator(), a(m_numberNone), a(m_defineNumber), MenuNode::separator(), a(m_restart),
                a(m_continue), a(m_setValue)};
    QList<MenuNode> multilevel;
    for (const ActionEntry *entry : m_multilevels) {
        multilevel.append(a(entry));
    }
    multilevel += {MenuNode::separator(), a(m_defineMultilevel)};
    QList<MenuNode> levels;
    for (const ActionEntry *entry : m_levels) {
        levels.append(a(entry));
    }
    return {MenuNode::submenu(QStringLiteral("writeBulletsMenu"), tr("B&ullets"), bullets),
            MenuNode::submenu(QStringLiteral("writeNumberingMenu"), tr("N&umbering"), numbers),
            MenuNode::submenu(QStringLiteral("writeMultilevelMenu"), tr("&Multilevel List"), multilevel),
            MenuNode::submenu(QStringLiteral("writeListLevelMenu"), tr("Change List &Level"), levels),
            a(m_indents)};
}

// ---- commands -----------------------------------------------------------------------

QList<QTextBlock> ListController::blocks() const
{
    return selectedBlocks(m_control->textCursor());
}

bool ListController::command(const std::function<bool()> &edit)
{
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    const bool changed = edit();
    if (changed) {
        Docx::layoutListText(m_control->document(), m_document->parts().defaultTabStop);
    }
    cursor.endEditBlock();
    if (changed) {
        m_document->setDirty(true);
        sync();
    }
    return changed;
}

bool ListController::applyDefinition(const QString &definition, bool bullet, const QVariantMap &levelChange)
{
    return command([&] {
        const QList<QTextBlock> selected = blocks();
        if (selected.isEmpty()) {
            return false;
        }
        const Docx::WordListItem first = Docx::wordListItem(selected.first());
        bool oneList = first.isValid();
        for (const QTextBlock &block : selected) {
            const Docx::WordListItem item = Docx::wordListItem(block);
            oneList = oneList && item.isValid() && item.numId == first.numId;
        }
        if (oneList && !levelChange.isEmpty()) {
            // Word's library changes the list it is used in, at the caret's
            // level (the level the dialogs and the library texts were read
            // for).
            const Docx::WordListItem caret = Docx::wordListItem(m_control->textCursor().block());
            const int level = caret.isValid() && caret.numId == first.numId ? caret.level : first.level;
            const bool wasBullet = Docx::listDefinition(first.definition)->levels[size_t(level)].isBullet();
            if (wasBullet == bullet) {
                Docx::redefineList(m_control->document(), first.numId, Docx::withLevel(first.definition, level, levelChange));
                return true;
            }
        }
        if (oneList && levelChange.isEmpty()) {
            Docx::redefineList(m_control->document(), first.numId, definition);
            return true;
        }
        Docx::makeList(selected, definition);
        return true;
    });
}

bool ListController::applyBullet(int index)
{
    const BulletPick &pick = kBullets[qBound(0, index, int(std::size(kBullets)) - 1)];
    m_lastBullet = qBound(0, index, int(std::size(kBullets)) - 1);
    const QString glyph(QChar(pick.glyph));
    return applyDefinition(Docx::bulletDefinition(glyph, QString::fromLatin1(pick.font)), true,
                           {{QStringLiteral("format"), QStringLiteral("bullet")}, {QStringLiteral("text"), glyph},
                            {QStringLiteral("font"), QString::fromLatin1(pick.font)}});
}

bool ListController::applyNumbering(int index)
{
    const NumberPick &pick = kNumbers[qBound(0, index, int(std::size(kNumbers)) - 1)];
    m_lastNumber = qBound(0, index, int(std::size(kNumbers)) - 1);
    const int level = qMax(0, Docx::wordListItem(m_control->textCursor().block()).level);
    return applyDefinition(Docx::numberingDefinition(QString::fromLatin1(pick.format), QString::fromLatin1(pick.text),
                                                     QString::fromLatin1(pick.jc)),
                           false,
                           {{QStringLiteral("format"), QString::fromLatin1(pick.format)},
                            {QStringLiteral("text"), atLevel(QString::fromLatin1(pick.text), level)},
                            {QStringLiteral("justification"), QString::fromLatin1(pick.jc)},
                            {QStringLiteral("font"), QString()}});
}

bool ListController::applyMultilevel(int index)
{
    return applyDefinition(Docx::multilevelDefinition(qBound(0, index, Docx::kMultilevelPresets - 1)), false, {});
}

bool ListController::removeNumbering()
{
    return command([&] {
        bool any = false;
        for (const QTextBlock &block : blocks()) {
            if (block.textList() != nullptr) {
                Docx::removeFromList(block, false);
                any = true;
            }
        }
        return any;
    });
}

bool ListController::toggleBullets()
{
    const QList<QTextBlock> selected = blocks();
    const bool allBullets = !selected.isEmpty() && std::all_of(selected.cbegin(), selected.cend(), [](const QTextBlock &b) {
        const Docx::WordListItem item = Docx::wordListItem(b);
        return item.isValid() && Docx::listDefinition(item.definition)->levels[size_t(item.level)].isBullet();
    });
    return allBullets ? removeNumbering() : applyBullet(m_lastBullet);
}

bool ListController::toggleNumbering()
{
    const QList<QTextBlock> selected = blocks();
    const bool allNumbers = !selected.isEmpty() && std::all_of(selected.cbegin(), selected.cend(), [](const QTextBlock &b) {
        const Docx::WordListItem item = Docx::wordListItem(b);
        return item.isValid() && !Docx::listDefinition(item.definition)->levels[size_t(item.level)].isBullet();
    });
    return allNumbers ? removeNumbering() : applyNumbering(m_lastNumber);
}

bool ListController::setLevel(int level)
{
    return command([&] {
        bool any = false;
        for (const QTextBlock &block : blocks()) {
            if (Docx::wordListItem(block).isValid()) {
                Docx::setListLevel(block, level);
                any = true;
            }
        }
        return any;
    });
}

bool ListController::changeLevel(int delta)
{
    return command([&] {
        bool any = false;
        for (const QTextBlock &block : blocks()) {
            if (const Docx::WordListItem item = Docx::wordListItem(block); item.isValid()) {
                Docx::setListLevel(block, qBound(0, item.level + delta, 8));
                any = true;
            }
        }
        return any;
    });
}

bool ListController::restartNumbering()
{
    const QTextBlock block = m_control->textCursor().block();
    const Docx::WordListItem item = Docx::wordListItem(block);
    if (!item.isValid()) {
        return false;
    }
    const int start = Docx::listDefinition(item.definition)->levels[size_t(item.level)].start;
    return command([&] { return Docx::restartNumbering(block, start) > 0; });
}

bool ListController::continueNumbering()
{
    const QTextBlock block = m_control->textCursor().block();
    return command([&] { return Docx::continueNumbering(block); });
}

bool ListController::handleKey(QKeyEvent *event)
{
    const QTextCursor cursor = m_control->textCursor();
    const QTextBlock block = cursor.block();
    const Docx::WordListItem item = Docx::wordListItem(block);
    if (!item.isValid() || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    const bool atStart = !cursor.hasSelection() && cursor.positionInBlock() == 0;
    const bool acrossItems = cursor.hasSelection() && blocks().size() > 1;
    if ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) && (atStart || acrossItems)
        && cursor.currentTable() == nullptr) {
        const bool up = event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
        return changeLevel(up ? -1 : 1) || true;
    }
    const bool enter = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & Qt::ShiftModifier);
    if (enter && !cursor.hasSelection() && block.text().isEmpty()) {
        // Word: Enter on an empty item moves it up a level, and at the
        // first level ends the list.
        return command([&] {
            if (item.level > 0) {
                Docx::setListLevel(block, item.level - 1);
            } else {
                Docx::removeFromList(block, false);
            }
            return true;
        });
    }
    if (event->key() == Qt::Key_Backspace && atStart) {
        // Word: the number goes, the text stays where it was.
        return command([&] {
            Docx::removeFromList(block, true);
            return true;
        });
    }
    return false;
}

void ListController::sync()
{
    m_syncing = true;
    const QTextBlock block = m_control->textCursor().block();
    const Docx::WordListItem item = Docx::wordListItem(block);
    const bool bullet = item.isValid() && Docx::listDefinition(item.definition)->levels[size_t(item.level)].isBullet();
    const QTextList *qtList = block.textList();
    const bool qtBullet = qtList != nullptr && !item.isValid()
        && (qtList->format().style() == QTextListFormat::ListDisc || qtList->format().style() == QTextListFormat::ListCircle
            || qtList->format().style() == QTextListFormat::ListSquare);
    m_bulletToggle->setChecked(bullet || qtBullet);
    m_numberToggle->setChecked((item.isValid() && !bullet) || (qtList != nullptr && !item.isValid() && !qtBullet));
    for (int level = 0; level < m_levels.size(); ++level) {
        m_levels.at(level)->setEnabled(item.isValid());
        m_levels.at(level)->setChecked(item.isValid() && item.level == level);
    }
    m_restart->setEnabled(item.isValid() && !bullet);
    m_setValue->setEnabled(item.isValid() && !bullet);
    m_continue->setEnabled(item.isValid() && !bullet && Docx::continuableNumId(block) != 0);
    m_indents->setEnabled(item.isValid());
    m_syncing = false;
}

} // namespace QindaOffice::Apps::Write
