// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The app-facing seams for AI features (plan section 5.3): the ONLY two
// interfaces between office_ai and the documents. Apps implement both;
// the panel and inline flows work against them, so every AI feature is
// available to any document type.

#include <QImage>
#include <QString>

namespace QindaOffice::Ai {

// What the user wants the model to see.
struct DocumentContext {
    QString selection;   // may be empty
    QString document;    // bounded by the app (plan: size estimate shown)
    QImage region;       // optional visual context
    qint64 documentSizeEstimate() const
    {
        return selection.toUtf8().size() + document.toUtf8().size();
    }
};

// How replies come back into the document.
class EditApplier {
public:
    virtual ~EditApplier() = default;
    virtual void replaceSelection(const QString &text) = 0;
    virtual void insertAtCursor(const QString &text) = 0;
};

// Apps provide current context on demand (lazy: building the document text
// can be expensive).
class DocumentContextProvider {
public:
    virtual ~DocumentContextProvider() = default;
    [[nodiscard]] virtual DocumentContext context(bool withDocument) = 0;
};

} // namespace QindaOffice::Ai
