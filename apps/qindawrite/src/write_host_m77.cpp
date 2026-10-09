// SPDX-License-Identifier: GPL-3.0-or-later
// WriteHost, M77 (D-086): the Mailings tab (MailMergeController), envelopes
// and labels, endnotes and the Footnote and Endnote dialog, picture wrap
// and position; documents made from this one opened in their own window
// or printed.

#include "write_host.h"

#include "menu_tree.h"
#include "page_picture_tool.h"
#include "page_text_control.h"
#include "write_envelopes.h"
#include "write_float.h"
#include "write_mailmerge.h"
#include "write_notes.h"

#include <QTextDocument>

#include <utility>

namespace QindaOffice::Apps::Write {

using Quick::MenuNode;

namespace {

// AGENT-NOTE: a document made here (a merged document, an envelope, a
// label sheet) reaches its new window through this slot: the host asks
// the application for a window (newWindowRequested, handled
// synchronously by OfficeApplication::newWindow) and the new WriteHost's
// constructor adopts the bytes. Nobody takes them without an application
// (the tests), so the slot is cleared right after.
QByteArray &pendingDocument()
{
    static QByteArray bytes;
    return bytes;
}

} // namespace

void WriteHost::createMailings()
{
    m_notes = new NotesController(m_control, m_document, actions(), this);
    m_pictureLayout = new PictureLayoutController(m_control, m_document, actions(), this);
    m_envelopes = new EnvelopesController(m_control, m_document, actions(), this);
    m_mailMerge = new MailMergeController(m_control, m_document, actions(), this);
    m_mailMerge->setEnvelopes(m_envelopes);
    const auto open = [this](const QByteArray &docx) { openDocumentWindow(docx); };
    const auto print = [this](std::shared_ptr<QTextDocument> document) { printOther(std::move(document)); };
    m_envelopes->openDocument = open;
    m_envelopes->printDocument = print;
    m_envelopes->mergeAddress = [this](QTextCursor &cursor) { m_mailMerge->insertDefaultAddressBlock(cursor); };
    m_mailMerge->openDocument = open;
    m_mailMerge->printDocument = print;
    const auto say = [this](const QString &text) { showStatusMessage(text, 6000); };
    connect(m_notes, &NotesController::message, this, say);
    connect(m_envelopes, &EnvelopesController::message, this, say);
    connect(m_mailMerge, &MailMergeController::message, this, say);
}

void WriteHost::adoptPendingDocument()
{
    if (pendingDocument().isEmpty()) {
        return;
    }
    const QByteArray docx = std::exchange(pendingDocument(), QByteArray());
    QString error;
    if (m_document->loadDocxBytes(docx, &error)) {
        documentReplaced();
    } else {
        showStatusMessage(tr("The new document could not be opened: %1").arg(error), 8000);
    }
}

void WriteHost::openDocumentWindow(const QByteArray &docx)
{
    emit documentMade(docx);
    pendingDocument() = docx;
    emit newWindowRequested();
    if (!pendingDocument().isEmpty()) {
        pendingDocument().clear(); // no window took it (no application, or the window limit)
    }
}

void WriteHost::printOther(std::shared_ptr<QTextDocument> document)
{
    m_printOverride = std::move(document);
    if (Quick::ActionEntry *print = action(QStringLiteral("filePrintAction"))) {
        print->trigger(); // the shell asks for a print job now (printRenderCallback)
    }
    m_printOverride.reset();
}

QList<MenuNode> WriteHost::mailingsMenu() const
{
    return m_mailMerge->menuItems();
}

QObject *WriteHost::mailMergeObject() const
{
    return m_mailMerge;
}

QObject *WriteHost::envelopesObject() const
{
    return m_envelopes;
}

QObject *WriteHost::notesObject() const
{
    return m_notes;
}

QObject *WriteHost::pictureLayoutObject() const
{
    return m_pictureLayout;
}

} // namespace QindaOffice::Apps::Write
