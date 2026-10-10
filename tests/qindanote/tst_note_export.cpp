// SPDX-License-Identifier: GPL-3.0-or-later
// M84 gates (D-093): a formatted, tagged page in the exports — Word (read
// back through office_docx), PDF (rendered back through Poppler) and
// Markdown (read back through the Markdown import).

#include "note_rich_fixture.h"

#include "docx_model.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "ooxml_package.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_page_render.h"
#include "qindanote/src/note_rich_export.h"
#include "qindanote/src/note_rich_import.h"
#include "qindanote/src/note_rich_style.h"
#include "qindanote/src/note_rich_text.h"
#include "qindanote/src/note_tags.h"
#include "qindanote/src/note_word_export.h"
#include "qindanote/src/notebook.h"
#include "support/ooxml_schema.h"

#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_spans.h>

#include <poppler-qt6.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;
using namespace QindaOffice::Apps::Notes;
using namespace QindaTK::RichText;

namespace {

using NoteFixture::kMark;
using NoteFixture::kRed;

// The formatted, tagged text block and a tagged checklist item.
NotePage formattedPage()
{
    NotePage page;
    page.title = QStringLiteral("Formatted");
    page.loaded = true;
    page.envelope.blocksJson = withBlocksArray(
        QByteArray("{}"), QJsonArray{NoteFixture::formattedTextBlock(), NoteFixture::taggedCheckBlock()});
    return page;
}

QTextBlock blockWith(const QTextDocument &doc, const QString &text)
{
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        if (block.text().contains(text)) {
            return block;
        }
    }
    return {};
}

QTextCharFormat charAt(const QTextDocument &doc, const QString &word)
{
    const QTextBlock block = blockWith(doc, word);
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        if (it.fragment().text().contains(word)) {
            return it.fragment().charFormat();
        }
    }
    return {};
}

