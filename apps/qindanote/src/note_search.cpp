// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_search.h"

#include "note_blocks.h"
#include "note_host.h"
#include "note_rich_render.h"
#include "note_tags.h"

#include "paths.h"
#include "recent_files.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QQuickWindow>

#include <algorithm>

namespace QindaOffice::Apps::Notes {

namespace {

QList<QPointer<NoteHost>> &registry()
{
    static QList<QPointer<NoteHost>> hosts;
    return hosts;
}

QHash<QString, NoteSearch::Navigation> &pending()
{
    static QHash<QString, NoteSearch::Navigation> targets;
    return targets;
}

QString canonical(const QString &dir)
{
    const QString path = QFileInfo(dir).canonicalFilePath();
    return path.isEmpty() ? QDir::cleanPath(dir) : path;
}

bool containsAny(const QString &text, const QStringList &words)
{
    return std::any_of(words.begin(), words.end(),
                       [&text](const QString &word) { return text.contains(word, Qt::CaseInsensitive); });
}

// The first word found, with a little before it and more after it (the
// pane is narrow; the match should be in view).
QString snippet(const QString &text, const QStringList &words)
{
    const QString flat = text.simplified();
    qsizetype at = -1;
    for (const QString &word : words) {
        const qsizetype found = flat.indexOf(word, 0, Qt::CaseInsensitive);
        if (found >= 0 && (at < 0 || found < at)) {
            at = found;
        }
    }
    const qsizetype from = qMax<qsizetype>(0, at - 16);
    QString out = flat.mid(from, 100);
    if (from > 0) {
        out.prepend(QStringLiteral("…"));
    }
    if (from + 100 < flat.size()) {
        out.append(QStringLiteral("…"));
    }
    return out;
}

QString tagItemText(const QJsonObject &block, const QJsonObject &entry)
{
    const QString paragraph = entry.value(QStringLiteral("p")).toString();
    if (!paragraph.isEmpty()) {
        return RichRender::paragraphText(block.value(QStringLiteral("rich")).toObject(), paragraph);
    }
    return block.value(QStringLiteral("text")).toString().section(QLatin1Char('\n'), 0, 0);
}

void searchPage(const Notebook &notebook, const NoteSection &section, const NotePage &page,
                const QStringList &words, QList<SearchHit> *out)
{
    const QJsonArray blocks = blocksArray(page.envelope.blocksJson);
    QString everything = page.title;
    for (const QJsonValue &value : blocks) {
        const QJsonObject block = value.toObject();
        everything += QLatin1Char('\n') + block.value(QStringLiteral("text")).toString();
        for (const QJsonValue &tag : block.value(QStringLiteral("tags")).toArray()) {
            everything += QLatin1Char('\n') + Tags::fromEntry(tag.toObject()).name;
        }
    }
    for (const QString &word : words) {
        if (!everything.contains(word, Qt::CaseInsensitive)) {
            return; // OneNote's rule: every word somewhere on the page
        }
    }
    const QString dir = notebook.directory();
    const QString name = notebook.name().isEmpty() ? QFileInfo(dir).fileName() : notebook.name();
    SearchHit base{dir, name, section.id, section.name, page.id, page.title, -1, QString(), QString(), QString()};
    if (containsAny(page.title, words)) {
        SearchHit hit = base;
        hit.kind = QStringLiteral("title");
        hit.snippet = page.title;
        out->append(hit);
    }
    for (int i = 0; i < blocks.size(); ++i) {
        const QJsonObject block = blocks.at(i).toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        const QString text = block.value(QStringLiteral("text")).toString();
        if ((type == QLatin1String("text") || type == QLatin1String("check") || type == QLatin1String("markdown"))
            && containsAny(text, words)) {
            SearchHit hit = base;
            hit.block = i;
            hit.kind = type;
            hit.snippet = snippet(text, words);
            out->append(hit);
        }
        for (const QJsonValue &value : block.value(QStringLiteral("tags")).toArray()) {
            const Tags::TagDef def = Tags::fromEntry(value.toObject());
            if (def.isValid() && containsAny(def.name, words)) {
                SearchHit hit = base;
                hit.block = i;
                hit.kind = QStringLiteral("tag");
                hit.tagId = def.id;
                hit.snippet = QStringLiteral("%1 — %2").arg(def.name, tagItemText(block, value.toObject()));
                out->append(hit);
            }
        }
    }
}

} // namespace

namespace NoteSearch {

QList<SearchHit> searchNotebook(Notebook &notebook, const QString &query, const QString &sectionId,
                                const QString &pageId)
{
    QList<SearchHit> hits;
    const QStringList words = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return hits;
    }
    for (const NoteSection &constSection : notebook.sections()) {
        if (!sectionId.isEmpty() && constSection.id != sectionId) {
            continue;
        }
        NoteSection *section = notebook.sectionById(constSection.id);
        for (NotePage &page : section->pages) {
            if (!pageId.isEmpty() && page.id != pageId) {
                continue;
            }
            // AGENT-GUARD: unopened pages are empty in memory (notebook.h).
            if (!page.loaded && notebook.isOpen()) {
                static_cast<void>(notebook.loadPage(*section, page, nullptr));
            }
            searchPage(notebook, *section, page, words, &hits);
        }
    }
    return hits;
}

void registerHost(NoteHost *host)
{
    registry().append(host);
}

void unregisterHost(NoteHost *host)
{
    registry().removeIf([host](const QPointer<NoteHost> &entry) { return entry.isNull() || entry == host; });
}

QList<NoteHost *> openHosts()
{
    QList<NoteHost *> hosts;
    for (const QPointer<NoteHost> &host : std::as_const(registry())) {
        if (!host.isNull()) {
            hosts.append(host);
        }
    }
    return hosts;
}

void requestNavigation(const QString &notebookDir, const Navigation &target)
{
    pending().insert(canonical(notebookDir), target);
}

std::optional<Navigation> takeNavigation(const QString &notebookDir)
{
    const QString key = canonical(notebookDir);
    if (!pending().contains(key)) {
        return std::nullopt;
    }
    return pending().take(key);
}

} // namespace NoteSearch

