// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Audio and video in QindaShow (M75, D-084), as PowerPoint's Insert ▸
// Video / Audio and the contextual Playback tab: a file becomes a media
// shape (show_model's show_media.h: the file's own bytes, a poster frame,
// its playback), Start On Click or Automatically, Loop until Stopped, Hide
// While Not Playing, Play in the editor, a poster frame from a picture.
// The slide show plays media through Qt Multimedia (MediaSurface.qml,
// loaded at run time): automatic media with the slide, On Click media at
// its click (its build step), a click on a video plays or pauses it.
//
// AGENT-NOTE: Qt Multimedia is optional at run time. `available` is false
// when its QML module or a backend is missing (or QQO_SHOW_MEDIA=off — the
// tests use it, they never play anything); the poster shows then and the
// status bar says why. Playback reads the bytes from a file written into a
// private temporary directory (one per process, by content hash).
// QQO_SHOW_MEDIA_SURFACE (tests only) names a stand-in QML file with
// MediaSurface.qml's interface that plays nothing: playback is then
// "available" through it, so the show's media logic is tested silently.
//
// M91 (D-099): Trim, Fade Duration and bookmarks (p14:trim, p14:fade,
// p14:bmkLst), edited in one dialog and honoured by the show: a medium
// plays from its trimmed start to its trimmed end, fading in and out, and
// Alt+End / Alt+Home jump to its next / previous bookmark (PlayerMedia.qml).
//
// AGENT-CONTRACT: the command ids (MediaActionId) are the dbusmenu
// activation identity (AGENTS.md rule 5): never renamed, never reused.

#include "presentation.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>

#include <memory>

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

namespace MediaActionId {
inline const char kInsertVideo[] = "showInsertVideoAction";
inline const char kInsertAudio[] = "showInsertAudioAction";
inline const char kPlay[] = "showMediaPlayAction";
inline const char kOnClick[] = "showMediaStartOnClickAction";
inline const char kAutomatically[] = "showMediaStartAutomaticallyAction";
inline const char kLoop[] = "showMediaLoopAction";
inline const char kHide[] = "showMediaHideAction";
inline const char kPoster[] = "showMediaPosterAction";
// M91 (D-099): Trim, Fade Duration and bookmarks (MediaEditDialog.qml).
inline const char kTrim[] = "showMediaTrimAction";
} // namespace MediaActionId

class ShowMedia final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString unavailableReason READ unavailableReason CONSTANT)
    // The selected media shape for the editor's Play: {shapeId, url, kind,
    // x, y, width, height (slide points), loop, volume}; empty when none.
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    // The QML file that plays one medium (loaded by a Loader).
    Q_PROPERTY(QUrl surface READ surface CONSTANT)
public:
    explicit ShowMedia(ShowHost *host);

    [[nodiscard]] bool available() const { return m_available; }
    [[nodiscard]] QString unavailableReason() const { return m_reason; }
    [[nodiscard]] QVariantMap selected() const;
    [[nodiscard]] bool playing() const { return m_playing; }
    void setPlaying(bool playing);
    [[nodiscard]] QUrl surface() const;
    [[nodiscard]] QList<Quick::MenuNode> insertMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> playbackMenuItems() const;

    // The media of slide `row` for the player: [{shapeId, url, kind, x, y,
    // width, height, autoPlay, step, loop, hideWhenStopped, volume}].
    Q_INVOKABLE QVariantList slideMedia(int row) const;
    // The answers of the file dialogs.
    Q_INVOKABLE bool insertFile(const QUrl &url, bool audio);
    Q_INVOKABLE bool setPosterFile(const QUrl &url);
    // M91 (D-099): the selected medium's {durationMs, trimStartMs, trimEndMs
    // (cut off the end), fadeInMs, fadeOutMs, bookmarks: [{name, timeMs}]}
    // for MediaEditDialog.qml, and its answer — ONE undo step.
    Q_INVOKABLE QVariantMap mediaEdits() const;
    Q_INVOKABLE bool applyMediaEdits(const QVariantMap &edits);
    // A file URL holding a medium's bytes (or its link).
    [[nodiscard]] QUrl urlFor(const ShowModel::MediaInfo &media) const;

signals:
    void fileRequested(bool audio);
    void posterRequested();
    void editRequested(); // M91: Trim, Fade and Bookmarks…
    void selectionChanged();
    void playingChanged();

private:
    [[nodiscard]] const ShowModel::Shape *selectedMedia() const;
    bool editSelected(const QString &label, const std::function<void(ShowModel::Shape &)> &mutate);
    void sync();

    ShowHost *m_host;
    bool m_available = false;
    QString m_reason;
    QUrl m_surface;
    bool m_playing = false;
    bool m_syncing = false;
    std::unique_ptr<QTemporaryDir> m_files;
    QHash<QString, Quick::ActionEntry *> m_actions;
};

} // namespace QindaOffice::Apps::Show
