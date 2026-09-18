// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow AI seams (plan section 6.3): the deck outline as document
// context (titles + shape texts + speaker notes) and an EditApplier that
// turns replies into slides. slidesFromOutline parses the structured
// deck format the generator prompts for, one slide per stanza:
//
//   ## <title>
//   - body line
//   - body line
//   Notes: <speaker notes>
//
// The parser is pure so tests and the applier share exactly one path.

#include "presentation.h"

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

#include "document_context.h"
#include "presentation.h"

#include <QString>

namespace QindaOffice::Ai {
class ModelCatalog;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

// Panel seams over a deck: the outline (titles + shape texts + notes) is
// the document context; replies either replace the current slide's first
// text box or append parsed outline slides.
class ShowContext final : public Ai::DocumentContextProvider {
public:
    explicit ShowContext(ShowHost *host);
    Ai::DocumentContext context(bool withDocument) override;

private:
    ShowHost *m_host;
};

class ShowApplier final : public Ai::EditApplier {
public:
    explicit ShowApplier(ShowHost *host);
    void replaceSelection(const QString &text) override;
    void insertAtCursor(const QString &text) override;

private:
    ShowHost *m_host;
};

[[nodiscard]] QList<ShowModel::Slide> slidesFromOutline(const QString &outline,
                                                        const QSizeF &slideSize);

// Runs the deck-generation prompt for `outline` (the window asked for it)
// against the first configured model and appends the parsed slides on
// success (the host owns undo/dirty handling). False when no provider is
// configured.
bool generateDeckFromOutline(ShowHost *host, Ai::ModelCatalog *catalog, const QString &outline);

} // namespace QindaOffice::Apps::Show
