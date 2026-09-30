// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace QindaOffice::Apps::Notes {
class NoteHost;
// Read-only, current page only. Names every block and distinguishes notes
// from timestamped speech. Omission is explicit; ink/images are not OCR.
QString noteEvidence(const NoteHost *host, int budget);
QString boundedEvidence(const QString &text, int budget);
}
