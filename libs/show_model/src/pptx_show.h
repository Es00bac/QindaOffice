// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Custom shows and Set Up Slide Show between PowerPoint and the model
// (internal to show_model; M91, D-099; show_settings.h):
//
//   presentation.xml  <p:custShowLst><p:custShow name="Short" id="0">
//                       <p:sldLst><p:sld r:id="rId3"/>…</p:sldLst>
//                     </p:custShow></p:custShowLst>
//                     (r:id: the presentation part's own slide relationships)
//   presProps.xml     <p:showPr loop="1" showNarration="1" showAnimation="0" useTimings="0">
//                       <p:present/> | <p:browse showScrollbar="0"/> | <p:kiosk restart="300000"/>
//                       <p:sldAll/> | <p:sldRg st="2" end="5"/> | <p:custShow id="0"/>
//                       <p:penClr><a:srgbClr val="FF0000"/></p:penClr>
//                       <p:extLst><p:ext uri="{EC167BDD-8182-4AB7-AECC-EB403E3ABB37}">
//                         <p14:laserClr><a:srgbClr val="FF0000"/></p14:laserClr></p:ext>…</p:extLst>
//                     </p:showPr>
//
// AGENT-CONTRACT: the reader (pptx_reader.cpp) fills Presentation::
// customShows / showSettings and DeckSource::showSettings; the writer
// (pptx_writer.cpp) carries presProps.xml byte for byte while the settings
// equal what was read, else writes the source's part (or a new one) with
// the model's p:showPr in place of its own — every other child and every
// other extension kept.

#include "pptx_read_masters.h"
#include "pptx_write_context.h"

namespace QindaOffice::Show::Pptx {

inline const char16_t kLaserColorUri[] = u"{EC167BDD-8182-4AB7-AECC-EB403E3ABB37}";

// ---- reading ----
[[nodiscard]] QList<CustomShow> readCustomShows(const DeckReadState &state);
// The settings of the deck's presProps part (`part` gets its name); the
// schema's defaults when it has no p:showPr.
[[nodiscard]] ShowSettings readShowSettings(const DeckReadState &state, QString *part);

// ---- writing ----
// p:custShowLst naming each slide through the presentation part's
// relationship to it (`slideRelationships`: model slide id → r:id);
// nullptr when the deck has none.
[[nodiscard]] Ooxml::XmlElementPtr customShowsElement(const QList<CustomShow> &shows,
                                                      const QHash<QString, QString> &slideRelationships);
// p:showPr for `settings`, keeping the extensions of `was` (the source's,
// may be null) other than the laser colour.
[[nodiscard]] Ooxml::XmlElementPtr showPropertiesElement(const ShowSettings &settings, const Ooxml::XmlElement *was);
// p:presentationPr: `source` (the source part's root, may be null) with
// its p:showPr replaced by the model's.
[[nodiscard]] Ooxml::XmlElementPtr presentationPropertiesElement(const ShowSettings &settings,
                                                                 const Ooxml::XmlElement *source);

} // namespace QindaOffice::Show::Pptx