NoteSearchModel::NoteSearchModel(NoteHost *host)
    : QAbstractListModel(host)
    , m_host(host)
{
}

int NoteSearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NoteSearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case KindRole:
        return row.kind;
    case TextRole:
    case Qt::DisplayRole:
        return row.text;
    case DetailRole:
        return row.detail;
    case MatchRole:
        return row.hit.kind;
    case IconRole: {
        if (row.kind == QLatin1String("section")) {
            return QStringLiteral("notebook");
        }
        if (row.kind == QLatin1String("page")) {
            return QStringLiteral("file-text");
        }
        const QString kind = row.hit.kind;
        if (kind == QLatin1String("tag")) {
            const Tags::TagDef def = Tags::byId(row.hit.tagId);
            return def.icon.isEmpty() ? QStringLiteral("tag") : def.icon;
        }
        return kind == QLatin1String("title")      ? QStringLiteral("heading")
            : kind == QLatin1String("check")       ? QStringLiteral("square-check")
            : kind == QLatin1String("markdown")    ? QStringLiteral("audio-lines")
                                                   : QStringLiteral("type");
    }
    case ColorRole:
        return row.hit.kind == QLatin1String("tag") ? Tags::byId(row.hit.tagId).color : QColor();
    default:
        return {};
    }
}

QHash<int, QByteArray> NoteSearchModel::roleNames() const
{
    return {{KindRole, "kind"},   {TextRole, "text"}, {DetailRole, "detail"},
            {MatchRole, "match"}, {IconRole, "icon"}, {ColorRole, "tagColor"}};
}

