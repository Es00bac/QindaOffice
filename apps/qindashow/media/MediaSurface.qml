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
// paints under it shows before and after.
Item {
    id: surface

    property url source
    property bool video: true
    property bool loop: false
    property real volume: 0.8
    property bool playing: false
    readonly property bool running: player.playbackState === MediaPlayer.PlayingState
    readonly property bool started: player.playbackState !== MediaPlayer.StoppedState

    signal finished()

    function stop() { player.stop() }

    objectName: "mediaSurface"

    MediaPlayer {
        id: player
        source: surface.source
        loops: surface.loop ? MediaPlayer.Infinite : 1
        audioOutput: AudioOutput { volume: surface.volume }
        videoOutput: output
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia && !surface.loop) surface.finished()
        }
    }
    VideoOutput {
        id: output
        objectName: "mediaVideoOutput"
        anchors.fill: parent
        visible: surface.video && surface.started
        fillMode: VideoOutput.PreserveAspectFit
    }
    // AGENT-GUARD: the owner sets these in any order (a Loader's onLoaded,
    // a Binding that applies first): play only once there is a source.
    function sync() {
        if (playing && String(source).length > 0) player.play()
        else if (!playing && started) player.pause()
    }
    onPlayingChanged: sync()
    onSourceChanged: sync()
    Component.onCompleted: sync()
}
