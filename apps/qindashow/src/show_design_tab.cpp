// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow's Design tab (show_design_tab.h).

#include "show_design_tab.h"

#include "menu_tree.h"
#include "show_background.h"
#include "show_document.h"
#include "show_host.h"
#include "show_master_view.h"
#include "show_theme.h"

#include <QUrl>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace DesignId = DesignActionId;

namespace {

// PowerPoint's two standard sizes, in points.
const QSizeF kWidescreen(960, 540);
const QSizeF kStandard(720, 540);

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

QList<MenuNode> actionsOf(const QStringList &ids)
{
    QList<MenuNode> nodes;
    for (const QString &id : ids) {
        nodes.append(MenuNode::action(id));
    }
    return nodes;
}

} // namespace

QString designIdPart(const QString &name)
{
    QString out;
    for (const QChar c : name) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}

ShowDesignTab::ShowDesignTab(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    createActions();
    connect(host, &ShowHost::deckChanged, this, [this] { sync(); });
    connect(host, &ShowHost::currentSlideChanged, this, [this] { sync(); });
    sync();
}

void ShowDesignTab::createActions()
{
    Quick::ActionModel *model = m_host->actions();
    const auto make = [model](const QString &id, const QString &text, const char *icon = nullptr) {
        return model->add(id, text, {}, icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    connect(make(QString::fromLatin1(DesignId::kSizeWidescreen), tr("&Widescreen (16:9)"), "monitor"),
            &ActionEntry::triggered, this, [this] { requestSlideSize(kWidescreen.width(), kWidescreen.height()); });
    connect(make(QString::fromLatin1(DesignId::kSizeStandard), tr("&Standard (4:3)"), "square"), &ActionEntry::triggered,
            this, [this] { requestSlideSize(kStandard.width(), kStandard.height()); });
    connect(make(QString::fromLatin1(DesignId::kSizeCustom), tr("&Custom Slide Size…"), "ruler"), &ActionEntry::triggered,
            this, [this] { emit customSizeRequested(slideSize().width(), slideSize().height()); });
    for (const QString &id : {QString::fromLatin1(DesignId::kSizeWidescreen), QString::fromLatin1(DesignId::kSizeStandard)}) {
        model->entry(id)->setCheckable(true);
        model->entry(id)->setRadioGroup(QStringLiteral("showSlideSize"));
    }
    ActionEntry *pane = make(QString::fromLatin1(DesignId::kFormatBackground), tr("Format &Background…"), "paint-bucket");
    pane->setCheckable(true);
    connect(pane, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            emit backgroundPaneRequested(on);
        }
    });
    ActionEntry *hide = make(QString::fromLatin1(DesignId::kHideBackgroundGraphics), tr("&Hide Background Graphics"), "eye-off");
    hide->setCheckable(true);
    connect(hide, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            m_host->background()->setHideGraphics(on);
        }
    });
    connect(make(QString::fromLatin1(DesignId::kBackgroundApplyToAll), tr("Apply Background to &All"), "check-check"),
            &ActionEntry::triggered, this, [this] { m_host->background()->applyToAll(); });
    connect(make(QString::fromLatin1(DesignId::kBackgroundReset), tr("&Reset Background"), "rotate-ccw"),
            &ActionEntry::triggered, this, [this] { m_host->background()->reset(); });
    connect(make(QString::fromLatin1(DesignId::kBrowseThemes), tr("&Browse for Themes…"), "folder-open"),
            &ActionEntry::triggered, this, &ShowDesignTab::themeFileRequested);

    // The built-in sets: one radio entry each.
    for (const ShowModel::ThemeSpec &theme : ShowModel::builtInThemes()) {
        const QString id = QStringLiteral("showTheme_%1").arg(designIdPart(theme.name));
        ActionEntry *entry = make(id, theme.name, "paintbrush");
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("showTheme"));
        connect(entry, &ActionEntry::triggered, this,
                [this, theme] { applyTheme(theme, ShowModel::ThemePart::Whole, tr("Theme")); });
        m_themeIds.append(id);
    }
    for (const ShowModel::ThemeColors &colors : ShowModel::builtInColorSets()) {
        const QString id = QStringLiteral("showThemeColors_%1").arg(designIdPart(colors.name));
        ActionEntry *entry = make(id, colors.name, "palette");
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("showThemeColors"));
        connect(entry, &ActionEntry::triggered, this, [this, colors] {
            ShowModel::ThemeSpec theme;
            theme.colors = colors;
            applyTheme(theme, ShowModel::ThemePart::Colors, tr("Theme colours"));
        });
        m_colorIds.append(id);
    }
    for (const ShowModel::ThemeFonts &fonts : ShowModel::builtInFontSets()) {
        const QString id = QStringLiteral("showThemeFonts_%1").arg(designIdPart(fonts.name));
        ActionEntry *entry = make(id, QStringLiteral("%1 (%2 / %3)").arg(fonts.name, fonts.major, fonts.minor), "type");
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("showThemeFonts"));
        connect(entry, &ActionEntry::triggered, this, [this, fonts] {
            ShowModel::ThemeSpec theme;
            theme.fonts = fonts;
            applyTheme(theme, ShowModel::ThemePart::Fonts, tr("Theme fonts"));
        });
        m_fontIds.append(id);
    }
    for (const QString &effects : ShowModel::builtInEffectSets()) {
        const QString id = QStringLiteral("showThemeEffects_%1").arg(designIdPart(effects));
        ActionEntry *entry = make(id, effects, "sparkles");
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("showThemeEffects"));
        connect(entry, &ActionEntry::triggered, this, [this, effects] {
            ShowModel::ThemeSpec theme;
            theme.effects = effects;
            applyTheme(theme, ShowModel::ThemePart::Effects, tr("Theme effects"));
        });
        m_effectIds.append(id);
    }
}

