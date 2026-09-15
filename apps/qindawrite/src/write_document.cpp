// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_document.h"

#include "page_geometry.h"

#include <QFile>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

WriteDocument::WriteDocument()
    : m_text(std::make_unique<QTextDocument>())
{
    setupPaged(QSizeF(PageGeometry::kDefaultPageW, PageGeometry::kDefaultPageH), m_margin);
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
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QString html = QString::fromUtf8(file.readAll());
    m_text->setHtml(html);
    // Page setup meta (plan section 6.1): "W H margin" in 96-dpi px.
    const QTextCursor cursor(m_text.get());
    Q_UNUSED(cursor);
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    return true;
}

bool WriteDocument::save(const QString &path, QString *error)
{
    QString html = m_text->toHtml();
    const QString meta = QStringLiteral(
        "<meta name=\"qindawrite-page\" content=\"%1 %2 %3\">")
                             .arg(m_text->pageSize().width())
                             .arg(m_text->pageSize().height())
                             .arg(m_margin);
    const int head = html.indexOf(QStringLiteral("</head>"));
    if (head > 0) {
        html.insert(head, meta);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(html.toUtf8());
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList WriteDocument::mimeTypes() const
{
    // M5 adds the full matrix (md/txt/odt native; docx/rtf via LibreOffice).
    return {QStringLiteral("text/html")};
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
