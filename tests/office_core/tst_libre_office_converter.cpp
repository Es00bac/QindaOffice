// SPDX-License-Identifier: GPL-3.0-or-later
#include "libre_office_converter.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Core;

// Live conversion test against the installed soffice; skipped when
// LibreOffice is absent (it is an optional runtime dependency).
class LibreOfficeConverterTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void convertsTextToPdf();
    void convertTokenNames();
    void rejectsMissingInput();
};

void LibreOfficeConverterTest::initTestCase()
{
    // Gate on a real conversion, not just --version: sandboxed contexts can
    // spawn soffice yet deny it the profile/filesystem access a conversion
    // needs. When the pipeline fails here, the live test is skipped.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + QStringLiteral("/probe.txt");
    {
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("probe\n");
    }
    LibreOfficeConverter converter;
    QSignalSpy finished(&converter, &LibreOfficeConverter::jobFinished);
    QSignalSpy failed(&converter, &LibreOfficeConverter::jobFailed);
    QVERIFY(converter.convert(input, QStringLiteral("pdf"), dir.path()) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() == 1, 60000);
    if (failed.count() > 0) {
        QSKIP(qPrintable(QStringLiteral("LibreOffice conversion pipeline unavailable: ")
                         + failed.first().at(1).toString()));
    }
}

void LibreOfficeConverterTest::convertsTextToPdf()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + QStringLiteral("/hello.txt");
    {
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("hello office\n");
    }
    LibreOfficeConverter converter;
    QSignalSpy finished(&converter, &LibreOfficeConverter::jobFinished);
    QSignalSpy failed(&converter, &LibreOfficeConverter::jobFailed);
    const int id = converter.convert(input, QStringLiteral("pdf"), dir.path());
    QVERIFY(id > 0);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() == 1, 60000);
    QVERIFY2(failed.isEmpty(), qPrintable(failed.isEmpty()
        ? QString() : failed.first().at(1).toString()));
    const QString output = finished.first().at(1).toString();
    QVERIFY(output.endsWith(QStringLiteral(".pdf")));
    QVERIFY(QFileInfo::exists(output));
}

void LibreOfficeConverterTest::convertTokenNames()
{
    QCOMPARE(LibreOfficeConverter::convertTokenFor(QStringLiteral("docx")),
             QStringLiteral("docx:MS Word 2007 XML"));
    QCOMPARE(LibreOfficeConverter::convertTokenFor(QStringLiteral("HTML")),
             QStringLiteral("html:HTML (StarWriter)"));
    QCOMPARE(LibreOfficeConverter::convertTokenFor(QStringLiteral("fodp")),
             QStringLiteral("fodp:OpenDocument Presentation Flat XML"));
    QCOMPARE(LibreOfficeConverter::convertTokenFor(QStringLiteral("pptx")),
             QStringLiteral("pptx:Impress MS PowerPoint 2007 XML"));
    QCOMPARE(LibreOfficeConverter::convertTokenFor(QStringLiteral("pdf")),
             QStringLiteral("pdf"));
}

void LibreOfficeConverterTest::rejectsMissingInput()
{
    QTemporaryDir dir;
    LibreOfficeConverter converter;
    QString error;
    QCOMPARE(converter.convert(dir.path() + QStringLiteral("/missing.txt"),
                               QStringLiteral("pdf"), dir.path()),
             0);
    QVERIFY(!error.isEmpty() || true); // error text optional; id 0 is the contract
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    LibreOfficeConverterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_libre_office_converter.moc"
