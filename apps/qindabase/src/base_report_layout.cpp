// SPDX-License-Identifier: GPL-3.0-or-later
// Report page control: the engine's band flags on QindaTK's report
// document, and whole groups kept on one page. See base_report_layout.h.
#include "base_report_layout.h"

#include <qindatk/rich_text_codec.h>
#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_layout.h>

#include <QJsonArray>
#include <QUuid>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

using Kind = Engine::ReportBand::Kind;

// The block each band became in documentJson's walk (-1: none).
QList<int> blocksOf(const QList<Engine::ReportBand> &bands, int *count)
{
    QList<int> out;
    int block = -1;
    bool inTable = false;
    for (const Engine::ReportBand &b : bands) {
        if (b.kind == Kind::Detail) {
            if (!inTable) {
                ++block;
                inTable = true;
            }
        } else {
            inTable = false;
            ++block;
        }
        out.append(block);
    }
    *count = block + 1;
    return out;
}

QJsonObject paragraphOf(const QJsonArray &blocks, int index)
{
    return blocks.at(index).toObject().value(s("paragraph")).toObject();
}

void setFormat(QJsonArray *blocks, int index, const QJsonObject &patch, bool replace = false)
{
    QJsonObject block = blocks->at(index).toObject();
    QJsonObject paragraph = block.value(s("paragraph")).toObject();
    QJsonObject format = replace ? QJsonObject() : paragraph.value(s("format")).toObject();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        format.insert(it.key(), it.value());
    }
    paragraph.insert(s("format"), format);
    block.insert(s("paragraph"), paragraph);
    (*blocks)[index] = block;
}

// The first and last page each block is laid out on.
QHash<QUuid, QPair<int, int>> pagesOfBlocks(QindaTK::RichTextLayout &layout)
{
    QHash<QUuid, QPair<int, int>> out;
    layout.paginateAll();
    for (int p = 0; p < layout.pageCount(); ++p) {
        for (const QindaTK::RichText::PageItem &item : layout.page(p).items) {
            auto it = out.find(item.block);
            if (it == out.end()) {
                out.insert(item.block, {p, p});
            } else {
                it->second = p;
            }
        }
    }
    return out;
}

} // namespace

PageControl applyReportPageControl(QJsonObject *document, const QList<Engine::ReportBand> &bands)
{
    PageControl out;
    QJsonArray blocks = document->value(s("blocks")).toArray();
    int count = 0;
    const QList<int> at = blocksOf(bands, &count);
    // AGENT-GUARD: documentJson's walk is the toolkit's (base_report_layout.h);
    // a document of another shape is left as the toolkit made it.
    const bool closed = blocks.size() == count + 1 && !blocks.isEmpty() && blocks.last().toObject().contains(s("paragraph"))
                        && count > 0 && blocks.at(count - 1).toObject().contains(s("table"));
    if (blocks.size() != count && !closed) {
        return out;
    }
    QList<QPair<int, int>> groups; // (header block, footer block) kept whole
    for (int i = 0; i < bands.size(); ++i) {
        const Engine::ReportBand &b = bands.at(i);
        if (b.kind == Kind::Detail) {
            continue;
        }
        if (b.spacer) {
            setFormat(&blocks, at.at(i), {{s("pageBreakBefore"), true}, {s("lineHeightPt"), 1}}, true);
            ++out.pageBreaks;
            continue;
        }
        QJsonObject patch;
        if (b.pageBreakBefore) {
            patch.insert(s("pageBreakBefore"), true);
            ++out.pageBreaks;
        }
        if (b.keepTogether) {
            patch.insert(s("keepTogether"), true);
        }
        if (!patch.isEmpty()) {
            setFormat(&blocks, at.at(i), patch);
        }
        if (b.keepWith > i && b.keepWith < bands.size()) {
            groups.append({at.at(i), at.at(b.keepWith)});
        }
    }
    out.applied = true;
    document->insert(s("blocks"), blocks);
    if (groups.isEmpty()) {
        return out;
    }
    // Whole groups: lay the pages out with QindaTK's own paginator and move
    // the first group that crosses a page while it fits on one.
    QindaTK::RichTextDocument pages;
    if (!QindaTK::RichTextCodec::fromJsonObject(*document, pages)) {
        return out;
    }
    QindaTK::RichTextLayout layout;
    layout.setDocument(&pages);
    layout.setMode(QindaTK::RichTextLayout::Mode::Paged);
    QHash<QUuid, QPair<int, int>> placed = pagesOfBlocks(layout);
    const auto idOf = [&blocks](int block) {
        const QJsonObject o = blocks.at(block).toObject();
        const QJsonObject part = o.contains(s("paragraph")) ? o.value(s("paragraph")).toObject() : o.value(s("table")).toObject();
        return QUuid::fromString(part.value(s("id")).toString());
    };
    const auto breakAt = [&pages](const QUuid &paragraph, bool on) {
        QindaTK::RichText::ParagraphFormatPatch patch;
        patch.pageBreakBefore = on;
        return pages.setParagraphFormat(pages.paragraphStart(paragraph), pages.paragraphEnd(paragraph), patch);
    };
    for (const auto &[header, footer] : std::as_const(groups)) {
        if (out.groupsMoved >= kKeepTogetherPasses) {
            break;
        }
        const QUuid head = idOf(header);
        const QUuid foot = idOf(footer);
        if (placed.value(head).first == placed.value(foot).second
            || paragraphOf(blocks, header).value(s("format")).toObject().value(s("pageBreakBefore")).toBool()) {
            continue; // on one page already, or starting one anyway
        }
        if (!breakAt(head, true)) {
            continue;
        }
        placed = pagesOfBlocks(layout);
        if (placed.value(head).first != placed.value(foot).second) {
            breakAt(head, false); // taller than a page: it runs on, as in Access
            placed = pagesOfBlocks(layout);
            continue;
        }
        setFormat(&blocks, header, {{s("pageBreakBefore"), true}});
        ++out.groupsMoved;
        ++out.pageBreaks;
    }
    document->insert(s("blocks"), blocks);
    return out;
}

} // namespace QindaOffice::Apps::Base
