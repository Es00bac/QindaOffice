// SPDX-License-Identifier: GPL-3.0-or-later
// PowerPoint's Slide Show tab in QindaShow (show_slide_show.h).

#include "show_slide_show.h"

#include "action_model.h"
#include "menu_tree.h"
#include "presentation_controller.h"
#include "show_document.h"
#include "show_host.h"
#include "show_rehearsal.h"
#include "standard_actions.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace Id = SlideShowActionId;

namespace {

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

QString entryId(int n)
{
    return QString::fromLatin1(Id::kCustomShowPrefix) + QString::number(n);
}

QString typeName(ShowModel::ShowType type)
{
    switch (type) {
    case ShowModel::ShowType::Window: return QStringLiteral("window");
    case ShowModel::ShowType::Kiosk: return QStringLiteral("kiosk");
    case ShowModel::ShowType::Speaker: break;
    }
    return QStringLiteral("speaker");
}

QString rangeName(ShowModel::ShowRange range)
{
    switch (range) {
    case ShowModel::ShowRange::Slides: return QStringLiteral("slides");
    case ShowModel::ShowRange::Custom: return QStringLiteral("custom");
    case ShowModel::ShowRange::All: break;
    }
    return QStringLiteral("all");
}

} // namespace

ShowSlideShow::ShowSlideShow(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    Quick::ActionModel *model = host->actions();
    const auto make = [this, model](const char *id, const QString &text, const QKeySequence &shortcut, const char *icon) {
        ActionEntry *e = model->add(QString::fromLatin1(id), text, shortcut, QString::fromLatin1(icon));
        m_actions.insert(QString::fromLatin1(id), e);
        return e;
    };
    connect(make(Id::kFromCurrent, tr("From &Current Slide"), QKeySequence(QStringLiteral("Shift+F5")), "monitor-play"),
            &ActionEntry::triggered, this, [this] { m_host->startShow(); });
    for (int n = 1; n <= Id::kCustomShowEntries; ++n) {
        ActionEntry *entry = model->add(entryId(n), tr("Custom Show %1").arg(n), {}, QStringLiteral("presentation"));
        connect(entry, &ActionEntry::triggered, this, [this, n] { playCustomShow(n - 1); });
        m_entries.append(entry);
    }
    connect(make(Id::kCustomShows, tr("&Custom Shows…"), {}, "list-ordered"), &ActionEntry::triggered, this,
            &ShowSlideShow::customShowsRequested);
    connect(make(Id::kSetUp, tr("Set &Up Slide Show…"), {}, "settings-2"), &ActionEntry::triggered, this,
            &ShowSlideShow::setUpRequested);
    ActionEntry *hide = make(Id::kHideSlide, tr("&Hide Slide"), {}, "eye-off");
    hide->setCheckable(true);
    connect(hide, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            toggleHidden();
        }
    });
    connect(make(Id::kRehearse, tr("&Rehearse Timings"), {}, "timer"), &ActionEntry::triggered, this, [this] {
        PresentationController::Options options;
        options.rehearse = true;
        m_host->startShow(options);
    });
    ActionEntry *use = make(Id::kUseTimings, tr("Use &Timings"), {}, "clock");
    use->setCheckable(true);
    connect(use, &ActionEntry::triggered, this, [this, use] {
        if (m_syncing) {
            return;
        }
        QVariantMap s = settings();
        s.insert(QStringLiteral("useTimings"), use->isChecked());
        applySettings(s);
    });
    connect(make(Id::kClearTimings, tr("C&lear Timings on All Slides"), {}, "eraser"), &ActionEntry::triggered, this,
            &ShowSlideShow::clearTimings);
    connect(host, &ShowHost::deckChanged, this, &ShowSlideShow::sync);
    connect(host, &ShowHost::currentSlideChanged, this, &ShowSlideShow::sync);
    sync();
}

