// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The package post-pass (xlsx_post.cpp): applies a PatchManifest to a saved
// xlsx — the write-side mirror of xlsx_package.h's read-side normalization,
// for every feature QXlsx has no model for.

#include "xlsx_manifest.h"

namespace QindaOffice::Calc::IO {

// Applies `manifest` to the package at `path` in place. Returns false with
// `error` set when the package does not look like QXlsx's output (a silent
// miss would be data loss the user never hears about — D-012).
[[nodiscard]] bool applyPatchManifest(const QString &path, const PatchManifest &manifest,
                                      QString *error = nullptr);

} // namespace QindaOffice::Calc::IO
