// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_agent.h"
#include "note_host.h"
#include "note_tools.h"
#include "navigator_model.h"
#include "note_blocks.h"
#include <QJsonDocument>
#include <QQuickItem>
namespace QindaOffice::Apps::Notes {
NoteAgent::NoteAgent(NoteHost *host, NoteTools *tools)
    : AgentProvider(host), m_host(host), m_tools(tools)
{
    connect(host->noteDocument(), &Core::Document::contentChanged, this, &AgentProvider::touch);
    connect(host->noteDocument(), &Core::Document::dirtyChanged, this, &AgentProvider::changed);
    connect(host, &NoteHost::currentPageChanged, this, &AgentProvider::touch);
    connect(host, &NoteHost::editorSelectionChanged, this, &AgentProvider::changed);
    // Structural commands historically mark dirty rather than emitting
    // contentChanged. Navigator resets cover rename/delete/reorder ABA.
    connect(host->navigator(), &QAbstractItemModel::modelReset, this, &AgentProvider::touch);
}
QJsonObject NoteAgent::state() const
{
    const auto *page = m_host->currentPage();
    const auto *item = m_host->pageItem();
    QJsonArray sections;
    int pageCount = 0, emitted = 0;
    for (const auto &section : m_host->noteDocument()->notebook()->sections()) {
        QJsonArray pages;
        for (const auto &p : section.pages) {
            ++pageCount;
            if (emitted >= 1000) continue;
            pages.append(QJsonObject{{QStringLiteral("id"), p.id},
                {QStringLiteral("title"), p.title.left(512)},
                {QStringLiteral("title_truncated"), p.title.size() > 512},
                {QStringLiteral("loaded"), p.loaded},
                {QStringLiteral("current"), p.id == m_host->currentPageId()}});
            ++emitted;
        }
        if (!pages.isEmpty()) sections.append(QJsonObject{{QStringLiteral("id"), section.id},
            {QStringLiteral("name"), section.name.left(512)},
            {QStringLiteral("name_truncated"), section.name.size() > 512}, {QStringLiteral("pages"), pages}});
    }
    QJsonArray blocks;
    int bytes = 0;
    const auto all = page ? blocksArray(page->envelope.blocksJson) : QJsonArray{};
    // Whole block text/metadata is omitted explicitly when it cannot fit.
    // Image bytes and ink geometry are never exposed as interpreted text.
    constexpr int byteBudget = 524288;
    for (qsizetype i = 0; i < all.size() && i < 500; ++i) {
        const auto block = all.at(i).toObject();
        QJsonObject entry{{QStringLiteral("index"), int(i)},
            {QStringLiteral("type"), block.value(QStringLiteral("type"))}};
        for (const auto *key : {"uid", "text", "checked", "level", "transcript",
                               "session_started_at", "session_scenario", "session_sources"}) {
            const QString name = QString::fromLatin1(key);
            if (!block.contains(name)) continue;
            const QJsonObject field{{name, block.value(name)}};
            const int cost = int(QJsonDocument(field).toJson(QJsonDocument::Compact).size());
            if (cost + bytes > byteBudget) {
                entry.insert(name + QStringLiteral("_omitted"), true);
            } else { entry.insert(name, block.value(name)); bytes += cost; }
        }
        bool recorded = !block.value(QStringLiteral("audio")).toString().isEmpty();
        for (const auto &source : block.value(QStringLiteral("session_sources")).toArray())
            recorded = recorded || !source.toObject().value(QStringLiteral("audio")).toString().isEmpty();
        entry.insert(QStringLiteral("has_recording"), recorded);
        blocks.append(entry);
    }
    QJsonArray modals;
    const auto *window = m_host->window();
    if (window) for (const auto *object : window->findChildren<QObject *>())
        if (object->property("modal").toBool() && object->property("visible").toBool())
            modals.append(object->objectName());
    QJsonValue draft = QJsonValue::Null;
    if (item && item->editingBlock() >= 0)
        draft = QJsonObject{{QStringLiteral("block"), item->editingBlock()},
            {QStringLiteral("text"), item->editingText().left(65536)},
            {QStringLiteral("text_truncated"), item->editingText().size() > 65536},
            {QStringLiteral("checked"), item->editingChecked()}};
    QJsonObject view;
    const auto *viewport = window ? window->findChild<QObject *>(QStringLiteral("noteViewport")) : nullptr;
    if (viewport) {
        for (const auto *key : {"zoom", "width", "height"})
            view.insert(QString::fromLatin1(key), QJsonValue::fromVariant(viewport->property(key)));
        const auto *scroll = viewport->property("scrollItem").value<QQuickItem *>();
        if (scroll) for (const auto *key : {"contentX", "contentY"})
            view.insert(QString::fromLatin1(key), QJsonValue::fromVariant(scroll->property(key)));
    }
    return {{QStringLiteral("type"), QStringLiteral("notebook")},
        {QStringLiteral("title"), m_host->noteDocument()->displayTitle().left(512)},
        {QStringLiteral("title_truncated"), m_host->noteDocument()->displayTitle().size() > 512},
        {QStringLiteral("dirty"), m_host->noteDocument()->isDirty()},
        {QStringLiteral("sections"), sections},
        {QStringLiteral("omitted_page_titles"), pageCount - emitted},
        {QStringLiteral("section_id"), m_host->currentSectionId()},
        {QStringLiteral("page_id"), m_host->currentPageId()},
        {QStringLiteral("page_title"), page ? page->title.left(512) : QString()},
        {QStringLiteral("page_title_truncated"), page && page->title.size() > 512},
        {QStringLiteral("blocks"), blocks},
        {QStringLiteral("omitted_blocks"), int(all.size() - blocks.size())},
        {QStringLiteral("content_bytes_limit"), byteBudget},
        {QStringLiteral("ink_strokes_not_read"), page ? int(page->envelope.ink.strokes().size()) : 0},
        {QStringLiteral("selection"), m_host->editorSelection().left(65536)},
        {QStringLiteral("selection_truncated"), m_host->editorSelection().size() > 65536},
        {QStringLiteral("draft"), draft}, {QStringLiteral("modals"), modals},
        {QStringLiteral("drawing"), item && item->hasActiveGesture()},
        {QStringLiteral("view"), view},
        {QStringLiteral("live_session"), m_host->live() ? QJsonValue(QJsonObject{
            {QStringLiteral("state"), m_host->live()->state()},
            {QStringLiteral("topic"), m_host->live()->topic().left(4096)},
            {QStringLiteral("topic_truncated"), m_host->live()->topic().size() > 4096},
            {QStringLiteral("source"), m_host->live()->source()},
            {QStringLiteral("recent_transcript"), m_host->live()->recentTranscript().left(65536)},
            {QStringLiteral("recent_transcript_truncated"), m_host->live()->recentTranscript().size() > 65536}}) : QJsonValue::Null},
        {QStringLiteral("ai_draft"), m_host->live() ? QJsonValue(QJsonObject{
            {QStringLiteral("working"), m_host->live()->working()},
            {QStringLiteral("kind"), m_host->live()->draftKind()},
            {QStringLiteral("title"), m_host->live()->draftTitle().left(1024)},
            {QStringLiteral("title_truncated"), m_host->live()->draftTitle().size() > 1024},
            {QStringLiteral("text"), m_host->live()->draftText().left(65536)},
            {QStringLiteral("text_truncated"), m_host->live()->draftText().size() > 65536}}) : QJsonValue::Null},
        {QStringLiteral("focus"), window && window->activeFocusItem()
            ? window->activeFocusItem()->objectName() : QString()}};
}
QString NoteAgent::blockedReason() const
{
    const auto *item = m_host->pageItem();
    if (!m_host->currentPage() || !item) return QStringLiteral("No note page is open");
    if (item->editingBlock() >= 0 || item->hasActiveGesture())
        return QStringLiteral("Finish typing or drawing before an agent edits");
    if (m_host->live() && m_host->live()->active())
        return QStringLiteral("Stop live notes before an agent edits the page");
    if (!state().value(QStringLiteral("modals")).toArray().isEmpty())
        return QStringLiteral("Close the active dialog before an agent edits");
    return {};
}
QString NoteAgent::undoRevisionToken() const
{
    return QString::number(generation()) + QLatin1Char('/') + m_host->currentPageId()
        + QLatin1Char('/') + QString::number(m_host->pageItem() ? m_host->pageItem()->historyIndex() : -1)
        + QLatin1Char('/') + blockedReason();
}
}
