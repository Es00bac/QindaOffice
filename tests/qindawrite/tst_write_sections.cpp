// SPDX-License-Identifier: GPL-3.0-or-later
// M88 gates for Word sections in QindaWrite (D-096), through the real
// window: a section break inserted at the caret and one section turned
// landscape (each page its section's size, a narrower section's lines at
// its own width, a shorter section's text inside its own text area, Undo
// of each), PDF export with each page's own size, headers per section with
// Link to Previous, a page-number restart in roman numerals, Word's keep
// rules inside a shorter section, a Word document with portrait /
// landscape / portrait sections read, laid out (table rows broken at the
// landscape page's foot) and saved, and grabs of it at 1280 and 520 px
// (QQO_GRAB_DIR).

#include "write_test_support.h"

#include "docx_properties.h"
#include "docx_sections.h"
#include "docx_text.h"
#include "office_docx/docx_sections_fixture.h"
#include "qindawrite/src/page_story_editor.h"
#include "qindawrite/src/pdf_exporter.h"
#include "qindawrite/src/write_pagination.h"
#include "qindawrite/src/write_section_commands.h"
#include "qindawrite/src/write_sections.h"
#include "support/quick_dump.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextTable>

#include <poppler-qt6.h>

#include <cmath>

using namespace QindaOffice;
using Apps::Write::PageFrame;
using Apps::Write::StoryEditor;
using Apps::Write::WriteDocument;
using WriteTest::Scene;

namespace {

const QSizeF kPortrait(794, 1123); // the new document's A4
const QSizeF kLandscape(1123, 794);

void fill(Scene &scene, int paragraphs, const QString &prefix)
{
    QTextCursor cursor(scene.doc->textDocument());
    cursor.movePosition(QTextCursor::End);
    for (int i = 0; i < paragraphs; ++i) {
        if (i > 0 || !scene.doc->textDocument()->isEmpty()) {
            cursor.insertBlock();
        }
        cursor.insertText(QStringLiteral("%1 paragraph %2: words enough to run over one line of a portrait page, "
                                         "and to show where a landscape section wraps them.")
                              .arg(prefix)
                              .arg(i + 1));
    }
}

void caretAt(Scene &scene, int blockNumber)
{
    QTextCursor cursor(scene.doc->textDocument()->findBlockByNumber(blockNumber));
    scene.control()->setTextCursor(cursor);
}

bool trigger(Scene &scene, const char *id)
{
    return scene.host.actions()->trigger(QString::fromLatin1(id));
}

void settle(Scene &scene)
{
    scene.doc->keepRules()->applyAll();
    QCoreApplication::processEvents();
}

QList<QSizeF> pageSizes(const Scene &scene)
{
    QList<QSizeF> out;
    for (const PageFrame &frame : scene.doc->pageFrames()) {
        out.append(frame.size);
    }
    return out;
}

// Every line of `block`: its top and bottom on the canvas and its page.
struct Line {
    int page;
    qreal top;
    qreal bottom;
    qreal width;
};

QList<Line> linesOf(const QTextDocument &doc, const QTextBlock &block)
{
    QList<Line> out;
    const qreal origin = doc.documentLayout()->blockBoundingRect(block).top();
    const qreal height = doc.pageSize().height();
    for (int i = 0; block.layout() != nullptr && i < block.layout()->lineCount(); ++i) {
        const QTextLine line = block.layout()->lineAt(i);
        const qreal top = origin + line.y();
        out.append({int(std::floor((top + line.height() / 2) / height)), top, top + line.height(),
                    line.naturalTextWidth()});
    }
    return out;
}

// Every line of the body below its page's text area (none expected).
QStringList overflows(const Scene &scene)
{
    QStringList bad;
    const QTextDocument &doc = *scene.doc->textDocument();
    const QList<PageFrame> &frames = scene.doc->pageFrames();
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (const Line &line : linesOf(doc, block)) {
            if (line.page < 0 || line.page >= frames.size()) {
                continue;
            }
            const PageFrame &frame = frames.at(line.page);
            const qreal bottom = frame.textBottom > 0 ? frame.textBottom : frame.size.height();
            if (line.bottom - frame.origin.y() > bottom + 0.5) {
                bad.append(QStringLiteral("'%1' on page %2").arg(block.text().left(24)).arg(line.page + 1));
            }
        }
    }
    return bad;
}

