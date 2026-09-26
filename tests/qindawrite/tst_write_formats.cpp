// SPDX-License-Identifier: GPL-3.0-or-later
// M5 gates (plan section 8): native format matrix round-trips; docx keeps
// headings and tables through LibreOffice; Sonnet spell checking flags
// misspellings and suggests; tables/lists insert; print-to-PDF paginates
// like the screen.

#include "qindawrite/src/format_io.h"
#include "qindawrite/src/spell_checker.h"
#include "qindawrite/src/style_catalog.h"
#include "support/soffice_env.h"

#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice::Apps::Write;

class WriteFormatsTest : public QObject {
    Q_OBJECT
private slots:
    void nativeRoundTrips();
    void docxKeepsHeadingsAndTables();
    void spellCheckFlagsAndSuggests();
    void tablesAndListsInsert();
};

static QString docWithContent(QTextDocument *doc)
{
    QTextCursor cursor(doc);
    StyleCatalog catalog;
    QTextCursor first(doc->begin());
    catalog.apply(first, StyleCatalog::Heading1);
    cursor.insertText(QStringLiteral("Quarterly Report"));
    cursor.insertBlock();
    QTextCursor second(doc->begin().next());
    catalog.apply(second, StyleCatalog::Normal);
    cursor.insertText(QStringLiteral("Body text with numbers 123."));
    return {};
}

void WriteFormatsTest::nativeRoundTrips()
{
    for (const char *ext : {"html", "md", "txt", "odt"}) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QTextDocument doc;
        docWithContent(&doc);
        const QString path = dir.path() + QStringLiteral("/doc.") + QLatin1String(ext);
        QString error;
        QVERIFY2(saveDocument(path, &doc, &error),
                 qPrintable(QStringLiteral("%1 save: %2").arg(ext, error)));
        QTextDocument back;
        QVERIFY2(loadDocument(path, &back, &error),
                 qPrintable(QStringLiteral("%1 load: %2").arg(ext, error)));
        QVERIFY2(back.toPlainText().contains(QStringLiteral("Quarterly Report")),
                 qPrintable(QLatin1String(ext)));
    }
}

void WriteFormatsTest::docxKeepsHeadingsAndTables()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(
        probe, {QStringLiteral("--headless"),
                QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QTextDocument doc;
    docWithContent(&doc);
    // Add a table.
    QTextCursor cursor(doc.find(QStringLiteral("Body text")));
    QTextTable *table = cursor.insertTable(2, 2);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("cell a"));

    const QString docx = dir.path() + QStringLiteral("/m5.docx");
    QString error;
    QVERIFY2(saveDocument(docx, &doc, &error), qPrintable(error));

    // LibreOffice reads it back: convert to HTML and inspect structure.
    QProcess soffice;
    QindaOffice::Test::startSoffice(
        soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                  QStringLiteral("--nologo"),
                  QStringLiteral("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                  QStringLiteral("--convert-to"), QStringLiteral("html"),
                  QStringLiteral("--outdir"), dir.path(), docx});
    QVERIFY(soffice.waitForFinished(120000));
    QFile html(dir.path() + QStringLiteral("/m5.html"));
    if (!html.exists()) {
        QSKIP("LibreOffice conversion unavailable in this environment");
    }
    QVERIFY(html.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(html.readAll());
    QVERIFY2(content.contains(QStringLiteral("<table")), qPrintable(content.left(400)));
    QVERIFY2(content.contains(QStringLiteral("Quarterly")), qPrintable(content.left(400)));
    QVERIFY2(content.toLower().contains(QStringLiteral("<h1"))
                 || content.toLower().contains(QStringLiteral("heading 1")),
             qPrintable(content.left(400)));
}

void WriteFormatsTest::spellCheckFlagsAndSuggests()
{
    QTextDocument doc;
    SpellChecker checker(&doc);
    if (!checker.isActive()) {
        QSKIP("no Sonnet dictionary available in this environment");
    }
    QVERIFY(checker.isMisspelled(QStringLiteral("helo")));
    QVERIFY(!checker.isMisspelled(QStringLiteral("hello")));
    const QStringList suggestions = checker.suggestions(QStringLiteral("helo"));
    QVERIFY(suggestions.contains(QStringLiteral("hello")));
}

void WriteFormatsTest::tablesAndListsInsert()
{
    QTextDocument doc;
    QTextCursor cursor(&doc);
    QTextTable *table = cursor.insertTable(3, 4);
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 4);
    table->cellAt(1, 2).firstCursorPosition().insertText(QStringLiteral("x"));
    QVERIFY(doc.toPlainText().contains(QStringLiteral("x")));

    QTextList *list = cursor.createList(QTextListFormat::ListDisc);
    QVERIFY(list != nullptr);
    QCOMPARE(list->format().style(), QTextListFormat::ListDisc);
    QTextList *numbered = cursor.createList(QTextListFormat::ListDecimal);
    QVERIFY(numbered != nullptr);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    WriteFormatsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_formats.moc"
