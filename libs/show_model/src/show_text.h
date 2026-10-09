// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Runs and paragraphs of slide text. Every value here is EFFECTIVE: the
// pptx reader resolves inheritance (presentation default text style →
// master title/body/other style → master and layout placeholder list
// styles → the shape's own list style → the paragraph) before storing, so
// the editor, the renderer and the writers never chase a style chain.
//
// AGENT-GUARD: the first fields of TextRun and TextParagraph keep their
// pre-M62 order — callers and tests initialise them positionally
// ({text, bold, italic, color, pointSize, fontFamily} and
// {runs, alignment, bullet, listLevel}). New fields go after them, each
// with a default member initializer (`{}` at least), so a positional or
// designated initialiser that stops early stays warning-free under
// -Wmissing-field-initializers.

#include <QColor>
#include <QList>
#include <QString>

namespace QindaOffice::Show {

struct TextRun {
    QString text; // U+2028 = a line break inside the paragraph (a:br)
    bool bold = false;
    bool italic = false;
    QColor color = Qt::black;
    qreal pointSize = 18;
    QString fontFamily;
    // ---- M62 ----
    QString underline{};  // "" = none; DrawingML ST_TextUnderlineType ("sng", "dbl", "wavy", …)
    QString strike{};     // "" = none; "sngStrike", "dblStrike"
    int baseline = 0;     // 1/1000 % of the size: 30000 superscript, -25000 subscript
    QString caps{};       // "" = none; "all", "small"
    qreal spacing = 0;    // extra letter spacing, pt
    QColor highlight{};   // invalid = none
    // Click target: an absolute URL, "#<slide id>" for a jump to a slide of
    // this deck, or a "ppaction://…" verb kept as written.
    QString hyperlink{};
    QString fieldType{};  // a:fld/@type ("slidenum", "datetime1", …); empty = plain text
    QString fieldId{};    // a:fld/@id, kept so the field stays the same field
    bool operator==(const TextRun &) const = default;
};

// Line and paragraph spacing: a percentage of the line (100 = single) or
// a fixed number of points.
struct Spacing {
    enum class Unit { Percent, Points };
    Unit unit = Unit::Percent;
    qreal value = 0;
    bool operator==(const Spacing &) const = default;
    [[nodiscard]] static Spacing percent(qreal v) { return {Unit::Percent, v}; }
    [[nodiscard]] static Spacing points(qreal v) { return {Unit::Points, v}; }
};

struct TextParagraph {
    QList<TextRun> runs;
    int alignment = 0; // Qt::Alignment (0 = the default, left)
    bool bullet = false;
    int listLevel = 0; // 0..8
    // ---- M62 ----
    QString bulletChar{};    // empty with `bullet` = "•"
    QString autoNumber{};    // a:buAutoNum/@type ("arabicPeriod", …); non-empty = numbered
    int autoNumberStart = 1;
    QString bulletFont{};    // empty = the first run's family
    QColor bulletColor{};    // invalid = the first run's colour
    qreal bulletSize = 100;  // percent of the first run's size
    qreal marginLeft = 0;    // pt (a:pPr/@marL)
    qreal indent = 0;        // pt, first line (negative = hanging)
    Spacing lineSpacing = Spacing::percent(100);
    Spacing spaceBefore = Spacing::points(0);
    Spacing spaceAfter = Spacing::points(0);
    qreal endPointSize = 0;  // a:endParaRPr/@sz; 0 = the last run's (empty lines' height)
    bool operator==(const TextParagraph &) const = default;
};

} // namespace QindaOffice::Show
