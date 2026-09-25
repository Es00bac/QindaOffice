// SPDX-License-Identifier: GPL-3.0-or-later
// BooksHost: the company document, the workspace, and the print source.
#include "books_host.h"

#include "books_document.h"
#include "company_file.h"
#include "form_renderer.h"
#include "page_output.h"
#include "printable_document.h"
#include "report_output.h"
#include "standard_actions.h"

#include <QPrinter>
#include <QTimer>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
namespace ActionId = Quick::ActionId;

BooksHost::BooksHost()
    : m_document(new BooksDocument)
{
    setDocument(m_document);
    m_service = new BooksService(m_document, this);
    createActions();
    insertMenus();
    connect(m_service, &BooksService::companyChanged, this, [this] {
        // A different company: whatever was selected or printable belonged
        // to the old one.
        setSelectedDocumentId(0);
        clearPrintSource();
        emit titleChanged(); // the path (and so the title) moved
        syncEnablement();
    });
    finalizeHost();
    syncEnablement();
    // A window still without a company once start-up settles (File ▸ New
    // Company, a bare launch) asks for the setup wizard; an Open or a CLI
    // path loads synchronously before this runs.
    QTimer::singleShot(0, this, [this] {
        if (!m_service->hasCompany()) {
            emit companySetupRequested();
        }
    });
}

BooksHost::~BooksHost() = default;

QString BooksHost::filterString() const
{
    return tr("QindaBooks company (*.qbooks);;All files (*)");
}

void BooksHost::documentReplaced()
{
    syncEnablement();
}

QStringList BooksHost::viewKeys()
{
    return {QStringLiteral("dashboard"), QStringLiteral("sales"),      QStringLiteral("expenses"),
            QStringLiteral("banking"),   QStringLiteral("accounting"), QStringLiteral("reports")};
}

void BooksHost::setCurrentView(const QString &view)
{
    if (!viewKeys().contains(view)) {
        return;
    }
    if (m_view != view) {
        m_view = view;
        emit currentViewChanged();
    }
    // Keep the radio group in step when QML (a sidebar click) sets the view.
    QString id = view;
    id[0] = id[0].toUpper();
    if (Quick::ActionEntry *entry = action(QStringLiteral("booksView") + id + QStringLiteral("Action"))) {
        entry->setChecked(true);
    }
}

void BooksHost::setSelectedDocumentId(qint64 id)
{
    if (m_selectedDocumentId == id) {
        return;
    }
    m_selectedDocumentId = id;
    emit selectedDocumentIdChanged();
    syncEnablement();
}

void BooksHost::setPrintSource(const QString &kind, const QVariant &id, const QVariantMap &options)
{
    m_printKind.clear();
    m_printReportId.clear();
    m_printDocumentId = 0;
    m_printOptions = options;
    m_printLandscape = false;
    if (kind == QLatin1String("report") && m_service->hasCompany()) {
        m_printKind = kind;
        m_printReportId = id.toString();
        if (const std::optional<ReportTable> table = m_service->buildReport(m_printReportId, options)) {
            CompanyFile &file = *m_service->file();
            const ReportStyle style = ReportPdf::styleFor(file);
            m_printLandscape = ReportPdf::prefersLandscape(*table, defaultPageSize(style.locale), style);
        }
    } else if (kind == QLatin1String("document") && m_service->hasCompany()) {
        m_printKind = kind;
        m_printDocumentId = Format::id(id);
    }
    emit printSourceChanged();
    syncEnablement();
}

Quick::PrintRenderCallback BooksHost::printRenderCallback() const
{
    CompanyFile *file = m_service != nullptr ? m_service->file() : nullptr;
    if (file == nullptr || m_printKind.isEmpty()) {
        return {};
    }
    // Built when the job is created (Print / Preview / Export PDF), so it
    // shows the books as they are now; the painter is the same one the
    // service's PDF export uses (D-046: paper and PDF cannot drift).
    PagePainter painter;
    if (m_printKind == QLatin1String("report")) {
        const std::optional<ReportTable> table = m_service->buildReport(m_printReportId, m_printOptions);
        if (!table) {
            return {};
        }
        painter = ReportPdf::painter(*table, ReportPdf::styleFor(*file));
    } else {
        PrintableDocument printable;
        if (!buildPrintableDocument(*file, m_printDocumentId, &printable)) {
            return {};
        }
        painter = FormRenderer::painter(printable);
    }
    return asPrintCallback<QPrinter>(std::move(painter));
}

} // namespace QindaOffice::Apps::Books