QList<MenuNode> ShowSlideShow::menuItems() const
{
    QList<MenuNode> shows;
    for (const ActionEntry *entry : m_entries) {
        shows.append(MenuNode::action(entry->id()));
    }
    shows += {MenuNode::separator(), a(Id::kCustomShows)};
    return {a(Quick::ActionId::kSlideStartShow), a(Id::kFromCurrent),
            MenuNode::action(QStringLiteral("showPresenterViewAction")),
            MenuNode::submenu(QStringLiteral("showCustomShowMenu"), tr("Custom Slide S&how"), shows),
            MenuNode::separator(), a(Id::kSetUp), a(Id::kHideSlide), a(Id::kRehearse), a(Id::kUseTimings),
            a(Id::kClearTimings)};
}

QStringList ShowSlideShow::customShowMenu() const
{
    QStringList ids;
    for (const ActionEntry *entry : m_entries) {
        ids.append(entry->id());
    }
    ids += {QStringLiteral("|"), QString::fromLatin1(Id::kCustomShows)};
    return ids;
}

void ShowSlideShow::sync()
{
    m_syncing = true;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (int n = 0; n < m_entries.size(); ++n) {
        ActionEntry *entry = m_entries.at(n);
        const bool there = n < deck->customShows.size();
        entry->setText(there ? deck->customShows.at(n).name : tr("Custom Show %1").arg(n + 1));
        entry->setVisible(there);
        entry->setEnabled(there && !deck->slides.isEmpty());
    }
    const ShowModel::Slide *slide = m_host->currentSlide();
    ActionEntry *hide = m_actions.value(QString::fromLatin1(Id::kHideSlide));
    hide->setEnabled(slide != nullptr && !m_host->masterViewActive());
    hide->setChecked(slide != nullptr && slide->hidden);
    m_actions.value(QString::fromLatin1(Id::kUseTimings))->setChecked(deck->showSettings.useTimings);
    const bool any = !deck->slides.isEmpty();
    for (const char *id : {Id::kFromCurrent, Id::kRehearse}) {
        m_actions.value(QString::fromLatin1(id))->setEnabled(any);
    }
    const bool timed = std::any_of(deck->slides.cbegin(), deck->slides.cend(),
                                   [](const ShowModel::Slide &s) { return s.advanceAfterMs >= 0; });
    m_actions.value(QString::fromLatin1(Id::kClearTimings))->setEnabled(timed);
    m_syncing = false;
    emit changed();
}

// ---- what the dialogs show ---------------------------------------------------------

QVariantList ShowSlideShow::slides() const
{
    QVariantList out;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (qsizetype i = 0; i < deck->slides.size(); ++i) {
        const ShowModel::Slide &slide = deck->slides.at(i);
        out.append(QVariantMap{{QStringLiteral("index"), int(i)},
                               {QStringLiteral("title"), slide.title.isEmpty() ? tr("Slide %1").arg(i + 1) : slide.title},
                               {QStringLiteral("hidden"), slide.hidden}});
    }
    return out;
}

QVariantList ShowSlideShow::customShows() const
{
    QVariantList out;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (const ShowModel::CustomShow &show : deck->customShows) {
        QVariantList positions;
        for (const QString &id : show.slides) {
            for (qsizetype i = 0; i < deck->slides.size(); ++i) {
                if (deck->slides.at(i).id == id) {
                    positions.append(int(i));
                    break;
                }
            }
        }
        out.append(QVariantMap{{QStringLiteral("name"), show.name},
                               {QStringLiteral("number"), show.number},
                               {QStringLiteral("slides"), positions}});
    }
    return out;
}

QVariantMap ShowSlideShow::settings() const
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::ShowSettings &s = deck->showSettings;
    const int custom = ShowModel::customShowIndex(*deck, s.customShow);
    return {{QStringLiteral("type"), typeName(s.type)},
            {QStringLiteral("range"), rangeName(s.range)},
            {QStringLiteral("from"), s.from},
            {QStringLiteral("to"), s.range == ShowModel::ShowRange::Slides ? s.to : int(deck->slides.size())},
            {QStringLiteral("customShow"), custom},
            {QStringLiteral("loop"), s.loop},
            {QStringLiteral("narration"), s.narration},
            {QStringLiteral("animation"), s.animation},
            {QStringLiteral("useTimings"), s.useTimings},
            {QStringLiteral("showScrollbar"), s.showScrollbar},
            {QStringLiteral("penColor"), s.penColor},
            {QStringLiteral("laserColor"), s.laserColor}};
}

