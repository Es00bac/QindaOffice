// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Mailings ▸ Envelopes and Labels in QindaWrite (M77, D-086; office_docx
// docx_envelopes.h):
//  - Envelopes: delivery and return address (remembered, or omitted), the
//    envelope size (#10, DL, C5, C6, … or custom), where the addresses go
//    (Word's Auto or from the left / top); Add to Document puts the
//    envelope first in its own section (an empty document becomes the
//    envelope), New Document opens it in a window, Print prints it;
//  - Labels: Avery's US Letter and A4 products, a full page of the same
//    label or one label at a row and column; New Document or Print;
//  - for a mail merge (Start Mail Merge ▸ Envelopes / Labels) the current
//    document becomes the envelope with an «AddressBlock» or the label
//    sheet with «Next Record» before every label but the first.
// Changing the current document is one undo step (its page layout too).

#include "action_model.h"
#include "docx_model.h"

#include <QObject>
#include <QVariantMap>

#include <functional>
#include <memory>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class EnvelopesController final : public QObject {
    Q_OBJECT
public:
    EnvelopesController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                        QObject *parent);

    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // Hooks the host sets: open a Word package in a new window; print a
    // document laid out like this one (its pages, its decoration).
    std::function<void(const QByteArray &)> openDocument;
    std::function<void(std::shared_ptr<QTextDocument>)> printDocument;
    // Writes a merge's delivery address (an «AddressBlock») at the cursor.
    std::function<void(QTextCursor &)> mergeAddress;

    // {sizes: [{id, name, width, height}], delivery (the selection),
    //  returnAddress, omitReturn, size}
    Q_INVOKABLE QVariantMap envelopeInfo() const;
    // {mode: "add" | "new" | "print" | "merge", delivery, returnAddress,
    //  omitReturn, size (an id or "custom"), width, height (px),
    //  deliveryLeft, deliveryTop (px, −1 Auto)}
    Q_INVOKABLE bool applyEnvelope(const QVariantMap &answer);
    // {vendors: [names], products: [{id, vendor, product, description,
    //  details}], address, product (the last used id)}
    Q_INVOKABLE QVariantMap labelInfo() const;
    // {mode: "new" | "print" | "merge", product (id), address, fullPage,
    //  row, column (1-based)}
    Q_INVOKABLE bool applyLabels(const QVariantMap &answer);

signals:
    // `merge`: Start Mail Merge's Envelope / Label Options.
    void envelopeDialogRequested(bool merge);
    void labelsDialogRequested(bool merge);
    void message(const QString &text);

private:
    // A new document on `layout` filled by `fill`: opened in a window as
    // a .docx (`new`) or printed (`print`).
    bool deliver(const QString &mode, const Docx::PageLayout &layout, const std::function<void(QTextCursor &)> &fill);
    // The current document replaced by `fill` on `layout`: one undo step.
    void replaceContent(const Docx::PageLayout &layout, const std::function<void(QTextCursor &)> &fill);

    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_envelopes = nullptr;
    Quick::ActionEntry *m_labels = nullptr;
};

} // namespace QindaOffice::Apps::Write