// Where the document's first table starts.
int firstTable(const Scene &scene)
{
    for (QTextFrame *frame : scene.doc->textDocument()->rootFrame()->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            return table->firstPosition();
        }
    }
    return 0;
}

// A two-section document: the caret's section turned landscape (two
// pages or more of each).
void portraitThenLandscape(Scene &scene)
{
    fill(scene, 60, QStringLiteral("Portrait"));
    fill(scene, 80, QStringLiteral("Landscape"));
    caretAt(scene, 60);
    QVERIFY(trigger(scene, "writeSectionBreakNextPageAction"));
    QVERIFY(trigger(scene, "writeOrientationLandscapeAction"));
    settle(scene);
}

} // namespace

class WriteSectionsTest : public QObject {
    Q_OBJECT
private slots:
    void sectionBreakAndLandscapeWithUndo();
    void pdfPagesTakeTheirSectionsSize();
    void headersPerSectionWithLinkToPrevious();
    void pageNumbersRestartInRomanNumerals();
    void keepRulesHoldInsideAShorterSection();
    void wordDocumentWithSections();
    void grabs();
};

void WriteSectionsTest::sectionBreakAndLandscapeWithUndo()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    fill(scene, 60, QStringLiteral("Portrait"));
    fill(scene, 80, QStringLiteral("Landscape"));
    settle(scene);
    const int onePageSize = int(scene.doc->pageFrames().size());
    caretAt(scene, 60);
    QVERIFY(trigger(scene, "writeSectionBreakNextPageAction"));
    QCOMPARE(scene.doc->sections().count(), 2);
    // The part before the caret ended a section; the caret starts the next.
    QCOMPARE(scene.doc->sectionAt(scene.control()->textCursor().position()), 1);
    QVERIFY(trigger(scene, "writeOrientationLandscapeAction"));
    settle(scene);
    const Apps::Write::SectionLayout &sections = scene.doc->sections();
    QCOMPARE(sections.sections().at(0).pageSize(), kPortrait);
    QCOMPARE(sections.sections().at(1).pageSize(), kLandscape);
    QVERIFY(scene.host.actions()->entry(QStringLiteral("writeOrientationLandscapeAction"))->isChecked());

    // Each page its section's size: portrait pages, then landscape ones.
    const QList<QSizeF> sizes = pageSizes(scene);
    QVERIFY(sizes.size() >= 4);
    QCOMPARE(sizes.first(), kPortrait);
    QCOMPARE(sizes.last(), kLandscape);
    const qsizetype firstWide = sizes.indexOf(kLandscape);
    QVERIFY(firstWide >= 2 && sizes.size() - firstWide >= 2); // two pages or more of each
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        QCOMPARE(sizes.at(i), i < firstWide ? kPortrait : kLandscape);
    }
    QCOMPARE(scene.page->pageCount(), int(sizes.size()));
    // A narrower section's lines wrap at its own width, the wider one's at its.
    const QTextDocument &doc = *scene.doc->textDocument();
    const qreal portraitWidth = sections.sections().at(0).textWidth();
    const qreal landscapeWidth = sections.sections().at(1).textWidth();
    QVERIFY(landscapeWidth > portraitWidth + 100);
    bool wide = false;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const int section = scene.doc->sectionAt(block.position());
        for (const Line &line : linesOf(doc, block)) {
            QVERIFY(line.width <= (section == 0 ? portraitWidth : landscapeWidth) + 0.5);
            wide = wide || line.width > portraitWidth + 1;
        }
    }
    QVERIFY(wide);
    QVERIFY2(overflows(scene).isEmpty(), qPrintable(overflows(scene).join(QStringLiteral(", "))));
    // The section layout's pads are display only: Word sees no indents.
    QCOMPARE(Docx::ownRightMargin(doc.begin().blockFormat()), 0.0);
    QVERIFY(doc.begin().blockFormat().rightMargin() > 300);

    // Undo the landscape, then the break.
    scene.control()->undo();
    settle(scene);
    QCOMPARE(scene.doc->sections().count(), 2);
    QCOMPARE(scene.doc->sections().sections().at(1).pageSize(), kPortrait);
    for (const QSizeF &size : pageSizes(scene)) {
        QCOMPARE(size, kPortrait);
    }
    scene.control()->undo();
    settle(scene);
    QCOMPARE(scene.doc->sections().count(), 1);
    QCOMPARE(int(scene.doc->pageFrames().size()), onePageSize);
    QCOMPARE(doc.begin().blockFormat().rightMargin(), 0.0);
}

