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

class QWidget;

namespace QindaOffice::Ai {
class ModelCatalog;
}

namespace QindaOffice::Apps::Show {

class ShowWindow;

// Panel seams over a deck: the outline (titles + shape texts + notes) is
// the document context; replies either replace the current slide's first
// text box or append parsed outline slides.
class ShowContext final : public Ai::DocumentContextProvider {
public:
    explicit ShowContext(ShowWindow *window);
    Ai::DocumentContext context(bool withDocument) override;

private:
    ShowWindow *m_window;
};

class ShowApplier final : public Ai::EditApplier {
public:
    explicit ShowApplier(ShowWindow *window);
    void replaceSelection(const QString &text) override;
    void insertAtCursor(const QString &text) override;

private:
    ShowWindow *m_window;
};

[[nodiscard]] QList<ShowModel::Slide> slidesFromOutline(const QString &outline,
                                                        const QSizeF &slideSize);

// Runs the deck-generation prompt against the picked model and appends
// the parsed slides on success (the window owns undo/dirty handling).
void generateDeckFromOutline(ShowWindow *window, Ai::ModelCatalog *catalog, QWidget *dialogParent);

} // namespace QindaOffice::Apps::Show
