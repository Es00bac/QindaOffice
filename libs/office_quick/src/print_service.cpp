// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_service.h"

#include <QDir>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QTemporaryFile>

#ifdef QQO_HAVE_POPPLER
#include <poppler-qt6.h>
#endif

namespace QindaOffice::Quick {

// ---- PrinterModel ---------------------------------------------------------

PrinterModel::PrinterModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();
}

int PrinterModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_names.size());
}

QVariant PrinterModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_names.size()) {
        return {};
    }
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return m_names.at(index.row());
    case DefaultRole:
        return index.row() == m_defaultIndex;
    case DescriptionRole:
        return m_descriptions.at(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> PrinterModel::roleNames() const
{
    return {{NameRole, "name"}, {DefaultRole, "isDefault"}, {DescriptionRole, "description"}};
}

int PrinterModel::defaultIndex() const
{
    return m_defaultIndex;
}

void PrinterModel::refresh()
{
    beginResetModel();
    m_names.clear();
    m_descriptions.clear();
    m_defaultIndex = -1;
    const QString defaultName = QPrinterInfo::defaultPrinterName();
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    for (const QPrinterInfo &info : printers) {
        if (info.printerName() == defaultName) {
            m_defaultIndex = int(m_names.size());
        }
        m_names.append(info.printerName());
        m_descriptions.append(info.description());
    }
    endResetModel();
    emit countChanged();
}

QString PrinterModel::nameAt(int row) const
{
    return m_names.value(row);
}

// ---- PrintJob -------------------------------------------------------------

class PrintJob::Private {
public:
#ifdef QQO_HAVE_POPPLER
    std::unique_ptr<Poppler::Document> document;
#endif
    std::unique_ptr<QTemporaryFile> pdf;
};

PrintJob::PrintJob(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

PrintJob::~PrintJob()
{
    delete d;
}

void PrintJob::setRender(PrintRenderCallback render)
{
    m_render = std::move(render);
}

void PrintJob::setPrinterName(const QString &name)
{
    if (m_printerName == name) {
        return;
    }
    m_printerName = name;
    emit settingsChanged();
}

void PrintJob::setCopies(int copies)
{
    copies = qBound(1, copies, 99);
    if (m_copies == copies) {
        return;
    }
    m_copies = copies;
    emit settingsChanged();
}

void PrintJob::setFromPage(int page)
{
    page = qMax(0, page);
    if (m_fromPage == page) {
        return;
    }
    m_fromPage = page;
    emit settingsChanged();
}

void PrintJob::setToPage(int page)
{
    page = qMax(0, page);
    if (m_toPage == page) {
        return;
    }
    m_toPage = page;
    emit settingsChanged();
}

void PrintJob::setLandscape(bool landscape)
{
    if (m_landscape == landscape) {
        return;
    }
    m_landscape = landscape;
    emit settingsChanged();
}

bool PrintJob::previewAvailable()
{
#ifdef QQO_HAVE_POPPLER
    return true;
#else
    return false;
#endif
}

void PrintJob::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

void PrintJob::setPageLayout(const QPageLayout &layout)
{
    m_pageLayout = layout;
}

void PrintJob::applyPageLayout(QPrinter *printer) const
{
    if (m_pageLayout.has_value()) {
        QPageLayout layout = *m_pageLayout;
        layout.setOrientation(m_landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
        printer->setPageLayout(layout);
        return;
    }
    printer->setPageOrientation(m_landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
}

void PrintJob::configure(QPrinter *printer) const
{
    applyPageLayout(printer);
    printer->setCopyCount(m_copies);
    if (m_fromPage > 0 && m_toPage >= m_fromPage) {
        printer->setFromTo(m_fromPage, m_toPage);
    }
}

bool PrintJob::preparePreview()
{
    if (!m_render) {
        setLastError(tr("Nothing to print"));
        return false;
    }
    d->pdf = std::make_unique<QTemporaryFile>(QDir::tempPath()
                                              + QStringLiteral("/qindaoffice-preview-XXXXXX.pdf"));
    if (!d->pdf->open()) {
        setLastError(tr("Could not create the preview file"));
        return false;
    }
    d->pdf->close();
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(d->pdf->fileName());
    applyPageLayout(&printer);
    {
        QPainter painter(&printer);
        if (!painter.isActive()) {
            setLastError(tr("Could not render the preview"));
            return false;
        }
        m_render(&printer, &painter);
    }
    m_previewPdf = d->pdf->fileName();
#ifdef QQO_HAVE_POPPLER
    d->document = Poppler::Document::load(m_previewPdf);
    if (d->document == nullptr) {
        setLastError(tr("Could not read the rendered preview"));
        m_pageCount = 0;
        emit pagesChanged();
        return false;
    }
    d->document->setRenderHint(Poppler::Document::Antialiasing, true);
    d->document->setRenderHint(Poppler::Document::TextAntialiasing, true);
    m_pageCount = d->document->numPages();
#else
    m_pageCount = 0;
#endif
    setLastError({});
    emit pagesChanged();
    return true;
}

QImage PrintJob::page(int index, qreal dpi) const
{
#ifdef QQO_HAVE_POPPLER
    if (d->document == nullptr || index < 0 || index >= m_pageCount) {
        return {};
    }
    std::unique_ptr<Poppler::Page> page(d->document->page(index));
    if (page == nullptr) {
        return {};
    }
    return page->renderToImage(dpi, dpi);
#else
    Q_UNUSED(index);
    Q_UNUSED(dpi);
    return {};
#endif
}

QSizeF PrintJob::pageSizePoints(int index) const
{
#ifdef QQO_HAVE_POPPLER
    if (d->document == nullptr || index < 0 || index >= m_pageCount) {
        return {};
    }
    std::unique_ptr<Poppler::Page> page(d->document->page(index));
    return page != nullptr ? page->pageSizeF() : QSizeF();
#else
    Q_UNUSED(index);
    return {};
#endif
}

bool PrintJob::print()
{
    if (!m_render) {
        setLastError(tr("Nothing to print"));
        return false;
    }
    QPrinter printer(QPrinter::HighResolution);
    if (!m_printerName.isEmpty()) {
        printer.setPrinterName(m_printerName);
    }
    configure(&printer);
    QPainter painter(&printer);
    if (!painter.isActive()) {
        setLastError(tr("Could not start the print job on %1").arg(printer.printerName()));
        return false;
    }
    m_render(&printer, &painter);
    setLastError({});
    return true;
}

bool PrintJob::exportPdf(const QString &path)
{
    if (!m_render) {
        setLastError(tr("Nothing to export"));
        return false;
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    applyPageLayout(&printer);
    QPainter painter(&printer);
    // AGENT-GUARD: report honestly — an unwritable target must not return
    // true while the render goes to an inactive painter.
    if (!painter.isActive()) {
        setLastError(tr("Could not write %1").arg(path));
        return false;
    }
    m_render(&printer, &painter);
    setLastError({});
    return true;
}

// ---- PagePreviewItem ------------------------------------------------------

PagePreviewItem::PagePreviewItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void PagePreviewItem::setJob(PrintJob *job)
{
    if (m_job == job) {
        return;
    }
    if (m_job != nullptr) {
        disconnect(m_job, nullptr, this, nullptr);
    }
    m_job = job;
    if (m_job != nullptr) {
        connect(m_job, &PrintJob::pagesChanged, this, [this] {
            updateSize();
            update();
        });
    }
    updateSize();
    update();
    emit jobChanged();
}

void PagePreviewItem::setPageIndex(int index)
{
    if (m_pageIndex == index) {
        return;
    }
    m_pageIndex = index;
    updateSize();
    update();
    emit pageIndexChanged();
}

void PagePreviewItem::setZoom(qreal zoom)
{
    zoom = qBound(0.1, zoom, 8.0);
    if (qFuzzyCompare(m_zoom, zoom)) {
        return;
    }
    m_zoom = zoom;
    updateSize();
    update();
    emit zoomChanged();
}

void PagePreviewItem::updateSize()
{
    // Points at 96 dpi times zoom: a Letter page previews at 816×1056 px.
    const QSizeF points = m_job != nullptr ? m_job->pageSizePoints(m_pageIndex) : QSizeF();
    if (points.isEmpty()) {
        setImplicitSize(0, 0);
        return;
    }
    setImplicitSize(points.width() / 72.0 * 96.0 * m_zoom, points.height() / 72.0 * 96.0 * m_zoom);
}

void PagePreviewItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), Qt::white);
    if (m_job == nullptr || width() <= 0) {
        return;
    }
    const QSizeF points = m_job->pageSizePoints(m_pageIndex);
    if (points.isEmpty()) {
        return;
    }
    const qreal dpi = width() / (points.width() / 72.0);
    const QImage image = m_job->page(m_pageIndex, dpi);
    if (!image.isNull()) {
        painter->drawImage(boundingRect(), image);
    }
}

} // namespace QindaOffice::Quick
