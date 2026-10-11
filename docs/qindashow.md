<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaShow

QindaShow edits one presentation per ordinary desktop window. PowerPoint
packages retain unmodelled parts; the shared slide renderer draws the editor,
slide show, print, pictures and video frames.

## Actions and links (M91, D-099)

Insert ▸ Action edits a shape or picture's Mouse Click and Mouse Over actions.
Choose the next, previous, first, last or last viewed slide, a particular slide,
a custom show (optionally returning afterwards), End Show, a web page, another
file, a sound or Highlight Click. Insert ▸ Shapes ▸ Action Buttons offers the
twelve PowerPoint presets. Each inserts its default navigation action and opens
Action Settings. Sounds are WAV files or three generated tones.

Insert ▸ Link (Ctrl+K) offers web/file addresses, e-mail and Place in This
Document for selected text or a shape. Text links retain PowerPoint's hyperlink
colour and underline. Settings changes and link edits each undo in one step.

The show and presenter view hit-test shape actions and linked text. Hidden
slides are excluded from ordinary navigation but an explicit slide link can
reach one. A custom show's Show and Return restores the invoking playlist and
slide. Programs and macros are preserved for PowerPoint and produce a notice;
QindaShow never executes them. Web/e-mail links use the desktop URL handler.
Other files require confirmation; executable files and script types are refused.

Interchange uses `a:hlinkClick` / `a:hlinkHover`, slide relationships and
`ppaction://` actions. Unknown actions remain preserved while unchanged.

## Custom shows, setup and rehearsal

Slide Show ▸ Custom Shows creates named slide lists, edits their order, copies,
deletes or plays them. Repeated slides are supported. Set Up Slide Show chooses
speaker, individual window or kiosk; all slides, a range or a custom show; loop,
narration, animation and timing options; pen and laser colours. Kiosks advance
through timings or actions, loop, and restart after their configured idle time.
Esc always ends a show. Hide Slide keeps the slide editable while skipping it
in the playlist. From Beginning is F5; From Current Slide is Shift+F5.

Rehearse Timings adds a recording bar with slide/total clocks, Pause, Repeat
and Next. On exit, Keep stores the last visit's elapsed time per slide and
turns Use Timings on; Discard leaves the deck unchanged. Keeping the rehearsal
is one undo step. The slide list/sorter shows saved timings. Advance Slide in
the slide panel edits timing and click advancement directly.

`p:custShowLst` lives in `presentation.xml`; `p:showPr` in `presProps.xml`;
slide timing is `p:transition/@advTm`. Unchanged presentation properties are
kept byte for byte. Theme-based pen colours and unknown extensions are retained.

## Media and effects

Playback ▸ Trim, Fade and Bookmarks edits start/end trimming, audio/video
fade durations and named bookmarks. The show applies these through optional
Qt Multimedia; Alt+End and Alt+Home seek between bookmarks. If multimedia is
unavailable the poster remains and the application reports the limitation.
Media settings round-trip through `p14:trim`, `p14:fade` and `p14:bmkLst`.

Effect Options offers the supported directions of Fly, Wipe, Peek, Float and
Split. Exit effects have their own direction menu. Entrance/exit direction
is written as PowerPoint's `presetSubtype`, and the renderer uses it in the
show. This does not implement PowerPoint's entire effect/timing vocabulary.

## Video and picture export

Slide ▸ Create a Video exports 720p, 1080p or 4K MP4 using the system
`/usr/bin/ffmpeg` (Portage package `media-video/ffmpeg`). If it is missing, the
command is disabled and explains why. Choose recorded timings and a fallback
seconds-per-slide. A worker renders a snapshot of the deck and pipes frames to
ffmpeg; progress and cancellation remain responsive. Cancel/error removes the
partial output and preserves a previous export at the destination. Replacement
commits atomically after encoding succeeds.

Visible slides play in deck order; builds are evenly distributed over each
slide. Non-cut transitions become cross-fades. Embedded audio is mixed with
trim, volume and fades. Embedded video remains its poster frame, not moving
footage; action-triggered sounds are not synthesized. The show and video exporter honour
Show Without Narration for audio marked `p:audio/@isNarration` or media marked
`p15:isNarration`, while retaining ordinary slide sounds. These markers follow
[Microsoft’s audio definition](https://learn.microsoft.com/en-us/office/open-xml/presentation/how-to-add-an-audio-to-a-slide-in-a-presentation)
and [shape narration extension](https://learn.microsoft.com/en-us/openspecs/office_standards/ms-pptx/ce6eae7c-fec0-41a7-9758-6adf67baf4c4).
Picture export writes the current slide or every slide as PNG/JPEG, including
hidden slides, using the same renderer.

## Comment positions

Comment pins show each thread's slide point or shape anchor in the editor.
Click a pin to select its thread and open Comments. X/Y fields in the thread
card move it in slide points, detach its shape anchor, and undo as one step.
Positions round-trip using the [native comment position](https://learn.microsoft.com/en-us/openspecs/office_standards/ms-pptx/161bc2c9-98fc-46b7-852b-ba7ee77e2e54). Pins stay out of print and shows.

## Remaining interoperability checks

The automated gates use synthetic PowerPoint-shaped packages, ECMA schemas,
LibreOffice and ffprobe. A real PowerPoint desktop round trip, audible media
playback, actual URL/file handlers and multi-monitor presenter use still need
manual verification with the owner's consent. The full
PowerPoint animation catalogue, video footage inside exported movies, advanced
trigger chains and advanced narration recording/editing remain open.
