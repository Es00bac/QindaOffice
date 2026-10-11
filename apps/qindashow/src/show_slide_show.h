// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PowerPoint's Slide Show tab in QindaShow (M91, D-099): From Beginning
// (slideStartShowAction, F5 — the first slide of the show Set Up
// describes) and From Current Slide (Shift+F5); Custom Slide Show — the
// deck's custom shows to play (showCustomShow_<n>, by position, re-labelled
// as the shows change, the rest hidden) and Custom Shows… (new, edit: pick
// and order slides, copy, remove, show; CustomShowsDialog.qml); Set Up
// Slide Show… (SetUpShowDialog.qml: speaker / window / kiosk, all slides /
// from–to / a custom show, loop, without narration or animation, by hand
// or by timings, pen and laser colours); Hide Slide (the show skips it, the
// editor keeps it); Rehearse Timings (the show with the Recording toolbar,
// then "keep the new timings?"); Use Timings; Clear Timings; and the
// current slide's Advance Slide (on a click, after N seconds: the Slide
// panel). Each change is ONE undo step (ShowHost::applyDeckChange); kept
// rehearsal timings are one step too, so Undo puts the old ones back.
//
// AGENT-CONTRACT: the command ids (SlideShowActionId) are the dbusmenu
// activation identity (AGENTS.md rule 5): never renamed, never reused; the
// custom show entries are ids by position, like Open Recent's.

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Quick {
struct MenuNode;
class ActionEntry;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace SlideShowActionId {
inline const char kFromCurrent[] = "showFromCurrentAction";
inline const char kCustomShows[] = "showCustomShowsAction";
inline const char kSetUp[] = "showSetUpAction";
inline const char kHideSlide[] = "showHideSlideAction";
inline const char kRehearse[] = "showRehearseTimingsAction";
inline const char kUseTimings[] = "showUseTimingsAction";
inline const char kClearTimings[] = "showClearTimingsAction";
// showCustomShow_1 … showCustomShow_10: the deck's custom shows by position.
inline const char kCustomShowPrefix[] = "showCustomShow_";
inline constexpr int kCustomShowEntries = 10;
} // namespace SlideShowActionId

class ShowSlideShow final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    // [{name, number, slides: [deck positions]}] for Custom Shows….
    Q_PROPERTY(QVariantList customShows READ customShows NOTIFY changed)
    // [{index, title, hidden}] — every slide, for the dialogs.
    Q_PROPERTY(QVariantList slides READ slides NOTIFY changed)
    // The deck's Set Up Slide Show settings as a map (settingsMap()).
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    // The ribbon's Custom Slide Show menu: the entries, "|", Custom Shows….
    Q_PROPERTY(QStringList customShowMenu READ customShowMenu CONSTANT)
    // The current slide's Advance Slide (the Slide panel): after this many
    // seconds (-1: no timing) and on a mouse click.
    Q_PROPERTY(qreal advanceSeconds READ advanceSeconds NOTIFY changed)
    Q_PROPERTY(bool advanceOnClick READ advanceOnClick NOTIFY changed)
public:
    explicit ShowSlideShow(ShowHost *host);

    [[nodiscard]] QVariantList customShows() const;
    [[nodiscard]] QVariantList slides() const;
    [[nodiscard]] QVariantMap settings() const;
    [[nodiscard]] QStringList customShowMenu() const;
    [[nodiscard]] qreal advanceSeconds() const;
    [[nodiscard]] bool advanceOnClick() const;
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // ---- the dialogs' answers (each one undo step) ----
    // A new custom show of the slides at `positions` (deck positions, in
    // show order); its index, -1 when nothing was made.
    Q_INVOKABLE int addCustomShow(const QString &name, const QVariantList &positions);
    Q_INVOKABLE bool editCustomShow(int index, const QString &name, const QVariantList &positions);
    Q_INVOKABLE int copyCustomShow(int index);
    Q_INVOKABLE bool removeCustomShow(int index);
    Q_INVOKABLE void playCustomShow(int index);
    // Set Up Slide Show's OK: keys as settings() gives them.
    Q_INVOKABLE bool applySettings(const QVariantMap &settings);
    Q_INVOKABLE bool setAdvance(qreal seconds, bool onClick);
    // The rehearsal's "keep the new timings?" answer.
    Q_INVOKABLE void keepRehearsal(bool keep);
    // The slide sorter's timing under a slide ("00:05"; "" without one).
    Q_INVOKABLE QString timingText(int row) const;
    // The rehearsal ended (ShowHost::endShow): ask whether to keep it.
    void rehearsalFinished(const QHash<int, qint64> &timings, qint64 totalMs);

signals:
    void changed();
    void customShowsRequested();
    void setUpRequested();
    // "The total time for the slide show was 0:01:23. Keep the new timings?"
    void keepTimingsRequested(const QString &total);

private:
    void sync();
    void toggleHidden();
    void clearTimings();

    ShowHost *m_host;
    QHash<QString, Quick::ActionEntry *> m_actions;
    QList<Quick::ActionEntry *> m_entries;
    QHash<int, qint64> m_pending;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
