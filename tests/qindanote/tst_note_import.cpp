// SPDX-License-Identifier: GPL-3.0-or-later
// M84 gates for File ▸ Import (D-093): a Word document becomes pages with
// its headings, lists, tables, pictures and formatting (through
// office_docx); Markdown and HTML become a page each, HTML sanitised; a
// OneNote .one file is refused with the way round.

#include "note_test_support.h"

#include "docx_model.h"
#include "docx_properties.h"
#include "docx_text.h"
#include "docx_writer.h"
#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_import.h"
#include "qindanote/src/note_rich_text.h"

#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_spans.h>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

using namespace NoteTest;
using namespace QindaOffice;
using namespace QindaOffice::Apps::Notes;
using namespace QindaTK::RichText;

namespace {

std::unique_ptr<QindaTK::RichTextDocument> docOf(const Import::ImportedPage &page)
{
    return RichBlock::forBlock(page.blocks.first().toObject());
}

const Paragraph *paragraphWith(const QindaTK::RichTextDocument &doc, const QString &text)
{
    for (const Paragraph *p : doc.paragraphsInOrder()) {
        if (p->text.contains(text)) {
            return p;
        }
    }
    return nullptr;
}

bool hasTable(const QindaTK::RichTextDocument &doc, const QString &cellText)
{
    for (const Block &block : doc.blocks()) {
        if (!block.table) {
            continue;
        }
        for (const TableRow &row : block.table->rows) {
            for (const TableCell &cell : row.cells) {
                for (const ParagraphPtr &p : cell.paragraphs) {
                    if (p->text == cellText) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool isBold(const QindaTK::RichTextDocument &doc, const Paragraph *p, const QString &word)
{
    const qsizetype at = p->text.indexOf(word);
    return at >= 0 && doc.charFormat(Spans::formatAt(p->spans, int(at))).weight >= 600;
}

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(bytes);
    return file.fileName();
}

// A report in Word: a preamble, then "Introduction" and "Details" as
// Heading 1, with a bold run, a picture, a Heading 2, a bulleted list and
// a table.
QString writeReport(const QTemporaryDir &dir)
{
    QTextDocument doc;
    Docx::WordParts parts;
    parts.properties.title = QStringLiteral("Board report");
    QTextCursor cursor(&doc);
    cursor.insertText(QStringLiteral("Prepared for the board"));
    const auto heading = [&](int level, const QString &text) {
        QTextBlockFormat format;
        format.setHeadingLevel(level);
        format.setProperty(Docx::kWriteStyleIdProperty, 1 + level);
        cursor.insertBlock(format, QTextCharFormat());
        cursor.insertText(text);
    };
    heading(1, QStringLiteral("Introduction"));
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.insertText(QStringLiteral("The "));
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    cursor.insertText(QStringLiteral("key point"), bold);
    cursor.insertText(QStringLiteral(" stands. "), QTextCharFormat());
    QImage picture(40, 20, QImage::Format_ARGB32);
    picture.fill(Qt::darkCyan);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    QTextImageFormat image;
    image.setName(Docx::addPicture(&doc, &parts.media, png));
    image.setWidth(40);
    image.setHeight(20);
    cursor.insertImage(image);
    heading(1, QStringLiteral("Details"));
    heading(2, QStringLiteral("Numbers"));
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextListFormat bullets;
    bullets.setStyle(QTextListFormat::ListDisc);
    cursor.createList(bullets);
    cursor.insertText(QStringLiteral("alpha"));
    cursor.insertBlock();
    cursor.insertText(QStringLiteral("beta"));
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextTable *table = cursor.insertTable(2, 2);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("Q1"));
    table->cellAt(1, 1).firstCursorPosition().insertText(QStringLiteral("Q2"));
    const QString path = dir.filePath(QStringLiteral("report.docx"));
    QString error;
    Docx::writeDocx(path, doc, parts, {}, &error);
    return path;
}

} // namespace

class NoteImportTest final : public QObject {
    Q_OBJECT

private slots:
    void wordDocumentBecomesPages()
    {
        QTemporaryDir dir;
        const QString path = writeReport(dir);
        QList<Import::ImportedPage> pages;
        QString error;
        QVERIFY2(Import::importFile(path, &pages, &error), qPrintable(error));
        // The preamble (named after the document), then a page per Heading 1.
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages.at(0).title, QStringLiteral("Board report"));
        QCOMPARE(pages.at(1).title, QStringLiteral("Introduction"));
        QCOMPARE(pages.at(2).title, QStringLiteral("Details"));
        const auto intro = docOf(pages.at(1));
        const Paragraph *point = paragraphWith(*intro, QStringLiteral("key point"));
        QVERIFY(point != nullptr);
        QVERIFY(isBold(*intro, point, QStringLiteral("key point")));
        QVERIFY(!isBold(*intro, point, QStringLiteral("stands")));
        // The picture travels inline with its own bytes.
        bool picture = false;
        for (const RichObject &object : intro->objects()) {
            picture = picture || object.source.startsWith(QLatin1String("data:image/"));
        }
        QVERIFY(picture);
        QVERIFY(paragraphWith(*intro, QStringLiteral("Introduction")) == nullptr); // it is the title
        const auto details = docOf(pages.at(2));
        QCOMPARE(paragraphWith(*details, QStringLiteral("Numbers"))->format.headingLevel, 2);
        QVERIFY(!paragraphWith(*details, QStringLiteral("alpha"))->format.listId.isEmpty());
        QCOMPARE(paragraphWith(*details, QStringLiteral("alpha"))->format.listId,
                 paragraphWith(*details, QStringLiteral("beta"))->format.listId);
        QVERIFY(hasTable(*details, QStringLiteral("Q1")));
        QVERIFY(hasTable(*details, QStringLiteral("Q2")));
    }

    void markdownAndHtmlBecomePages()
    {
        QTemporaryDir dir;
        const QString md = writeFile(dir, QStringLiteral("shopping.md"),
                                     "# Shopping\n\nSome **bold** text\n\n## Today\n\n- [ ] milk\n- [x] eggs\n\n"
                                     "| Item | Price |\n|---|---|\n| bread | 2 |\n");
        QList<Import::ImportedPage> pages;
        QString error;
        QVERIFY2(Import::importFile(md, &pages, &error), qPrintable(error));
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().title, QStringLiteral("Shopping"));
        const QJsonObject block = pages.first().blocks.first().toObject();
        const QJsonArray tags = block.value(QStringLiteral("tags")).toArray();
        QCOMPARE(tags.size(), 2);
        QVERIFY(!tags.at(0).toObject().value(QStringLiteral("done")).toBool());
        QVERIFY(tags.at(1).toObject().value(QStringLiteral("done")).toBool());
        auto doc = docOf(pages.first());
        QVERIFY(isBold(*doc, paragraphWith(*doc, QStringLiteral("bold")), QStringLiteral("bold")));
        QCOMPARE(paragraphWith(*doc, QStringLiteral("Today"))->format.headingLevel, 2);
        QVERIFY(paragraphWith(*doc, QStringLiteral("milk"))->format.listId.isEmpty()); // a box, not a bullet
        QVERIFY(hasTable(*doc, QStringLiteral("bread")));

        const QString html = writeFile(dir, QStringLiteral("article.html"),
                                       "<html><head><title>An article</title><script>alert('x')</script></head><body>"
                                       "<h1>Story</h1><p onclick=\"steal()\">It is <b>bold</b>. "
                                       "<a href=\"javascript:steal()\">bad</a> <a href=\"https://example.org\">good</a>"
                                       "<img src=\"https://tracker.example/p.png\" alt=\"\"></p>"
                                       "<table><tr><td>cell</td></tr></table></body></html>");
        pages.clear();
        QVERIFY2(Import::importFile(html, &pages, &error), qPrintable(error));
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().title, QStringLiteral("An article"));
        doc = docOf(pages.first());
        const QString plain = RichBlock::plainText(*doc);
        QVERIFY2(!plain.contains(QStringLiteral("alert")), qPrintable(plain));
        QVERIFY(doc->objects().isEmpty()); // the remote picture is not fetched or kept
        QVERIFY(isBold(*doc, paragraphWith(*doc, QStringLiteral("bold")), QStringLiteral("bold")));
        QCOMPARE(paragraphWith(*doc, QStringLiteral("Story"))->format.headingLevel, 1);
        QVERIFY(hasTable(*doc, QStringLiteral("cell")));
        const Paragraph *links = paragraphWith(*doc, QStringLiteral("bad"));
        QVERIFY(doc->charFormat(Spans::formatAt(links->spans, int(links->text.indexOf(QStringLiteral("bad"))))).href.isEmpty());
        QCOMPARE(doc->charFormat(Spans::formatAt(links->spans, int(links->text.indexOf(QStringLiteral("good"))))).href,
                 QStringLiteral("https://example.org"));
    }

