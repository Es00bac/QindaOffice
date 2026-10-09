// SPDX-License-Identifier: GPL-3.0-or-later
// Word comments (docx_comments.h): ranges over paragraphs, reference marks,
// replies and resolved state read; an untouched set copied byte for byte;
// an edited, resolved or new comment written from the model; a comment
// whose text was deleted dropped, one marked only in a header kept;
// output XSD-validated with QQO_OOXML_SCHEMA_DIR.

#include "docx_comments.h"
#include "docx_fixture.h"
#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using namespace QindaOffice;

namespace {

const char *kBody =
    R"(<w:p><w:r><w:t xml:space="preserve">Before </w:t></w:r><w:commentRangeStart w:id="0"/><w:commentRangeStart w:id="1"/>)"
    R"(<w:r><w:t>commented</w:t></w:r></w:p>)"
    R"(<w:p><w:r><w:t>second</w:t></w:r><w:commentRangeEnd w:id="0"/>)"
    R"(<w:r><w:rPr><w:rStyle w:val="CommentReference"/></w:rPr><w:commentReference w:id="0"/></w:r>)"
    R"(<w:commentRangeEnd w:id="1"/><w:r><w:rPr><w:rStyle w:val="CommentReference"/></w:rPr><w:commentReference w:id="1"/></w:r>)"
    R"(<w:r><w:t xml:space="preserve"> after</w:t></w:r></w:p>)";

const char *kComments =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
    R"(<w:comments xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main")"
    R"( xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml")"
    R"( xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" mc:Ignorable="w14">)"
    R"(<w:comment w:id="0" w:author="Dana Reviewer" w:date="2026-09-30T10:00:00Z" w:initials="DR">)"
    R"(<w:p w14:paraId="1A2B3C4D" w14:textId="77777777"><w:r><w:t>Please check this.</w:t></w:r></w:p></w:comment>)"
    R"(<w:comment w:id="1" w:author="Sam Author" w:date="2026-10-01T09:00:00Z" w:initials="SA">)"
    R"(<w:p w14:paraId="2B3C4D5E" w14:textId="77777777"><w:r><w:t>Checked.</w:t></w:r></w:p></w:comment></w:comments>)";

const char *kExtended =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)"
    R"(<w15:commentsEx xmlns:w15="http://schemas.microsoft.com/office/word/2012/wordml">)"
    R"(<w15:commentEx w15:paraId="1A2B3C4D" w15:done="0"/>)"
    R"(<w15:commentEx w15:paraId="2B3C4D5E" w15:paraIdParent="1A2B3C4D" w15:done="0"/></w15:commentsEx>)";

struct Loaded {
    QTextDocument body;
    Docx::WordParts parts;
    std::shared_ptr<const Ooxml::Package> package;
};

// The fixture document; `header` (children of w:hdr) and `comments` (a
// complete comments part) replace the defaults when given.
std::unique_ptr<Loaded> load(const QString &header = QString(), const QByteArray &comments = QByteArray(kComments))
{
    Test::DocxFixture fixture;
    fixture.body = QString::fromLatin1(kBody);
    fixture.header = header;
    fixture.extras.insert(QStringLiteral("rIdComments"),
                          {QStringLiteral("word/comments.xml"), QString::fromUtf16(Ooxml::RelType::Comments),
                           QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml"),
                           comments});
    fixture.extras.insert(
        QStringLiteral("rIdCommentsEx"),
        {QStringLiteral("word/commentsExtended.xml"),
         QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/commentsExtended"),
         QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.commentsExtended+xml"),
         QByteArray(kExtended)});
    auto loaded = std::make_unique<Loaded>();
    QString error;
    loaded->package = Ooxml::Package::fromData(fixture.build(), &error);
    if (!loaded->package || !Docx::readDocx(loaded->package, &loaded->body, &loaded->parts, &error)) {
        qWarning() << error;
        return nullptr;
    }
    return loaded;
}

std::shared_ptr<const Ooxml::Package> written(const QTextDocument &body, const Docx::WordParts &parts)
{
    QString error;
    auto package = Ooxml::Package::fromData(Docx::docxBytes(body, parts), &error);
    if (package && Test::schemaValidationAvailable()) {
        const QStringList errors = Test::validatePackage(*package);
        if (!errors.isEmpty()) {
            qWarning().noquote() << errors.join(QLatin1Char('\n'));
            return nullptr;
        }
    }
    return package;
}

} // namespace

