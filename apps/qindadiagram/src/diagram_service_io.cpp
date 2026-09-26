// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService: clipboard (between windows and to other apps), find and
// replace, auto-layout, draw.io / org chart / picture import, PNG and SVG
// export.
#include "diagram_service.h"

#include "drawing_document.h"
#include "drawing_io.h"
#include "export.h"
#include "importers.h"
#include "layout.h"
#include "ops.h"

#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeData>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

bool readFile(const QString &path, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    *bytes = file.readAll();
    return true;
}

} // namespace

// ---- clipboard ----------------------------------------------------------------

bool DiagramService::copy()
{
    const Page *page = currentPage();
    if (page == nullptr || m_selection.isEmpty()) {
        return false;
    }
    const QSet<QString> ids(m_selection.begin(), m_selection.end());
    auto *mime = new QMimeData;
    // The native fragment for QindaDiagram windows, and a picture for
    // everything else (documents, chats, image editors).
    mime->setData(QString::fromLatin1(kFragmentMime), fragmentOf(*page, m_selection));
    mime->setImageData(renderImage(*page, ids, 2.0));
    mime->setData(QStringLiteral("image/svg+xml"), svgBytes(*page, ids));
    QStringList texts;
    for (const QString &id : m_selection) {
        const QString text = itemMap(id).value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) {
            texts.append(text);
        }
    }
    mime->setText(texts.join(QLatin1Char('\n')));
    QGuiApplication::clipboard()->setMimeData(mime);
    m_pasteCount = 0;
    return true;
}

bool DiagramService::cut()
{
    return copy() && deleteSelection();
}

QStringList DiagramService::paste()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr) {
        return {};
    }
    QStringList pasted;
    if (mime->hasFormat(QString::fromLatin1(kFragmentMime))) {
        const QByteArray fragment = mime->data(QString::fromLatin1(kFragmentMime));
        // Each paste of the same copy steps down-right so copies don't stack.
        const double step = 24.0 * ++m_pasteCount;
        QString error;
        editPage(tr("Paste"), [&](Drawing &d, Page &page) {
            pasted = Ops::paste(d, page, fragment, QPointF(step, step), &error);
            return !pasted.isEmpty();
        });
        if (pasted.isEmpty()) {
            refuse(tr("Paste"), error);
        }
    } else if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const QRectF area = currentPage() != nullptr ? QRectF(QPointF(0, 0), currentPage()->size) : QRectF();
        editPage(tr("Paste picture"), [&](Drawing &d, Page &page) {
            const QString id = Ops::addShape(d, page, QStringLiteral("picture"), area.center(), QString());
            Shape *shape = page.shape(id);
            const QSizeF size = QSizeF(image.size()).scaled(QSizeF(std::min(480.0, area.width() * 0.6),
                                                                   std::min(360.0, area.height() * 0.6)),
                                                            Qt::KeepAspectRatio);
            shape->rect = QRectF(area.center() - QPointF(size.width() / 2, size.height() / 2), size);
            shape->image = png;
            pasted = {id};
            return true;
        });
    }
    if (!pasted.isEmpty()) {
        setSelection(pasted);
    }
    return pasted;
}

bool DiagramService::copyAsImage()
{
    const Page *page = currentPage();
    if (page == nullptr) {
        return false;
    }
    const QSet<QString> ids(m_selection.begin(), m_selection.end());
    auto *mime = new QMimeData;
    mime->setImageData(renderImage(*page, ids, 2.0));
    mime->setData(QStringLiteral("image/svg+xml"), svgBytes(*page, ids));
    QGuiApplication::clipboard()->setMimeData(mime);
    emit notify(tr("Copied as picture"),
                ids.isEmpty() ? tr("The whole page is on the clipboard as a picture.")
                              : tr("The selection is on the clipboard as a picture."),
                QStringLiteral("success"));
    return true;
}

// ---- find and replace ------------------------------------------------------------

int DiagramService::find(const QString &text)
{
    const Page *page = currentPage();
    m_findMatches = page != nullptr ? Ops::find(*page, text) : QStringList();
    m_findIndex = m_findMatches.isEmpty() ? -1 : 0;
    if (m_findMatches.isEmpty()) {
        refuse(tr("Find"), tr("Nothing on this page contains \"%1\".").arg(text));
        return 0;
    }
    selectAndReveal({m_findMatches.first()});
    return int(m_findMatches.size());
}

int DiagramService::findNext(int step)
{
    if (m_findMatches.isEmpty()) {
        return -1;
    }
    m_findIndex = int((m_findIndex + step + m_findMatches.size()) % m_findMatches.size());
    selectAndReveal({m_findMatches.at(m_findIndex)});
    return m_findIndex;
}

