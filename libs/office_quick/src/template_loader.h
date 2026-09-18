// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Template loader (plan M10): finds the starter documents bundled for an
// app (installed under share/qindaoffice/templates/<AppName>/). A "New
// from Template" pick loads the file into the current pristine window and
// immediately detaches it from the template path, so saving never
// overwrites the shared template.

#include <QString>
#include <QStringList>

namespace QindaOffice::Quick {

struct TemplateEntry {
    QString name; // display name (file base name, spaced)
    QString path;
};

// Templates in one directory (files only, sorted by name).
[[nodiscard]] QList<TemplateEntry> templatesIn(const QString &directory);

// Templates for an app in the installed template root; empty list when
// the tree is absent (dev runs, minimal installs).
[[nodiscard]] QList<TemplateEntry> templatesFor(const QString &appName);

} // namespace QindaOffice::Quick
