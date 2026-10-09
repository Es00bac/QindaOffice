// SPDX-License-Identifier: GPL-3.0-or-later
// Filling in a Word form in QindaWrite through the real window
// (write_controls.h): a click on a check box ticks it (not a click beside
// it), Undo clears it; a drop-down opens its list and an item replaces the
// placeholder; a date field opens the calendar and a day is written in the
// field's format; a click on a text placeholder selects it and typing
// replaces it in ordinary ink; the saved .docx carries every state.

#include "write_test_support.h"

#include "../office_docx/docx_fixture.h"
#include "docx_controls.h"
#include "docx_reader.h"
#include "qindawrite/src/write_controls.h"
#include "support/quick_dump.h"

#include "standard_actions.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
namespace Docx = QindaOffice::Docx;

namespace {

const char *kStyles =
    R"(<w:style w:type="character" w:styleId="PlaceholderText"><w:name w:val="Placeholder Text"/>)"
    R"(<w:rPr><w:color w:val="808080"/></w:rPr></w:style>)";

const char *kBody =
    R"(<w:p><w:r><w:t xml:space="preserve">Agreed </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:id w:val="101"/><w14:checkbox><w14:checked w14:val="0"/>)"
    R"(<w14:checkedState w14:val="2612" w14:font="MS Gothic"/><w14:uncheckedState w14:val="2610" w14:font="MS Gothic"/>)"
    R"(</w14:checkbox></w:sdtPr><w:sdtContent><w:r><w:t>☐</w:t></w:r></w:sdtContent></w:sdt>)"
    R"(<w:r><w:t xml:space="preserve"> to the terms in every respect.</w:t></w:r></w:p>)"
    R"(<w:p><w:r><w:t xml:space="preserve">Colour: </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:alias w:val="Colour"/><w:id w:val="102"/><w:showingPlcHdr/><w:dropDownList>)"
    R"(<w:listItem w:displayText="Choose an item." w:value=""/><w:listItem w:displayText="Red" w:value="R"/>)"
    R"(<w:listItem w:displayText="Green" w:value="G"/></w:dropDownList></w:sdtPr><w:sdtContent>)"
    R"(<w:r><w:rPr><w:rStyle w:val="PlaceholderText"/></w:rPr><w:t>Choose an item.</w:t></w:r></w:sdtContent></w:sdt></w:p>)"
    R"(<w:p><w:r><w:t xml:space="preserve">Date: </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:id w:val="103"/><w:showingPlcHdr/><w:date><w:dateFormat w:val="M/d/yyyy"/>)"
    R"(<w:lid w:val="en-US"/><w:storeMappedDataAs w:val="dateTime"/><w:calendar w:val="gregorian"/></w:date></w:sdtPr>)"
    R"(<w:sdtContent><w:r><w:rPr><w:rStyle w:val="PlaceholderText"/></w:rPr><w:t>Enter a date.</w:t></w:r>)"
    R"(</w:sdtContent></w:sdt></w:p>)"
    R"(<w:p><w:r><w:t xml:space="preserve">Name: </w:t></w:r>)"
    R"(<w:sdt><w:sdtPr><w:id w:val="104"/><w:showingPlcHdr/><w:text/></w:sdtPr><w:sdtContent>)"
    R"(<w:r><w:rPr><w:rStyle w:val="PlaceholderText"/></w:rPr><w:t>Click here to enter text.</w:t></w:r>)"
    R"(</w:sdtContent></w:sdt></w:p>)";

QString writeFixture(const QTemporaryDir &dir)
{
    QindaOffice::Test::DocxFixture fixture;
    fixture.body = QString::fromUtf8(kBody);
    fixture.styles = QString::fromLatin1(kStyles);
    const QString path = dir.filePath(QStringLiteral("form.docx"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(fixture.build());
    }
    return path;
}

int positionOf(const Scene &scene, const QString &text)
{
    return int(scene.doc->textDocument()->toPlainText().indexOf(text));
}

// The middle of the character at `position` in the text's layout.
QPointF charCentre(const QTextDocument *document, int position)
{
    const QTextBlock block = document->findBlock(position);
    const QTextLine line = block.layout()->lineForTextPosition(position - block.position());
    const QPointF origin = document->documentLayout()->blockBoundingRect(block).topLeft();
    const qreal x0 = line.cursorToX(position - block.position());
    const qreal x1 = line.cursorToX(position + 1 - block.position());
    return origin + QPointF((x0 + x1) / 2, line.y() + line.height() / 2);
}

QObject *popup(const Scene &scene, const char *name)
{
    return scene.window->findChild<QObject *>(QString::fromLatin1(name));
}

} // namespace

class WriteControlsTest : public QObject {
    Q_OBJECT
private slots:
    void formFieldsThroughTheWindow();
};

void WriteControlsTest::formFieldsThroughTheWindow()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QString error;
    QVERIFY2(scene.doc->load(writeFixture(dir), &error), qPrintable(error));
    auto *controls = qobject_cast<WriteControls *>(scene.host.property("controls").value<QObject *>());
    QVERIFY(controls != nullptr);
    const QTextDocument *text = scene.doc->textDocument();

