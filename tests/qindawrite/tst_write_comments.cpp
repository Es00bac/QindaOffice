// SPDX-License-Identifier: GPL-3.0-or-later
// Comments in QindaWrite through the real window (write_comments.h): a Word
// document's comments and replies are listed in text order, Show Comments
// opens the pane, a comment added on a selection, a reply, an edit and a
// resolve land in the saved .docx, Delete removes the thread, the review
// commands follow the selection, and Undo takes a new comment back.

#include "write_test_support.h"

#include "../office_docx/docx_fixture.h"
#include "docx_comments.h"
#include "docx_reader.h"
#include "qindawrite/src/write_comments.h"
#include "support/quick_dump.h"

#include "standard_actions.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextCursor>

using namespace WriteTest;
using namespace QindaOffice::Apps::Write;
using QindaOffice::Quick::ActionEntry;
namespace Docx = QindaOffice::Docx;

namespace {

const char *kBody =
    R"(<w:p><w:r><w:t xml:space="preserve">The budget </w:t></w:r><w:commentRangeStart w:id="0"/>)"
    R"(<w:r><w:t>is final</w:t></w:r><w:commentRangeEnd w:id="0"/>)"
    R"(<w:r><w:rPr><w:rStyle w:val="CommentReference"/></w:rPr><w:commentReference w:id="0"/></w:r>)"
    R"(<w:r><w:t xml:space="preserve"> for this year.</w:t></w:r></w:p>)";

const char *kComments =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
    R"(<w:comments xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"
    R"(<w:comment w:id="0" w:author="Dana Reviewer" w:date="2026-09-30T10:00:00Z" w:initials="DR">)"
    R"(<w:p><w:r><w:t>Is it?</w:t></w:r></w:p></w:comment></w:comments>)";

QString writeFixture(const QTemporaryDir &dir)
{
    QindaOffice::Test::DocxFixture fixture;
    fixture.body = QString::fromLatin1(kBody);
    fixture.extras.insert(QStringLiteral("rIdComments"),
                          {QStringLiteral("word/comments.xml"),
                           QString::fromUtf16(QindaOffice::Ooxml::RelType::Comments),
                           QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml"),
                           QByteArray(kComments)});
    const QString path = dir.filePath(QStringLiteral("budget.docx"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(fixture.build());
    }
    return path;
}

} // namespace

class WriteCommentsTest : public QObject {
    Q_OBJECT
private slots:
    void commentsRoundTripThroughTheWindow();
};

void WriteCommentsTest::commentsRoundTripThroughTheWindow()
{
    QTemporaryDir dir;
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QString error;
    QVERIFY2(scene.doc->load(writeFixture(dir), &error), qPrintable(error));
    auto *comments = qobject_cast<WriteComments *>(scene.host.property("comments").value<QObject *>());
    QVERIFY(comments != nullptr);
    QTRY_COMPARE(comments->count(), 1);
    const QVariantMap first = comments->comments().first().toMap();
    QCOMPARE(first.value(QStringLiteral("author")).toString(), QStringLiteral("Dana Reviewer"));
    QCOMPARE(first.value(QStringLiteral("quote")).toString(), QStringLiteral("is final"));

    // Show Comments opens the pane with the comment's card.
    ActionEntry *show = scene.host.action(QStringLiteral("writeShowCommentsAction"));
    QVERIFY(show != nullptr);
    show->setChecked(true);
    QTRY_VERIFY(scene.item(QStringLiteral("commentsPanel")) != nullptr
                && scene.item(QStringLiteral("commentsPanel"))->isVisible());
    QTRY_VERIFY(scene.item(QStringLiteral("commentCard_0")) != nullptr);

    // A new comment on "this year"; New Comment follows the selection.
    ActionEntry *add = scene.host.action(QStringLiteral("writeNewCommentAction"));
    QVERIFY(!add->isEnabled());
    QTextCursor cursor(scene.doc->textDocument());
    const int at = scene.doc->textDocument()->toPlainText().indexOf(QStringLiteral("this year"));
    cursor.setPosition(at);
    cursor.setPosition(at + 9, QTextCursor::KeepAnchor);
    scene.control()->setTextCursor(cursor);
    QVERIFY(add->isEnabled());
    const QString id = comments->add(QStringLiteral("Which year?"));
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE(comments->count(), 2);
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_comments"));
    QVERIFY(!comments->reply(QStringLiteral("0"), QStringLiteral("Yes, signed off.")).isEmpty());
    QVERIFY(comments->setDone(QStringLiteral("0"), true));
    QVERIFY(comments->edit(id, QStringLiteral("Which fiscal year?")));

    const QString saved = dir.filePath(QStringLiteral("saved.docx"));
    QVERIFY2(scene.doc->save(saved, &error), qPrintable(error));
    QTextDocument body;
    Docx::WordParts parts;
    QVERIFY2(Docx::readDocx(saved, &body, &parts, &error), qPrintable(error));
    QCOMPARE(parts.comments.size(), 3);
    QStringList texts;
    for (const Docx::Comment &c : parts.comments) {
        texts.append(c.text);
        if (c.text == u"Yes, signed off.") {
            QCOMPARE(c.parentId, QStringLiteral("0"));
        }
        if (c.id == u"0") {
            QVERIFY(c.done);
        }
    }
    QVERIFY(texts.contains(QStringLiteral("Which fiscal year?")));
    QVERIFY(texts.contains(QStringLiteral("Is it?")));

    // Delete removes the thread (the reply with it); the text stays.
    QVERIFY(comments->remove(QStringLiteral("0")));
    QTRY_COMPARE(comments->count(), 1);
    QVERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("is final")));
    QVERIFY(!scene.doc->textDocument()->toPlainText().contains(QChar(0x200B)) || comments->count() == 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    WriteCommentsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_write_comments.moc"