class DocxCommentsTest : public QObject {
    Q_OBJECT
private slots:
    void readsRangesRepliesAndState();
    void untouchedCommentsAreCopied();
    void editedCommentsAreWritten();
    void deletedTextDropsItsComment();
    void headerCommentsSurviveARewrite();
    void newCommentOnASelection();
};

void DocxCommentsTest::readsRangesRepliesAndState()
{
    auto doc = load();
    QVERIFY(doc);
    QCOMPARE(doc->parts.comments.size(), 2);
    const Docx::Comment &first = doc->parts.comments.at(0);
    QCOMPARE(first.author, QStringLiteral("Dana Reviewer"));
    QCOMPARE(first.initials, QStringLiteral("DR"));
    QCOMPARE(first.text, QStringLiteral("Please check this."));
    QCOMPARE(first.date, QDateTime(QDate(2026, 9, 30), QTime(10, 0), QTimeZone::UTC));
    QCOMPARE(doc->parts.comments.at(1).parentId, QStringLiteral("0"));
    QVERIFY(!doc->parts.comments.at(1).done);
    // The range runs over both paragraphs; the marks are zero-width spaces.
    const QList<Docx::CommentSpan> spans = Docx::commentSpans(doc->body);
    QCOMPARE(spans.size(), 2);
    const QString text = doc->body.toPlainText();
    QCOMPARE(text.mid(spans.at(0).start, spans.at(0).end - spans.at(0).start), QStringLiteral("commented\nsecond"));
    QVERIFY(spans.at(0).reference > spans.at(0).end - 1);
    QCOMPARE(text.at(spans.at(0).reference), QChar(0x200B));
    QVERIFY(doc->parts.warnings.isEmpty() || !doc->parts.warnings.join(QString()).contains(QStringLiteral("comment")));
}

void DocxCommentsTest::untouchedCommentsAreCopied()
{
    auto doc = load();
    QVERIFY(doc);
    QVERIFY(!Docx::commentsChanged(doc->parts, doc->body));
    const auto out = written(doc->body, doc->parts);
    QVERIFY(out);
    QCOMPARE(out->data(QStringLiteral("word/comments.xml")), QByteArray(kComments));
    const QString body = QString::fromUtf8(out->data(out->mainPart()));
    QCOMPARE(body.count(QStringLiteral("<w:commentRangeStart")), 2);
    QCOMPARE(body.count(QStringLiteral("<w:commentRangeEnd")), 2);
    QCOMPARE(body.count(QStringLiteral("<w:commentReference")), 2);
    // Reading our output gives the same comments over the same text.
    QTextDocument again;
    Docx::WordParts parts;
    QString error;
    QVERIFY2(Docx::readDocx(out, &again, &parts, &error), qPrintable(error));
    QCOMPARE(Docx::commentSpans(again).at(0).end - Docx::commentSpans(again).at(0).start,
             Docx::commentSpans(doc->body).at(0).end - Docx::commentSpans(doc->body).at(0).start);
}

void DocxCommentsTest::editedCommentsAreWritten()
{
    auto doc = load();
    QVERIFY(doc);
    doc->parts.comments[0].text = QStringLiteral("Please check this paragraph.\nAnd the next one.");
    doc->parts.comments[1].done = true;
    QVERIFY(Docx::commentsChanged(doc->parts, doc->body));
    const auto out = written(doc->body, doc->parts);
    QVERIFY(out);
    QTextDocument again;
    Docx::WordParts parts;
    QString error;
    QVERIFY2(Docx::readDocx(out, &again, &parts, &error), qPrintable(error));
    QCOMPARE(parts.comments.size(), 2);
    QCOMPARE(parts.comments.at(0).text, QStringLiteral("Please check this paragraph.\nAnd the next one."));
    QCOMPARE(parts.comments.at(0).author, QStringLiteral("Dana Reviewer"));
    QCOMPARE(parts.comments.at(1).text, QStringLiteral("Checked.")); // unchanged one kept
    QCOMPARE(parts.comments.at(1).parentId, QStringLiteral("0"));
    QVERIFY(parts.comments.at(1).done);
    QVERIFY(QString::fromUtf8(out->data(QStringLiteral("[Content_Types].xml"))).contains(QStringLiteral("comments+xml")));
}