    // The check box: a click beside it does nothing, one on it ticks it.
    const int box = positionOf(scene, QStringLiteral("☐"));
    QVERIFY(box > 0);
    QCOMPARE(controls->activate(box + 1, charCentre(text, box + 6)), QString());
    QCOMPARE(controls->activate(box + 1, charCentre(text, box)), QStringLiteral("checkbox"));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Agreed ☒")));
    scene.host.action(QString::fromLatin1(QindaOffice::Quick::ActionId::kEditUndo))->trigger();
    QVERIFY(text->toPlainText().contains(QStringLiteral("Agreed ☐")));
    QCOMPARE(controls->activate(box), QStringLiteral("checkbox"));

    // The drop-down: its list opens at the click; an item replaces the placeholder.
    QCOMPARE(controls->activate(positionOf(scene, QStringLiteral("Choose")) + 2, QPointF(-1, -1), QPointF(200, 120)),
             QStringLiteral("list"));
    QCOMPARE(controls->items().size(), 3);
    QObject *menu = popup(scene, "formListMenu");
    QVERIFY(menu != nullptr);
    QTRY_VERIFY(menu->property("opened").toBool());
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_form_list"));
    QVERIFY(controls->choose(2));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Colour: Green")));
    menu->setProperty("visible", false);
    QTRY_VERIFY(!menu->property("opened").toBool());

    // The date: the calendar opens; a day is written as M/d/yyyy.
    QCOMPARE(controls->activate(positionOf(scene, QStringLiteral("Enter a date")) + 1, QPointF(-1, -1),
                                QPointF(200, 150)),
             QStringLiteral("date"));
    QObject *calendar = popup(scene, "formDatePopover");
    QVERIFY(calendar != nullptr);
    QTRY_VERIFY(calendar->property("opened").toBool());
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_form_date"));
    QVERIFY(controls->chooseDate(QStringLiteral("2026-10-08")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Date: 10/8/2026")));
    QVERIFY(!controls->chooseDate(QStringLiteral("2026-10-09"))); // nothing open any more

    // A text placeholder: a click selects it, typing replaces it.
    const int name = positionOf(scene, QStringLiteral("Click here"));
    QCOMPARE(controls->activate(name + 3), QStringLiteral("placeholder"));
    QCOMPARE(scene.control()->textCursor().selectedText(), QStringLiteral("Click here to enter text."));
    scene.control()->insertText(QStringLiteral("Ada Lovelace"));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Name: Ada Lovelace")));
    QTextCursor typed(scene.doc->textDocument());
    typed.setPosition(name + 2);
    QVERIFY(typed.charFormat().foreground().color() != QColor(0x80, 0x80, 0x80));
    QCOMPARE(Docx::controlAt(*text, name + 2).end, name + 12);

    // Everything lands in the saved file.
    const QString saved = dir.filePath(QStringLiteral("saved.docx"));
    QVERIFY2(scene.doc->save(saved, &error), qPrintable(error));
    QTextDocument body;
    Docx::WordParts parts;
    QVERIFY2(Docx::readDocx(saved, &body, &parts, &error), qPrintable(error));
    QCOMPARE(parts.contentControls.size(), 4);
    QStringList properties;
    for (const Docx::ContentControl &control : std::as_const(parts.contentControls)) {
        properties.append(control.propertiesXml);
    }
    const QString all = properties.join(QLatin1Char('\n'));
    QVERIFY(all.contains(QStringLiteral("w14:val=\"1\"")));
    QVERIFY(all.contains(QStringLiteral("w:lastValue=\"G\"")));
    QVERIFY(all.contains(QStringLiteral("w:fullDate=\"2026-10-08T00:00:00Z\"")));
    QVERIFY(!all.contains(QStringLiteral("showingPlcHdr")));
    QVERIFY(body.toPlainText().contains(QStringLiteral("Name: Ada Lovelace")));
}

WRITE_TEST_MAIN(WriteControlsTest)

#include "tst_write_controls.moc"