void NoteSearchModel::setQuery(const QString &query)
{
    if (m_query == query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    refresh();
}

void NoteSearchModel::setScope(const QString &scope)
{
    if (m_scope == scope) {
        return;
    }
    m_scope = scope;
    emit scopeChanged();
    refresh();
}

void NoteSearchModel::refresh()
{
    QList<SearchHit> hits;
    if (!m_query.trimmed().isEmpty()) {
        m_host->flushPersist();
        Notebook *own = m_host->noteDocument()->notebook();
        const QString section = m_scope == QLatin1String("section") || m_scope == QLatin1String("page")
            ? m_host->currentSectionId() : QString();
        const QString page = m_scope == QLatin1String("page") ? m_host->currentPageId() : QString();
        hits = NoteSearch::searchNotebook(*own, m_query, section, page);
        for (SearchHit &hit : hits) {
            hit.host = m_host;
        }
        if (m_scope == QLatin1String("all")) {
            // Directories already searched; untitled notebooks have none.
            QStringList seen;
            if (!own->directory().isEmpty()) {
                seen.append(canonical(own->directory()));
            }
            for (NoteHost *other : NoteSearch::openHosts()) {
                Notebook *notebook = other->noteDocument()->notebook();
                const QString dir = notebook->directory();
                if (other == m_host || (!dir.isEmpty() && seen.contains(canonical(dir)))) {
                    continue;
                }
                if (!dir.isEmpty()) {
                    seen.append(canonical(dir));
                }
                QList<SearchHit> found = NoteSearch::searchNotebook(*notebook, m_query);
                for (SearchHit &hit : found) {
                    hit.host = other;
                }
                hits += found;
            }
            // Recent notebooks on disk, read without opening a window.
            Core::RecentFiles recent;
            static_cast<void>(recent.load(Core::configDirFor(Core::appLeaf()) + QStringLiteral("/recent.json")));
            for (const QString &path : recent.paths()) {
                if (seen.contains(canonical(path)) || !QFileInfo::exists(path + QStringLiteral("/notebook.json"))) {
                    continue;
                }
                seen.append(canonical(path));
                Notebook notebook;
                if (notebook.load(path)) {
                    hits += NoteSearch::searchNotebook(notebook, m_query);
                }
            }
        }
    }
    beginResetModel();
    m_rows.clear();
    m_hitCount = int(hits.size());
    m_pageCount = 0;
    QString lastSection;
    QString lastPage;
    int pageRow = -1;
    int pageHits = 0;
    const auto closePage = [&] {
        if (pageRow >= 0) {
            m_rows[pageRow].detail = tr("%n match(es)", "", pageHits);
        }
    };
    for (const SearchHit &hit : std::as_const(hits)) {
        const QString sectionKey = hit.notebookDir + QLatin1Char('/') + hit.sectionId;
        if (sectionKey != lastSection) {
            const QString text = m_scope == QLatin1String("all")
                ? QStringLiteral("%1 — %2").arg(hit.notebookName, hit.sectionName) : hit.sectionName;
            m_rows.append({QStringLiteral("section"), text, QString(), hit});
            lastSection = sectionKey;
            lastPage.clear();
        }
        if (hit.pageId != lastPage) {
            closePage();
            SearchHit pageHit = hit;
            pageHit.block = -1;
            pageRow = int(m_rows.size());
            pageHits = 0;
            m_rows.append({QStringLiteral("page"), hit.pageTitle, QString(), pageHit});
            lastPage = hit.pageId;
            ++m_pageCount;
        }
        ++pageHits;
        const QString detail = hit.kind == QLatin1String("title") ? tr("Page title")
            : hit.kind == QLatin1String("check")                  ? tr("Checklist")
            : hit.kind == QLatin1String("markdown")               ? tr("Notes")
            : hit.kind == QLatin1String("tag")                    ? tr("Tag")
                                                                  : tr("Text");
        m_rows.append({QStringLiteral("hit"), hit.snippet, detail, hit});
    }
    closePage();
    endResetModel();
    emit resultsChanged();
}

const SearchHit *NoteSearchModel::hitAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).kind != QLatin1String("section") ? &m_rows.at(row).hit
                                                                                              : nullptr;
}

void NoteSearchModel::activate(int row)
{
    const SearchHit *hit = hitAt(row);
    if (hit == nullptr) {
        return;
    }
    if (hit->host == m_host) {
        m_host->navigateTo(hit->sectionId, hit->pageId, hit->block);
        return;
    }
    const QString target = canonical(hit->notebookDir);
    for (NoteHost *other : NoteSearch::openHosts()) {
        const QString dir = other->noteDocument()->notebook()->directory();
        if (other == hit->host || (!hit->notebookDir.isEmpty() && !dir.isEmpty() && canonical(dir) == target)) {
            other->navigateTo(hit->sectionId, hit->pageId, hit->block);
            if (QQuickWindow *window = other->window()) {
                window->raise();
                window->requestActivate();
            }
            return;
        }
    }
    if (hit->notebookDir.isEmpty()) {
        return; // an untitled notebook whose window has closed
    }
    // Not open: its window opens it, then goes there (documentReplaced).
    NoteSearch::requestNavigation(hit->notebookDir, {hit->sectionId, hit->pageId, hit->block});
    emit m_host->openPathsRequested({hit->notebookDir});
}

} // namespace QindaOffice::Apps::Notes
