// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaWrite document (plan section 6.1): a QTextDocument owned behind
// the suite's Core::Document interface, plus everything a Word document
// carries besides its text (Docx::WordParts, D-068): the page layout (paper,
// orientation, four margins, header/footer distances), rich headers and
// footers per slot (default / first page / even pages) with PAGE and
// NUMPAGES fields, footnote texts, the original bytes of every picture, and
// the package a .docx was opened from so a save keeps what Write does not
// model.
//
// Formats (write_document_io.cpp): .docx/.docm/.dotx/.dotm natively;
// .doc/.rtf/.odt through LibreOffice to and from .docx (never HTML);
// .html/.md/.txt as before. Crash recovery snapshots are .docx bytes.

#include "docx_model.h"
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

    // ---- page layout -----------------------------------------------------------------
    [[nodiscard]] const Docx::PageLayout &pageLayout() const { return m_parts.layout; }
    void setPageLayout(const Docx::PageLayout &layout);
    // Uniform-margin shorthand (M14 callers, tests).
    void setupPaged(const QSizeF &pageSize, qreal margin);
    [[nodiscard]] QSizeF pageSize() const;
    // The text column's left edge in document px (left margin + gutter):
    // what the ruler is numbered from.
    [[nodiscard]] qreal pageMargin() const;
    [[nodiscard]] qreal contentWidth() const { return m_parts.layout.contentWidth(); }

    // ---- headers and footers -----------------------------------------------------------
    // Plain patterns for the default slot; {page}/{pages} become fields.
    // An unchanged pattern leaves a rich (imported) header untouched.
    void setHeaderFooter(const QString &header, const QString &footer);
    [[nodiscard]] QString headerText() const;
    [[nodiscard]] QString footerText() const;
    // False when the default header/footer is richer than one plain line
    // (pictures, tables, formatting): the dialog then warns before
    // replacing it.
    [[nodiscard]] bool headerIsSimple() const;
    [[nodiscard]] bool footerIsSimple() const;
    [[nodiscard]] bool differentFirstPage() const { return m_parts.differentFirstPage; }
    void setDifferentFirstPage(bool different);

    // ---- footnotes (write_footnotes.h): texts keyed by the marker's id ----------------
    [[nodiscard]] const QHash<QString, QString> &footnotes() const { return m_parts.footnotes; }
    void setFootnotes(QHash<QString, QString> notes);
    void setFootnoteText(const QString &id, const QString &text);
    [[nodiscard]] QString footnoteText(const QString &id) const { return m_parts.footnotes.value(id); }

    // ---- pictures ----------------------------------------------------------------------
    // Stores the bytes with the document and returns the resource name the
    // QTextImageFormat must carry; empty when the bytes are not an image.
    QString addPicture(const QByteArray &bytes);

    // Header, footer AND the per-page footnote lines. The screen view, the
    // print path and the PDF exporter all render from this one struct.
    [[nodiscard]] PageDecoration decoration() const;
    [[nodiscard]] const Docx::WordParts &parts() const { return m_parts; }
    // What the last open approximated ("2 text boxes …"), for the status bar.
    [[nodiscard]] QStringList openWarnings() const { return m_parts.warnings; }

signals:
    // Emitted whenever the layout changes (ctor, load, Page Setup) so the
    // view can re-derive its page geometry — without this, a document
    // loaded with a non-default page size kept rendering (and clipping at)
    // the old size.
    void pageSetupChanged();

private:
    void applyLayout();
    // Replaces the parts after a load; the text was filled already.
    void adoptParts(Docx::WordParts parts);
    void finishLoad(const QString &path);
    bool loadWordPackage(const QString &path, QString *error);
    bool loadConverted(const QString &path, QString *error);
    bool loadMarkup(const QString &path, QString *error);
    bool saveWordPackage(const QString &path, QString *error) const;
    bool saveConverted(const QString &path, QString *error) const;
    bool saveHtml(const QString &path, QString *error) const;

    std::unique_ptr<QTextDocument> m_text;
    Docx::WordParts m_parts;
};

} // namespace QindaOffice::Apps::Write
