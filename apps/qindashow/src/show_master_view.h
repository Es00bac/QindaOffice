// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Slide Master view (M75, D-084), as PowerPoint's: View ▸ Slide Master
// swaps the slide sorter for the master and its layouts (MasterPane.qml)
// and puts the selected one on the canvas as a slide the canvas edits —
// its placeholders (moved, resized, retyped, deleted) and decorations
// (shapes and pictures inserted, styled, removed) — while the Slide Master
// tab and the View ▸ Master Tools menu insert, duplicate, rename, delete
// and preserve layouts, switch a layout's Title and Footers, insert
// placeholders, and the Text Styles pane sets the master's title and body
// levels 1–5. Close Master View goes back to the slides.
//
// Every change is ONE undo step: the canvas's template slide is put back
// into the master or layout (show_model's master_edit.h) and the deck is
// re-resolved (pptx_design.h), so every slide using the layout or master
// follows — through the same inheritance the pptx reader resolves when a
// deck opens. A deck QindaShow made gets PowerPoint's structure (a master
// with its theme, placeholders and layouts) the first time the view opens.
//
// AGENT-GUARD: while the view is active the canvas holds m_template, a
// slide that is NOT in the deck: ShowHost pushes no slide commands for
// canvas edits then (they would name a slide the deck does not have); the
// view commits them itself, queued after the canvas's signal returns —
// committing re-seats the canvas, which must not happen inside its own
// gesture handling.
//
// AGENT-CONTRACT: the command ids (MasterActionId) are the dbusmenu
// activation identity (AGENTS.md rule 5): never renamed, never reused.

#include "master_edit.h"
#include "presentation.h"
#include "slide_renderer.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QPainter;

namespace QindaOffice::Quick {
struct MenuNode;
class ActionEntry;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace MasterActionId {
inline const char kView[] = "showSlideMasterViewAction";
inline const char kClose[] = "showCloseMasterViewAction";
inline const char kInsertLayout[] = "showInsertLayoutAction";
inline const char kDuplicateLayout[] = "showDuplicateLayoutAction";
inline const char kDeleteLayout[] = "showDeleteLayoutAction";
inline const char kRename[] = "showRenameLayoutAction";
inline const char kPreserve[] = "showPreserveLayoutAction";
inline const char kTitle[] = "showLayoutTitleAction";
inline const char kFooters[] = "showLayoutFootersAction";
inline const char kContentPlaceholder[] = "showInsertContentPlaceholderAction";
inline const char kTextPlaceholder[] = "showInsertTextPlaceholderAction";
inline const char kPicturePlaceholder[] = "showInsertPicturePlaceholderAction";
inline const char kTextStyles[] = "showMasterTextStylesAction";
} // namespace MasterActionId

class ShowMasterView final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // [{masterId, layoutId, name, master (bool), uses (slides), preserve}]
    // — each master followed by its layouts.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    // Bumped on every change: the thumbnails repaint.
    Q_PROPERTY(int revision READ revision NOTIFY itemsChanged)
    // The Text Styles pane: Title and Text levels 1–5 of the current master,
    // [{label, family, size, bold, italic, underline, color, bullet, alignment, indent}].
    Q_PROPERTY(QVariantList styleLevels READ styleLevels NOTIFY itemsChanged)
    Q_PROPERTY(QStringList themeFonts READ themeFonts NOTIFY itemsChanged)
public:
    explicit ShowMasterView(ShowHost *host);

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] QVariantList items() const;
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);
    [[nodiscard]] QString currentName() const;
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QString currentMasterId() const;
    [[nodiscard]] QString currentLayoutId() const { return m_layoutId; }
    [[nodiscard]] QVariantList styleLevels() const;
    [[nodiscard]] QStringList themeFonts() const;
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    Q_INVOKABLE void enter();
    Q_INVOKABLE void close();
    // The canvas again (after undo / redo, a load, any deck change).
    void refreshCanvas();
    // A thumbnail of item `index` (the pane's items) in `size`.
    void renderItem(int index, QPainter *painter, const QSizeF &size) const;
    // A master (`layoutId` empty) or layout of `deck` drawn in `size`, its
    // placeholders framed — the master view's thumbnails and (M82) the New
    // Slide gallery's.
    static void renderTemplate(QPainter *painter, const ShowModel::Presentation &deck, const QString &masterId,
                               const QString &layoutId, const QSizeF &size);
    // The Text Styles pane: row 0 = Title, rows 1–5 = Text levels 1–5;
    // `look` holds what changed (family, size, bold, italic, underline,
    // color, bullet, alignment, indent).
    Q_INVOKABLE void setStyleLevel(int row, const QVariantMap &look);
    Q_INVOKABLE void renameCurrent(const QString &name);

signals:
    void activeChanged();
    void itemsChanged();
    void currentChanged();
    void renameRequested(const QString &current);
    void textStylesRequested(bool visible);

private:
    struct Item {
        QString masterId;
        QString layoutId; // empty: the master
    };
    [[nodiscard]] QList<Item> listed() const;
    [[nodiscard]] ShowModel::SlideBackdrop backdrop() const;
    void select(const QString &masterId, const QString &layoutId);
    void scheduleCommit();
    void commitCanvas();
    // One master-view change: `mutate` on a copy of the deck, re-resolved,
    // pushed as `label`; false when nothing changed.
    bool change(const QString &label, const std::function<bool(ShowModel::Presentation &)> &mutate);
    void createActions();
    void syncActions();

    ShowHost *m_host;
    bool m_active = false;
    bool m_commitPending = false;
    QString m_masterId;
    QString m_layoutId;
    ShowModel::Slide m_template;
    int m_revision = 0;
    QHash<QString, Quick::ActionEntry *> m_actions;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
