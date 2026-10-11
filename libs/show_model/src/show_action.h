// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Action settings (M91, D-099): what a click on a shape — or the mouse
// passing over it — does in the slide show, as PowerPoint's Insert ▸ Action
// keeps it in a:hlinkClick / a:hlinkHover: a jump (next, previous, first,
// last or last viewed slide, a slide of the deck, a custom show, the end of
// the show), a web address, another file, a program or a macro, a sound
// played with it, and the shape highlighted when it is clicked.
//
// ShowAction::link uses the spelling TextRun::hyperlink uses (show_text.h):
//   ""                                  no jump (a sound or a highlight alone)
//   "#<slide id>"                       a slide of this deck
//   an URL ("https://…", "mailto:…", "file:///…") or a path ("notes.docx")
//   "ppaction://hlinkshowjump?jump=<j>" nextslide, previousslide, firstslide,
//                                       lastslide, lastslideviewed, endshow
//   "ppaction://customshow?id=<n>[&return=true]"  CustomShow::number n
//   "ppaction://program", "ppaction://hlinkfile",
//   "ppaction://hlinkpres?…"            with the program / file / deck in
//                                       ShowAction::address
//   any other "ppaction://…" verb       kept as written (noaction, media,
//                                       macro?name=…, ole?verb=…)
//
// AGENT-GUARD (safety): QindaShow NEVER runs a program or a macro. Both are
// read and written so the deck keeps them for PowerPoint; the show tells
// the presenter instead (PresentationController). Another file opens only
// after the presenter agrees, and only web, e-mail and file addresses are
// handed to the desktop's URL handler (ShowHost::openActionTarget).

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Show {

struct ShowAction {
    QString link;
    QString address;        // the target a program / file / presentation verb names
    QString tooltip;        // the ScreenTip (@tooltip)
    bool highlight = false; // @highlightClick: the shape flashes when clicked
    bool stopSound = false; // @endSnd: [Stop Previous Sound]
    QByteArray sound;       // a:snd: a WAV played with the action
    QString soundName;      // its name ("Chime", "applause.wav")
    bool operator==(const ShowAction &) const = default;
    [[nodiscard]] bool isEmpty() const
    {
        return link.isEmpty() && address.isEmpty() && tooltip.isEmpty() && sound.isEmpty() && !stopSound && !highlight;
    }
};

enum class ActionKind {
    None,
    NextSlide,
    PreviousSlide,
    FirstSlide,
    LastSlide,
    LastViewed,
    EndShow,
    Slide,
    CustomShow,
    Url,          // a web or e-mail address (http, https, mailto, …)
    File,         // another file (a path or a file: URL)
    Presentation, // another PowerPoint presentation (opened as a file)
    Program,      // kept, never run
    Macro,        // kept, never run
    Media,        // ppaction://media: the picture plays its video or sound
    Other,        // any other verb, kept as written
};

// What a link means.
struct ActionTarget {
    ActionKind kind = ActionKind::None;
    QString slideId;          // Slide: the model slide id
    quint32 customShow = 0;   // CustomShow: CustomShow::number
    bool returnAfter = false; // CustomShow: "Show and return"
    QString address;          // Url / File / Presentation / Program; Macro: its name
};

[[nodiscard]] ActionTarget parseAction(const QString &link, const QString &address = QString());
[[nodiscard]] inline ActionTarget parseAction(const ShowAction &action)
{
    return parseAction(action.link, action.address);
}
// The link of a jump: NextSlide … EndShow; empty for any other kind.
[[nodiscard]] QString jumpLink(ActionKind kind);
[[nodiscard]] QString customShowLink(quint32 number, bool returnAfter);
// "#<slide id>".
[[nodiscard]] QString slideLink(const QString &slideId);
// The scheme of a web or e-mail address ("https", "mailto", …); empty for
// a path or a file: URL.
[[nodiscard]] QString webScheme(const QString &address);

// ---- action buttons (ECMA-376 Part 1 §20.1.10.56, actionButton*) ----
struct ActionButtonPreset {
    QString geometry; // "actionButtonForwardNext"
    QString label;    // "Forward or Next"
    QString iconName; // the gallery's QindaTK icon
    QString link;     // the action PowerPoint gives a new button ("" = none)
};
// PowerPoint's twelve, in its gallery order (Back, Forward, Beginning, End,
// Home, Information, Return, Movie, Document, Sound, Help, Custom).
[[nodiscard]] QList<ActionButtonPreset> actionButtonPresets();

// ---- sounds ----
// The sounds Action Settings offers without a file: short tones made here
// (QindaShow bundles no audio), as RIFF WAVE bytes; empty for an unknown
// name.
[[nodiscard]] QStringList builtInSounds();
[[nodiscard]] QByteArray builtInSound(const QString &name);
// True when `bytes` is a RIFF WAVE file (a:snd holds WAV only).
[[nodiscard]] bool isWave(const QByteArray &bytes);

} // namespace QindaOffice::Show
