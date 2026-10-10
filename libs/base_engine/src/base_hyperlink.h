// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Hyperlink fields (M86, D-094): Access's Hyperlink data type.
//
// AGENT-CONTRACT (storage — base_types.h, the XML interchange in
// base_xml_export.h / base_xml_import.h and the window's Edit Hyperlink
// dialog all read it):
//  * A Hyperlink field is text (declared "HYPERLINK TEXT", so SQLite gives it
//    TEXT affinity and other tools see text) holding either a bare address
//    ("https://example.org/#top") or Access's own form
//    "display#address#subaddress#screentip" with at least two '#'. A text
//    with fewer than two '#' is always a bare address, so a URL with a
//    fragment survives being typed in.
//  * The datasheet shows the display text (the address when there is none)
//    and edits the stored text; Access's form goes to Access unchanged.
//  * Following a link opens `target()`: the address with "#subaddress".

#include <QString>

namespace QindaOffice::Base {

constexpr int kHyperlinkLimit = 8192; // characters, all four parts together

struct Hyperlink {
    QString display;
    QString address;
    QString subAddress;
    QString screenTip;

    // What a datasheet cell shows: the display text, else the target.
    [[nodiscard]] QString shown() const { return display.isEmpty() ? target() : display; }
    // What a click opens ("" when the link has no address).
    [[nodiscard]] QString target() const;
    // Access's stored form: "display#address#" plus the subaddress and the
    // screen tip when there are any; a bare address when there is no
    // display text, subaddress or tip.
    [[nodiscard]] QString text() const;
    // Access's form with the display text always written ("#address#" when
    // empty) — what Access's XML and CSV interchange carry. A bare
    // address's "#fragment" is Access's subaddress there.
    [[nodiscard]] QString accessText() const;
};

[[nodiscard]] Hyperlink parseHyperlink(const QString &stored);

} // namespace QindaOffice::Base