    void oneNoteFilesAreRefusedPlainly()
    {
        QTemporaryDir dir;
        const QString one = writeFile(dir, QStringLiteral("Section.one"), QByteArray("\xe4\x52\x5c\x7b", 4));
        QList<Import::ImportedPage> pages;
        QString error;
        QVERIFY(!Import::importFile(one, &pages, &error));
        QVERIFY2(error.contains(QStringLiteral("MS-ONESTORE")) && error.contains(QStringLiteral("Word")), qPrintable(error));
        NoteHost host;
        QSignalSpy told(&host, &NoteHost::messageRequested);
        QVERIFY(!host.importFile(QUrl::fromLocalFile(one)));
        QCOMPARE(told.count(), 1);
        QVERIFY(told.first().at(1).toString().contains(QStringLiteral("MS-ONESTORE")));
    }

    void importInsertsPagesAfterTheOpenOneInOrder()
    {
        QTemporaryDir dir;
        const QString path = writeReport(dir);
        NoteHost host;
        const QString start = host.currentPageId();
        QVERIFY(host.importFile(QUrl::fromLocalFile(path)));
        const NoteSection *section = host.currentSection();
        QStringList titles;
        for (const NotePage &page : section->pages) {
            titles.append(page.title);
        }
        QCOMPARE(titles.size(), 4);
        QCOMPARE(section->pages.first().id, start);
        QCOMPARE(titles.mid(1), (QStringList{QStringLiteral("Board report"), QStringLiteral("Introduction"),
                                             QStringLiteral("Details")}));
        QCOMPARE(host.currentPageTitle(), QStringLiteral("Board report"));
    }

