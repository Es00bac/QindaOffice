// SPDX-License-Identifier: GPL-3.0-or-later
// BaseHost: the document, the view state, the clipboard and the print
// source. Commands and menus are in base_host_actions.cpp.
#include "base_host.h"

#include "base_document.h"

#include <qindatk/local_report_document_model.h>
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

void BaseHost::setReportOutput(QObject *model)
{
    auto *report = qobject_cast<QindaTK::LocalReportDocumentModel *>(model);
    if (m_report == report) {
        return;
    }
    m_report = report;
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
    if (m_report.isNull() || m_report->document() == nullptr) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Open a report to export it.")}};
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!exportPdf(path)) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("The PDF could not be written.")}};
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
}

Quick::PrintRenderCallback BaseHost::printRenderCallback() const
{
    if (m_report.isNull() || m_report->document() == nullptr) {
        return {};
    }
    // AGENT-NOTE (one output description, QindaTK D-130): the report prints
    // through QindaTK's own sheet painter — the same pages its preview and
    // exportPdf show — scaled from its 1/96-inch units to the printer's.
    QPointer<QindaTK::RichTextDocument> document = m_report->document();
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
