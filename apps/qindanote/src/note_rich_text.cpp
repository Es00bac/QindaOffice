// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_rich_text.h"

#include "note_blocks.h"

#include <qindatk/rich_text_codec.h>
#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_html.h>
#include <qindatk/rich_text_spans.h>

#include <QSet>

namespace QindaOffice::Apps::Notes::RichBlock {

using namespace QindaTK::RichText;

namespace {

// Paper ink (D-004): the same literal the plain blocks paint with.
const QColor kInk(0x20, 0x20, 0x28);

QJsonObject defaultsJson()
{
    const DocumentDefaults defaults = pageDefaults();
    return {{QStringLiteral("family"), defaults.family},
            {QStringLiteral("sizePt"), defaults.sizePt},
            {QStringLiteral("color"), defaults.color.name(QColor::HexRgb)}};
}

QString paragraphPlain(const Paragraph &p)
{
    QString text = p.text;
    text.remove(QChar(ObjectChar));
    text.replace(QChar(LineBreakChar), QLatin1Char('\n'));
    return text;
}

bool runIsPlain(const QindaTK::RichTextDocument &doc, const Paragraph &p)
{
    if (p.text.contains(QChar(LineBreakChar)) || p.text.contains(QChar(ObjectChar))) {
        return false;
    }
    for (const Span &span : p.spans) {
        if (!(doc.charFormat(span.format) == CharFormat{})) {
            return false;
        }
    }
    return p.format == ParagraphFormat{};
}

} // namespace

DocumentDefaults pageDefaults()
{
    DocumentDefaults defaults;
    const QFont font = pageFont();
    defaults.family = font.family();
    defaults.sizePt = font.pointSizeF() > 0 ? font.pointSizeF() : 11.0;
    defaults.color = kInk;
    return defaults;
}

std::unique_ptr<QindaTK::RichTextDocument> fromFragment(Fragment fragment)
{
    if (fragment.formats.isEmpty() || !(fragment.formats.first() == CharFormat{})) {
        fragment.formats.prepend(CharFormat{});
        // Every span shifts by the new format 0.
        const auto shift = [](const ParagraphPtr &p) {
            for (Span &span : p->spans) {
                ++span.format;
            }
            ++p->markFormat;
        };
        for (Block &block : fragment.blocks) {
            if (block.paragraph) {
                shift(block.paragraph);
            } else if (block.table) {
                for (TableRow &row : block.table->rows) {
                    for (TableCell &cell : row.cells) {
                        for (const ParagraphPtr &p : cell.paragraphs) {
                            shift(p);
                        }
                    }
                }
            }
        }
    }
    // AGENT-GUARD: the model's body always ends with a paragraph; an HTML
    // or Word body that ends with a table needs one after it.
    if (fragment.blocks.isEmpty() || !fragment.blocks.last().paragraph) {
        auto closing = std::make_shared<Paragraph>();
        closing->id = QUuid::createUuid();
        fragment.blocks.append(Block{closing, {}});
    }
    auto doc = std::make_unique<QindaTK::RichTextDocument>();
    doc->resetContent(fragment.blocks, fragment.formats, fragment.lists, fragment.objects, PageSetup{},
                      pageDefaults(), QindaTK::DocumentId::create(), fragment.styles);
    return doc;
}

std::unique_ptr<QindaTK::RichTextDocument> fromPlainText(const QString &text)
{
    return fromFragment(QindaTK::RichTextHtml::fromPlainText(text));
}

std::unique_ptr<QindaTK::RichTextDocument> fromJson(const QJsonObject &rich)
{
    if (rich.value(QStringLiteral("schema")).toString() != QindaTK::RichTextCodec::schemaId()) {
        return nullptr;
    }
    QJsonObject root = rich;
    root.insert(QStringLiteral("defaults"), defaultsJson());
    auto doc = std::make_unique<QindaTK::RichTextDocument>();
    if (!QindaTK::RichTextCodec::fromJsonObject(root, *doc)) {
        return nullptr;
    }
    return doc;
}

std::unique_ptr<QindaTK::RichTextDocument> forBlock(const QJsonObject &block)
{
    const QString text = block.value(QStringLiteral("text")).toString();
    const QJsonObject rich = block.value(QStringLiteral("rich")).toObject();
    if (!rich.isEmpty()) {
        if (auto doc = fromJson(rich); doc && plainText(*doc) == text) {
            return doc;
        }
    }
    return fromPlainText(text);
}

QJsonObject toJson(const QindaTK::RichTextDocument &doc)
{
    return QindaTK::RichTextCodec::toJsonObject(doc);
}

QString plainText(const QindaTK::RichTextDocument &doc)
{
    QStringList lines;
    for (const Block &block : doc.blocks()) {
        if (block.paragraph) {
            lines.append(paragraphPlain(*block.paragraph));
            continue;
        }
        for (const TableRow &row : block.table->rows) {
            QStringList cells;
            for (const TableCell &cell : row.cells) {
                if (cell.paragraphs.isEmpty()) {
                    continue; // a slot a merged cell covers
                }
                QStringList parts;
                for (const ParagraphPtr &p : cell.paragraphs) {
                    parts.append(paragraphPlain(*p));
                }
                cells.append(parts.join(QLatin1Char(' ')));
            }
            lines.append(cells.join(QLatin1Char('\t')));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

bool isPlain(const QindaTK::RichTextDocument &doc)
{
    for (const Block &block : doc.blocks()) {
        if (block.table || !runIsPlain(doc, *block.paragraph)) {
            return false;
        }
    }
    return true;
}

QStringList paragraphIds(const QindaTK::RichTextDocument &doc)
{
    QStringList ids;
    for (const Paragraph *p : doc.paragraphsInOrder()) {
        ids.append(p->id.toString(QUuid::WithoutBraces));
    }
    return ids;
}

void store(QJsonObject *block, const QindaTK::RichTextDocument &doc)
{
    block->insert(QStringLiteral("text"), plainText(doc));
    const QStringList ids = paragraphIds(doc);
    const QSet<QString> alive(ids.cbegin(), ids.cend());
    QJsonArray tags;
    for (const QJsonValue &value : block->value(QStringLiteral("tags")).toArray()) {
        const QString paragraph = value.toObject().value(QStringLiteral("p")).toString();
        if (paragraph.isEmpty() || alive.contains(paragraph)) {
            tags.append(value);
        }
    }
    if (tags.isEmpty()) {
        block->remove(QStringLiteral("tags"));
    } else {
        block->insert(QStringLiteral("tags"), tags);
    }
    if (tags.isEmpty() && isPlain(doc)) {
        block->remove(QStringLiteral("rich"));
    } else {
        block->insert(QStringLiteral("rich"), toJson(doc));
    }
}

int replaceAll(QindaTK::RichTextDocument &doc, const QString &needle, const QString &replacement,
               Qt::CaseSensitivity sensitivity)
{
    if (needle.isEmpty() || !doc.beginTransaction(QStringLiteral("Replace"))) {
        return 0;
    }
    int hits = 0;
    const QVector<Paragraph *> paragraphs = doc.paragraphsInOrder();
    for (const Paragraph *p : paragraphs) {
        const QUuid id = p->id;
        // Back to front, so earlier offsets stay valid.
        QList<qsizetype> found;
        for (qsizetype at = p->text.indexOf(needle, 0, sensitivity); at >= 0;
             at = p->text.indexOf(needle, at + needle.size(), sensitivity)) {
            if (!p->text.mid(at, needle.size()).contains(QChar(ObjectChar))) {
                found.prepend(at);
            }
        }
        for (const qsizetype at : std::as_const(found)) {
            const Paragraph *now = doc.paragraph(id);
            const int format = Spans::formatAt(now->spans, int(at));
            const std::optional<Position> start = doc.deleteRange({id, int(at)}, {id, int(at + needle.size())});
            if (!start || (!replacement.isEmpty() && !doc.insertText(*start, replacement, format))) {
                doc.cancelTransaction();
                return 0;
            }
            ++hits;
        }
    }
    if (hits == 0) {
        doc.cancelTransaction();
        return 0;
    }
    return doc.commitTransaction() ? hits : 0;
}

bool sameEditorContent(QJsonObject a, QJsonObject b)
{
    for (QJsonObject *block : {&a, &b}) {
        block->remove(QStringLiteral("tags"));
        block->remove(QStringLiteral("rect"));
    }
    if (a == b) {
        return true;
    }
    const bool aRich = a.contains(QStringLiteral("rich"));
    const bool bRich = b.contains(QStringLiteral("rich"));
    if (aRich == bRich) {
        return false;
    }
    const auto doc = fromJson((aRich ? a : b).value(QStringLiteral("rich")).toObject());
    a.remove(QStringLiteral("rich"));
    b.remove(QStringLiteral("rich"));
    return a == b && doc != nullptr && isPlain(*doc) && plainText(*doc) == a.value(QStringLiteral("text")).toString();
}

void setPlainText(QJsonObject *block, const QString &text)
{
    block->insert(QStringLiteral("text"), text);
    if (!block->contains(QStringLiteral("rich"))) {
        return;
    }
    // The rewrite replaces the words; the runs and paragraph ids of the old
    // text no longer describe them.
    block->remove(QStringLiteral("rich"));
    QJsonArray kept;
    for (const QJsonValue &value : block->value(QStringLiteral("tags")).toArray()) {
        if (value.toObject().value(QStringLiteral("p")).toString().isEmpty()) {
            kept.append(value);
        }
    }
    if (kept.isEmpty()) {
        block->remove(QStringLiteral("tags"));
    } else {
        block->insert(QStringLiteral("tags"), kept);
    }
}

} // namespace QindaOffice::Apps::Notes::RichBlock