void WriteSectionsTest::pdfPagesTakeTheirSectionsSize()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    portraitThenLandscape(scene);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("sections.pdf"));
    QString error;
    QVERIFY2(Apps::Write::exportDocumentPdf(scene.doc->textDocument(), path, &error, scene.doc->decoration()),
             qPrintable(error));
    const std::unique_ptr<Poppler::Document> pdf = Poppler::Document::load(path);
    QVERIFY(pdf != nullptr);
    const QList<QSizeF> sizes = pageSizes(scene);
    QCOMPARE(pdf->numPages(), int(sizes.size()));
    QString exportedText;
    for (int i = 0; i < pdf->numPages(); ++i) {
        const std::unique_ptr<Poppler::Page> page = pdf->page(i);
        QVERIFY(page != nullptr);
        const QSizeF points = page->pageSizeF();
        QVERIFY2(qAbs(points.width() - sizes.at(i).width() * 0.75) < 1
                     && qAbs(points.height() - sizes.at(i).height() * 0.75) < 1,
                 qPrintable(QStringLiteral("page %1: %2 x %3 pt").arg(i + 1).arg(points.width()).arg(points.height())));
        const QString text = page->text(QRectF());
        QVERIFY(!text.contains(sizes.at(i) == kPortrait ? QStringLiteral("Landscape paragraph")
                                                       : QStringLiteral("Portrait paragraph")));
        exportedText += text;
    }
    // Every source paragraph reaches exactly one PDF page: per-page
    // clipping must neither hide text nor repeat the previous canvas page.
    for (const auto &[label, count] : {std::pair{QStringLiteral("Portrait"), 60},
                                      std::pair{QStringLiteral("Landscape"), 80}}) {
        for (int n = 1; n <= count; ++n) {
            QCOMPARE(exportedText.count(QStringLiteral("%1 paragraph %2:").arg(label).arg(n)), 1);
        }
    }
    if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
        const QString kept = grabs + QStringLiteral("/qindawrite_m88_sections.pdf");
        QFile::remove(kept);
        QFile::copy(path, kept);
    }
}

void WriteSectionsTest::headersPerSectionWithLinkToPrevious()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    portraitThenLandscape(scene);
    // A header on the first page: the second section links to it.
    caretAt(scene, 0);
    scene.page->editHeader();
    QVERIFY(scene.page->isEditingStory());
    QCOMPARE(scene.page->storyLabel(), QStringLiteral("Header -Section 1-"));
    scene.control()->insertText(QStringLiteral("Report"));
    scene.page->closeStory();
    settle(scene);
    const QList<PageFrame> frames = scene.doc->pageFrames();
    QCOMPARE(frames.first().header->toPlainText(), QStringLiteral("Report"));
    QCOMPARE(frames.last().header, frames.first().header);

    // On the landscape section's page: "Same as Previous"; Link to Previous off.
    caretAt(scene, scene.doc->textDocument()->blockCount() - 1);
    scene.page->editHeader();
    QCOMPARE(scene.page->storyLabel(), QStringLiteral("Header -Section 2-  Same as Previous"));
    Quick::ActionEntry *link = scene.host.actions()->entry(QStringLiteral("writeLinkToPreviousAction"));
    QVERIFY(link->isEnabled());
    QVERIFY(link->isChecked());
    QVERIFY(trigger(scene, "writeLinkToPreviousAction"));
    QVERIFY(scene.page->isEditingStory());
    QVERIFY(!link->isChecked());
    QCOMPARE(scene.page->storyLabel(), QStringLiteral("Header -Section 2-"));
    QCOMPARE(scene.control()->document()->toPlainText(), QStringLiteral("Report")); // Word copies it
    scene.control()->selectAll();
    scene.control()->insertText(QStringLiteral("Appendix"));
    scene.page->closeStory();
    settle(scene);
    QCOMPARE(scene.doc->pageFrames().first().header->toPlainText(), QStringLiteral("Report"));
    QCOMPARE(scene.doc->pageFrames().last().header->toPlainText(), QStringLiteral("Appendix"));

    // Saved as each section's own header.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("headers.docx"));
    QString error;
    QVERIFY2(scene.doc->save(path, &error), qPrintable(error));
    WriteDocument again;
    QVERIFY2(again.load(path, &error), qPrintable(error));
    QCOMPARE(again.sections().count(), 2);
    QCOMPARE(again.pageFrames().first().header->toPlainText(), QStringLiteral("Report"));
    QCOMPARE(again.pageFrames().last().header->toPlainText(), QStringLiteral("Appendix"));
    QVERIFY(!again.linkedToPrevious(1, false, Docx::HeaderFooterSlot::Default));

    // Undo links the section to the previous one again.
    scene.control()->undo();
    settle(scene);
    QVERIFY(scene.doc->linkedToPrevious(1, false, Docx::HeaderFooterSlot::Default));
    QCOMPARE(scene.doc->pageFrames().last().header->toPlainText(), QStringLiteral("Report"));
}

