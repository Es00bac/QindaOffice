// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaWrite document (plan section 6.1): a QTextDocument owned behind
// the suite's Core::Document interface. Paged defaults follow probe #4
// (setUseDesignMetrics + symmetric documentMargin + setPageSize). M4 scope:
// HTML load/save (full matrix in M5); page setup is stored in an HTML
// <meta name="qindawrite-page"> element (documented limitation: not
// round-tripped through ODT/DOCX in v1).

#include "document.h"
#include "page_setup_io.h"

#include <QSizeF>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

class WriteDocument final : public Core::Document {
    Q_OBJECT
public:
    WriteDocument();

    [[nodiscard]] QTextDocument *textDocument() { return m_text.get(); }
    [[nodiscard]] const QTextDocument *textDocument() const { return m_text.get(); }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // Paged setup (probe #4 facts).
    void setupPaged(const QSizeF &pageSize, qreal margin);
    [[nodiscard]] QSizeF pageSize() const;
    [[nodiscard]] qreal pageMargin() const;

    // Header/footer drawn into the page margins on screen, print and PDF
    // (M14). {page}/{pages} tokens expand per page at render time.
    void setHeaderFooter(const QString &header, const QString &footer);
    [[nodiscard]] QString headerText() const { return m_header; }
    [[nodiscard]] QString footerText() const { return m_footer; }
    // Footnotes (write_footnotes.h): the note TEXTS, keyed by the id their
    // marker carries in the body. Numbering is never stored — it is the
    // marker's position in document order.
    [[nodiscard]] const QHash<QString, QString> &footnotes() const { return m_footnotes; }
    void setFootnotes(QHash<QString, QString> notes);
    void setFootnoteText(const QString &id, const QString &text);
    [[nodiscard]] QString footnoteText(const QString &id) const
    {
        return m_footnotes.value(id);
    }

    // Header, footer AND the per-page footnote lines. The screen view, the
    // print path and the PDF exporter all render from this one struct.
    [[nodiscard]] PageDecoration decoration() const;

signals:
    // Emitted by setupPaged (ctor, load, Page Setup dialog) so the view can
    // re-derive its page geometry — without this, a document loaded with a
    // non-default page size kept rendering (and clipping at) the old size.
    void pageSetupChanged();

private:
    std::unique_ptr<QTextDocument> m_text;
    qreal m_margin = 96;
    QString m_header;
    QString m_footer;
    QHash<QString, QString> m_footnotes;
};

} // namespace QindaOffice::Apps::Write
