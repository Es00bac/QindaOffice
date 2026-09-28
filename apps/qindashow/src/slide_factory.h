// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A fresh slide for one of the six layouts ("title", "title+content",
// "title+2content", "two-content", "content", "blank"), with the title and
// body text boxes where the Slide menu puts them. Defined in
// show_host_slides.cpp; shared with the assistant's tools (show_tools.cpp).

#include "presentation.h"

namespace QindaOffice::Apps::Show {

[[nodiscard]] QindaOffice::Show::Slide makeSlide(const QString &title, const QString &layout,
                                                 const QSizeF &size);

} // namespace QindaOffice::Apps::Show