qreal ShowSlideShow::advanceSeconds() const
{
    const ShowModel::Slide *slide = m_host->currentSlide();
    return slide != nullptr && slide->advanceAfterMs >= 0 ? slide->advanceAfterMs / 1000.0 : -1.0;
}

bool ShowSlideShow::advanceOnClick() const
{
    const ShowModel::Slide *slide = m_host->currentSlide();
    return slide == nullptr || slide->advanceOnClick;
}

QString ShowSlideShow::timingText(int row) const
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    if (row < 0 || row >= deck->slides.size() || deck->slides.at(row).advanceAfterMs < 0) {
        return {};
    }
    return ShowModel::formatTiming(deck->slides.at(row).advanceAfterMs);
}

// ---- changes ----------------------------------------------------------------------

int ShowSlideShow::addCustomShow(const QString &name, const QVariantList &positions)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    ShowModel::CustomShow show;
    show.number = ShowModel::nextCustomShowNumber(after);
    show.name = name.trimmed().isEmpty() ? tr("Custom Show %1").arg(after.customShows.size() + 1) : name.trimmed();
    for (const QVariant &value : positions) {
        const int row = value.toInt();
        if (row >= 0 && row < after.slides.size()) {
            show.slides.append(after.slides.at(row).id);
        }
    }
    after.customShows.append(show);
    if (!m_host->applyDeckChange(after, tr("New custom show"))) {
        return -1;
    }
    return int(after.customShows.size()) - 1;
}

bool ShowSlideShow::editCustomShow(int index, const QString &name, const QVariantList &positions)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    if (index < 0 || index >= after.customShows.size()) {
        return false;
    }
    ShowModel::CustomShow &show = after.customShows[index];
    if (!name.trimmed().isEmpty()) {
        show.name = name.trimmed();
    }
    show.slides.clear();
    for (const QVariant &value : positions) {
        const int row = value.toInt();
        if (row >= 0 && row < after.slides.size()) {
            show.slides.append(after.slides.at(row).id);
        }
    }
    return m_host->applyDeckChange(after, tr("Edit custom show"));
}

int ShowSlideShow::copyCustomShow(int index)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    if (index < 0 || index >= after.customShows.size()) {
        return -1;
    }
    ShowModel::CustomShow copy = after.customShows.at(index);
    copy.number = ShowModel::nextCustomShowNumber(after);
    copy.name = tr("Copy of %1").arg(copy.name);
    after.customShows.insert(index + 1, copy);
    return m_host->applyDeckChange(after, tr("Copy custom show")) ? index + 1 : -1;
}

bool ShowSlideShow::removeCustomShow(int index)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    if (index < 0 || index >= after.customShows.size()) {
        return false;
    }
    const quint32 number = after.customShows.at(index).number;
    after.customShows.removeAt(index);
    // A show set up to play it plays every slide again.
    if (after.showSettings.range == ShowModel::ShowRange::Custom && after.showSettings.customShow == number) {
        after.showSettings.range = ShowModel::ShowRange::All;
        after.showSettings = after.showSettings.normalized();
    }
    return m_host->applyDeckChange(after, tr("Remove custom show"));
}

void ShowSlideShow::playCustomShow(int index)
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    if (index < 0 || index >= deck->customShows.size()) {
        return;
    }
    if (ShowModel::showPlaylist(*deck, index).isEmpty()) {
        m_host->showStatusMessage(tr("“%1” has no slides to show").arg(deck->customShows.at(index).name), 6000);
        return;
    }
    PresentationController::Options options;
    options.customShow = index;
    m_host->startShow(options);
}