int DiagramService::replace(const QString &find, const QString &replacement)
{
    int count = 0;
    editPage(tr("Replace"), [&](Drawing &, Page &page) {
        count = Ops::replaceText(page, find, replacement);
        return count > 0;
    });
    emit notify(tr("Replace"), tr("%n replacement(s) on this page.", nullptr, count),
                count > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    return count;
}

// ---- layout --------------------------------------------------------------------

bool DiagramService::autoLayout(const QString &kind)
{
    LayoutOptions options;
    options.kind = kind == QLatin1String("leftRight") ? LayoutKind::LeftRight
                 : kind == QLatin1String("tree")      ? LayoutKind::Tree
                                                      : LayoutKind::TopDown;
    if (options.kind == LayoutKind::Tree) {
        options.nodeGap = 28;
        options.rankGap = 48;
    }
    QStringList ids;
    for (const QString &id : m_selection) {
        if (currentPage() != nullptr && currentPage()->shapeIndex(id) >= 0) {
            ids.append(id);
        }
    }
    QString error;
    const bool ok = editPage(tr("Auto-layout"),
                             [&](Drawing &, Page &page) { return Diagram::autoLayout(page, ids, options, &error); });
    if (!ok) {
        refuse(tr("Auto-layout"), error.isEmpty() ? tr("The shapes are already arranged.") : error);
    }
    return ok;
}

// ---- import --------------------------------------------------------------------

bool DiagramService::importDrawio(const QUrl &file)
{
    const QString path = localPath(file);
    QByteArray bytes;
    QString error;
    if (!readFile(path, &bytes, &error)) {
        refuse(tr("Import draw.io"), tr("Could not read %1: %2").arg(path, error));
        return false;
    }
    QVector<Page> imported;
    QStringList warnings;
    QString firstPage;
    const bool ok = m_document->edit(tr("Import draw.io"), [&](Drawing &d) {
        if (!Diagram::importDrawio(d, bytes, &imported, &warnings, &error)) {
            return false;
        }
        // A fresh window's lone empty page gives way to the imported pages.
        if (d.pages.size() == 1 && d.pages.first().shapes.isEmpty() && d.pages.first().connectors.isEmpty()) {
            d.pages.clear();
        }
        for (const Page &page : std::as_const(imported)) {
            d.pages.append(page);
        }
        firstPage = imported.first().id;
        return true;
    });
    if (!ok) {
        refuse(tr("Import draw.io"), error);
        return false;
    }
    setCurrentPageId(firstPage);
    emit notify(tr("Imported from draw.io"),
                warnings.isEmpty() ? tr("%n page(s) imported.", nullptr, int(imported.size())) : warnings.join(QLatin1Char('\n')),
                warnings.isEmpty() ? QStringLiteral("success") : QStringLiteral("info"));
    return true;
}

bool DiagramService::importOrgChart(const QUrl &file)
{
    const QString path = localPath(file);
    QByteArray bytes;
    QString error;
    if (!readFile(path, &bytes, &error)) {
        refuse(tr("Org chart"), tr("Could not read %1: %2").arg(path, error));
        return false;
    }
    return importOrgChartText(QString::fromUtf8(bytes));
}

bool DiagramService::importOrgChartText(const QString &csv)
{
    OrgChartResult result;
    QString error;
    const bool ok = editPage(tr("Org chart from CSV"), [&](Drawing &d, Page &page) {
        return importOrgChartCsv(d, page, csv.toUtf8(), &result, &error);
    });
    if (!ok) {
        refuse(tr("Org chart"), error);
        return false;
    }
    selectAndReveal(result.shapeIds);
    emit notify(tr("Org chart created"),
                result.warnings.isEmpty() ? tr("%n people placed.", nullptr, int(result.shapeIds.size()))
                                          : result.warnings.join(QLatin1Char('\n')),
                result.warnings.isEmpty() ? QStringLiteral("success") : QStringLiteral("info"));
    return true;
}

bool DiagramService::insertPicture(const QUrl &file, double x, double y)
{
    const QString path = localPath(file);
    QByteArray bytes;
    QString error;
    if (!readFile(path, &bytes, &error)) {
        refuse(tr("Insert picture"), tr("Could not read %1: %2").arg(path, error));
        return false;
    }
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid()) {
        refuse(tr("Insert picture"), tr("%1 is not a picture this system can read.").arg(QFileInfo(path).fileName()));
        return false;
    }
    QString id;
    // The picture keeps the author's bytes (D-040's rule, applied here too).
    editPage(tr("Insert picture"), [&](Drawing &d, Page &page) {
        id = Ops::addShape(d, page, QStringLiteral("picture"), QPointF(x, y), QString());
        Shape *shape = page.shape(id);
        const QSizeF fitted = QSizeF(size).scaled(QSizeF(std::min(360.0, page.size.width() * 0.5),
                                                         std::min(270.0, page.size.height() * 0.5)),
                                                  Qt::KeepAspectRatio);
        shape->rect = QRectF(QPointF(x, y) - QPointF(fitted.width() / 2, fitted.height() / 2), fitted);
        shape->image = bytes;
        setDataValue(shape->data, tr("File"), QFileInfo(path).fileName());
        return true;
    });
    setSelection({id});
    return !id.isEmpty();
}

// ---- export --------------------------------------------------------------------

bool DiagramService::exportPng(const QUrl &file, bool selectionOnly)
{
    const Page *page = currentPage();
    QString error;
    const QSet<QString> ids = selectionOnly ? QSet<QString>(m_selection.begin(), m_selection.end()) : QSet<QString>();
    const QString path = localPath(file);
    if (page == nullptr || !Diagram::exportPng(path, *page, ids, 2.0, false, &error)) {
        refuse(tr("Export PNG"), error);
        return false;
    }
    emit notify(tr("Exported"), tr("Saved %1").arg(QFileInfo(path).fileName()), QStringLiteral("success"));
    return true;
}

bool DiagramService::exportSvg(const QUrl &file, bool selectionOnly)
{
    const Page *page = currentPage();
    QString error;
    const QSet<QString> ids = selectionOnly ? QSet<QString>(m_selection.begin(), m_selection.end()) : QSet<QString>();
    const QString path = localPath(file);
    if (page == nullptr || !Diagram::exportSvg(path, *page, ids, &error)) {
        refuse(tr("Export SVG"), error);
        return false;
    }
    emit notify(tr("Exported"), tr("Saved %1").arg(QFileInfo(path).fileName()), QStringLiteral("success"));
    return true;
}

} // namespace QindaOffice::Apps::Diagram
