// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaWrite window: shared document frame hosting the PageTextView,
// with the Format (styles, character formats), Insert (page break) and
// Page Setup app menus (plan section 6.1 v1 scope; full format/insert
// richness lands with M5).

#include "document_window.h"

#include <QTextCharFormat>

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;

class WriteWindow final : public Shell::DocumentWindow {
    Q_OBJECT
public:
    WriteWindow();
    ~WriteWindow() override;

    [[nodiscard]] WriteDocument *writeDocument() const { return m_document; }
    [[nodiscard]] PageTextView *pageView() const { return m_view; }

    void exportPdf();

protected:
    void insertApplicationMenus(QMenu *before) override;
    QString filterString() const override;

private:
    void applyStyle(int styleId);
    void applyCharFormat(const QTextCharFormat &format);

    WriteDocument *m_document;
    PageTextView *m_view;
};

} // namespace QindaOffice::Apps::Write
