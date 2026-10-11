// SPDX-License-Identifier: GPL-3.0-or-later
// Word sections in the body (docx_sections.h): which paragraphs end
// sections, the sections in order, and the headers and footers each one
// has or links to. The w:sectPr mapping is docx_sections.cpp.

#include "docx_sections.h"

#include "docx_properties.h"

#include <QRandomGenerator>
#include <QTextDocument>
#include <QTextFrame>

namespace QindaOffice::Docx {

bool carriesSectionBreak(const QTextBlockFormat &format)
{
    return format.hasProperty(kSectionSettings) || format.hasProperty(kSectionXml);
}

SectionSettings sectionSettings(const QTextBlockFormat &format)
{
    if (format.hasProperty(kSectionSettings)) {
        return SectionSettings::fromMap(format.property(kSectionSettings).toMap());
    }
    const Ooxml::XmlElementPtr sectPr = Ooxml::parseFragment(format.stringProperty(kSectionXml));
    return readSectionSettings(sectPr.get());
}

void markSectionEnd(QTextBlockFormat &format, const QString &key, const QString &sectPrXml)
{
    const Ooxml::XmlElementPtr sectPr = Ooxml::parseFragment(sectPrXml);
    const QVariantMap settings = readSectionSettings(sectPr.get()).toMap();
    format.setProperty(kSectionXml, sectPrXml);
    format.setProperty(kSectionSettings, settings);
    format.setProperty(kSectionSnapshot, settings);
    format.setProperty(kSectionId, key);
}

void clearSectionEnd(QTextBlockFormat &format)
{
    for (const int key : {kSectionXml, kSectionSettings, kSectionSnapshot, kSectionId}) {
        format.clearProperty(key);
    }
}

QString currentSectionXml(const QTextBlockFormat &format)
{
    const SectionSettings settings = sectionSettings(format);
    const Ooxml::XmlElementPtr sectPr = Ooxml::parseFragment(format.stringProperty(kSectionXml));
    if (!sectPr) {
        return Ooxml::serializeFragment(*newSectionElement(settings));
    }
    const QVariantMap snapshot = format.property(kSectionSnapshot).toMap();
    const SectionSettings was = snapshot.isEmpty() ? readSectionSettings(sectPr.get()) : SectionSettings::fromMap(snapshot);
    if (!(settings == was)) {
        writeSectionSettings(*sectPr, settings, was);
    }
    return Ooxml::serializeFragment(*sectPr);
}

QString newSectionKey()
{
    return QStringLiteral("s%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
}

QList<QTextBlock> sectionEnds(const QTextDocument &body)
{
    QList<QTextBlock> ends;
    QTextBlock pending;
    QString pendingKey;
    auto flush = [&] {
        if (pending.isValid()) {
            ends.append(pending);
            pending = QTextBlock();
        }
    };
    for (auto it = body.rootFrame()->begin(); !it.atEnd(); ++it) {
        if (it.currentFrame() != nullptr) {
            flush(); // a table between two marked paragraphs parts them
            continue;
        }
        const QTextBlock block = it.currentBlock();
        const QTextBlockFormat format = block.blockFormat();
        if (!carriesSectionBreak(format)) {
            flush();
            continue;
        }
        const QString key = format.stringProperty(kSectionId);
        if (pending.isValid() && (key.isEmpty() || key != pendingKey)) {
            flush();
        }
        pending = block;
        pendingKey = key;
    }
    flush();
    // AGENT-GUARD: the last paragraph belongs to the closing section (Word
    // keeps the body's w:sectPr after it); a break it carries is ignored,
    // and the writer drops it.
    if (!ends.isEmpty() && ends.last() == body.lastBlock()) {
        ends.removeLast();
    }
    return ends;
}

QList<SectionRef> documentSections(const QTextDocument &body, const WordParts &parts)
{
    QList<SectionRef> out;
    for (const QTextBlock &end : sectionEnds(body)) {
        const QTextBlockFormat format = end.blockFormat();
        out.append({end, format.stringProperty(kSectionId), sectionSettings(format)});
    }
    out.append({QTextBlock(), QString(), closingSettings(parts)});
    return out;
}

int sectionIndexAt(const QList<SectionRef> &sections, int position)
{
    for (qsizetype i = 0; i < sections.size(); ++i) {
        const QTextBlock &end = sections.at(i).end;
        if (!end.isValid() || position < end.position() + end.length()) {
            return int(i);
        }
    }
    return int(sections.size()) - 1;
}

SectionSettings closingSettings(const WordParts &parts)
{
    return {parts.layout, parts.differentFirstPage, parts.closingFlow};
}

SectionSettings importedClosingSettings(const WordParts &parts)
{
    return {parts.importedLayout, parts.importedDifferentFirstPage, parts.importedClosingFlow};
}

void setClosingSettings(WordParts &parts, const SectionSettings &settings)
{
    parts.layout = settings.layout;
    parts.differentFirstPage = settings.titlePage;
    parts.closingFlow = settings.flow;
}

HeaderFooter *sectionStory(WordParts &parts, const QString &key, bool footer, HeaderFooterSlot slot, bool create)
{
    if (key.isEmpty()) {
        return footer ? &parts.footer(slot) : &parts.header(slot);
    }
    auto it = parts.sectionStories.find(key);
    if (it == parts.sectionStories.end()) {
        if (!create) {
            return nullptr;
        }
        it = parts.sectionStories.emplace(key, SectionStories()).first;
    }
    return footer ? &it->second.footers[size_t(slot)] : &it->second.headers[size_t(slot)];
}

const HeaderFooter *sectionStory(const WordParts &parts, const QString &key, bool footer, HeaderFooterSlot slot)
{
    if (key.isEmpty()) {
        return footer ? &parts.footer(slot) : &parts.header(slot);
    }
    const auto it = parts.sectionStories.find(key);
    if (it == parts.sectionStories.end()) {
        return nullptr;
    }
    return footer ? &it->second.footers[size_t(slot)] : &it->second.headers[size_t(slot)];
}

int storyOwner(const WordParts &parts, const QList<SectionRef> &sections, int index, bool footer, HeaderFooterSlot slot)
{
    for (int i = qMin(index, int(sections.size()) - 1); i >= 0; --i) {
        const HeaderFooter *story = sectionStory(parts, sections.at(i).key, footer, slot);
        if (story != nullptr && story->defines()) {
            return i;
        }
    }
    return -1;
}

void forEachStory(WordParts &parts, const std::function<void(HeaderFooter &)> &fn)
{
    auto each = [&fn](std::array<HeaderFooter, kHeaderFooterSlots> &stories) {
        for (HeaderFooter &story : stories) {
            if (story.document) {
                fn(story);
            }
        }
    };
    each(parts.headers);
    each(parts.footers);
    for (auto &[key, stories] : parts.sectionStories) {
        each(stories.headers);
        each(stories.footers);
    }
}

void forEachStory(const WordParts &parts, const std::function<void(const HeaderFooter &)> &fn)
{
    forEachStory(const_cast<WordParts &>(parts), [&fn](HeaderFooter &story) { fn(story); });
}

} // namespace QindaOffice::Docx
