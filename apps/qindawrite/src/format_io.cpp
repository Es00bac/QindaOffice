// SPDX-License-Identifier: GPL-3.0-or-later
#include "format_io.h"

#include "libre_office_converter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextDocumentWriter>
#include <QTemporaryDir>

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

QString extensionOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// Conversion goes through Core::convertBlocking (office_core): this file and
// show_document.cpp each carried a byte-identical private copy of the nested
// event loop, timeout and intermediate-cleanup rules.
QString convertAndWait(const QString &input, const QString &targetExt, QString *error)
{
    return Core::convertBlocking(input, targetExt, QStringLiteral(".qindawrite-lo"), error);
}

bool saveViaHtml(const QString &path, QTextDocument *document, const QString &targetExt,
                 QString *error)
{
    QTemporaryDir temp;
    if (!temp.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("no temporary directory");
        }
        return false;
    }
    const QString htmlPath = temp.path() + QStringLiteral("/document.html");
    if (!saveDocument(htmlPath, document, error)) {
        return false;
    }
    const QString output = convertAndWait(htmlPath, targetExt, error);
    if (output.isEmpty()) {
        return false;
    }
    // AGENT-GUARD: copy through QSaveFile at the destination — the converted
    // file lives in the QTemporaryDir (often tmpfs) while `path` is on the
    // user's filesystem, so QFile::rename fails with EXDEV. The previous
    // code removed the target first, destroying the user's file on that
    // failure. QSaveFile also keeps the write atomic and never truncates
    // the original when the conversion output is broken.
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = out.errorString();
        }
        return false;
    }
    QFile converted(output);
    if (!converted.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = converted.errorString();
        }
        out.cancelWriting();
        return false;
    }
    const QByteArray payload = converted.readAll();
    if (payload.isEmpty() || out.write(payload) != payload.size() || !out.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("converting to %1 produced no usable output").arg(targetExt);
        }
        out.cancelWriting();
        return false;
    }
    return true;
}

} // namespace

bool loadDocument(const QString &path, QTextDocument *document, QString *error)
{
    const QString ext = extensionOf(path);
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        document->setHtml(QString::fromUtf8(file.readAll()));
        return true;
    }
    if (ext == QLatin1String("md")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        document->setMarkdown(QString::fromUtf8(file.readAll()));
        return true;
    }
    if (ext == QLatin1String("txt")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        document->setPlainText(QString::fromUtf8(file.readAll()));
        return true;
    }
    if (ext == QLatin1String("odt") || ext == QLatin1String("docx") || ext == QLatin1String("doc")
        || ext == QLatin1String("rtf")) {
        const QString html = convertAndWait(path, QStringLiteral("html"), error);
        if (html.isEmpty()) {
            return false;
        }
        QFile file(html);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        document->setHtml(QString::fromUtf8(file.readAll()));
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("Unsupported format .%1").arg(ext);
    }
    return false;
}

bool saveDocument(const QString &path, QTextDocument *document, QString *error)
{
    const QString ext = extensionOf(path);
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
        return writeUtf8Atomically(path, document->toHtml().toUtf8(), error);
    }
    if (ext == QLatin1String("md")) {
        return writeUtf8Atomically(
            path, document->toMarkdown(QTextDocument::MarkdownDialectGitHub).toUtf8(), error);
    }
    if (ext == QLatin1String("txt")) {
        return writeUtf8Atomically(path, document->toPlainText().toUtf8(), error);
    }
    if (ext == QLatin1String("odt")) {
        QTextDocumentWriter writer(path, "odf");
        if (!writer.write(document)) {
            if (error != nullptr) {
                *error = QStringLiteral("ODF write failed");
            }
            return false;
        }
        return true;
    }
    if (ext == QLatin1String("docx") || ext == QLatin1String("rtf")) {
        return saveViaHtml(path, document, ext, error);
    }
    if (error != nullptr) {
        *error = QStringLiteral("Unsupported save format .%1").arg(ext);
    }
    return false;
}

QString defaultSaveFilter()
{
    return QStringLiteral("OpenDocument Text (*.odt);;HTML (*.html);;Markdown (*.md);;"
                          "Plain text (*.txt);;Word (*.docx);;RTF (*.rtf)");
}

QString openFilterString()
{
    return QStringLiteral(
        "Documents (*.html *.htm *.md *.txt *.odt *.docx *.doc *.rtf);;All files (*)");
}

} // namespace QindaOffice::Apps::Write
