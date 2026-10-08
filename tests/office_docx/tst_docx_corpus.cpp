// SPDX-License-Identifier: GPL-3.0-or-later
// Real-document gate for office_docx (M60): reads every .docx listed in the
// file QQO_DOCX_CORPUS names (one path per line), then writes each back and
// reads the result again when the writer is available. Prints aggregate
// counts only — never document content — so it can run over private files.
// Skips when QQO_DOCX_CORPUS is unset.

#include "docx_properties.h"
#include "docx_reader.h"
#include "docx_writer.h"
#include "support/ooxml_schema.h"

#include <QElapsedTimer>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

using namespace QindaOffice;

class DocxCorpusTest : public QObject {
    Q_OBJECT
private slots:
    void readsEveryDocument();
};

namespace {

struct Stats {
    int paragraphs = 0;
    int tables = 0;
    int images = 0;
    int listItems = 0;
    int preserved = 0;
};

void count(QTextFrame *frame, Stats &stats)
{
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame()) {
            if (qobject_cast<QTextTable *>(child) != nullptr) {
                ++stats.tables;
            }
            count(child, stats);
            continue;
        }
        const QTextBlock block = it.currentBlock();
        ++stats.paragraphs;
        if (block.textList() != nullptr) {
            ++stats.listItems;
        }
        for (auto f = block.begin(); !f.atEnd(); ++f) {
            const QTextCharFormat cf = f.fragment().charFormat();
            if (cf.isImageFormat()) {
                ++stats.images;
            }
            if (cf.hasProperty(Docx::kInlineXml)) {
                ++stats.preserved;
            }
        }
    }
}

// Paragraph/table outline with text lengths only (no content).
void outline(QTextFrame *frame, QStringList &out)
{
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                out.append(QStringLiteral("T%1x%2[").arg(table->rows()).arg(table->columns()));
                for (int r = 0; r < table->rows(); ++r) {
                    for (int c = 0; c < table->columns(); ++c) {
                        const QTextTableCell cell = table->cellAt(r, c);
                        if (cell.row() != r || cell.column() != c) {
                            continue;
                        }
                        out.append(QStringLiteral("C%1,%2{").arg(r).arg(c));
                        for (auto ci = cell.begin(); !ci.atEnd(); ++ci) {
                            if (QTextFrame *nested = ci.currentFrame()) {
                                QStringList inner;
                                QTextFrame wrapper(nullptr);
                                Q_UNUSED(wrapper);
                                out.append(QStringLiteral("N"));
                                Q_UNUSED(nested);
                            } else {
                                const QTextBlock b = ci.currentBlock();
                                out.append(QStringLiteral("P%1%2").arg(b.text().size())
                                               .arg(b.blockFormat().boolProperty(Docx::kParagraphSynthetic) ? "s" : ""));
                            }
                        }
                        out.append(QStringLiteral("}"));
                    }
                }
                out.append(QStringLiteral("]"));
            } else {
                outline(child, out);
            }
            continue;
        }
        const QTextBlock b = it.currentBlock();
        out.append(QStringLiteral("P%1%2%3").arg(b.text().size())
                       .arg(b.blockFormat().boolProperty(Docx::kParagraphSynthetic) ? "s" : "")
                       .arg(b.blockFormat().boolProperty(Docx::kPageBreakRun) ? "b" : ""));
    }
}

} // namespace

