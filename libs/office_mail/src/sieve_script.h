// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Automatic replies as a Sieve vacation (RFC 5228, RFC 5230; scheduled
// with RFC 5260 `currentdate` and RFC 5231 `:value`) placed INSIDE the
// mailbox's active script, so the user's own filters keep running
// (M81, D-090; docs/mail.md#automatic-replies).
//
// AGENT-CONTRACT (ManageSieveProvider, tst_mail_sieve): QindaMail owns
// exactly two pieces of a script and nothing else:
//   1. one line at the very top:
//        require ["vacation", …]; # QindaMail automatic reply
//   2. one block right after the script's leading `require` commands,
//      from the line "# BEGIN QindaMail automatic reply …" to the line
//      "# END QindaMail automatic reply", holding a settings comment
//      ("# qindamail-settings: {json}") and the vacation command.
// withoutVacation() removes those lines and only those, so turning the
// reply off gives the user's script back byte for byte. Line ends follow
// the script (CRLF when it has none yet, as RFC 5228 writes them).
// AGENT-GUARD: never move, reformat or "clean up" the user's text — a
// change outside the two pieces breaks their filters and the round trip.

#include "auto_reply.h"

#include <QString>
#include <QStringList>
#include <QTimeZone>

namespace QindaOffice::Mail::Sieve {

inline constexpr const char kRequireMarker[] = "# QindaMail automatic reply";
inline constexpr const char kBeginMarker[] = "# BEGIN QindaMail automatic reply";
inline constexpr const char kEndMarker[] = "# END QindaMail automatic reply";
inline constexpr const char kSettingsPrefix[] = "# qindamail-settings: ";

// A Sieve quoted string ("…" with \" and \\ escaped; line breaks folded
// into spaces — use multiline() for text that keeps them).
QString quoted(const QString &text);
// A `text:` multi-line string: lines dot-stuffed, ending "." EOL.
QString multiline(const QString &text, const QString &eol);

// The extensions the block needs: vacation, plus date and relational when
// the reply is scheduled.
QStringList requiredExtensions(const AutoReply &reply);

// Offset just past the script's leading `require` commands (with the rest
// of the last one's line); 0 when it has none.
qsizetype requireSectionEnd(const QString &script);

// `script` with QindaMail's reply in place of any earlier one (or added).
// `addresses` become :addresses (the user's own addresses); `zone` gives
// the :zone offset of a scheduled reply's local dates.
QString withVacation(const QString &script, const AutoReply &reply, const QStringList &addresses,
                     const QTimeZone &zone = QTimeZone::systemTimeZone());
// `script` without QindaMail's pieces.
QString withoutVacation(const QString &script);
bool hasManagedVacation(const QString &script);
// The settings QindaMail wrote (found false when the script has none).
AutoReply readManaged(const QString &script, bool *found);
// True when the script has a vacation command outside QindaMail's block
// (a reply another program set up).
bool hasForeignVacation(const QString &script);

} // namespace QindaOffice::Mail::Sieve
