// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Printing without QPrintDialog/QPrintPreviewDialog (plan §3.7): the app
// supplies the same paginating render callback the PDF exporter uses, the
// QML PrintDialog fills a PrintJob (printer, copies, range, orientation),
// and PrintPreview shows the pages a PagePreviewItem rasterises.
//
// AGENT-CONTRACT: pagination is identical across screen, PDF and paper
// because there is ONE callback; the preview renders the very PDF the job
// would print, rasterised by Poppler when the build found it.

#include <QAbstractListModel>
#include <QImage>
#include <QPageLayout>
#include <QObject>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

class QPainter;
class QPrinter;

namespace QindaOffice::Quick {

using PrintRenderCallback = std::function<void(QPrinter *printer, QPainter *painter)>;
// GUI-thread callback, owned by the job; captured document data must outlive it.
// A 1-based original document page maps to its paper; invalid means no such page.
using PrintPageLayoutCallback = std::function<QPageLayout(int page)>;

class PrinterModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int defaultIndex READ defaultIndex NOTIFY countChanged)
public:
    enum Roles { NameRole = Qt::UserRole + 1, DefaultRole, DescriptionRole };
    explicit PrinterModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int defaultIndex() const;
    Q_INVOKABLE void refresh();
    [[nodiscard]] Q_INVOKABLE QString nameAt(int row) const;

signals:
    void countChanged();

private:
    QStringList m_names;
    QStringList m_descriptions;
    int m_defaultIndex = -1;
};

class PrintJob : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A PrintJob is created by the application with its render callback.")
    Q_PROPERTY(QString printerName READ printerName WRITE setPrinterName NOTIFY settingsChanged)
    Q_PROPERTY(int copies READ copies WRITE setCopies NOTIFY settingsChanged)
    Q_PROPERTY(int fromPage READ fromPage WRITE setFromPage NOTIFY settingsChanged)
    Q_PROPERTY(int toPage READ toPage WRITE setToPage NOTIFY settingsChanged)
    Q_PROPERTY(bool landscape READ landscape WRITE setLandscape NOTIFY settingsChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pagesChanged)
    Q_PROPERTY(bool previewAvailable READ previewAvailable CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit PrintJob(QObject *parent = nullptr);
    ~PrintJob() override;

    void setRender(PrintRenderCallback render);
    // M78 (D-087): the paper the document asks for (a sheet's Page Setup:
    // size, orientation). Applied to the printer BEFORE the painter starts —
    // a layout changed during painting only reaches the next page — with
    // the print dialog's orientation over it. The render draws its own
    // margins; the layout's are taken as given.
    void setPageLayout(const QPageLayout &layout);
    // Variable-paper documents resolve the first selected page BEFORE painting.
    // An unchanged orientation follows that page; explicit changes override it.
    void setPageLayoutCallback(PrintPageLayoutCallback callback);

    [[nodiscard]] QString printerName() const { return m_printerName; }
    void setPrinterName(const QString &name);
    [[nodiscard]] int copies() const { return m_copies; }
    void setCopies(int copies);
    [[nodiscard]] int fromPage() const { return m_fromPage; }
    void setFromPage(int page);
    [[nodiscard]] int toPage() const { return m_toPage; }
    void setToPage(int page);
    [[nodiscard]] bool landscape() const { return m_landscape; }
    void setLandscape(bool landscape);
    [[nodiscard]] int pageCount() const { return m_pageCount; }
    [[nodiscard]] static bool previewAvailable();
    [[nodiscard]] QString lastError() const { return m_lastError; }

    // Renders the job to a temporary PDF and (with Poppler) rasterises the
    // pages; pageCount and page() are valid afterwards.
    Q_INVOKABLE bool preparePreview();
    [[nodiscard]] QImage page(int index, qreal dpi) const;
    [[nodiscard]] QSizeF pageSizePoints(int index) const;
    Q_INVOKABLE bool print();
    Q_INVOKABLE bool exportPdf(const QString &path);

signals:
    void settingsChanged();
    void pagesChanged();
    void lastErrorChanged();

private:
    void configure(QPrinter *printer) const;
    void setLastError(const QString &error);
    bool validateSettings();

    PrintRenderCallback m_render;
    QString m_printerName;
    QString m_previewPdf;
    QString m_lastError;
    int m_copies = 1;
    int m_fromPage = 0;
    int m_toPage = 0;
    bool m_landscape = false;
    bool m_orientationOverridden = false;
    int m_pageCount = 0;
    std::optional<QPageLayout> m_pageLayout;
    PrintPageLayoutCallback m_pageLayoutCallback;
    // The page layout (when the document gave one) with the job's orientation.
    void applyPageLayout(QPrinter *printer) const;
    class Private;
    Private *d = nullptr;
};

// Paints one page of a PrintJob's preview, scaled to its own size.
class PagePreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Quick::PrintJob *job READ job WRITE setJob NOTIFY jobChanged)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
public:
    explicit PagePreviewItem(QQuickItem *parent = nullptr);

    [[nodiscard]] PrintJob *job() const { return m_job; }
    void setJob(PrintJob *job);
    [[nodiscard]] int pageIndex() const { return m_pageIndex; }
    void setPageIndex(int index);
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    void paint(QPainter *painter) override;

signals:
    void jobChanged();
    void pageIndexChanged();
    void zoomChanged();

private:
    void updateSize();

    PrintJob *m_job = nullptr;
    int m_pageIndex = 0;
    qreal m_zoom = 1.0;
};

} // namespace QindaOffice::Quick