void DocxCommentsTest::deletedTextDropsItsComment()
{
    auto doc = load();
    QVERIFY(doc);
    // Delete everything comment 1 covers, its mark included (comment 0's
    // mark stays: its own range shrinks to what is left).
    const Docx::CommentSpan reply = Docx::commentSpans(doc->body).at(1);
    QTextCursor cursor(&doc->body);
    cursor.setPosition(reply.reference);
    cursor.setPosition(reply.reference + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    QVERIFY(Docx::commentsChanged(doc->parts, doc->body));
    const auto out = written(doc->body, doc->parts);
    QVERIFY(out);
    const QString comments = QString::fromUtf8(out->data(QStringLiteral("word/comments.xml")));
    QVERIFY(comments.contains(QStringLiteral("Please check this.")));
    QVERIFY(!comments.contains(QStringLiteral("Checked.")));
}

void DocxCommentsTest::headerCommentsSurviveARewrite()
{
    // Comment 2 is marked in the header only: Write does not list it, and a
    // save that rewrites the comments part (comment 0 edited) must keep it,
    // or the header's reference to it dangles.
    QByteArray comments(kComments);
    comments.replace("</w:comments>",
                     R"(<w:comment w:id="2" w:author="Lee" w:date="2026-10-02T09:00:00Z" w:initials="L">)"
                     R"(<w:p w14:paraId="3C4D5E6F" w14:textId="77777777"><w:r><w:t>Header note.</w:t></w:r></w:p>)"
                     R"(</w:comment></w:comments>)");
    const QString header = QStringLiteral(
        R"(<w:p><w:commentRangeStart w:id="2"/><w:r><w:t>Draft</w:t></w:r><w:commentRangeEnd w:id="2"/>)"
        R"(<w:r><w:rPr><w:rStyle w:val="CommentReference"/></w:rPr><w:commentReference w:id="2"/></w:r></w:p>)");
    auto doc = load(header, comments);
    QVERIFY(doc);
    QCOMPARE(doc->parts.comments.size(), 3);
    QCOMPARE(doc->parts.sourceCommentIds.size(), 2);
    doc->parts.comments[0].text = QStringLiteral("Please check this twice.");
    QVERIFY(Docx::commentsChanged(doc->parts, doc->body));
    const auto out = written(doc->body, doc->parts);
    QVERIFY(out);
    const QString part = QString::fromUtf8(out->data(QStringLiteral("word/comments.xml")));
    QVERIFY(part.contains(QStringLiteral("Please check this twice.")));
    QVERIFY(part.contains(QStringLiteral("Header note.")));
    QVERIFY(QString::fromUtf8(out->data(QStringLiteral("word/commentsExtended.xml"))).contains(QStringLiteral("3C4D5E6F")));
}

void DocxCommentsTest::newCommentOnASelection()
{
    QTextDocument body;
    QTextCursor cursor(&body);
    cursor.insertText(QStringLiteral("Quarterly results are final."));
    // "results" gets a comment: its range ids, then its mark after it.
    QTextCursor range(&body);
    range.setPosition(10);
    range.setPosition(17, QTextCursor::KeepAnchor);
    QTextCharFormat ids;
    ids.setProperty(Docx::kCommentIds, QStringList{QStringLiteral("0")});
    range.mergeCharFormat(ids);
    QTextCursor mark(&body);
    mark.setPosition(17);
    QTextCharFormat reference;
    reference.setProperty(Docx::kCommentReference, QStringLiteral("0"));
    mark.insertText(QString(QChar(0x200B)), reference);
    Docx::WordParts parts;
    Docx::Comment comment;
    comment.id = QStringLiteral("0");
    comment.author = QStringLiteral("Jordan");
    comment.initials = QStringLiteral("J");
    comment.date = QDateTime(QDate(2026, 10, 8), QTime(12, 0), QTimeZone::UTC);
    comment.text = QStringLiteral("Are they?");
    parts.comments.append(comment);
    const auto out = written(body, parts);
    QVERIFY(out);
    QTextDocument again;
    Docx::WordParts read;
    QString error;
    QVERIFY2(Docx::readDocx(out, &again, &read, &error), qPrintable(error));
    QCOMPARE(read.comments.size(), 1);
    QCOMPARE(read.comments.at(0).text, QStringLiteral("Are they?"));
    QCOMPARE(read.comments.at(0).author, QStringLiteral("Jordan"));
    const Docx::CommentSpan span = Docx::commentSpans(again).at(0);
    QCOMPARE(again.toPlainText().mid(span.start, span.end - span.start), QStringLiteral("results"));
    const QString styles = QString::fromUtf8(out->data(QStringLiteral("word/styles.xml")));
    QVERIFY(styles.contains(QStringLiteral("w:styleId=\"CommentReference\"")));
    QVERIFY(styles.contains(QStringLiteral("w:styleId=\"CommentText\"")));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    DocxCommentsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_docx_comments.moc"
