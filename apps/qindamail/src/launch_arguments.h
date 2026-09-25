// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// `qindamail --attach <path> [--attach <path> …] [mailto:…]` (M48, D-048):
// another app (QindaBooks e-mailing an invoice) opens a composer draft with
// files already attached. Pure functions — no store, no files touched.
//
// The shared office runner (office_quick app_runner.cpp) knows only
// positional paths, and a second launch hands those to the running instance
// over D-Bus as a plain string list. So main() folds the --attach options
// and the mailto: URL into ONE positional token
//     qindamail-compose:?attach=<abs path>&attach=…&mailto=<mailto URL>
// (each value percent-encoded) that MailHost::openWithoutDocument turns into
// a draft whose attachments are imported into the attachment store.
//
// AGENT-GUARD: attachments come only from the command line of a process the
// user started. A mailto: URL never attaches a file (RFC 6068 §3 — the
// classic "?attach=/etc/passwd" link), and the qindamail-compose: scheme is
// never registered as a URL handler in the .desktop file, so a web page
// cannot reach it.

#include <QString>
#include <QStringList>

namespace QindaOffice::Apps::Mail::Launch {

inline constexpr const char *kComposeScheme = "qindamail-compose:";

// `arguments` WITHOUT the program name. Returns them with every
// `--attach <path>` / `--attach=<path>` removed and, when there was at
// least one, the first mailto: argument (or nothing) replaced by one
// compose token. Relative paths are made absolute against `workingDir`.
// Without --attach the arguments come back unchanged.
[[nodiscard]] QStringList foldAttachArguments(const QStringList &arguments, const QString &workingDir);

struct ComposeRequest {
    bool ok = false;
    QString mailto;          // may be empty: a blank draft
    QStringList attachments; // absolute local paths
};

// Reads a compose token back (ok = false for anything else).
[[nodiscard]] ComposeRequest parseComposeToken(const QString &token);

} // namespace QindaOffice::Apps::Mail::Launch
