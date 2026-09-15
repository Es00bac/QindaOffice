// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaCalc formatting depth (M12): the cell-formatting toolbar and its
// persistent named actions. Same discipline as QindaWrite's
// FormatController: actions are created once, shared by menus and
// toolbar, and the UI state re-reads the cursor cell's style so check
// marks and combos can never drift from the document.

#include "spreadsheet_document.h"

#include <QObject>

class QComboBox;
class QFontComboBox;
class QMenu;
class QToolBar;

namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetView;

struct CalcFormatActions {
    QAction *bold = nullptr;
    QAction *italic = nullptr;
    QAction *underline = nullptr;
    QAction *alignLeft = nullptr;
    QAction *alignCenter = nullptr;
    QAction *alignRight = nullptr;
    QAction *textColor = nullptr;
    QAction *fillColor = nullptr;
    [[nodiscard]] QList<QAction *> all() const;
};

[[nodiscard]] CalcFormatActions createCalcFormatActions(QObject *parent);

class CalcFormatController final : public QObject {
    Q_OBJECT
public:
    CalcFormatController(SpreadsheetDocument *document, SpreadsheetView *view, QObject *parent);

    [[nodiscard]] const CalcFormatActions &actions() const { return m_actions; }
    [[nodiscard]] QToolBar *toolBar();
    // Reuses the persistent actions so menus and toolbar stay in sync.
    void populateFormatMenu(QMenu *menu);

private:
    void applyPatch(const StylePatch &patch, const QString &undoText);
    void pickColor(bool fill);
    void syncFromCursor();

    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
    CalcFormatActions m_actions;
    QToolBar *m_toolBar = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QComboBox *m_numberCombo = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::QindaCalc
