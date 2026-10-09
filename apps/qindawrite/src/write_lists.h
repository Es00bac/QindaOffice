// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's bullets, numbering and multilevel lists in QindaWrite (M73,
// D-082), on office_docx's list model (docx_lists.h, docx_list_edit.h):
//  - the Bullets (● ○ ■ ◆ ➢ ✓ –), Numbering (1. 1) I. A. a) a. i.) and
//    Multilevel List (1. 1.1. 1.1.1., 1) a) i), bullet levels,
//    Article/Section) libraries, each with None and Define New…;
//  - the Bullets and Numbering buttons (the last library pick, or off);
//  - Change List Level (1–9), Tab / Shift+Tab at an item's start, and
//    Increase / Decrease Indent on list items;
//  - Restart at 1, Continue Numbering, Set Numbering Value, Adjust List
//    Indents;
//  - Enter on an empty item ends the list (or promotes a nested item) and
//    Backspace at an item's start removes its number, as in Word.
// A library pick on paragraphs of one list of that kind changes the list
// (Word's library changes the list's format); otherwise the selected
// paragraphs become a new list. Every command is one undo step on the
// control's current story; the dialogs are QML (DefineBulletDialog,
// DefineNumberDialog, DefineMultilevelDialog, NumberingValueDialog,
// ListIndentsDialog) answering through the invokables below.

#include "action_model.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <functional>

class QKeyEvent;
class QTextBlock;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class ListController final : public QObject {
    Q_OBJECT
public:
    ListController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);

    // Format ▸ Bullets, Numbering, Multilevel List, Change List Level, and
    // the numbering commands.
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // The library entries (index into the library) and the toggles.
    bool applyBullet(int index);
    bool applyNumbering(int index);
    bool applyMultilevel(int index);
    bool toggleBullets();
    bool toggleNumbering();
    bool removeNumbering();
    // Change List Level for the selected items; false outside a list.
    bool setLevel(int level);
    bool changeLevel(int delta);
    bool restartNumbering();
    bool continueNumbering();
    // Tab, Shift+Tab, Enter and Backspace on list items (the control's key
    // filter); true when the key was a list command.
    bool handleKey(QKeyEvent *event);

    // Define New Bullet: {glyph (as Word stores it), font, color
    // ("#rrggbb" or ""), justification}.
    Q_INVOKABLE QVariantMap bulletInfo() const;
    Q_INVOKABLE bool applyDefineBullet(const QVariantMap &answer);
    // Define New Number Format: {format, text ("%1" is the number), start,
    // justification, level, levelText}. On a level that also shows other
    // levels' numbers ("%1.%2.") levelText is true and the text is Word's
    // own %1 … %9; the answer carries levelText back.
    Q_INVOKABLE QVariantMap numberFormatInfo() const;
    Q_INVOKABLE bool applyNumberFormat(const QVariantMap &answer);
    // Define New Multilevel List: {levels: nine level specs
    // (Docx::levelSpec, positions in twips), level (the caret's), inList}.
    Q_INVOKABLE QVariantMap multilevelInfo() const;
    Q_INVOKABLE bool applyMultilevelDefinition(const QVariantMap &answer);
    // Set Numbering Value: {value, canContinue}; answer {mode ("new",
    // "continue"), value}.
    Q_INVOKABLE QVariantMap numberingValueInfo() const;
    Q_INVOKABLE bool applyNumberingValue(const QVariantMap &answer);
    // Adjust List Indents: {numberPosition, textIndent (px), suffix ("tab",
    // "space", "nothing"), tab (px, −1 none)}.
    Q_INVOKABLE QVariantMap indentsInfo() const;
    Q_INVOKABLE bool applyIndents(const QVariantMap &answer);
    // Marker text for a level spec (the dialogs' previews): `level`'s
    // number is `value`, every other level's 1.
    Q_INVOKABLE QString previewLabel(const QVariantList &levels, int level, int value) const;
    // Number formats the dialogs offer: [{format, label}].
    Q_INVOKABLE QVariantList numberFormats() const;

signals:
    void defineBulletRequested();
    void defineNumberRequested();
    void defineMultilevelRequested();
    void numberingValueRequested();
    void listIndentsRequested();

private:
    // write_lists.cpp
    void createActions(Quick::ActionModel *model);
    void sync();
    // One command on the control's story as one undo step; lays the
    // markers' text out after it. `edit` returns whether it changed anything.
    bool command(const std::function<bool()> &edit);
    [[nodiscard]] QList<QTextBlock> blocks() const;
    // A new list from the selection, or `definition` on the selected
    // paragraphs' list when they all are items of one list of that kind.
    bool applyDefinition(const QString &definition, bool bullet, const QVariantMap &levelChange);
    // write_lists_dialogs.cpp
    [[nodiscard]] QVariantMap caretLevel() const;

    PageTextControl *m_control;
    WriteDocument *m_document;
    QList<Quick::ActionEntry *> m_bullets;
    QList<Quick::ActionEntry *> m_numbers;
    QList<Quick::ActionEntry *> m_multilevels;
    QList<Quick::ActionEntry *> m_levels;
    Quick::ActionEntry *m_bulletToggle = nullptr;
    Quick::ActionEntry *m_numberToggle = nullptr;
    Quick::ActionEntry *m_restart = nullptr;
    Quick::ActionEntry *m_continue = nullptr;
    Quick::ActionEntry *m_setValue = nullptr;
    Quick::ActionEntry *m_indents = nullptr;
    Quick::ActionEntry *m_defineBullet = nullptr;
    Quick::ActionEntry *m_defineNumber = nullptr;
    Quick::ActionEntry *m_defineMultilevel = nullptr;
    Quick::ActionEntry *m_bulletNone = nullptr;
    Quick::ActionEntry *m_numberNone = nullptr;
    int m_lastBullet = 0;
    int m_lastNumber = 0;
    bool m_syncing = false;
    QTimer m_syncTimer; // coalesces the document's change signals
};

} // namespace QindaOffice::Apps::Write
