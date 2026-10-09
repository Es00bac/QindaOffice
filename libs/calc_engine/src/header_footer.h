// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Headers and footers (M78, D-087): Excel's codes (ECMA-376 §18.3.1.46) —
// &L / &C / &R open the left, centre and right sections; &P the page
// number (&P+n, &P-n), &N the number of pages, &D the date, &T the time,
// &F the file name, &Z its folder, &A the sheet's name, &G a picture, &&
// an ampersand; &"font,style", &nn (size), &B, &I, &U, &E, &S, &X, &Y and
// &K… (colour) format what follows. Parsed into sections of runs for the
// printer, turned into Page Setup's own &[Page] spelling for editing and
// back, and filled in for one page. Qt Core only.

#include "value.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QTime>

#include <optional>

namespace QindaOffice::Calc {

struct HeaderFooterRun {
    enum class Field { None, Page, Pages, Date, Time, File, Path, Sheet, Picture };
    QString text;       // literal text (Field::None)
    Field field = Field::None;
    int pageOffset = 0; // &P+n / &P-n
    QString font;       // &"font,style": the family ("-" keeps the default)
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool doubleUnderline = false;
    bool strikeout = false;
    bool superscript = false;
    bool subscript = false;
    double size = 0;    // points; 0 = the default (Calibri 11 in Excel)
    std::optional<Rgb> color;
    bool operator==(const HeaderFooterRun &) const = default;
};

struct HeaderFooterSection {
    QList<HeaderFooterRun> runs;
    [[nodiscard]] bool isEmpty() const { return runs.isEmpty(); }
    bool operator==(const HeaderFooterSection &) const = default;
};

struct HeaderFooterText {
    HeaderFooterSection left;
    HeaderFooterSection center;
    HeaderFooterSection right;
    bool operator==(const HeaderFooterText &) const = default;
};

// Excel's code → sections. Text before any &L/&C/&R goes to the centre (Excel's rule).
[[nodiscard]] HeaderFooterText parseHeaderFooter(const QString &code);

// What a page shows in a field.
struct HeaderFooterContext {
    int page = 1;
    int pages = 1;
    QDate date;
    QTime time;
    QString fileName;  // "Budget.xlsx"; empty for an unsaved workbook ("Book1" is shown)
    QString filePath;  // its folder, with the trailing separator
    QString sheetName;
};
// One section as plain text with its fields filled in (the printer draws
// run by run; tests and the preview read this).
[[nodiscard]] QString headerFooterSectionText(const HeaderFooterSection &section, const HeaderFooterContext &context);
// The text of one run with its field filled in.
[[nodiscard]] QString headerFooterRunText(const HeaderFooterRun &run, const HeaderFooterContext &context);

// Page Setup's Custom Header / Custom Footer spelling of one section of a
// code: the fields as &[Page], &[Pages], &[Date], &[Time], &[File],
// &[Path], &[Tab], &[Picture], literal ampersands as &&, formatting codes
// kept as written. And back: the three sections joined into the file's
// code ("&LDraft&CPage &P of &N&R&D"); empty sections are left out.
struct HeaderFooterParts {
    QString left;
    QString center;
    QString right;
};
[[nodiscard]] HeaderFooterParts headerFooterParts(const QString &code);
[[nodiscard]] QString headerFooterCode(const HeaderFooterParts &parts);

// The ready-made headers and footers of Excel's Header/Footer dropdowns
// ("Page 1", "Page 1 of ?", "Sheet1", "Confidential, 10/9/2026, Page 1",
// the file name, …) as codes, with the text the dropdown shows for them.
struct HeaderFooterPreset {
    QString code;
    QString label;
};
[[nodiscard]] QList<HeaderFooterPreset> headerFooterPresets(const HeaderFooterContext &context);

} // namespace QindaOffice::Calc
