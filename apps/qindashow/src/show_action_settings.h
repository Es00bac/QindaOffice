// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Insert ▸ Action, Insert ▸ Link and the action buttons in QindaShow (M91,
// D-099), as PowerPoint has them:
//  - Action Settings (ActionSettingsDialog.qml) for the selected shape or
//    picture, on Mouse Click and on Mouse Over: none; a hyperlink to the
//    next, previous, first, last or last viewed slide, the end of the show,
//    a slide, a custom show (show and return), a web address, another file;
//    run a program (written for PowerPoint; QindaShow NEVER runs it); a
//    macro already there (kept); play a sound (built-in tones or a WAV) or
//    stop the one playing; highlight the shape when clicked. ONE undo step.
//  - Insert ▸ Link… (Ctrl+K; LinkDialog.qml): an address, an e-mail, or a
//    Place in This Document (first / last / next / previous slide, a slide
//    by title, a custom show) for the text selected in the editor (its
//    runs, PowerPoint's underlined hyperlink look), else for the selected
//    shape's click; Remove Link.
//  - The twelve action-button shapes (show_model's actionButtonPresets())
//    in Insert ▸ Shapes ▸ Action Buttons, each with PowerPoint's default
//    action; Action Settings opens on a new one, as in PowerPoint.
// The show runs them through PresentationController (show_action.h).
//
// AGENT-CONTRACT: the command ids (ActionUiId and the action-button ids)
// are the dbusmenu activation identity (AGENTS.md rule 5).

#include "presentation.h"

#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Quick {
struct MenuNode;
class ActionEntry;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace ActionUiId {
inline const char kInsertAction[] = "showInsertActionAction";
inline const char kInsertLink[] = "showInsertLinkAction";
} // namespace ActionUiId

class ShowActionSettings final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    // The action-button entries for the ribbon's Action Buttons menu.
    Q_PROPERTY(QStringList buttonIds READ buttonIds CONSTANT)
    // [{index, title}] and the custom show names, for the dialogs' lists.
    Q_PROPERTY(QVariantList slideChoices READ slideChoices NOTIFY choicesChanged)
    Q_PROPERTY(QStringList customShowChoices READ customShowChoices NOTIFY choicesChanged)
    // "", "stop", the built-in names, and the shape's own sound when it is
    // none of them — Action Settings' Play sound list (soundLabels shows them).
    Q_PROPERTY(QStringList soundChoices READ soundChoices NOTIFY choicesChanged)
    Q_PROPERTY(QStringList soundLabels READ soundLabels NOTIFY choicesChanged)
public:
    explicit ShowActionSettings(ShowHost *host);

    [[nodiscard]] QStringList buttonIds() const;
    [[nodiscard]] QVariantList slideChoices() const;
    [[nodiscard]] QStringList customShowChoices() const;
    [[nodiscard]] QStringList soundChoices() const;
    [[nodiscard]] QStringList soundLabels() const;
    [[nodiscard]] QList<Quick::MenuNode> insertMenuItems() const;

    // ---- Action Settings ----
    // {click: {...}, hover: {...}, name}: each side as actionMap() spells it.
    Q_INVOKABLE QVariantMap load() const;
    Q_INVOKABLE bool apply(const QVariantMap &click, const QVariantMap &hover);
    // Other Sound…: a WAV file for the list; its name, empty (with a status
    // message) when the file is not a WAV.
    Q_INVOKABLE QString addSoundFile(const QUrl &url);

    // ---- Insert ▸ Link ----
    // {target: "text"|"shape", text, kind: "web"|"place"|"mail"|"", address,
    // place: "first"|"last"|"next"|"previous"|"slide"|"custom", slide, custom}
    Q_INVOKABLE QVariantMap loadLink() const;
    // The same keys; kind "" removes the link.
    Q_INVOKABLE bool applyLink(const QVariantMap &link);

    // ---- the model side, for the tests and the dialogs ----
    // A map of `action` for the dialog, and back (`was` supplies what the
    // dialog cannot show: a macro, another verb, the ScreenTip, a sound
    // read from the deck).
    [[nodiscard]] QVariantMap actionMap(const ShowModel::ShowAction &action) const;
    [[nodiscard]] ShowModel::ShowAction actionFrom(const QVariantMap &map, const ShowModel::ShowAction &was) const;
    // Inserts the action button `geometry` with its default action.
    bool insertButton(const QString &geometry);

signals:
    void actionDialogRequested();
    void linkDialogRequested();
    void choicesChanged();

private:
    // The editor's selection when Insert ▸ Link fires (show_links.cpp).
    struct LinkSelection {
        QString shapeId;
        int start = 0;
        int end = 0;
    };
    [[nodiscard]] const ShowModel::Shape *target() const;
    [[nodiscard]] QString linkForPlace(const QVariantMap &link) const;
    void captureSelection();
    void sync();

    ShowHost *m_host;
    LinkSelection m_linkSelection;
    QHash<QString, Quick::ActionEntry *> m_actions;
    QHash<QString, QByteArray> m_sounds; // Other Sound… files by name
    QStringList m_buttons;
};

// The look PowerPoint gives hyperlinked text: the theme's hyperlink colour
// (0563C1 for the Office theme). text_bridge.h's setHyperlink applies it.
[[nodiscard]] QColor hyperlinkColor(const ShowModel::Presentation &deck, const ShowModel::Slide *slide);

} // namespace QindaOffice::Apps::Show