bool hasColor(const QImage &image, const QColor &wanted, int tolerance)
{
    for (int y = 0; y < image.height(); y += 1) {
        for (int x = 0; x < image.width(); x += 1) {
            const QColor c = image.pixelColor(x, y);
            if (qAbs(c.red() - wanted.red()) <= tolerance && qAbs(c.green() - wanted.green()) <= tolerance
                && qAbs(c.blue() - wanted.blue()) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

class NoteExportTest : public QObject {
    Q_OBJECT
private slots:
    void formattingReachesWord()
    {
        const NotePage page = formattedPage();
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("formatted.docx"));
        QString error;
        QVERIFY2(exportPageAsWord(page, 760, path, &error), qPrintable(error));
        if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
            QFile::remove(grabs + QStringLiteral("/qindanote_formatted.docx"));
            QFile::copy(path, grabs + QStringLiteral("/qindanote_formatted.docx"));
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
        QVERIFY(charAt(doc, QStringLiteral("Bold")).fontWeight() >= QFont::Bold);
        QVERIFY(charAt(doc, QStringLiteral("Italic")).fontItalic());
        QVERIFY(charAt(doc, QStringLiteral("Under")).fontUnderline());
        QVERIFY(charAt(doc, QStringLiteral("Strike")).fontStrikeOut());
        QCOMPARE(charAt(doc, QStringLiteral("Super")).verticalAlignment(), QTextCharFormat::AlignSuperScript);
        QCOMPARE(charAt(doc, QStringLiteral("Sub")).verticalAlignment(), QTextCharFormat::AlignSubScript);
        QCOMPARE(charAt(doc, QStringLiteral("Red")).foreground().color(), kRed);
        QVERIFY(charAt(doc, QStringLiteral("Mark")).hasProperty(QTextFormat::BackgroundBrush));
        QCOMPARE(charAt(doc, QStringLiteral("Link")).anchorHref(), QStringLiteral("https://example.org/"));
        QCOMPARE(blockWith(doc, QStringLiteral("Heading two")).blockFormat().headingLevel(), 2);
        QCOMPARE(blockWith(doc, QStringLiteral("A title")).blockFormat().property(Docx::kParagraphStyleId).toString(),
                 QStringLiteral("Title"));
        QCOMPARE(blockWith(doc, QStringLiteral("A quote")).blockFormat().property(Docx::kParagraphStyleId).toString(),
                 QStringLiteral("Quote"));
        QCOMPARE(blockWith(doc, QStringLiteral("some_code()")).blockFormat().property(Docx::kParagraphStyleId).toString(),
                 QStringLiteral("Code"));
        QVERIFY(blockWith(doc, QStringLiteral("Bullet")).textList() != nullptr);
        QVERIFY(blockWith(doc, QStringLiteral("Number")).textList() != nullptr);
        QVERIFY(blockWith(doc, QStringLiteral("Bullet")).textList() != blockWith(doc, QStringLiteral("Number")).textList());
        // Tags: To Do as boxes, the others as their icons (pictures).
        QVERIFY(blockWith(doc, QStringLiteral("Call the bank")).text().startsWith(QStringLiteral("☐ ")));
        QVERIFY(blockWith(doc, QStringLiteral("Pay the rent")).text().startsWith(QStringLiteral("☑ ")));
        QVERIFY(blockWith(doc, QStringLiteral("Heading two")).text().contains(QChar::ObjectReplacementCharacter));
        QVERIFY(blockWith(doc, QStringLiteral("Send the report")).text().contains(QChar::ObjectReplacementCharacter));
        QVERIFY(parts.media.size() >= 2); // Question's and Important's icons
        // The table, with its cells.
        QTextTable *table = nullptr;
        for (QTextFrame *frame : doc.rootFrame()->childFrames()) {
            table = table != nullptr ? table : qobject_cast<QTextTable *>(frame);
        }
        QVERIFY(table != nullptr);
        QCOMPARE(table->rows(), 2);
        QCOMPARE(table->cellAt(1, 1).firstCursorPosition().block().text(), QStringLiteral("Cell"));
    }

    void formattingReachesPdf()
    {
        const NotePage page = formattedPage();
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("formatted.pdf"));
        {
            // The print and Export PDF path: the one page renderer.
            QPdfWriter writer(path);
            writer.setResolution(96);
            QPainter painter(&writer);
            const qreal scale = qMin(writer.width() / 760.0, writer.height() / 1004.0);
            painter.scale(scale, scale);
            renderNotePage(&painter, page, QSizeF(760, 1004));
        }
        const std::unique_ptr<Poppler::Document> pdf = Poppler::Document::load(path);
        QVERIFY(pdf != nullptr);
        const std::unique_ptr<Poppler::Page> first = pdf->page(0);
        QVERIFY(first != nullptr);
        const QString text = first->text(QRectF());
        QVERIFY2(text.contains(QStringLiteral("Bold")) && text.contains(QStringLiteral("Heading two")), qPrintable(text));
        const QImage image = first->renderToImage(150, 150);
        QVERIFY(!image.isNull());
        if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
            image.save(grabs + QStringLiteral("/qindanote_formatted_pdf.png"));
        }
        QVERIFY2(hasColor(image, kRed, 40), "the red run is red in the PDF");
        QVERIFY2(hasColor(image, kMark, 20), "the highlight is in the PDF");
        QVERIFY2(hasColor(image, Tags::byId(QStringLiteral("todo")).color, 40), "the To Do boxes are in the PDF");
    }

    void markdownExportReadsBack()
    {
        const NotePage page = formattedPage();
        const QJsonObject text = blocksArray(page.envelope.blocksJson).first().toObject();
        const QString md = RichExport::markdown(text);
        for (const char *expected : {"**Bold**", "*Italic*", "++Under++", "~~Strike~~", "^Super^", "~Sub~",
                                     "[Link](https://example.org/)", "==Mark==", "# A title", "## Heading two",
                                     "> A quote", "```\nsome_code()\n```", "- Bullet", "1. Number",
                                     "- [ ] Call the bank", "- [x] Pay the rent", "[#Question]", "| Head |"}) {
            QVERIFY2(md.contains(QString::fromUtf8(expected)), qPrintable(QString::fromUtf8(expected) + QLatin1Char('\n') + md));
        }
        // And back through the Markdown import, with its To Do boxes.
        const RichImport::Imported back = RichImport::fromMarkdown(md);
        QVERIFY(back.doc != nullptr);
        const QString plain = RichBlock::plainText(*back.doc);
        QVERIFY2(plain.contains(QStringLiteral("Heading two")), qPrintable(plain));
        int todos = 0;
        int done = 0;
        for (const QJsonValue &value : back.tags) {
            todos += value.toObject().value(QStringLiteral("tag")).toString() == QLatin1String("todo");
            done += value.toObject().value(QStringLiteral("done")).toBool();
        }
        QCOMPARE(todos, 2);
        QCOMPARE(done, 1);
        bool bold = false;
        bool heading = false;
        bool table = false;
        for (const Block &block : back.doc->blocks()) {
            table = table || block.table != nullptr;
            if (!block.paragraph) {
                continue;
            }
            const Paragraph &p = *block.paragraph;
            heading = heading || (p.format.headingLevel == 2 && p.text.contains(QStringLiteral("Heading two")));
            const qsizetype b = p.text.indexOf(QStringLiteral("Bold"));
            bold = bold || (b >= 0 && back.doc->charFormat(Spans::formatAt(p.spans, int(b))).weight >= 600);
        }
        QVERIFY(bold);
        QVERIFY(heading);
        QVERIFY(table);
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QGuiApplication app(argc, argv);
    NoteExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_note_export.moc"
