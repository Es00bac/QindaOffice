// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"

#include "format_io.h"
#include "page_geometry.h"

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
    connect(m_text.get(), &QTextDocument::contentsChange, this, [this] {
        setDirty(true);
        emit contentChanged();
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
        }
    }
    setCanonicalPath(path);
    refreshByteRevision();
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
            html = injectPageSetupMeta(html, setup);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(html.toUtf8());
            }
        }
    }
    setCanonicalPath(path);
    refreshByteRevision();
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
    setDirty(true);
    emit contentChanged();
    return true;
}

} // namespace QindaOffice::Apps::Write
