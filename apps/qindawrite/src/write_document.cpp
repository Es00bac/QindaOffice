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
    if (!loadDocument(path, m_text.get(), error)) {
        return false;
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
        // Page setup meta (plan 6.1): re-inject after the generic writer.
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString html = QString::fromUtf8(file.readAll());
            file.close();
            const QString meta =
                QStringLiteral("<meta name=\"qindawrite-page\" content=\"%1 %2 %3\">")
                    .arg(m_text->pageSize().width())
                    .arg(m_text->pageSize().height())
                    .arg(m_margin);
            const int head = html.indexOf(QStringLiteral("</head>"));
            if (head > 0) {
                html.insert(head, meta);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    file.write(html.toUtf8());
                }
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
