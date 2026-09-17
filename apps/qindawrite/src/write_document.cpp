// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"

#include "format_io.h"
#include "page_geometry.h"
#include "write_footnotes.h"

#include <QFile>
#include <QFileInfo>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

WriteDocument::WriteDocument()
    : m_text(std::make_unique<QTextDocument>())
{
    setupPaged(QSizeF(PageGeometry::kDefaultPageW, PageGeometry::kDefaultPageH), m_margin);
    // AGENT-NOTE (Qt 6.11 quirk): QTextDocument::setPageSize() records an
    // undo item, so every fresh document was born with one phantom undo
    // step - Undo looked enabled on a pristine document and triggering it
    // reset the page size. Page geometry is setup, not user content.
    m_text->clearUndoRedoStacks();
    m_text->setModified(false);
    // AGENT-GUARD: dirty follows QTextDocument's OWN modified flag, not
    // contentsChange. contentsChange also fires for FORMAT-only edits, and
    // the spell checker is a QSyntaxHighlighter whose first pass runs while
    // the window is still being built — so every freshly created QindaWrite
    // window was born dirty. A dirty untitled window is not pristine, so
    // File ▸ Open never reused it and always spawned a second window,
    // leaving the blank one behind (ADR-0110 pristine reuse).
    connect(m_text.get(), &QTextDocument::modificationChanged, this,
            [this](bool modified) { setDirty(modified); });
    // The recovery journal still wants every real content edit; a
    // format-only pass reports zero characters added and removed.
    connect(m_text.get(), &QTextDocument::contentsChange, this,
            [this](int, int charsRemoved, int charsAdded) {
                if (charsRemoved != 0 || charsAdded != 0) {
                    emit contentChanged();
                }
            });
}

void WriteDocument::setupPaged(const QSizeF &pageSize, qreal margin)
{
    m_margin = margin;
    m_text->setUseDesignMetrics(true);
    m_text->setDocumentMargin(margin);
    m_text->setPageSize(pageSize);
    emit pageSetupChanged();
}

PageDecoration WriteDocument::decoration() const
{
    PageDecoration out;
    out.header = m_header;
    out.footer = m_footer;
    out.margin = m_margin;
    if (!m_footnotes.isEmpty()) {
        // The page lookup uses the document's OWN page size, which is the
        // same size PageGeometry paints with — zoom does not affect which
        // page a position falls on.
        PageGeometry geometry;
        geometry.setPageSize(m_text->pageSize());
        out.footnotes = footnotesByPage(m_text.get(), geometry, m_footnotes);
    }
    return out;
}

void WriteDocument::setFootnotes(QHash<QString, QString> notes)
{
    m_footnotes = std::move(notes);
}

void WriteDocument::setFootnoteText(const QString &id, const QString &text)
{
    if (id.isEmpty()) {
        return;
    }
    if (text.isEmpty()) {
        m_footnotes.remove(id);
    } else {
        m_footnotes.insert(id, text);
    }
    m_text->setModified(true);
    emit contentChanged();
}

QSizeF WriteDocument::pageSize() const
{
    return m_text->pageSize();
}

qreal WriteDocument::pageMargin() const
{
    return m_margin;
}

bool WriteDocument::load(const QString &path, QString *error)
{
    if (!loadDocument(path, m_text.get(), error)) {
        return false;
    }
    // Page setup + header/footer travel in HTML metas (see page_setup_io);
    // QTextDocument::setHtml drops them, so parse the raw source. ODT/DOCX
    // via the LibreOffice bridge lose them - documented limitation.
    if (path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive)) {
        QFile raw(path);
        if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const PageSetup setup = pageSetupFromHtml(QString::fromUtf8(raw.readAll()));
            if (setup.hasCustomPageSize) {
                setupPaged(setup.pageSize, setup.margin);
            }
            m_header = setup.header;
            m_footer = setup.footer;
            m_footnotes = setup.footnotes;
        }
    }
    setCanonicalPath(path);
    refreshByteRevision();
    // Both flags: ours drives the title and close consent, QTextDocument's
    // drives the signal that sets ours.
    m_text->setModified(false);
    setDirty(false);
    emit contentChanged();
    return true;
}

bool WriteDocument::save(const QString &path, QString *error)
{
    if (!saveDocument(path, m_text.get(), error)) {
        return false;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
        // Page setup + header/footer metas: re-inject after the generic
        // writer (QTextDocumentWriter does not know about them).
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString html = QString::fromUtf8(file.readAll());
            file.close();
            PageSetup setup;
            setup.pageSize = m_text->pageSize();
            setup.margin = m_margin;
            setup.header = m_header;
            setup.footer = m_footer;
            setup.footnotes = m_footnotes;
            html = injectPageSetupMeta(html, setup);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(html.toUtf8());
            }
        }
    }
    setCanonicalPath(path);
    refreshByteRevision();
    m_text->setModified(false);
    setDirty(false);
    return true;
}

QStringList WriteDocument::mimeTypes() const
{
    return {QStringLiteral("text/html"), QStringLiteral("text/markdown"),
            QStringLiteral("text/plain"),
            QStringLiteral("application/vnd.oasis.opendocument.text"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"),
            QStringLiteral("application/rtf")};
}

void WriteDocument::setHeaderFooter(const QString &header, const QString &footer)
{
    if (m_header == header && m_footer == footer) {
        return;
    }
    m_header = header;
    m_footer = footer;
    m_text->setModified(true);
    setDirty(true);
    emit contentChanged();
}

QByteArray WriteDocument::recoverySnapshot() const
{
    return m_text->toHtml().toUtf8();
}

bool WriteDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Q_UNUSED(error);
    m_text->setHtml(QString::fromUtf8(snapshot));
    m_text->setModified(true);
    setDirty(true);
    emit contentChanged();
    return true;
}

} // namespace QindaOffice::Apps::Write
