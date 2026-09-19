// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ImageSpec (M39, D-040): a picture anchored on a sheet as a floating
// object, the image sibling of ChartSpec. The engine holds the ENCODED
// bytes, never a QImage — calc_engine links Qt Core only, and the file's own
// bytes are what has to survive a round trip (re-encoding a photo as PNG
// would inflate the package and lose the author's format).

#include "value.h"

#include <QByteArray>
#include <QList>
#include <QSharedPointer>
#include <QString>

namespace QindaOffice::Calc {

struct ImageSpec {
    // Shared and immutable: an image is never edited in place, and every
    // undo step copies the whole workbook — a two-megabyte photo copied per
    // keystroke is the difference between a responsive grid and a stalled
    // one. Equality therefore compares the pointer, which is exactly what a
    // snapshot comparison wants: same bytes object, same image.
    QSharedPointer<const QByteArray> bytes;
    QString format = QStringLiteral("png"); // the media part's extension
    qint32 sheet = 0;
    int anchorRow = 0;
    int anchorCol = 0;
    int widthPx = 0;  // on-sheet size; 0 means "ask the decoded image"
    int heightPx = 0;

    [[nodiscard]] bool isValid() const { return bytes && !bytes->isEmpty(); }
    bool operator==(const ImageSpec &) const = default;
};

// The media content type for a format extension ("png" -> "image/png").
// Unknown extensions fall back to PNG, the one every consumer reads.
[[nodiscard]] QString imageContentType(const QString &format);

} // namespace QindaOffice::Calc