void WriteSectionsTest::pageNumbersRestartInRomanNumerals()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    portraitThenLandscape(scene);
    scene.doc->setHeaderFooter(QString(), QStringLiteral("Page {page} of {pages}"), 0);
    caretAt(scene, scene.doc->textDocument()->blockCount() - 1);
    QSignalSpy asked(scene.host.sectionsController(), &Apps::Write::SectionsController::pageNumbersRequested);
    QVERIFY(trigger(scene, "writePageNumberFormatAction"));
    QCOMPARE(asked.size(), 1);
    QVERIFY(scene.host.sectionsController()->applyPageNumbers(
        {{QStringLiteral("format"), QStringLiteral("lowerRoman")}, {QStringLiteral("start"), 1}}));
    settle(scene);
    const QList<PageFrame> frames = scene.doc->pageFrames();
    int firstWide = -1;
    for (int i = 0; i < frames.size(); ++i) {
        if (frames.at(i).section == 1 && firstWide < 0) {
            firstWide = i;
        }
        QCOMPARE(frames.at(i).number, frames.at(i).section == 0 ? i + 1 : i - firstWide + 1);
    }
    QVERIFY(firstWide > 0);
    const PageFrame &wide = frames.at(firstWide);
    QCOMPARE(wide.numberFormat, QStringLiteral("lowerRoman"));
    const auto footer = Docx::instantiateHeaderFooter(
        *wide.footer, Docx::PageFieldValues{wide.number, wide.numberFormat, int(frames.size()), wide.sectionPages});
    QCOMPARE(footer->toPlainText(), QStringLiteral("Page i of %1").arg(frames.size()));
    QCOMPARE(scene.host.sectionsController()->pageNumberInfo().value(QStringLiteral("start")).toInt(), 1);

    // Undo: the numbers continue from the first section again.
    scene.control()->undo();
    settle(scene);
    QCOMPARE(scene.doc->pageFrames().at(firstWide).number, firstWide + 1);
    QVERIFY(scene.doc->pageFrames().at(firstWide).numberFormat.isEmpty());
}

void WriteSectionsTest::keepRulesHoldInsideAShorterSection()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    fill(scene, 6, QStringLiteral("Portrait"));
    // A landscape section of headings (kept with their next paragraph) and
    // two-line paragraphs (moved whole rather than split).
    QTextCursor cursor(scene.doc->textDocument());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat heading;
    heading.setProperty(Docx::kKeepWithNext, true);
    heading.setHeadingLevel(2);
    for (int i = 0; i < 24; ++i) {
        cursor.insertBlock(heading);
        cursor.insertText(QStringLiteral("Heading %1").arg(i + 1));
        cursor.insertBlock(QTextBlockFormat());
        cursor.insertText(QStringLiteral("Body %1 runs over two lines of a landscape page with words, words, words, "
                                         "and words enough for the second line: more words, words, words and words "
                                         "until it wraps once more.")
                              .arg(i + 1));
    }
    caretAt(scene, 6);
    QVERIFY(trigger(scene, "writeSectionBreakNextPageAction"));
    QVERIFY(trigger(scene, "writeOrientationLandscapeAction"));
    settle(scene);
    QVERIFY(scene.doc->pageFrames().size() >= 3);
    const QTextDocument &doc = *scene.doc->textDocument();
    int moved = 0;
    for (QTextBlock block = doc.findBlockByNumber(7); block.isValid(); block = block.next()) {
        const QList<Line> lines = linesOf(doc, block);
        if (block.blockFormat().boolProperty(Docx::kKeepWithNext) && block.next().isValid()) {
            QCOMPARE(lines.last().page, linesOf(doc, block.next()).first().page); // with its next paragraph
        } else if (lines.size() == 2) {
            QCOMPARE(lines.first().page, lines.last().page); // not split one and one
        }
        moved += block.blockFormat().hasProperty(Docx::kSyntheticBreak) ? 1 : 0;
    }
    QVERIFY(moved > 0);
    QVERIFY2(overflows(scene).isEmpty(), qPrintable(overflows(scene).join(QStringLiteral(", "))));
}

