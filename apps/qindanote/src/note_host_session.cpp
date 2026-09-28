// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's session review (M56, D-061): what the Session panel shows for
// the open page — its recording, its sections' times, the spots the model
// could not make out — and the page-level commands around live notes:
// fixing an unclear spot, splitting long notes into pages by topic, study
// and notes pages, series memory, and export (Markdown, QindaWrite, Anki).

#include "note_host.h"

#include "navigator_model.h"
#include "note_blocks.h"
#include "note_page_item.h"

#include "live_notes_extras.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace QindaOffice::Apps::Notes {

namespace Extras = Ai::LiveNotesExtras;

namespace {

// The page's typed content as Markdown: markdown blocks as they are, text
// blocks as paragraphs, checklist items as task lines.
QString pageMarkdown(const NotePage &page)
{
    QStringList parts;
    for (const QJsonValue &value : blocksArray(page.envelope.blocksJson)) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        const QString text = block.value(QStringLiteral("text")).toString();
        if (type == QLatin1String("check")) {
            parts.append(QStringLiteral("- [%1] %2")
                             .arg(block.value(QStringLiteral("checked")).toBool() ? QLatin1Char('x') : QLatin1Char(' '))
                             .arg(text));
        } else if (type == QLatin1String("markdown") || type == QLatin1String("text")) {
            parts.append(text);
        }
    }
    return parts.join(QStringLiteral("\n\n"));
}

// The page's live-notes block (the last one carrying a transcript or a
// recording), or -1.
int liveBlockIndex(const QJsonArray &blocks)
{
    for (qsizetype i = blocks.size() - 1; i >= 0; --i) {
        const QJsonObject block = blocks.at(i).toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("markdown")
            && (block.contains(QStringLiteral("transcript")) || block.contains(QStringLiteral("audio")))) {
            return int(i);
        }
    }
    return -1;
}

} // namespace

QString NoteHost::currentPageNotes() const
{
    const NotePage *page = currentPage();
    return page != nullptr ? pageMarkdown(*page) : QString();
}

QString NoteHost::recordingPathFor(const LiveTarget &target) const
{
    const QString name = QStringLiteral("live-%1.wav").arg(target.blockUid);
    const QString dir = m_document->notebook()->directory();
    return dir.isEmpty() ? Core::stateDirFor(Core::appLeaf()) + QStringLiteral("/recordings/") + name
                         : dir + QStringLiteral("/assets/") + name;
}

void NoteHost::setLiveRecording(const LiveTarget &target, const QString &path)
{
    const QString dir = m_document->notebook()->directory();
    // Inside the notebook: a relative path, so the notebook can move.
    const QString stored = !dir.isEmpty() && path.startsWith(dir + QLatin1Char('/'))
        ? path.mid(dir.size() + 1) : path;
    writeLiveBlock(target, QJsonObject{{QStringLiteral("audio"), stored}}, false);
    emit sessionChanged();
}

QString NoteHost::earlierNotesInSection(int budget) const
{
    Notebook *notebook = m_document->notebook();
    NoteSection *section = notebook->sectionById(m_currentSectionId);
    if (section == nullptr) {
        return {};
    }
    QStringList earlier;
    for (NotePage &page : section->pages) {
        if (page.id == m_currentPageId) {
            break; // only the pages BEFORE this one
        }
        QString error;
        if (!page.loaded && !notebook->loadPage(*section, page, &error)) {
            continue;
        }
        const QString notes = pageMarkdown(page).trimmed();
        if (!notes.isEmpty()) {
            earlier.append(QStringLiteral("=== %1 ===\n%2").arg(page.title, notes));
        }
    }
    QString out;
    // The most recent sessions matter most: keep the end.
    for (qsizetype i = earlier.size() - 1; i >= 0 && out.size() < budget; --i) {
        out.prepend(earlier.at(i) + QStringLiteral("\n\n"));
    }
    return out.right(budget).trimmed();
}

