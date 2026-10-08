// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Writing a sheet's data validations and conditional formats through
// QXlsx's public model (split out of xlsx_write.cpp in M61, D-069).
//
// Excel refuses an inline validation list longer than 255 characters, and an
// inline item cannot hold a comma at all (there is no escape for it). Such a
// list is written as a range reference instead: the range the list was read
// from when it came from a file, a range of the workbook that already holds
// exactly those items, or else a column of a hidden sheet of list values
// (HiddenLists) that buildXlsx adds after the user's sheets.

#include <QList>
#include <QString>
#include <QStringList>

namespace QXlsx {
class Worksheet;
}

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

struct HiddenLists {
    QString sheetName;          // chosen before any sheet is written; unique in the workbook
    QList<QStringList> columns; // one column per list that needed a home
};

// A name for the hidden list sheet that no sheet of `wb` already uses.
[[nodiscard]] QString hiddenListsSheetName(const Workbook &wb);

void writeValidations(QXlsx::Worksheet *worksheet, const Workbook &wb, const Sheet &sheet,
                      HiddenLists *lists);
void writeCondFormats(QXlsx::Worksheet *worksheet, const Sheet &sheet);

} // namespace QindaOffice::Calc::IO
