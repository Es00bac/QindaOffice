// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's Design tab (M75, D-084), with PowerPoint's names: Themes (the
// built-in ones and Browse for Themes…), Variants (Colors, Fonts, Effects),
// Slide Size (Widescreen 16:9, Standard 4:3, Custom Slide Size… — with
// Maximize or Ensure Fit when content would shrink) and Format Background
// (the pane, Hide Background Graphics, Apply to All, Reset Background).
// Every command is a persistent ActionEntry in the Design menu and on the
// ribbon; each change is ONE undo step (ShowHost::applyDeckChange), and a
// theme or size change re-resolves the deck through show_model's reader
// (pptx_design.h), so every slide inherits it as PowerPoint will show it.
//
// AGENT-CONTRACT: the ids below are the dbusmenu activation identity of
// these commands (AGENTS.md rule 5) — app-scoped literals, never renamed,
// never reused. The theme, colour, font and effect ids are made from the
// built-in sets' names (showTheme_<Name>, showThemeColors_<Name>, …; one
// per entry of show_theme.h's lists, which only ever grow).

#include "action_model.h"
#include "pptx_design.h"

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <QSizeF>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace DesignActionId {
inline const char kSizeWidescreen[] = "showSlideSizeWidescreenAction";
inline const char kSizeStandard[] = "showSlideSizeStandardAction";
inline const char kSizeCustom[] = "showSlideSizeCustomAction";
inline const char kFormatBackground[] = "showFormatBackgroundAction";
inline const char kHideBackgroundGraphics[] = "showHideBackgroundGraphicsAction";
inline const char kBackgroundApplyToAll[] = "showBackgroundApplyToAllAction";
inline const char kBackgroundReset[] = "showBackgroundResetAction";
inline const char kBrowseThemes[] = "showThemeBrowseAction";
} // namespace DesignActionId

class ShowDesignTab final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    Q_PROPERTY(QStringList themeIds READ themeIds CONSTANT)
    Q_PROPERTY(QStringList colorIds READ colorIds CONSTANT)
    Q_PROPERTY(QStringList fontIds READ fontIds CONSTANT)
    Q_PROPERTY(QStringList effectIds READ effectIds CONSTANT)
    // The deck's slide size in points (for the Slide Size dialog).
    Q_PROPERTY(QSizeF slideSize READ slideSize NOTIFY stateChanged)
public:
    explicit ShowDesignTab(ShowHost *host);

    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QStringList themeIds() const { return m_themeIds; }
    [[nodiscard]] QStringList colorIds() const { return m_colorIds; }
    [[nodiscard]] QStringList fontIds() const { return m_fontIds; }
    [[nodiscard]] QStringList effectIds() const { return m_effectIds; }
    [[nodiscard]] QSizeF slideSize() const;
    // Check marks and enabled states after any deck change.
    void sync();

    // The dialogs' answers. A new size asks Maximize or Ensure Fit through
    // scaleChoiceRequested when content would shrink (resizeAsks).
    Q_INVOKABLE void requestSlideSize(qreal width, qreal height);
    Q_INVOKABLE void applySlideSize(qreal width, qreal height, bool maximize);
    Q_INVOKABLE bool applyThemeFile(const QUrl &url);

signals:
    void customSizeRequested(qreal width, qreal height);
    void scaleChoiceRequested(qreal width, qreal height);
    void themeFileRequested();
    // M82 (D-091): Browse for Themes chose a template or deck — its masters
    // and layouts come along: for every slide, or the selected one?
    void templateScopeRequested(const QUrl &url);
    // Format Background: show (or hide) the pane.
    void backgroundPaneRequested(bool visible);
    void stateChanged();

private:
    void createActions();
    void applyTheme(const ShowModel::ThemeSpec &theme, ShowModel::ThemePart part, const QString &label);

    ShowHost *m_host;
    QStringList m_themeIds;
    QStringList m_colorIds;
    QStringList m_fontIds;
    QStringList m_effectIds;
    bool m_syncing = false;
};

// "Office Theme" → "Office_Theme": the id-safe spelling of a set's name.
[[nodiscard]] QString designIdPart(const QString &name);

} // namespace QindaOffice::Apps::Show