void WriteSectionsTest::wordDocumentWithSections()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("mixed.docx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(Test::mixedDocument());
    file.close();
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(scene.host.loadPath(path));
    settle(scene);
    QCOMPARE(scene.doc->sections().count(), 3);
    const QList<QSizeF> sizes = pageSizes(scene);
    const QSizeF letter(816, 1056);
    const QSizeF wide(1056, 816);
    QCOMPARE(sizes.first(), letter);
    const qsizetype firstWide = sizes.indexOf(wide);
    const qsizetype back = sizes.indexOf(letter, firstWide);
    QVERIFY2(firstWide > 0 && back > firstWide + 1, qPrintable(QString::number(sizes.size())));
    // The 80 table rows broke at the landscape page's foot, not the canvas's.
    QVERIFY2(overflows(scene).isEmpty(), qPrintable(overflows(scene).join(QStringLiteral(", "))));
    QVERIFY(firstTable(scene) > 0);
    QVERIFY(Apps::Write::KeepRules::count(scene.doc->textDocument()) > 0);
    // Headers: the title page shows none, the landscape pages theirs, the
    // last section links to it; roman numbers, then a restart at 1.
    const QList<PageFrame> frames = scene.doc->pageFrames();
    QVERIFY(frames.first().header == nullptr || frames.first().header->isEmpty());
    QCOMPARE(frames.at(1).header->toPlainText(), QStringLiteral("Report header"));
    QCOMPARE(frames.at(firstWide).header->toPlainText(), QStringLiteral("Landscape appendix"));
    QCOMPARE(frames.last().header->toPlainText(), QStringLiteral("Landscape appendix"));
    QCOMPARE(frames.at(1).numberFormat, QStringLiteral("lowerRoman"));
    QCOMPARE(frames.at(firstWide).number, 1);
    // Saved and read back the same.
    const QString saved = dir.filePath(QStringLiteral("saved.docx"));
    QString error;
    QVERIFY2(scene.doc->save(saved, &error), qPrintable(error));
    WriteDocument again;
    QVERIFY2(again.load(saved, &error), qPrintable(error));
    QCOMPARE(again.sections().count(), 3);
    for (int i = 0; i < 3; ++i) {
        QVERIFY(again.sections().sections().at(i).ref.settings == scene.doc->sections().sections().at(i).ref.settings);
    }
}

void WriteSectionsTest::grabs()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("mixed.docx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(Test::mixedDocument());
    file.close();
    for (const int width : {1280, 520}) {
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(path));
        settle(scene);
        // Show the portrait/landscape boundary. An oversized ensureVisible
        // rectangle aligns its far end and accidentally showed two landscape
        // pages in earlier captures, providing no mixed-orientation evidence.
        const QList<PageFrame> frames = scene.doc->pageFrames();
        int wide = 0;
        while (wide < frames.size() && frames.at(wide).section != 1) {
            ++wide;
        }
        QQuickItem *viewport = scene.viewport();
        QMetaObject::invokeMethod(viewport, "setZoom", Q_ARG(QVariant, width == 520 ? 0.4 : 0.6),
                                  Q_ARG(QVariant, QVariant()));
        QTest::qWait(100);
        const QRectF page = scene.page->geometry()->pageRect(wide - 1, scene.page->width());
        QMetaObject::invokeMethod(viewport, "ensureVisible",
                                  Q_ARG(QVariant, QRectF(page.left(), page.bottom() - 120, page.width(), 240)));
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        ribbon->setProperty("currentTabId", QStringLiteral("layout"));
        QTest::qWait(150);
        Test::saveGrab(scene.window, QStringLiteral("qindawrite_m88_sections_%1").arg(width));
        // A header open on the landscape page: Word's Header & Footer tab.
        caretAt(scene, scene.doc->textDocument()->findBlock(firstTable(scene)).blockNumber());
        scene.page->editHeader();
        QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("headerFooter")));
        ribbon->setProperty("currentTabId", QStringLiteral("headerFooterTools"));
        QTest::qWait(150);
        Test::saveGrab(scene.window, QStringLiteral("qindawrite_m88_header_%1").arg(width));
        scene.page->closeStory();
    }
}

WRITE_TEST_MAIN(WriteSectionsTest)

#include "tst_write_sections.moc"