QString NoteHost::addNotesPage(const QString &title, const QString &markdown)
{
    Notebook *notebook = m_document->notebook();
    NoteSection *section = currentSection();
    if (section == nullptr) {
        return {};
    }
    NotePage &created = notebook->addPage(*section, title);
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("markdown")},
                      {QStringLiteral("text"), markdown}};
    setBlockRect(&block, QRectF(40, kBlockTop, 680, 0));
    created.envelope.blocksJson = withBlocksArray(QByteArray("{}"), QJsonArray{block});
    // Right after the current page.
    const QString pageId = created.id;
    const qsizetype from = section->pages.size() - 1;
    qsizetype after = 0;
    for (qsizetype i = 0; i < section->pages.size(); ++i) {
        if (section->pages.at(i).id == m_currentPageId) {
            after = i;
        }
    }
    const QString sectionId = section->id;
    for (qsizetype i = from; i > after + 1; --i) {
        notebook->movePage(sectionId, pageId, -1);
    }
    if (notebook->isOpen()) {
        if (NoteSection *s = notebook->sectionById(sectionId)) {
            if (NotePage *p = notebook->pageById(sectionId, pageId)) {
                static_cast<void>(notebook->persistPage(*s, *p, nullptr));
            }
        }
    }
    m_document->setDirty(true);
    m_navigator->rebuild();
    if (m_pageItem != nullptr) {
        m_pageItem->rebindPage(currentPage()); // see NotePageItem::rebindPage's AGENT-GUARD
    }
    return pageId;
}

void NoteHost::splitLiveNotes(const LiveTarget &target)
{
    const QString notes = liveNotesText(target);
    if (notes.trimmed().isEmpty()) {
        return;
    }
    const NotePage *page = m_document->notebook()->pageById(target.sectionId, target.pageId);
    const Extras::SplitNotes split = Extras::splitByTopic(notes, page != nullptr ? page->title : QString());
    if (split.topics.size() < 2) {
        return; // nothing worth splitting
    }
    // The topic pages go after the notes page, in order.
    QStringList created;
    for (qsizetype i = split.topics.size() - 1; i >= 0; --i) {
        created.prepend(addNotesPage(split.topics.at(i).title, split.topics.at(i).markdown));
    }
    applyLiveNotes(target, split.index);
    showStatusMessage(tr("Split into %n topic page(s) after the index.", "", int(split.topics.size())), 6000);
    emit sessionChanged();
}

void NoteHost::splitCurrentPage()
{
    const NotePage *page = currentPage();
    if (page == nullptr) {
        return;
    }
    const QJsonArray blocks = blocksArray(page->envelope.blocksJson);
    int index = liveBlockIndex(blocks);
    if (index < 0) {
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            if (blocks.at(i).toObject().value(QStringLiteral("type")).toString() == QLatin1String("markdown")) {
                index = int(i);
            }
        }
    }
    if (index < 0) {
        showStatusMessage(tr("This page has no notes to split."), 5000);
        return;
    }
    QString uid = blocks.at(index).toObject().value(QStringLiteral("uid")).toString();
    if (uid.isEmpty() && m_pageItem != nullptr) {
        uid = Notebook::newId();
        m_pageItem->setBlockFields(index, QJsonObject{{QStringLiteral("uid"), uid}}, tr("Split by Topic"));
    }
    splitLiveNotes({m_currentSectionId, m_currentPageId, uid});
}