bool ShowSlideShow::applySettings(const QVariantMap &map)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    ShowModel::ShowSettings &s = after.showSettings;
    const QString type = map.value(QStringLiteral("type"), typeName(s.type)).toString();
    s.type = type == u"window" ? ShowModel::ShowType::Window
        : type == u"kiosk"     ? ShowModel::ShowType::Kiosk
                               : ShowModel::ShowType::Speaker;
    const QString range = map.value(QStringLiteral("range"), rangeName(s.range)).toString();
    const int custom = map.value(QStringLiteral("customShow"), -1).toInt();
    if (range == u"custom" && custom >= 0 && custom < after.customShows.size()) {
        s.range = ShowModel::ShowRange::Custom;
        s.customShow = after.customShows.at(custom).number;
    } else if (range == u"slides") {
        s.range = ShowModel::ShowRange::Slides;
        const int last = qMax(1, int(after.slides.size()));
        s.from = qBound(1, map.value(QStringLiteral("from"), s.from).toInt(), last);
        s.to = qBound(s.from, map.value(QStringLiteral("to"), s.to).toInt(), last);
    } else {
        s.range = ShowModel::ShowRange::All;
    }
    s.loop = map.value(QStringLiteral("loop"), s.loop).toBool();
    s.narration = map.value(QStringLiteral("narration"), s.narration).toBool();
    s.animation = map.value(QStringLiteral("animation"), s.animation).toBool();
    s.useTimings = map.value(QStringLiteral("useTimings"), s.useTimings).toBool();
    s.showScrollbar = map.value(QStringLiteral("showScrollbar"), s.showScrollbar).toBool();
    const QColor pen = map.value(QStringLiteral("penColor")).value<QColor>();
    const QColor laser = map.value(QStringLiteral("laserColor")).value<QColor>();
    s.penColor = pen.isValid() ? pen : s.penColor;
    s.laserColor = laser.isValid() ? laser : s.laserColor;
    s = s.normalized();
    const bool changedNow = m_host->applyDeckChange(after, tr("Set up slide show"));
    sync();
    return changedNow;
}

void ShowSlideShow::toggleHidden()
{
    const ShowModel::Slide *slide = m_host->currentSlide();
    if (slide == nullptr) {
        return;
    }
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    ShowModel::Slide *target = after.findSlide(slide->id);
    target->hidden = !target->hidden;
    m_host->applyDeckChange(after, target->hidden ? tr("Hide slide") : tr("Show slide"));
    sync();
}

bool ShowSlideShow::setAdvance(qreal seconds, bool onClick)
{
    const ShowModel::Slide *slide = m_host->currentSlide();
    if (slide == nullptr) {
        return false;
    }
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    ShowModel::Slide *target = after.findSlide(slide->id);
    target->advanceAfterMs = seconds < 0 ? -1 : int(qRound(qMin<qreal>(seconds, 86400.0) * 1000.0));
    target->advanceOnClick = onClick;
    return m_host->applyDeckChange(after, tr("Advance slide"));
}

void ShowSlideShow::clearTimings()
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    for (ShowModel::Slide &slide : after.slides) {
        slide.advanceAfterMs = -1;
    }
    m_host->applyDeckChange(after, tr("Clear timings"));
}

void ShowSlideShow::rehearsalFinished(const QHash<int, qint64> &timings, qint64 totalMs)
{
    m_pending = timings;
    if (!timings.isEmpty()) {
        emit keepTimingsRequested(rehearsalClock(totalMs));
    }
}

void ShowSlideShow::keepRehearsal(bool keep)
{
    const QHash<int, qint64> timings = std::exchange(m_pending, {});
    if (!keep || timings.isEmpty()) {
        return;
    }
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    for (auto it = timings.cbegin(); it != timings.cend(); ++it) {
        if (it.key() >= 0 && it.key() < after.slides.size()) {
            after.slides[it.key()].advanceAfterMs = int(qMin<qint64>(it.value(), 86400000));
        }
    }
    after.showSettings.useTimings = true; // PowerPoint turns Use Timings on with them
    m_host->applyDeckChange(after, tr("Rehearsed timings"));
    sync();
}

} // namespace QindaOffice::Apps::Show
