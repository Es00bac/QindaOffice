// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How a deck is shown (M91, D-099): PowerPoint's custom shows (Slide Show ▸
// Custom Slide Show; p:custShowLst in presentation.xml) and Set Up Slide
// Show (p:showPr in presProps.xml): presented by a speaker, browsed in a
// window or at a kiosk; all slides, a range or a custom show; looping until
// Esc; without narration or animation; advanced by hand or by the slides'
// rehearsed timings (Slide::advanceAfterMs); the pen and laser colours.
//
// showPlaylist() is the order a show plays: hidden slides are skipped (a
// link can still jump to one), and they stay in the deck to be edited.

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Show {

struct Presentation;

// A named list of the deck's slides in its own order (p:custShow).
struct CustomShow {
    quint32 number = 0; // p:custShow/@id — what "ppaction://customshow?id=" names
    QString name;
    QStringList slides; // model slide ids, in show order
    bool operator==(const CustomShow &) const = default;
};

enum class ShowType {
    Speaker, // Presented by a speaker (full screen) — p:present
    Window,  // Browsed by an individual (window) — p:browse
    Kiosk,   // Browsed at a kiosk (full screen) — p:kiosk: loops, a click never advances
};

enum class ShowRange {
    All,    // p:sldAll
    Slides, // p:sldRg (from, to)
    Custom, // p:custShow
};

struct ShowSettings {
    ShowType type = ShowType::Speaker;
    bool showScrollbar = true;   // Window: p:browse/@showScrollbar
    int kioskRestartMs = 300000; // Kiosk: back to the first slide after this much idle time
    ShowRange range = ShowRange::All;
    int from = 1;                // 1-based slide numbers (p:sldRg st / end)
    int to = 1;
    quint32 customShow = 0;      // CustomShow::number
    bool loop = false;           // Loop continuously until 'Esc' (a kiosk always loops)
    bool narration = true;       // showNarration — false: Show without narration
    bool animation = true;       // showAnimation — false: Show without animation
    bool useTimings = true;      // Advance slides: using timings, if present (else manually)
    QColor penColor = QColor(0xFF, 0x00, 0x00);   // p:penClr
    QColor laserColor = QColor(0xFF, 0x00, 0x00); // p14:laserClr
    bool operator==(const ShowSettings &) const = default;

    // The settings with what their show type and range do not use put back
    // to the defaults (a kiosk always loops) — what the file can say, so
    // a written deck reads back equal. The reader and Set Up apply it.
    [[nodiscard]] ShowSettings normalized() const
    {
        ShowSettings out = *this;
        const ShowSettings plain;
        if (out.type != ShowType::Window) {
            out.showScrollbar = plain.showScrollbar;
        }
        if (out.type != ShowType::Kiosk) {
            out.kioskRestartMs = plain.kioskRestartMs;
        } else {
            out.loop = true;
        }
        if (out.range != ShowRange::Slides) {
            out.from = plain.from;
            out.to = plain.to;
        }
        if (out.range != ShowRange::Custom) {
            out.customShow = plain.customShow;
        }
        out.penColor.setAlpha(255);
        out.laserColor.setAlpha(255);
        return out;
    }

    // What a deck without a p:showPr has (the schema's defaults).
    [[nodiscard]] static ShowSettings fileDefaults()
    {
        ShowSettings settings;
        settings.narration = false;
        return settings;
    }
};

// The deck positions a show of `deck` plays, in order: the settings' range
// or custom show (or `custom`, when ≥ 0 a custom show index to play
// instead), hidden slides left out. Empty when nothing is left to show.
[[nodiscard]] QList<int> showPlaylist(const Presentation &deck, int custom = -1);
// The custom show `number`, or -1.
[[nodiscard]] int customShowIndex(const Presentation &deck, quint32 number);
// A number no custom show of `deck` has yet.
[[nodiscard]] quint32 nextCustomShowNumber(const Presentation &deck);
// A slide timing as the slide sorter shows it: "00:05", "01:02:03".
[[nodiscard]] QString formatTiming(qint64 ms);

} // namespace QindaOffice::Show