QList<MenuNode> ShowDesignTab::menuItems() const
{
    QList<MenuNode> themes = actionsOf(m_themeIds);
    themes += {MenuNode::separator(), a(DesignId::kBrowseThemes)};
    return {MenuNode::submenu(QStringLiteral("showThemesMenu"), tr("&Themes"), themes),
            MenuNode::submenu(QStringLiteral("showThemeColorsMenu"), tr("&Colors"), actionsOf(m_colorIds)),
            MenuNode::submenu(QStringLiteral("showThemeFontsMenu"), tr("&Fonts"), actionsOf(m_fontIds)),
            MenuNode::submenu(QStringLiteral("showThemeEffectsMenu"), tr("&Effects"), actionsOf(m_effectIds)),
            MenuNode::separator(),
            MenuNode::submenu(QStringLiteral("showSlideSizeMenu"), tr("Slide &Size"),
                              {a(DesignId::kSizeWidescreen), a(DesignId::kSizeStandard), a(DesignId::kSizeCustom)}),
            MenuNode::separator(),
            a(DesignId::kFormatBackground),
            a(DesignId::kHideBackgroundGraphics),
            a(DesignId::kBackgroundApplyToAll),
            a(DesignId::kBackgroundReset)};
}

QSizeF ShowDesignTab::slideSize() const
{
    return m_host->showDocument()->presentation()->slideSize;
}

void ShowDesignTab::sync()
{
    m_syncing = true;
    Quick::ActionModel *model = m_host->actions();
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::ThemeSpec theme = ShowModel::deckTheme(*deck, m_host->masterView()->currentMasterId());
    const QList<ShowModel::ThemeSpec> themes = ShowModel::builtInThemes();
    for (qsizetype i = 0; i < m_themeIds.size(); ++i) {
        model->entry(m_themeIds.at(i))->setChecked(themes.at(i).name == theme.name);
    }
    const QList<ShowModel::ThemeColors> colors = ShowModel::builtInColorSets();
    for (qsizetype i = 0; i < m_colorIds.size(); ++i) {
        model->entry(m_colorIds.at(i))->setChecked(colors.at(i).values == theme.colors.values);
    }
    const QList<ShowModel::ThemeFonts> fonts = ShowModel::builtInFontSets();
    for (qsizetype i = 0; i < m_fontIds.size(); ++i) {
        model->entry(m_fontIds.at(i))->setChecked(fonts.at(i).major == theme.fonts.major && fonts.at(i).minor == theme.fonts.minor);
    }
    const QStringList effects = ShowModel::builtInEffectSets();
    for (qsizetype i = 0; i < m_effectIds.size(); ++i) {
        model->entry(m_effectIds.at(i))->setChecked(effects.at(i) == theme.effects);
    }
    model->entry(QString::fromLatin1(DesignId::kSizeWidescreen))->setChecked(deck->slideSize == kWidescreen);
    model->entry(QString::fromLatin1(DesignId::kSizeStandard))->setChecked(deck->slideSize == kStandard);
    const bool hasSlide = !deck->slides.isEmpty() || m_host->masterView()->active();
    for (const char *id : {DesignId::kHideBackgroundGraphics, DesignId::kBackgroundApplyToAll, DesignId::kBackgroundReset}) {
        model->entry(QString::fromLatin1(id))->setEnabled(hasSlide);
    }
    model->entry(QString::fromLatin1(DesignId::kHideBackgroundGraphics))->setChecked(m_host->background()->hideGraphics());
    m_syncing = false;
    emit stateChanged();
}

void ShowDesignTab::applyTheme(const ShowModel::ThemeSpec &theme, ShowModel::ThemePart part, const QString &label)
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    QString error;
    // In the Slide Master view a theme goes to the master being edited.
    const QString master = m_host->masterView()->active() ? m_host->masterView()->currentMasterId() : QString();
    const ShowModel::Presentation after = ShowModel::themed(*deck, theme, part, master, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The theme could not be applied: %1").arg(error), 8000);
        sync();
        return;
    }
    m_host->applyDeckChange(after, label);
}

void ShowDesignTab::requestSlideSize(qreal width, qreal height)
{
    const QSizeF size(width, height);
    if (size.isEmpty() || size == slideSize()) {
        sync();
        return;
    }
    if (ShowModel::resizeAsks(slideSize(), size)) {
        emit scaleChoiceRequested(width, height); // Maximize or Ensure Fit?
        return;
    }
    applySlideSize(width, height, false);
}

void ShowDesignTab::applySlideSize(qreal width, qreal height, bool maximize)
{
    const QSizeF size(qBound(72.0, width, 4032.0), qBound(72.0, height, 4032.0));
    QString error;
    const ShowModel::Presentation after = ShowModel::resized(
        *m_host->showDocument()->presentation(), size,
        maximize ? ShowModel::SlideScale::Maximize : ShowModel::SlideScale::EnsureFit, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The slide size could not be changed: %1").arg(error), 8000);
        sync();
        return;
    }
    m_host->applyDeckChange(after, tr("Slide size"));
}

bool ShowDesignTab::applyThemeFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    const std::optional<ShowModel::ThemeSpec> theme = ShowModel::themeFromFile(path, &error);
    if (!theme) {
        m_host->showStatusMessage(tr("No theme in %1: %2").arg(path, error), 8000);
        return false;
    }
    applyTheme(*theme, ShowModel::ThemePart::Whole, tr("Theme from file"));
    return true;
}

} // namespace QindaOffice::Apps::Show
