// SPDX-License-Identifier: GPL-3.0-or-later
#include "format_io.h"

#include "docx_reader.h"
#include "docx_writer.h"
#include "libre_office_converter.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTextDocumentWriter>

namespace QindaOffice::Apps::Write {

namespace {

// AGENT-GUARD: all direct saves go through this helper so a failed or
// truncated write (ENOSPC, quota) is reported instead of silently saving
// half a document and clearing the dirty flag.
bool writeUtf8Atomically(const QString &path, const QByteArray &payload, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool readText(const QString &path, QString *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    *out = QString::fromUtf8(file.readAll());
    return true;
}

QString extensionOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

} // namespace

bool loadDocument(const QString &path, QTextDocument *document, QString *error)
{
    const QString ext = extensionOf(path);
    QString text;
    if (ext == u"html" || ext == u"htm") {
        if (!readText(path, &text, error)) {
            return false;
        }
        document->setHtml(text);
        return true;
    }
    if (ext == u"md") {
        if (!readText(path, &text, error)) {
            return false;
        }
        document->setMarkdown(text);
        return true;
    }
    if (ext == u"txt") {
        if (!readText(path, &text, error)) {
            return false;
        }
        document->setPlainText(text);
        return true;
    }
    if (ext == u"docx" || ext == u"docm" || ext == u"dotx" || ext == u"dotm") {
        Docx::WordParts parts;
        return Docx::readDocx(path, document, &parts, error);
    }
    if (ext == u"odt" || ext == u"doc" || ext == u"rtf") {
        // LibreOffice writes beside its input: convert a private copy.
        QTemporaryDir scratch;
        const QString input = scratch.filePath(QFileInfo(path).fileName());
        if (!scratch.isValid() || !QFile::copy(path, input)) {
            if (error != nullptr) {
                *error = QStringLiteral("cannot read %1").arg(path);
            }
            return false;
        }
        const QString docx = Core::convertBlocking(input, QStringLiteral("docx"), QStringLiteral("lo"), error);
        if (docx.isEmpty()) {
            return false;
        }
        Docx::WordParts parts;
        return Docx::readDocx(docx, document, &parts, error);
    }
    if (error != nullptr) {
        *error = QStringLiteral("Unsupported format .%1").arg(ext);
    }
    return false;
}

bool saveDocument(const QString &path, QTextDocument *document, QString *error)
{
    const QString ext = extensionOf(path);
    if (ext == u"html" || ext == u"htm") {
        return writeUtf8Atomically(path, document->toHtml().toUtf8(), error);
    }
    if (ext == u"md") {
        return writeUtf8Atomically(path, document->toMarkdown(QTextDocument::MarkdownDialectGitHub).toUtf8(), error);
    }
    if (ext == u"txt") {
        return writeUtf8Atomically(path, document->toPlainText().toUtf8(), error);
    }
    if (ext == u"docx") {
        return Docx::writeDocx(path, *document, Docx::WordParts(), {}, error);
    }
    if (ext == u"odt") {
        QTextDocumentWriter writer(path, "odf");
        if (!writer.write(document)) {
            if (error != nullptr) {
                *error = QStringLiteral("ODF write failed");
            }
            return false;
        }
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Unsupported save format .%1").arg(ext);
    }
    return false;
}

QString defaultSaveFilter()
{
    return QStringLiteral("Word document (*.docx);;Word template (*.dotx);;Word macro-enabled document (*.docm);;"
                          "Word 97-2003 document (*.doc);;OpenDocument Text (*.odt);;Rich Text Format (*.rtf);;"
                          "Web page (*.html);;Markdown (*.md);;Plain text (*.txt)");
}

QString openFilterString()
{
    return QStringLiteral("Documents (*.docx *.docm *.dotx *.dotm *.doc *.dot *.odt *.ott *.fodt *.rtf *.wpd *.wps "
                          "*.html *.htm *.md *.txt);;Word documents (*.docx *.docm *.dotx *.dotm *.doc *.dot);;"
                          "All files (*)");
}

} // namespace QindaOffice::Apps::Write
