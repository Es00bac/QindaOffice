// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtMultimedia

// One video or audio of a slide, playing (M75, D-084): the editor's Play
// (SlideCanvasView.qml) and the slide show (PlayerMedia.qml) load this
// file at run time through a Loader — it is a plain resource
// (qrc:/qindashow/media/), NOT part of the QindaOffice.Show module, so the
// app builds and runs without Qt Multimedia; ShowMedia.available says
// whether loading it can work.
//
// AGENT-CONTRACT (show_media_actions.h, PlayerMedia.qml): the owner sets
// `source`, `video`, `loop`, `volume` (0..1) and drives `playing` (true
// plays from where it is, false pauses); `finished()` says a medium that
// does not loop reached its end; `stop()` rewinds. A video shows its
// frames only while it plays or is paused, so the poster frame the slide
// paints under it shows before and after. M91 (D-099): `startMs` (a play
// from the stopped state starts there), `endMs` (it ends there; 0: the
// file's end, less `trimEndMs`), `fadeInMs` / `fadeOutMs` (volume, and a
// video's picture, fade in after the start and out before the end),
// `position` and seek(ms) (bookmarks).
Item {
    id: surface

    property url source
    property bool video: true
    property bool loop: false
    property real volume: 0.8
    property bool playing: false
    property int startMs: 0
    property int endMs: 0
    property int trimEndMs: 0
    property int fadeInMs: 0
    property int fadeOutMs: 0
    readonly property bool running: player.playbackState === MediaPlayer.PlayingState
    readonly property bool started: player.playbackState !== MediaPlayer.StoppedState
    readonly property int position: player.position
    // Where it stops: the end time, else the file's end less what is cut off.
    readonly property int stopAt: endMs > 0 ? endMs : (trimEndMs > 0 && player.duration > 0 ? player.duration - trimEndMs : 0)
    // The fades, as a factor on the volume (and on a video's picture).
    readonly property real fade: {
        let f = 1
        if (fadeInMs > 0) f = Math.min(f, Math.max(0, (player.position - startMs) / fadeInMs))
        const end = stopAt > 0 ? stopAt : player.duration
        if (fadeOutMs > 0 && end > 0) f = Math.min(f, Math.max(0, (end - player.position) / fadeOutMs))
        return f
    }

    signal finished()

    function stop() { player.stop() }
    function seek(ms) { player.position = Math.max(startMs, stopAt > 0 ? Math.min(stopAt - 1, ms) : ms) }

    objectName: "mediaSurface"

    MediaPlayer {
        id: player
        source: surface.source
        loops: surface.loop ? MediaPlayer.Infinite : 1
        audioOutput: AudioOutput { volume: surface.volume * surface.fade }
        videoOutput: output
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && surface.playing && surface.startMs > 0)
                player.position = surface.startMs
            if (mediaStatus === MediaPlayer.EndOfMedia && !surface.loop) surface.finished()
        }
        // M91: the trimmed end ends it (or starts it over, looping).
        onPositionChanged: {
            if (surface.stopAt > 0 && position >= surface.stopAt) {
                if (surface.loop) {
                    position = surface.startMs
                } else {
                    pause()
                    surface.finished()
                }
            }
        }
    }
    VideoOutput {
        id: output
        objectName: "mediaVideoOutput"
        anchors.fill: parent
        visible: surface.video && surface.started
        opacity: surface.fade
        fillMode: VideoOutput.PreserveAspectFit
    }
    // AGENT-GUARD: the owner sets these in any order (a Loader's onLoaded,
    // a Binding that applies first): play only once there is a source.
    function sync() {
        if (playing && String(source).length > 0) {
            const fromStart = player.playbackState === MediaPlayer.StoppedState
            player.play()
            if (fromStart && startMs > 0) player.position = startMs
        } else if (!playing && started) {
            player.pause()
        }
    }
    onPlayingChanged: sync()
    onSourceChanged: sync()
    Component.onCompleted: sync()
}
