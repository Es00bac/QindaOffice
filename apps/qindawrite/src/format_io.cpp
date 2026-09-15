// SPDX-License-Identifier: GPL-3.0-or-later
#include "format_io.h"

#include "libre_office_converter.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextDocument>
#include <QTextDocumentWriter>
#include <QTemporaryDir>
#include <QTimer>

namespace QindaOffice::Apps::Write {

namespace {

QString extensionOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// Blocks until the async converter finishes one job (nested loop; the
// conversion is a local subprocess and the UI stays responsive).
QString convertAndWait(const QString &input, const QString &targetExt, QString *error)
{
    Core::LibreOfficeConverter converter;
    const QString outDir =
        QFileInfo(input).absolutePath() + QStringLiteral("/.qindawrite-lo");
    QDir().mkpath(outDir);
    QEventLoop loop;
    QString result;
    QString failure;
    QObject::connect(&converter, &Core::LibreOfficeConverter::jobFinished, &loop,
                     [&loop, &result](int, const QString &output) {
                         result = output;
                         loop.quit();
                     });
    QObject::connect(&converter, &Core::LibreOfficeConverter::jobFailed, &loop,
                     [&loop, &failure](int, const QString &message) {
                         failure = message;
                         loop.quit();
                     });
    const int id = converter.convert(input, targetExt, outDir, error);
    if (id == 0) {
        return {};
    }
    // A conversion that never answers must not hang the editor.
    QTimer::singleShot(130000, &loop, [&loop] { loop.quit(); });
    loop.exec();
    if (result.isEmpty() && failure.isEmpty() && error != nullptr) {
        *error = QStringLiteral("conversion timed out");
    } else if (!failure.isEmpty() && error != nullptr) {
        *error = failure;
    }
    // AGENT-GUARD: only intermediates we produced (inside the private
    // converter directory) may be removed - `input` is the user's document
    // on the load path.
    if (input.startsWith(outDir)) {
        QFile::remove(input);
    }
    return result;
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
    QFile::remove(path);
    return QFile::rename(output, path);
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
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        file.write(document->toHtml().toUtf8());
        return true;
    }
    if (ext == QLatin1String("md")) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        file.write(document->toMarkdown(QTextDocument::MarkdownDialectGitHub).toUtf8());
        return true;
    }
    if (ext == QLatin1String("txt")) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        file.write(document->toPlainText().toUtf8());
        return true;
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