    // Page polish: templates, page colour and subpages, through the
    // commands and the notebook's files.
    void templatesColoursAndSubpages()
    {
        QTemporaryDir dir;
        NoteHost host;
        QString error;
        QVERIFY2(host.noteDocument()->save(dir.filePath(QStringLiteral("Book")), &error), qPrintable(error));
        host.actions()->trigger(QStringLiteral("notePageTemplate_meeting"));
        QVERIFY(host.currentPageTitle().startsWith(QStringLiteral("Meeting notes")));
        const QJsonObject meeting = blocksArray(host.currentPage()->envelope.blocksJson).first().toObject();
        QCOMPARE(meeting.value(QStringLiteral("tags")).toArray().size(), 2); // the action items
        QVERIFY(meeting.value(QStringLiteral("text")).toString().contains(QStringLiteral("Agenda")));
        host.actions()->trigger(QStringLiteral("notePageTemplate_lined"));
        QCOMPARE(host.currentPage()->envelope.background, QStringLiteral("ruled"));
        host.actions()->trigger(QStringLiteral("notePageColor_yellow"));
        QCOMPARE(pageColor(host.currentPage()->envelope.blocksJson), QColor(0xff, 0xf8, 0xd6));
        QVERIFY(host.actions()->entry(QStringLiteral("notePageColor_yellow"))->isChecked());
        // The lined page hangs under the meeting page; not two levels at once.
        host.actions()->trigger(QStringLiteral("noteMakeSubpageAction"));
        QCOMPARE(host.currentPage()->level, 1);
        QVERIFY(!host.actions()->entry(QStringLiteral("noteMakeSubpageAction"))->isEnabled());
        const QString sub = host.currentPageId();
        QVERIFY(host.noteDocument()->save(dir.filePath(QStringLiteral("Book")), &error));
        Notebook reopened;
        QVERIFY(reopened.load(dir.filePath(QStringLiteral("Book")), &error));
        const NotePage *page = reopened.pageById(host.currentSectionId(), sub);
        QVERIFY(page != nullptr);
        QCOMPARE(page->level, 1);
        host.actions()->trigger(QStringLiteral("notePromoteSubpageAction"));
        QCOMPARE(host.currentPage()->level, 0);
    }
};

NOTE_TEST_MAIN(NoteImportTest)

#include "tst_note_import.moc"
