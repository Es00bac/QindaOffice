// SPDX-License-Identifier: GPL-3.0-or-later
// BaseHost: the document, the view state, the clipboard and the print
// source. Commands and menus are in base_host_actions.cpp.
#include "base_host.h"

#include "base_document.h"
#include "label_sheet.h"

#include <qindatk/print_output.h>
#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_layout.h>
#include <qindatk/rich_text_objects.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QPrinter>
#include <QUndoStack>

namespace QindaOffice::Apps::Base {

BaseHost::BaseHost()
    : m_document(new BaseDocument)
{
    setDocument(m_document);
    m_service = new BaseService(m_document, this);
    createActions();
    insertMenus();
    m_document->setDesignFlush([this](QString *error) {
        emit designSaveRequested(); // the QML saves synchronously (direct connection)
        if (m_designDirty && error != nullptr) {
            *error = tr("The design could not be saved.");
        }
        return !m_designDirty;
    });
    connect(m_service, &BaseService::fileChanged, this, [this] {
        setReportOutput(nullptr);
        emit titleChanged();
        syncEnablement();
    });
    connect(m_service, &BaseService::objectsChanged, this, &BaseHost::syncEnablement);
    connect(m_service->datasheet(), &DatasheetModel::rowsChanged, this, &BaseHost::updateStatus);
    connect(m_service->datasheet(), &DatasheetModel::sourceChanged, this, &BaseHost::syncEnablement);
    connect(m_document->undoStack(), &QUndoStack::indexChanged, this, &BaseHost::syncEnablement);
    connect(m_document, &BaseDocument::undoFailed, this, [this](const QString &message) {
        emit messageRequested(tr("Undo stopped"),
                              tr("%1 The undo history was cleared so nothing is replayed twice.").arg(message),
                              QStringLiteral("warning"));
    });
    finalizeHost();
    syncEnablement();
}

BaseHost::~BaseHost() = default;

QString BaseHost::filterString() const
{
    return tr("QindaBase database (*.qbase);;SQLite database (*.sqlite *.sqlite3 *.db *.db3);;All files (*)");
}

QString BaseHost::saveFilterString() const
{
    return tr("QindaBase database (*.qbase)");
}

void BaseHost::documentReplaced()
{
    syncEnablement();
}

void BaseHost::setCurrentView(const QString &view)
{
    if (m_view == view) {
        return;
    }
    m_view = view;
    QStringList contexts;
    if (view == QLatin1String("tableDesign")) {
        contexts.append(QStringLiteral("tableDesign"));
    } else if (view == QLatin1String("queryDesign")) {
        contexts.append(QStringLiteral("queryDesign"));
    } else if (view == QLatin1String("form") || view == QLatin1String("report")) {
        contexts.append(QStringLiteral("formDesign"));
    } else if (view == QLatin1String("macroDesign")) {
        contexts.append(QStringLiteral("macroDesign")); // M80
    }
    setRibbonContexts(contexts);
    if (view != QLatin1String("report")) {
        setReportOutput(nullptr);
    }
    emit viewChanged();
    syncEnablement();
}

void BaseHost::setCurrentKind(const QString &kind)
{
    if (m_kind != kind) {
        m_kind = kind;
        emit viewChanged();
        syncEnablement();
    }
}

void BaseHost::setCurrentName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        emit viewChanged();
        syncEnablement();
    }
}

void BaseHost::setSelectedKind(const QString &kind)
{
    if (m_selectedKind != kind) {
        m_selectedKind = kind;
        emit selectionChanged();
        syncEnablement();
    }
}

void BaseHost::setSelectedName(const QString &name)
{
    if (m_selectedName != name) {
        m_selectedName = name;
        emit selectionChanged();
        syncEnablement();
    }
}

void BaseHost::setFileTarget(const QVariantMap &target)
{
    if (m_fileTarget != target) {
        m_fileTarget = target;
        emit fileTargetChanged();
        syncEnablement();
    }
}

void BaseHost::setDesignDirty(bool dirty)
{
    if (m_designDirty == dirty) {
        return;
    }
    m_designDirty = dirty;
    m_document->setDesignPending(dirty);
    emit designChanged();
    emit titleChanged();
    syncEnablement();
}

void BaseHost::setDesignHistory(bool active)
{
    if (m_designHistory != active) {
        m_designHistory = active;
        emit designChanged();
        syncEnablement();
    }
}

void BaseHost::setDesignCanUndo(bool can)
{
    if (m_designCanUndo != can) {
        m_designCanUndo = can;
        emit designChanged();
        syncEnablement();
    }
}

