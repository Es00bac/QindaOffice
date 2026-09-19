// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_spec.h"

#include <QHash>

namespace QindaOffice::Calc {

QString imageContentType(const QString &format)
{
    static const QHash<QString, QString> kTypes{
        {QStringLiteral("png"), QStringLiteral("image/png")},
        {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("gif"), QStringLiteral("image/gif")},
        {QStringLiteral("bmp"), QStringLiteral("image/bmp")},
        {QStringLiteral("tif"), QStringLiteral("image/tiff")},
        {QStringLiteral("tiff"), QStringLiteral("image/tiff")},
        {QStringLiteral("webp"), QStringLiteral("image/webp")},
        {QStringLiteral("emf"), QStringLiteral("image/x-emf")},
        {QStringLiteral("wmf"), QStringLiteral("image/x-wmf")},
        {QStringLiteral("svg"), QStringLiteral("image/svg+xml")},
    };
    return kTypes.value(format.toLower(), QStringLiteral("image/png"));
}

} // namespace QindaOffice::Calc
