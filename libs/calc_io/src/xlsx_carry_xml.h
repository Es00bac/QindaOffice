// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the carry stage (M66, D-075): attribute edits on carried
// parts done on their own text, so a pivot table whose location moved, a
// cache marked refreshOnLoad or a slicer cache whose tabId changed keeps
// every other byte — prefixes, declarations, the producer's spelling —
// exactly as Excel wrote it (D-068 rule 3).

#include <QByteArray>
#include <QString>

#include <functional>

namespace QindaOffice::Calc::IO::Carry {

// Rewrites every start tag whose local name is `localName` (any prefix)
// through `edit`, which gets and returns the tag's full text ("<x:location
// ref=\"A3:B6\" …/>").
[[nodiscard]] QByteArray editStartTags(const QByteArray &xml, const QString &localName,
                                       const std::function<QString(const QString &)> &edit);

// An attribute of a start tag's text, entities decoded; empty when absent.
[[nodiscard]] QString tagAttribute(const QString &tag, const QString &name);
// The tag with the attribute set (replaced, or added before the end).
[[nodiscard]] QString withTagAttribute(const QString &tag, const QString &name, const QString &value);

} // namespace QindaOffice::Calc::IO::Carry
