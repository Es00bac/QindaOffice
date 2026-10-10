// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_rich_render.h"

#include "note_blocks.h"
#include "note_rich_text.h"

#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_layout.h>
#include <qindatk/rich_text_objects.h>
#include <qindatk/rich_text_spans.h>

#include <QHash>
#include <QJsonDocument>
#include <QPainter>

#include <cmath>
#include <memory>

namespace QindaOffice::Apps::Notes::RichRender {

using namespace QindaTK::RichText;

namespace {

constexpr int kCacheLimit = 48;

struct Entry {
    // AGENT-GUARD: declaration order is destruction order in reverse — the
    // layout (which watches the document) must go first.
    std::unique_ptr<QindaTK::RichTextDocument> doc;
    std::unique_ptr<QindaTK::RichTextLayout> layout;
    QString plain;
    qreal flowWidth = -1;
};

std::shared_ptr<Entry> entryFor(const QJsonObject &rich)
{
    // AGENT-NOTE: deliberately leaked. The entries hold QFonts and text
    // layouts; destroying them at static-destruction time, after the
    // QGuiApplication (and its font database) is gone, crashes on exit.
    static auto *cache = new QHash<QByteArray, std::shared_ptr<Entry>>();
    const QByteArray key = QJsonDocument(rich).toJson(QJsonDocument::Compact);
    if (const auto it = cache->constFind(key); it != cache->constEnd()) {
        return it.value();
    }
    auto entry = std::make_shared<Entry>();
    entry->doc = RichBlock::fromJson(rich);
    if (entry->doc) {
        entry->plain = RichBlock::plainText(*entry->doc);
        entry->layout = std::make_unique<QindaTK::RichTextLayout>();
        entry->layout->setDocument(entry->doc.get());
        entry->layout->setMode(QindaTK::RichTextLayout::Mode::Continuous);
    }
    if (cache->size() >= kCacheLimit) {
        cache->clear();
    }
    cache->insert(key, entry);
    return entry;
}

// The entry laid out at block width `width`; null when `rich` is invalid.
std::shared_ptr<Entry> laidOut(const QJsonObject &rich, qreal width)
{
    std::shared_ptr<Entry> entry = entryFor(rich);
    if (!entry->doc) {
        return nullptr;
    }
    const qreal flow = qMax<qreal>(20, width - 2 * kTextPadding);
    if (entry->flowWidth != flow) {
        entry->layout->setFlowWidth(flow);
        entry->flowWidth = flow;
    }
    if (!entry->layout->paginationComplete()) {
        entry->layout->paginateAll();
    }
    return entry;
}

QindaTK::RichTextObjectRenderer *pictures()
{
    // Pictures are data: URIs inside the document (an imported Word file,
    // a pasted image); leaked for the same reason as the cache.
    static auto *renderer = new QindaTK::RichTextImageRenderer();
    return renderer;
}

PaintContext paperContext()
{
    // Paper colours (D-004): ink, a link blue, grey rules — on every theme.
    PaintContext context;
    context.ink = QColor(0x20, 0x20, 0x28);
    context.link = QColor(0x1a, 0x5f, 0xb4);
    context.border = QColor(0x9a, 0x9a, 0xa6);
    context.marker = QColor(0x20, 0x20, 0x28);
    context.objectFill = QColor(0xee, 0xee, 0xf2);
    context.objectText = QColor(0x59, 0x59, 0x66);
    context.selectionBackground = QColor(0xb4, 0xd2, 0xff);
    context.selectionText = context.ink;
    context.caretColor = context.ink;
    context.objects = pictures();
    return context;
}

QString hrefAt(const QindaTK::RichTextDocument &doc, const Paragraph &p, int offset)
{
    if (offset < 0 || offset >= p.text.size()) {
        return {};
    }
    return doc.charFormat(Spans::formatAt(p.spans, offset)).href;
}

} // namespace

QString plainText(const QJsonObject &rich)
{
    const std::shared_ptr<Entry> entry = entryFor(rich);
    return entry->doc ? entry->plain : QString();
}

qreal height(const QJsonObject &rich, qreal width)
{
    const std::shared_ptr<Entry> entry = laidOut(rich, width);
    if (!entry) {
        return kTextBlockMinHeight;
    }
    return qMax<qreal>(kTextBlockMinHeight, std::ceil(entry->layout->flowHeight()) + 2 * kTextPadding);
}

void paint(QPainter *painter, const QRectF &box, const QJsonObject &rich)
{
    const std::shared_ptr<Entry> entry = laidOut(rich, box.width());
    if (!entry || entry->layout->pageCount() == 0) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->translate(box.topLeft() + QPointF(kTextPadding, kTextPadding));
    entry->layout->paintPage(painter, 0, paperContext());
    painter->restore();
}

QList<LineBox> lines(const QJsonObject &rich, qreal width)
{
    QList<LineBox> out;
    const std::shared_ptr<Entry> entry = laidOut(rich, width);
    if (!entry || entry->layout->pageCount() == 0) {
        return out;
    }
    for (const PageLine &line : entry->layout->pageLines(0)) {
        if (line.paragraph == nullptr) {
            continue;
        }
        out.append({line.paragraph->id.toString(QUuid::WithoutBraces),
                    line.box.translated(kTextPadding, kTextPadding), line.line == 0});
    }
    return out;
}

QString linkAt(const QJsonObject &rich, qreal width, const QPointF &point)
{
    const std::shared_ptr<Entry> entry = laidOut(rich, width);
    if (!entry || entry->layout->pageCount() == 0) {
        return {};
    }
    const QPointF inner = point - QPointF(kTextPadding, kTextPadding);
    bool onText = false;
    for (const PageLine &line : entry->layout->pageLines(0)) {
        onText = onText || line.box.contains(inner);
    }
    if (!onText) {
        return {};
    }
    const Position position = entry->layout->hitTest(0, inner);
    const Paragraph *p = entry->doc->paragraph(position.paragraph);
    if (p == nullptr) {
        return {};
    }
    const QString right = hrefAt(*entry->doc, *p, position.offset);
    return right.isEmpty() ? hrefAt(*entry->doc, *p, position.offset - 1) : right;
}

std::pair<QString, int> positionAt(const QJsonObject &rich, qreal width, const QPointF &point)
{
    const std::shared_ptr<Entry> entry = laidOut(rich, width);
    if (!entry || entry->layout->pageCount() == 0) {
        return {QString(), -1};
    }
    const Position position = entry->layout->hitTest(0, point - QPointF(kTextPadding, kTextPadding));
    if (!position.isValid()) {
        return {QString(), -1};
    }
    return {position.paragraph.toString(QUuid::WithoutBraces), position.offset};
}

QString paragraphText(const QJsonObject &rich, const QString &paragraphId)
{
    const std::shared_ptr<Entry> entry = entryFor(rich);
    if (!entry->doc) {
        return {};
    }
    const Paragraph *p = entry->doc->paragraph(QUuid(paragraphId));
    if (p == nullptr) {
        return {};
    }
    QString text = p->text;
    text.remove(QChar(ObjectChar));
    text.replace(QChar(LineBreakChar), QLatin1Char(' '));
    return text;
}

} // namespace QindaOffice::Apps::Notes::RichRender
