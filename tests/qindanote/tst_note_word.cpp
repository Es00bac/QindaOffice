// SPDX-License-Identifier: GPL-3.0-or-later
// M65 gate for QindaNote's Word export (D-068): a page with Markdown notes,
// typed text, a checklist, a picture and handwriting becomes a .docx that
// office_docx reads back with all of it — and, with QQO_OOXML_SCHEMA_DIR,
// validates against the ECMA-376 schemas.

#include "docx_reader.h"
#include "ooxml_package.h"
#include "qindanote/src/note_word_export.h"
#include "qindanote/src/notebook.h"
#include "support/ooxml_schema.h"

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice;
using QindaOffice::Apps::Notes::NotePage;

class NoteWordTest : public QObject {
    Q_OBJECT
private slots:
    void pageBecomesAWordDocument();
};

void NoteWordTest::pageBecomesAWordDocument()
{
    QImage picture(30, 20, QImage::Format_ARGB32);
    picture.fill(Qt::darkGreen);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    const QJsonArray blocks{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("markdown")},
                    {QStringLiteral("text"), QStringLiteral("## Decisions\n\n- **Ship** the portal\n- Train staff")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Typed note")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("check")}, {QStringLiteral("text"), QStringLiteral("Call the vendor")},
                    {QStringLiteral("checked"), true}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("data"), QString::fromLatin1(png.toBase64())}},
    };
    NotePage page;
    page.title = QStringLiteral("Weekly sync");
    page.loaded = true;
    page.envelope.blocksJson = QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}}).toJson();
    Ink::Stroke stroke;
    stroke.complete = true;
    for (int i = 0; i < 20; ++i) {
        stroke.samples.append({QPointF(100 + i * 5, 600 + (i % 4) * 6), 0.6});
    }
    page.envelope.ink.appendStroke(stroke);

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("sync.docx"));
    QString error;
    QVERIFY2(Apps::Notes::exportPageAsWord(page, 760, path, &error), qPrintable(error));
    if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
        QFile::remove(grabs + QStringLiteral("/qindanote_export.docx"));
        QFile::copy(path, grabs + QStringLiteral("/qindanote_export.docx")); // for review in Word / LibreOffice
    }

    const auto package = Ooxml::Package::load(path);
    QVERIFY(package);
    if (Test::schemaValidationAvailable()) {
        const QStringList errors = Test::validatePackage(*package);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
    }
    QTextDocument doc;
    Docx::WordParts parts;
    QVERIFY2(Docx::readDocx(package, &doc, &parts, &error), qPrintable(error));
    const QString text = doc.toPlainText();
    QVERIFY(text.startsWith(QStringLiteral("Weekly sync")));
    QVERIFY(text.contains(QStringLiteral("Decisions")));
    QVERIFY(text.contains(QStringLiteral("Ship the portal")));
    QVERIFY(text.contains(QStringLiteral("Typed note")));
    QVERIFY(text.contains(QStringLiteral("☑ Call the vendor")));
    QVERIFY(text.contains(QStringLiteral("Handwriting")));
    QCOMPARE(parts.media.size(), 2); // the picture and the handwriting
    QCOMPARE(doc.begin().blockFormat().headingLevel(), 1);
    // A Markdown "##" is Word's Heading 2 at the size the page shows it
    // (Qt's ×1.5 step over the 11 pt body), in a face Word knows.
    const QTextBlock heading = doc.findBlock(text.indexOf(QStringLiteral("Decisions")));
    QCOMPARE(heading.blockFormat().headingLevel(), 2);
    QCOMPARE(heading.begin().fragment().charFormat().fontPointSize(), 16.5);
    QVERIFY(!package->data(package->mainPart()).contains("Sans Serif"));
    QCOMPARE(parts.properties.title, QStringLiteral("Weekly sync"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    NoteWordTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_note_word.moc"