void DocxCorpusTest::readsEveryDocument()
{
    const QString listPath = qEnvironmentVariable("QQO_DOCX_CORPUS");
    if (listPath.isEmpty()) {
        QSKIP("QQO_DOCX_CORPUS unset");
    }
    QFile list(listPath);
    QVERIFY(list.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList paths = QString::fromUtf8(list.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const bool validate = Test::schemaValidationAvailable() && qEnvironmentVariableIsSet("QQO_DOCX_CORPUS_VALIDATE");
    int ok = 0;
    int failed = 0;
    int written = 0;
    int writeFailed = 0;
    int textChanged = 0;
    int tablesChanged = 0;
    int imagesChanged = 0;
    int invalid = 0;
    QMap<QString, int> schemaErrors;
    int withWarnings = 0;
    int withHeaders = 0;
    qint64 slowestMs = 0;
    Stats total;
    QStringList failures;
    QElapsedTimer all;
    all.start();
    for (const QString &path : paths) {
        QTextDocument body;
        Docx::WordParts parts;
        QString error;
        QElapsedTimer timer;
        timer.start();
        if (!Docx::readDocx(path, &body, &parts, &error)) {
            ++failed;
            failures.append(QStringLiteral("#%1: %2").arg(ok + failed).arg(error));
            continue;
        }
        slowestMs = qMax(slowestMs, timer.elapsed());
        ++ok;
        withWarnings += parts.warnings.isEmpty() ? 0 : 1;
        withHeaders += (parts.header(Docx::HeaderFooterSlot::Default).document
                        || parts.footer(Docx::HeaderFooterSlot::Default).document) ? 1 : 0;
        Stats before;
        count(body.rootFrame(), before);
        total.paragraphs += before.paragraphs;
        total.tables += before.tables;
        total.images += before.images;
        total.listItems += before.listItems;
        total.preserved += before.preserved;

        // Round trip through our writer.
        QString writeError;
        const QByteArray bytes = Docx::docxBytes(body, parts, {}, &writeError);
        if (bytes.isEmpty()) {
            ++writeFailed;
            failures.append(QStringLiteral("#%1 write: %2").arg(ok + failed).arg(writeError));
            continue;
        }
        ++written;
        if (qEnvironmentVariableIntValue("QQO_DOCX_CORPUS_DUMP") == ok + failed) {
            QFile dump(qEnvironmentVariable("QQO_DOCX_CORPUS_DUMP_PATH"));
            if (dump.open(QIODevice::WriteOnly)) {
                dump.write(bytes);
            }
        }
        const auto package = Ooxml::Package::fromData(bytes, &writeError);
        QTextDocument back;
        Docx::WordParts backParts;
        if (!package || !Docx::readDocx(package, &back, &backParts, &writeError)) {
            ++writeFailed;
            failures.append(QStringLiteral("#%1 reread: %2").arg(ok + failed).arg(writeError));
            continue;
        }
        Stats after;
        count(back.rootFrame(), after);
        if (back.toPlainText() != body.toPlainText()) {
            ++textChanged;
            if (qEnvironmentVariableIsSet("QQO_DOCX_CORPUS_DIFF")) {
                const QString a = body.toPlainText();
                const QString b = back.toPlainText();
                qsizetype at = 0;
                while (at < a.size() && at < b.size() && a.at(at) == b.at(at)) {
                    ++at;
                }
                // Structure only: letters and digits masked.
                auto mask = [](QString t) {
                    for (QChar &c : t) {
                        if (c.isLetter()) {
                            c = QLatin1Char('x');
                        } else if (c.isDigit()) {
                            c = QLatin1Char('9');
                        } else if (c == QLatin1Char('\n')) {
                            c = QChar(0x00B6);
                        } else if (c == QChar::ObjectReplacementCharacter) {
                            c = QLatin1Char('#');
                        }
                    }
                    return t;
                };
                QStringList ob;
                QStringList oa;
                outline(body.rootFrame(), ob);
                outline(back.rootFrame(), oa);
                qsizetype k = 0;
                while (k < ob.size() && k < oa.size() && ob.at(k) == oa.at(k)) {
                    ++k;
                }
                failures.append(QStringLiteral("    outline was: %1\n    outline now: %2")
                                    .arg(ob.mid(qMax<qsizetype>(0, k - 6), 14).join(QLatin1Char(' ')))
                                    .arg(oa.mid(qMax<qsizetype>(0, k - 6), 14).join(QLatin1Char(' '))));
                failures.append(QStringLiteral("#%1 text differs at %2 (len %3 vs %4)\n    was: %5\n    now: %6")
                                    .arg(ok + failed).arg(at).arg(a.size()).arg(b.size())
                                    .arg(mask(a.mid(qMax<qsizetype>(0, at - 30), 70)))
                                    .arg(mask(b.mid(qMax<qsizetype>(0, at - 30), 70))));
            }
        }
        tablesChanged += before.tables != after.tables ? 1 : 0;
        imagesChanged += before.images != after.images ? 1 : 0;
        if (validate) {
            // Only errors our writer ADDS count: Word itself writes markup
            // the ECMA schemas reject (attributes from later versions), and
            // a copied part keeps whatever the source had.
            // Every xmllint line counts (not just the first per part): the
            // order differs once the writer sorts children, so a first-line
            // comparison reported the source's own invalid values as new.
            auto messages = [](const QStringList &list) {
                QSet<QString> out;
                static const QRegularExpression kLine(QStringLiteral("^line:?\\d*:? *"));
                for (const QString &part : list) {
                    for (QString line : part.section(QStringLiteral(": "), 1).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                        out.insert(line.remove(kLine));
                    }
                }
                return out;
            };
            constexpr int kAllLines = 100000;
            const auto sourcePackage = Ooxml::Package::load(path);
            const QSet<QString> before =
                sourcePackage ? messages(Test::validatePackage(*sourcePackage, kAllLines)) : QSet<QString>();
            const QStringList errors = (messages(Test::validatePackage(*package, kAllLines)) - before).values();
            if (!errors.isEmpty()) {
                ++invalid;
                for (const QString &e : errors) {
                    // Aggregate by the schema message, not by content.
                    QString key = e.section(QStringLiteral("Element '"), 1).left(260);
                    if (key.isEmpty()) {
                        key = e.left(160);
                    }
                    schemaErrors[key] += 1;
                }
            }
        }
    }
    qInfo().noquote() << QStringLiteral(
        "corpus: %1 read, %2 failed, %3 ms total, slowest %4 ms; %5 paragraphs, %6 tables, %7 pictures, "
        "%8 list items, %9 preserved objects; %10 with headers/footers, %11 with approximations")
        .arg(ok).arg(failed).arg(all.elapsed()).arg(slowestMs).arg(total.paragraphs).arg(total.tables)
        .arg(total.images).arg(total.listItems).arg(total.preserved).arg(withHeaders).arg(withWarnings);
    qInfo().noquote() << QStringLiteral(
        "round trip: %1 written, %2 failed; text changed in %3, table count changed in %4, picture count changed in %5; "
        "%6 schema-invalid%7")
        .arg(written).arg(writeFailed).arg(textChanged).arg(tablesChanged).arg(imagesChanged).arg(invalid)
        .arg(validate ? QString() : QStringLiteral(" (validation off)"));
    for (auto it = schemaErrors.cbegin(); it != schemaErrors.cend(); ++it) {
        qInfo().noquote() << QStringLiteral("  %1x %2").arg(it.value()).arg(it.key());
    }
    for (const QString &f : std::as_const(failures)) {
        qInfo().noquote() << f;
    }
    QCOMPARE(failed, 0);
    QCOMPARE(writeFailed, 0);
}

QTEST_MAIN(DocxCorpusTest)
#include "tst_docx_corpus.moc"
