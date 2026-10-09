// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared by the M77 office_docx tests (D-086): write a document, validate
// the package against the ECMA-376 schemas (QQO_OOXML_SCHEMA_DIR), read it
// back, and have LibreOffice render it to PDF (kept under QQO_GRAB_DIR).

#include "docx_reader.h"
#include "docx_writer.h"
#include "ooxml_package.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

#include <memory>

namespace QindaOffice::Test {

struct WordDoc {
    QTextDocument body;
    Docx::WordParts parts;
};

inline std::unique_ptr<WordDoc> readWord(const QByteArray &bytes)
{
    auto doc = std::make_unique<WordDoc>();
    if (!Docx::readDocx(Ooxml::Package::fromData(bytes), &doc->body, &doc->parts)) {
        return nullptr;
    }
    return doc;
}

inline QByteArray partOf(const QByteArray &bytes, const QString &part)
{
    const auto package = Ooxml::Package::fromData(bytes);
    return package ? package->data(part) : QByteArray();
}

// QVERIFY inside: call through QVERIFY(validates(bytes)) style is not
// possible, so this records the failure and returns false.
inline bool validates(const QByteArray &bytes, QString *problems)
{
    if (!schemaValidationAvailable()) {
        qInfo("schema validation skipped (QQO_OOXML_SCHEMA_DIR unset)");
        return true;
    }
    const auto package = Ooxml::Package::fromData(bytes);
    if (!package) {
        *problems = QStringLiteral("not a package");
        return false;
    }
    const QStringList errors = validatePackage(*package);
    *problems = errors.join(QLatin1Char('\n'));
    return errors.isEmpty();
}

// Writes `bytes` as <name>.docx, converts it with LibreOffice and returns
// the PDF's path (empty when soffice failed); the files stay in
// QQO_GRAB_DIR when it is set (and PNG pages when pdftoppm is there).
inline QString renderWithLibreOffice(const QByteArray &bytes, const QString &name, QTemporaryDir &scratch)
{
    const QString keep = qEnvironmentVariable("QQO_GRAB_DIR");
    const QString dir = keep.isEmpty() ? scratch.path() : keep;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name + QStringLiteral(".docx"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    file.close();
    QTemporaryDir profile;
    QProcess soffice;
    startSoffice(soffice, {QStringLiteral("-env:UserInstallation=file://") + profile.path(), QStringLiteral("--headless"),
                           QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), dir, path});
    if (!soffice.waitForFinished(120000)) {
        return {};
    }
    const QString pdf = QDir(dir).filePath(name + QStringLiteral(".pdf"));
    if (QFileInfo(pdf).size() < 1000) {
        qWarning().noquote() << soffice.readAllStandardError();
        return {};
    }
    if (!keep.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()) {
        QProcess::execute(QStringLiteral("pdftoppm"),
                          {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("70"), pdf, QDir(dir).filePath(name)});
    }
    return pdf;
}

// The PDF's text (pdftotext), or empty when the tool is missing.
inline QString pdfText(const QString &pdf)
{
    if (QStandardPaths::findExecutable(QStringLiteral("pdftotext")).isEmpty()) {
        return {};
    }
    QProcess tool;
    tool.start(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), pdf, QStringLiteral("-")});
    tool.waitForFinished(30000);
    return QString::fromUtf8(tool.readAllStandardOutput());
}

} // namespace QindaOffice::Test