void BaseHost::setDesignCanRedo(bool can)
{
    if (m_designCanRedo != can) {
        m_designCanRedo = can;
        emit designChanged();
        syncEnablement();
    }
}

void BaseHost::setReportOutput(QObject *output)
{
    auto *report = qobject_cast<QindaTK::RichTextDocument *>(output);
    auto *labels = qobject_cast<LabelSheet *>(output);
    if (m_report == report && m_labels == labels) {
        return;
    }
    m_report = report;
    m_labels = labels;
    emit printSourceChanged();
    syncEnablement();
}

void BaseHost::copyToClipboard(const QString &text)
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString BaseHost::clipboardText() const
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr ? clipboard->text() : QString();
}

QVariantMap BaseHost::exportReportPdf(const QUrl &url)
{
    if (!printable()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Open a report to export it.")}};
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!exportPdf(path)) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("The PDF could not be written.")}};
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
}

std::optional<QPageLayout> BaseHost::printPageLayout() const
{
    if (!m_labels.isNull()) {
        return m_labels->pageLayout();
    }
    if (!m_report.isNull()) {
        // AGENT-NOTE (M80): the paper must be set before the painter starts
        // (print_service.h); a size set inside the render reached only the
        // second page, so a Letter report's first page came out A4.
        const QindaTK::RichText::PageSetup page = m_report->pageSetupValue();
        const QSizeF portrait(qMin(page.widthPt, page.heightPt), qMax(page.widthPt, page.heightPt));
        QPageLayout layout(QPageSize(portrait, QPageSize::Point),
                           printPrefersLandscape() ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
        layout.setMode(QPageLayout::FullPageMode);
        return layout;
    }
    return std::nullopt;
}

bool BaseHost::printPrefersLandscape() const
{
    if (m_report.isNull()) {
        return false;
    }
    const QindaTK::RichText::PageSetup page = m_report->pageSetupValue();
    return page.widthPt > page.heightPt;
}

Quick::PrintRenderCallback BaseHost::printRenderCallback() const
{
    if (!m_labels.isNull()) {
        // M80: labels print through the painter the window shows them with,
        // at the printer's resolution, from the paper's corner.
        QPointer<LabelSheet> sheet = m_labels;
        return [sheet](QPrinter *printer, QPainter *painter) {
            if (sheet.isNull()) {
                return;
            }
            for (int page = 0; page < sheet->pageCount(); ++page) {
                if (page > 0) {
                    printer->newPage();
                }
                sheet->paintPage(painter, page, qreal(printer->resolution()) / 72.0);
            }
        };
    }
    if (m_report.isNull()) {
        return {};
    }
    // AGENT-NOTE (one output description, QindaTK D-130): the report prints
    // through QindaTK's own sheet painter — the same pages its preview and
    // exportPdf show — scaled from its 1/96-inch units to the printer's.
    QPointer<QindaTK::RichTextDocument> document = m_report;
    return [document](QPrinter *printer, QPainter *painter) {
        if (document.isNull()) {
            return;
        }
        QindaTK::RichTextLayout layout;
        layout.setDocument(document.data());
        layout.setMode(QindaTK::RichTextLayout::Mode::Paged);
        layout.paginateAll();
        QindaTK::Print::Ticket ticket;
        ticket.paperId = QStringLiteral("document");
        ticket.scaleMode = QStringLiteral("actual");
        const QindaTK::Print::Paper paper = QindaTK::Print::standardPapers().first();
        const QVector<QindaTK::Print::Sheet> sheets =
            QindaTK::Print::planSheets(document->pageSetupValue(), layout.pageCount(), ticket, paper);
        QindaTK::RichTextImageRenderer objects;
        for (int i = 0; i < sheets.size(); ++i) {
            const QindaTK::Print::Sheet &sheet = sheets.at(i);
            if (i == 0) {
                printer->setPageSize(QPageSize(sheet.sizePt, QPageSize::Point));
                printer->setPageMargins(QMarginsF(0, 0, 0, 0));
            } else {
                printer->setPageSize(QPageSize(sheet.sizePt, QPageSize::Point));
                printer->newPage();
            }
            painter->save();
            const qreal scale = qreal(printer->resolution()) / QindaTK::Print::DeviceDpi;
            painter->scale(scale, scale);
            QindaTK::Print::paintSheet(painter, layout, sheet, &objects, false);
            painter->restore();
        }
    };
}

} // namespace QindaOffice::Apps::Base
