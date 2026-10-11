// SPDX-License-Identifier: GPL-3.0-or-later
// MailMergeController (write_mailmerge.h): Finish & Merge — the merged
// document in a new window, printed or as a PDF, and the e-mail messages
// handed to QindaMail one recipient at a time.

#include "write_mailmerge.h"

#include "docx_float.h"
#include "docx_writer.h"
#include "page_geometry.h"
#include "paths.h"
#include "pdf_exporter.h"
#include "write_document.h"
#include "write_endnotes.h"
#include "write_footnotes.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextDocument>
#include <QUrl>

namespace QindaOffice::Apps::Write {

namespace {

QString encoded(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

QString fileSafe(QString name)
{
    for (QChar &c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) {
            c = QLatin1Char('_');
        }
    }
    return name.left(40);
}

} // namespace

std::pair<int, int> MailMergeController::rangeOf(const QVariantMap &range) const
{
    const int count = int(m_order.size());
    const QString records = range.value(QStringLiteral("records"), QStringLiteral("all")).toString();
    if (records == u"current") {
        return {m_position, m_position};
    }
    if (records == u"range") {
        const int from = qBound(1, range.value(QStringLiteral("from"), 1).toInt(), qMax(1, count)) - 1;
        const int to = qBound(from + 1, range.value(QStringLiteral("to"), count).toInt(), qMax(1, count)) - 1;
        return {from, to};
    }
    return {0, count - 1};
}

std::shared_ptr<QTextDocument> MailMergeController::mergedDocument(const QVariantMap &range, Docx::MergeResult *result)
{
    auto out = std::make_shared<QTextDocument>();
    out->setUndoRedoEnabled(false);
    const auto [first, last] = rangeOf(range);
    Docx::MergeRequest request;
    request.main = m_document->textDocument();
    request.kind = kind();
    request.data = &m_data;
    request.order = m_order;
    request.fieldMap = settings().fieldMap;
    request.first = first;
    request.last = last;
    request.sectionXml = Docx::sectionPropertiesXml(m_document->parts());
    m_document->configureLike(out.get()); // first: rows are padded for this page (mergeDocument)
    const Docx::MergeResult merged = Docx::mergeDocument(out.get(), request);
    if (result != nullptr) {
        *result = merged;
    }
    if (Docx::hasFloatingPictures(*out)) {
        Docx::installPictureHandler(out.get());
    }
    return out;
}

bool MailMergeController::finishToDocument(const QVariantMap &range)
{
    Docx::MergeResult result;
    const std::shared_ptr<QTextDocument> merged = mergedDocument(range, &result);
    if (result.copies == 0) {
        emit message(tr("No records to merge"));
        return false;
    }
    QString error;
    // Each record's section shows the main document's headers (M88).
    Docx::WordParts parts = Docx::cloneWordParts(m_document->parts());
    Docx::shareStories(parts, QString::fromUtf16(Docx::kMergeSectionKey));
    const QByteArray bytes = Docx::docxBytes(*merged, parts, {}, &error);
    if (bytes.isEmpty()) {
        emit message(tr("The merged document could not be made: %1").arg(error));
        return false;
    }
    if (openDocument) {
        openDocument(bytes);
    }
    emit message(tr("%n record(s) merged into a new document", "", int(result.records.size())));
    return true;
}

bool MailMergeController::finishToPrinter(const QVariantMap &range)
{
    Docx::MergeResult result;
    const std::shared_ptr<QTextDocument> merged = mergedDocument(range, &result);
    if (result.copies == 0) {
        emit message(tr("No records to merge"));
        return false;
    }
    if (printDocument) {
        printDocument(merged);
    }
    return true;
}

bool MailMergeController::finishToPdf(const QUrl &url, const QVariantMap &range)
{
    Docx::MergeResult result;
    const std::shared_ptr<QTextDocument> merged = mergedDocument(range, &result);
    if (result.copies == 0) {
        emit message(tr("No records to merge"));
        return false;
    }
    // The main document's headers and footers on every page; the notes the
    // merged copies carry, numbered in them.
    PageDecoration decoration = m_document->decoration();
    decoration.pages.clear(); // the copies' pages are uniform (M88)
    PageGeometry geometry;
    geometry.setPageSize(merged->pageSize());
    decoration.footnotes = footnotesByPage(merged.get(), geometry, m_document->footnotes());
    decoration.endnotes = layoutEndnotes(*merged, m_document->endnotes(), m_document->noteOptions(true));
    decoration.overTextPictures = Docx::hasFloatingPictures(*merged);
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    if (!exportDocumentPdf(merged.get(), path, &error, decoration)) {
        emit message(tr("The PDF could not be written: %1").arg(error));
        return false;
    }
    emit message(tr("%n record(s) merged into %1", "", int(result.records.size())).arg(QFileInfo(path).fileName()));
    return true;
}

QVariantMap MailMergeController::emailInfo() const
{
    const Docx::MailMergeSettings s = settings();
    QString to = s.addressField;
    if (to.isEmpty() || m_data.fieldIndex(to) < 0) {
        to = Docx::matchedColumn(QStringLiteral("E-mail Address"), m_data, s.fieldMap);
    }
    return {{QStringLiteral("fields"), m_data.fields},
            {QStringLiteral("to"), to},
            {QStringLiteral("subject"), s.mailSubject},
            {QStringLiteral("format"), s.mailAsAttachment ? QStringLiteral("attachment") : QStringLiteral("text")},
            {QStringLiteral("records"), int(m_order.size())}};
}

int MailMergeController::sendEmails(const QVariantMap &answer)
{
    const QString to = answer.value(QStringLiteral("to")).toString();
    const QString subject = answer.value(QStringLiteral("subject")).toString();
    const bool attachment = answer.value(QStringLiteral("format")).toString() == u"attachment";
    if (to.isEmpty() || m_data.fieldIndex(to) < 0 || !launchMail) {
        emit message(tr("Choose the field that holds the e-mail addresses"));
        return 0;
    }
    Docx::MailMergeSettings s = settings();
    s.addressField = to;
    s.mailSubject = subject;
    s.mailAsAttachment = attachment;
    s.destination = QStringLiteral("email");
    store(s);
    const QString handoff = Core::stateDirFor(Core::appLeaf()) + QStringLiteral("/handoff");
    const auto [first, last] = rangeOf(answer);
    int sent = 0;
    int missing = 0;
    for (int position = first; position <= last && position < m_order.size(); ++position) {
        QVariantMap one{{QStringLiteral("records"), QStringLiteral("range")},
                        {QStringLiteral("from"), position + 1},
                        {QStringLiteral("to"), position + 1}};
        Docx::MergeResult result;
        const std::shared_ptr<QTextDocument> copy = mergedDocument(one, &result);
        if (result.copies == 0) {
            continue; // a SKIPIF dropped this recipient
        }
        const QString address = m_data.value(m_order.at(position), to).trimmed();
        if (address.isEmpty()) {
            ++missing;
            continue;
        }
        // AGENT-CONTRACT (apps/qindamail launch_arguments.h, mail_host.cpp
        // openWithoutDocument): `qindamail mailto:…` opens a composer draft;
        // `--attach <file> mailto:…` with the file attached. Nothing is sent
        // until the user sends it there.
        QString mailto = QStringLiteral("mailto:") + QString::fromLatin1(QUrl::toPercentEncoding(address, "@+"))
            + QStringLiteral("?subject=") + encoded(subject);
        QStringList arguments;
        if (attachment) {
            QDir().mkpath(handoff);
            const QString path = handoff + QStringLiteral("/%1-%2.docx").arg(fileSafe(subject.isEmpty() ? tr("Message") : subject)).arg(position + 1);
            QSaveFile file(path);
            Docx::WordParts parts = Docx::cloneWordParts(m_document->parts());
            Docx::shareStories(parts, QString::fromUtf16(Docx::kMergeSectionKey));
            const QByteArray bytes = Docx::docxBytes(*copy, parts);
            if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                continue;
            }
            arguments = {QStringLiteral("--attach"), path, mailto};
        } else {
            QString body = copy->toPlainText();
            body.replace(QChar(QChar::LineSeparator), QLatin1Char('\n')).replace(QChar(QChar::Nbsp), QLatin1Char(' '));
            arguments = {mailto + QStringLiteral("&body=") + encoded(body)};
        }
        sent += launchMail(arguments) ? 1 : 0;
    }
    emit message(missing > 0 ? tr("%n message(s) handed to QindaMail; %1 recipient(s) had no address", "", sent).arg(missing)
                             : tr("%n message(s) handed to QindaMail", "", sent));
    return sent;
}

} // namespace QindaOffice::Apps::Write
