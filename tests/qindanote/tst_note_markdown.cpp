// SPDX-License-Identifier: GPL-3.0-or-later
// M57 (D-063): the typesetting vocabulary of markdown blocks
// (note_markdown_style.h) — coloured highlights by meaning, underline,
// stand-out text, super/subscript, tag badges, small timestamps, callout
// boxes and ruled tables — rendered the same for screen and print, with
// the markup gone from the plain text and the HTML hand-off in colour.
// QQO_GRAB_DIR=<dir> saves the rendered sample as note_typeset.png.

#include "note_test_support.h"

#include "qindanote/src/note_markdown.h"

#include <QImage>
#include <QPainter>

using namespace QindaOffice::Apps::Notes;

namespace {

const QString kSample = QStringLiteral(
    "# Cell energy [00:00]\n\n"
    "> [!big]\n> Cells turn food and light into ATP.\n\n"
    "## Summary\n- **ATP** — the cell's energy currency\n- ==Mitochondria make most of it==\n\n"
    "## Respiration [01:10] [#exam]\n"
    "- =={term}Glycolysis== splits glucose; see =={example}a sprint== vs =={example}a marathon==\n"
    "- ++Oxygen is the final electron acceptor++; without it: =={warn}lactate builds up==\n"
    "- Yield: about =={date}30–32 ATP== per glucose; C~6~H~12~O~6~ + 6O~2~, x^2^ fits\n"
    "- !!Cyanide stops it cold!! *(unclear at 03:12: which complex?)*\n"
    "- Who pays? =={question}Why not 38 ATP?== [[ai: older textbooks count 38]]\n\n"
    "> [!def] Chemiosmosis\n> Protons flow back through **ATP synthase**, turning it like a turbine.\n\n"
    "> [!mnemonic] Order of stages\n> **G**o **K**ick **E**lectrons: Glycolysis, Krebs, ETC\n\n"
    "| Stage | Where | ATP |\n|---|---|---|\n| Glycolysis | cytoplasm | 2 |\n"
    "| Krebs | matrix | 2 |\n| ETC | inner membrane | ~28 |\n\n"
    "> [!warn]\n> Not the same as `photosynthesis` — that runs in chloroplasts.\n\n"
    "> [!quote] Dr. Lee\n> \"Follow the protons and you will never get lost.\"\n");

} // namespace

class NoteMarkdownTest final : public QObject {
    Q_OBJECT
private slots:
    void markupBecomesTypesetting()
    {
        const QString plain = markdownPlainText(kSample);
        for (const char *gone : {"=={", "==", "++", "!!", "^2^", "~6~", "> [!", "[[ai:", "[#exam]"}) {
            QVERIFY2(!plain.contains(QString::fromUtf8(gone)), gone);
        }
        for (const char *kept : {"Glycolysis", "Oxygen is the final", "Cyanide stops it cold", "C6H12O6",
                                 "DEFINITION", "Chemiosmosis", "MEMORY AID", "CAUTION", "#exam",
                                 "[01:10]", "ⓘ older textbooks count 38", "`photosynthesis`"}) {
            QVERIFY2(plain.contains(QString::fromUtf8(kept))
                         || (QByteArray(kept) == "`photosynthesis`" && plain.contains(QStringLiteral("photosynthesis"))),
                     kept);
        }
        // Code keeps its markup literally.
        QVERIFY(markdownPlainText(QStringLiteral("- run `a++b++c` and `x^2^`"))
                    .contains(QStringLiteral("a++b++c")));
        // A small model's callout inside a bullet still becomes a box.
        const QString bulleted = markdownPlainText(QStringLiteral(
            "- first\n- **> [!warn] Caution**\n  =={warn}text only==\n- after"));
        QVERIFY(bulleted.contains(QStringLiteral("CAUTION")));
        QVERIFY(!bulleted.contains(QStringLiteral("[!warn]")));
        QVERIFY(bulleted.contains(QStringLiteral("text only")));
        QVERIFY(bulleted.contains(QStringLiteral("after")));
        // C++ in prose is not underline.
        QVERIFY(markdownPlainText(QStringLiteral("- C++ is like C++ again")).contains(QStringLiteral("C++ is like C++")));
    }

    void htmlHandOffKeepsTheColours()
    {
        const QString html = markdownHtml(kSample);
        QVERIFY(html.contains(QStringLiteral("#cfe3ff"), Qt::CaseInsensitive));   // term
        QVERIFY(html.contains(QStringLiteral("#ffd0d0"), Qt::CaseInsensitive));   // warn
        QVERIFY(html.contains(QStringLiteral("vertical-align:sub")));
        QVERIFY(html.contains(QStringLiteral("vertical-align:super")));
        QVERIFY(html.contains(QStringLiteral("text-decoration: underline")));
    }

    void samplePaintsTallerThanPlainMarkdown()
    {
        const qreal width = 760;
        const qreal height = markdownBlockHeight(kSample, width);
        QVERIFY(height > 600);
        QImage image(QSize(int(width), int(height)), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        paintMarkdown(&painter, QRectF(0, 0, width, height), kSample);
        painter.end();
        // Callout tints reach the paper (the big-idea box's blue).
        bool tinted = false;
        for (int y = 0; y < image.height() && !tinted; y += 3) {
            tinted = image.pixelColor(int(width / 2), y) == QColor(0xee, 0xf2, 0xfa);
        }
        QVERIFY(tinted);
        const QString dir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!dir.isEmpty()) {
            image.save(dir + QStringLiteral("/note_typeset.png"));
        }
    }
};

NOTE_TEST_MAIN(NoteMarkdownTest)

#include "tst_note_markdown.moc"
