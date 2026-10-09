// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Format ▸ Font and Change Case (M70, D-079). Word's Font dialog: family,
// style, size, colour, underline style and colour, strikethrough and double
// strikethrough, superscript, subscript, small caps, all caps, hidden; and
// its Advanced page: spacing expanded or condensed, scale, position raised
// or lowered, kerning. What Qt can draw is drawn (underline styles beyond
// Qt's, double strikethrough and hidden text are shown approximately and
// kept: docx_properties.h kUnderlineWord, kDoubleStrike, kHiddenText).
// Change Case rewrites the selected letters keeping every character's
// formatting; Shift+F3 cycles lowercase → UPPERCASE → Capitalize Each
// Word → Sentence case. Each command is one undo step.

#include "action_model.h"

#include <QObject>
#include <QVariantMap>

namespace QindaOffice::Quick {
struct MenuNode;
}

class QFont;
class QTextCharFormat;
class QTextCursor;

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

// The Font dialog's question for a run's format, and its answer on the
// runs `cursor` selects (run by run), in any document: the page's story,
// or a style's sample in Modify Style (write_style_draft.h, M73). Keys are
// FontController's below; applyFontAt is not an edit block.
[[nodiscard]] QVariantMap fontInfoOf(const QTextCharFormat &format, const QFont &base);
bool applyFontAt(const QTextCursor &cursor, const QVariantMap &answer);

class FontController final : public QObject {
    Q_OBJECT
public:
    FontController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);

    // Format ▸ Font… and the Change Case submenu.
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QStringList caseIds() const;
    [[nodiscard]] QString fontId() const;

    // The Font dialog. Keys: family, size (pt), bold, italic, color
    // ("#rrggbb", "" automatic), underline ("none", "single", "words",
    // "double", "thick", "dotted", "dash", "dotDash", "dotDotDash",
    // "wave"), underlineColor, strike, doubleStrike, superscript,
    // subscript, smallCaps, allCaps, hidden, spacing (pt: + expanded,
    // − condensed), scale (%), position (pt: + raised, − lowered), kerning
    // (bool) and kernAt (pt). The answer carries `changed` (the keys the
    // user touched); only those are applied.
    Q_INVOKABLE QVariantMap fontInfo() const;
    Q_INVOKABLE bool applyFont(const QVariantMap &answer);

    enum class Case { Sentence, Lower, Upper, Capitalize, Toggle };
    // Rewrites the selection's letters; false without a selection.
    bool changeCase(Case mode);
    // Shift+F3: the next case after what the selection is now.
    bool cycleCase();

signals:
    void fontDialogRequested();

private:
    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_font = nullptr;
    QList<Quick::ActionEntry *> m_cases;
    Quick::ActionEntry *m_cycle = nullptr;
};

} // namespace QindaOffice::Apps::Write
