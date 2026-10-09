// SPDX-License-Identifier: GPL-3.0-or-later
// The page setup's pure parts (page_layout.h, M78, D-087): Excel's paper
// sizes by code, and the Page Layout ▸ Breaks commands over the manual
// breaks. Split from page_layout.cpp, which cuts the pages.

#include "page_layout.h"

#include <algorithm>

namespace QindaOffice::Calc {

namespace {

constexpr double kMmPerInch = 25.4;

struct Paper {
    int code;
    double width;  // mm, portrait
    double height;
    const char *name;
};

constexpr double inch(double value)
{
    return value * kMmPerInch;
}

// Windows' DMPAPER values (wingdi.h), the codes Excel writes in paperSize,
// with Excel's names for them. B4 and B5 are JIS (Excel's "B4 (JIS)").
const QList<Paper> &papers()
{
    static const QList<Paper> table{
        {1, inch(8.5), inch(11), "Letter"},
        {2, inch(8.5), inch(11), "Letter Small"},
        {3, inch(11), inch(17), "Tabloid"},
        {4, inch(17), inch(11), "Ledger"},
        {5, inch(8.5), inch(14), "Legal"},
        {6, inch(5.5), inch(8.5), "Statement"},
        {7, inch(7.25), inch(10.5), "Executive"},
        {8, 297, 420, "A3"},
        {9, 210, 297, "A4"},
        {10, 210, 297, "A4 Small"},
        {11, 148, 210, "A5"},
        {12, 257, 364, "B4 (JIS)"},
        {13, 182, 257, "B5 (JIS)"},
        {14, inch(8.5), inch(13), "Folio"},
        {15, 215, 275, "Quarto"},
        {16, inch(10), inch(14), "10x14"},
        {17, inch(11), inch(17), "11x17"},
        {18, inch(8.5), inch(11), "Note"},
        {19, inch(3.875), inch(8.875), "Envelope #9"},
        {20, inch(4.125), inch(9.5), "Envelope #10"},
        {21, inch(4.5), inch(10.375), "Envelope #11"},
        {22, inch(4.75), inch(11), "Envelope #12"},
        {23, inch(5), inch(11.5), "Envelope #14"},
        {24, inch(17), inch(22), "C size sheet"},
        {25, inch(22), inch(34), "D size sheet"},
        {26, inch(34), inch(44), "E size sheet"},
        {27, 110, 220, "Envelope DL"},
        {28, 162, 229, "Envelope C5"},
        {29, 324, 458, "Envelope C3"},
        {30, 229, 324, "Envelope C4"},
        {31, 114, 162, "Envelope C6"},
        {32, 114, 229, "Envelope C65"},
        {33, 250, 353, "Envelope B4"},
        {34, 176, 250, "Envelope B5"},
        {35, 176, 125, "Envelope B6"},
        {36, 110, 230, "Envelope Italy"},
        {37, inch(3.875), inch(7.5), "Envelope Monarch"},
        {38, inch(3.625), inch(6.5), "6 3/4 Envelope"},
        {39, inch(14.875), inch(11), "US Std Fanfold"},
        {40, inch(8.5), inch(12), "German Std Fanfold"},
        {41, inch(8.5), inch(13), "German Legal Fanfold"},
        {42, 250, 353, "B4 (ISO)"},
        {43, 100, 148, "Japanese Postcard"},
        {44, inch(9), inch(11), "9x11"},
        {45, inch(10), inch(11), "10x11"},
        {46, inch(15), inch(11), "15x11"},
        {47, 220, 220, "Envelope Invite"},
        {50, inch(9.5), inch(12), "Letter Extra"},
        {51, inch(9.5), inch(15), "Legal Extra"},
        {52, inch(11.69), inch(18), "Tabloid Extra"},
        {53, inch(9.27), inch(12.69), "A4 Extra"},
        {54, inch(8.5), inch(11), "Letter Transverse"},
        {55, 210, 297, "A4 Transverse"},
        {56, inch(9.5), inch(12), "Letter Extra Transverse"},
        {57, 227, 356, "Super A"},
        {58, 305, 487, "Super B"},
        {59, inch(8.5), inch(12.69), "Letter Plus"},
        {60, 210, 330, "A4 Plus"},
        {61, 148, 210, "A5 Transverse"},
        {62, 182, 257, "B5 (JIS) Transverse"},
        {63, 322, 445, "A3 Extra"},
        {64, 174, 235, "A5 Extra"},
        {65, 201, 276, "B5 (ISO) Extra"},
        {66, 420, 594, "A2"},
        {67, 297, 420, "A3 Transverse"},
        {68, 322, 445, "A3 Extra Transverse"},
        {69, 200, 148, "Japanese Double Postcard"},
        {70, 105, 148, "A6"},
    };
    return table;
}

const Paper *paperFor(int code)
{
    for (const Paper &paper : papers()) {
        if (paper.code == code) {
            return &paper;
        }
    }
    return nullptr;
}

} // namespace

QSizeF paperSizeMm(int code)
{
    const Paper *paper = paperFor(code);
    if (paper == nullptr) {
        paper = paperFor(1);
    }
    return {paper->width, paper->height};
}

QList<PaperChoice> commonPaperSizes()
{
    // Excel's Size menu on a US system, the everyday sizes first.
    QList<PaperChoice> out;
    for (const int code : {1, 5, 7, 3, 8, 9, 11, 70, 12, 13, 20, 27, 28, 34, 37, 43}) {
        out.append({code, paperFor(code)->name});
    }
    return out;
}

QString paperSizeName(int code)
{
    const Paper *paper = paperFor(code);
    return paper != nullptr ? QString::fromLatin1(paper->name) : QString();
}

namespace {

// Adds a manual break at `line` (sorted, unique); false when it was there.
bool addBreak(QList<qint32> &breaks, qint32 line)
{
    if (line <= 0 || breaks.contains(line)) {
        return false;
    }
    breaks.insert(std::lower_bound(breaks.begin(), breaks.end(), line), line);
    return true;
}

} // namespace

bool insertPageBreak(Sheet::PageSetup &setup, const CellAddr &cursor)
{
    if (!cursor.isValid()) {
        return false;
    }
    bool changed = false;
    if (cursor.row > 0) {
        changed = addBreak(setup.rowBreaks, cursor.row) || changed;
    }
    if (cursor.col > 0) {
        changed = addBreak(setup.colBreaks, cursor.col) || changed;
    }
    return changed;
}

bool removePageBreak(Sheet::PageSetup &setup, const CellAddr &cursor)
{
    if (!cursor.isValid()) {
        return false;
    }
    const qsizetype removed = setup.rowBreaks.removeAll(cursor.row) + setup.colBreaks.removeAll(cursor.col);
    return removed > 0;
}

void resetPageBreaks(Sheet::PageSetup &setup)
{
    setup.rowBreaks.clear();
    setup.colBreaks.clear();
}

bool hasPageBreakAt(const Sheet::PageSetup &setup, const CellAddr &cursor)
{
    return cursor.isValid() && (setup.rowBreaks.contains(cursor.row) || setup.colBreaks.contains(cursor.col));
}

} // namespace QindaOffice::Calc
