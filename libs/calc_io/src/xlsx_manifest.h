// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The PatchManifest: the write half of the bridge builds one while the model
// is in hand, and the post-pass (xlsx_post.cpp) applies it to the saved
// package — package surgery for the parts QXlsx has no model for, without
// touching the vendored library (the write-side mirror of xlsx_package.h's
// read-side normalization).

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

struct PatchManifest {
    // One sheet's additions, keyed by the sheet's part name (deterministic
    // from QXlsx: xl/worksheets/sheet<N>.xml in document order).
    struct SheetPatch {
        QString sheetPart;       // "xl/worksheets/sheet1.xml"
        QByteArray autoFilterXml; // "<autoFilter ...>...</autoFilter>", or empty
        QString commentsPart;    // "xl/comments1.xml", or empty
        QByteArray commentsXml;  // the comments part's content
    };
    QList<SheetPatch> sheets;
};

} // namespace QindaOffice::Calc::IO