QVariantMap NoteHost::pageSession() const
{
    QVariantMap out{{QStringLiteral("hasNotes"), false}};
    const NotePage *page = currentPage();
    if (page == nullptr) {
        return out;
    }
    const QJsonArray blocks = blocksArray(page->envelope.blocksJson);
    const QString notes = pageMarkdown(*page);
    out.insert(QStringLiteral("hasNotes"), !notes.trimmed().isEmpty());
    const int live = liveBlockIndex(blocks);
    QString audio;
    if (live >= 0) {
        audio = blocks.at(live).toObject().value(QStringLiteral("audio")).toString();
        if (!audio.isEmpty() && QFileInfo(audio).isRelative()) {
            audio = m_document->notebook()->directory() + QLatin1Char('/') + audio;
        }
    }
    out.insert(QStringLiteral("audioUrl"), !audio.isEmpty() && QFileInfo::exists(audio)
                                              ? QUrl::fromLocalFile(audio).toString() : QString());
    QVariantList sections;
    for (const Extras::SectionTime &section : Extras::sectionTimes(notes)) {
        sections.append(QVariantMap{{QStringLiteral("heading"), section.heading},
                                    {QStringLiteral("ms"), section.ms}});
    }
    out.insert(QStringLiteral("sections"), sections);
    QVariantList unclear;
    for (const Extras::UnclearSpot &spot : Extras::unclearSpots(notes)) {
        unclear.append(QVariantMap{{QStringLiteral("text"), spot.text},
                                   {QStringLiteral("guess"), spot.guess},
                                   {QStringLiteral("ms"), spot.ms}});
    }
    out.insert(QStringLiteral("unclear"), unclear);
    out.insert(QStringLiteral("hasCards"), !Extras::ankiTsv(notes).isEmpty());
    return out;
}

bool NoteHost::resolveUnclear(const QString &annotation, const QString &replacement)
{
    const NotePage *page = currentPage();
    if (page == nullptr || m_pageItem == nullptr || annotation.isEmpty()) {
        return false;
    }
    const QJsonArray blocks = blocksArray(page->envelope.blocksJson);
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        QString text = blocks.at(i).toObject().value(QStringLiteral("text")).toString();
        if (!text.contains(annotation)) {
            continue;
        }
        text.replace(text.indexOf(annotation), annotation.size(), replacement.trimmed());
        if (m_pageItem->setBlockFields(int(i), QJsonObject{{QStringLiteral("text"), text}}, tr("Fix Unclear Spot"))) {
            emit sessionChanged();
            return true;
        }
    }
    return false;
}

bool NoteHost::exportMarkdownTo(const QUrl &url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showStatusMessage(tr("Export failed: %1").arg(file.errorString()), 8000);
        return false;
    }
    file.write(currentPageNotes().toUtf8() + '\n');
    showStatusMessage(tr("Exported to %1").arg(QFileInfo(file).fileName()), 5000);
    return true;
}

void NoteHost::openInWrite()
{
    const QString dir = Core::stateDirFor(Core::appLeaf()) + QStringLiteral("/handoff");
    QDir().mkpath(dir);
    QString name = currentPageTitle().simplified();
    name.replace(QRegularExpression(QStringLiteral("[^\\w .-]")), QStringLiteral("_"));
    const QString path = dir + QLatin1Char('/') + (name.isEmpty() ? QStringLiteral("notes") : name) + QStringLiteral(".md");
    if (!exportMarkdownTo(QUrl::fromLocalFile(path))) {
        return;
    }
    // AGENT-GUARD: QQO_NO_HANDOFF (set for the tests) writes the file but
    // launches nothing — a detached app inherits the test's output pipe and
    // ctest waits on it until it times out.
    const QString write = qEnvironmentVariableIsSet("QQO_NO_HANDOFF")
        ? QString() : QStandardPaths::findExecutable(QStringLiteral("qindawrite"));
    if (write.isEmpty() || !QProcess::startDetached(write, {path})) {
        showStatusMessage(tr("QindaWrite is not installed; the notes were saved to %1").arg(path), 8000);
    }
}

bool NoteHost::exportAnkiTo(const QUrl &url)
{
    const QString cards = Extras::ankiTsv(currentPageNotes());
    if (cards.isEmpty()) {
        showStatusMessage(tr("No flashcard table on this page — make a study page first."), 6000);
        return false;
    }
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showStatusMessage(tr("Export failed: %1").arg(file.errorString()), 8000);
        return false;
    }
    file.write(cards.toUtf8());
    showStatusMessage(tr("%n card(s) exported for Anki (File ▸ Import in Anki).", "",
                         int(cards.count(QLatin1Char('\n')) - 2)), 6000);
    return true;
}

} // namespace QindaOffice::Apps::Notes
