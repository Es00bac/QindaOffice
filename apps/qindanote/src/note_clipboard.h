// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The clipboard the formatted-text editor pastes from (M84, D-093): the
// system clipboard seen through RichImport::sanitizeHtml(), so a paste from
// the web, Word or QindaWrite keeps its formatting and nothing else. A
// picture alone on the clipboard (a screenshot) pastes as an inline
// picture. QindaTK's own fragments (copy within the suite) pass unchanged.
//
// AGENT-CONTRACT: set on the editor's Tk.RichTextArea through
// RichTextArea::setClipboardAdapter (NotePageItem::attachRichArea); the
// area reads mimeData() on every paste. GUI thread.

#include <qindatk/clipboard_service.h>

#include <QMimeData>

#include <memory>

namespace QindaOffice::Apps::Notes {

class NoteClipboard final : public QindaTK::ClipboardAdapter {
    Q_OBJECT
public:
    explicit NoteClipboard(QObject *parent = nullptr);
    [[nodiscard]] const QMimeData *mimeData() const override;
    bool setMimeData(QMimeData *data) override;

private:
    mutable std::unique_ptr<QMimeData> m_view;
};

} // namespace QindaOffice::Apps::Notes
