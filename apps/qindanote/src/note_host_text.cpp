// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's text side: find / replace across every page of the notebook
// (titles and typed blocks; ink carries no text), Export PDF and the print
// renderer. Split from note_host.cpp for the line budget.

#include "note_host.h"

#include "navigator_model.h"
#include "note_blocks.h"
#include "note_page_item.h"
#include "note_page_render.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPrinter>
#include <QUrl>

namespace QindaOffice::Apps::Notes {

namespace {

// Title plus every typed text block of one page. Ink is not text and is
// deliberately not searched (handwriting recognition is not in this suite).
QString pageText(const NotePage &page)
{
    QString text = page.title;
    for (const QJsonValue &value : blocksArray(page.envelope.blocksJson)) {
        const QJsonObject object = value.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text") || type == QLatin1String("check")
            || type == QLatin1String("markdown")) {
            text += QLatin1Char('\n') + object.value(QStringLiteral("text")).toString();
        }
    }
    return text;
}

// Rewrites every typed block and the page title; returns the number of
// occurrences replaced. Ink carries no text, so it is untouched.
int replaceInPage(NotePage &page, const QString &needle, const QString &replacement,
                  Qt::CaseSensitivity sensitivity)
{
    int hits = page.title.count(needle, sensitivity);
    if (hits > 0) {
        page.title.replace(needle, replacement, sensitivity);
        page.envelope.title = page.title;
    }
    QJsonArray blocks = blocksArray(page.envelope.blocksJson);
    bool touched = false;
    for (int i = 0; i < blocks.size(); ++i) {
        QJsonObject object = blocks.at(i).toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("text") && type != QLatin1String("check")
            && type != QLatin1String("markdown")) {
            continue;
        }
        QString text = object.value(QStringLiteral("text")).toString();
        const int found = text.count(needle, sensitivity);
        if (found == 0) {
            continue;
        }
        text.replace(needle, replacement, sensitivity);
        object.insert(QStringLiteral("text"), text);
        blocks.replace(i, object);
        hits += found;
        touched = true;
    }
    if (touched) {
        page.envelope.blocksJson = withBlocksArray(page.envelope.blocksJson, blocks);
    }
    return hits;
}

} // namespace

bool NoteHost::findStep(const QString &needle, bool caseSensitive, bool backward)
{
    if (needle.isEmpty()) {
        return false;
    }
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    Notebook *notebook = m_document->notebook();
    struct Hit {
        QString sectionId;
        QString pageId;
    };
    QList<Hit> hits;
    for (const NoteSection &constSection : notebook->sections()) {
        NoteSection *section = notebook->sectionById(constSection.id);
        if (section == nullptr) {
            continue;
        }
        for (NotePage &page : section->pages) {
            // AGENT-GUARD: unopened pages are empty in memory; a search that
            // skipped them would report "not found" for text that is there.
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            if (pageText(page).contains(needle, cs)) {
                hits.append({constSection.id, page.id});
            }
        }
    }
    if (hits.isEmpty()) {
        showStatusMessage(tr("Not found: %1").arg(needle));
        return false;
    }
    int current = -1;
    for (int i = 0; i < hits.size(); ++i) {
        if (hits.at(i).pageId == m_currentPageId) {
            current = i;
        }
    }
    // Wrap in both directions; off the hit list, Next starts at the first
    // match and Previous at the last.
    int index = current < 0 ? (backward ? int(hits.size()) - 1 : 0)
                            : (backward ? (current - 1 + int(hits.size())) % int(hits.size())
                                        : (current + 1) % int(hits.size()));
    const Hit &hit = hits.at(index);
    selectPage(hit.sectionId, hit.pageId);
    showStatusMessage(tr("Match %1 of %2").arg(index + 1).arg(hits.size()));
    return true;
}

bool NoteHost::findNext(const QString &needle, bool caseSensitive)
{
    return findStep(needle, caseSensitive, false);
}

bool NoteHost::findPrevious(const QString &needle, bool caseSensitive)
{
    return findStep(needle, caseSensitive, true);
}

int NoteHost::replaceAll(const QString &needle, const QString &replacement)
{
    if (needle.isEmpty()) {
        return 0;
    }
    // AGENT-GUARD: persist and DETACH the open page first. Its live editor
    // writes into blocksJson, so a rewrite underneath it would be
    // overwritten by the next keystroke.
    if (m_pageItem != nullptr) {
        m_pageItem->commitEdit();
    }
    flushPersist();
    const QString sectionId = m_currentSectionId;
    const QString pageId = m_currentPageId;
    if (m_pageItem != nullptr) {
        m_pageItem->setPage(nullptr);
    }
    Notebook *notebook = m_document->notebook();
    int hits = 0;
    for (const NoteSection &constSection : notebook->sections()) {
        NoteSection *section = notebook->sectionById(constSection.id);
        if (section == nullptr) {
            continue;
        }
        for (NotePage &page : section->pages) {
            // Pages that were never opened hold nothing in memory; a sweep
            // that skipped them would silently miss most of the notebook.
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            hits += replaceInPage(page, needle, replacement, Qt::CaseSensitive);
        }
    }
    if (hits > 0) {
        m_document->setDirty(true);
    }
    rebuildNavigator();
    openPage(sectionId, pageId);
    showStatusMessage(hits > 0 ? tr("Replaced %1 occurrence(s)").arg(hits)
                               : tr("“%1” not found").arg(needle));
    return hits;
}

void NoteHost::exportPdfTo(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        return;
    }
    flushPersist();
    static_cast<void>(exportPdf(path));
}

Quick::PrintRenderCallback NoteHost::printRenderCallback() const
{
    // Snapshot the pages up front: the render runs inside the print flow
    // and must not read a notebook the user could still be editing. Pages
    // that were never opened are read from disk first.
    QList<NotePage> pages;
    Notebook *notebook = m_document->notebook();
    for (const NoteSection &constSection : notebook->sections()) {
        NoteSection *section = notebook->sectionById(constSection.id);
        if (section == nullptr) {
            continue;
        }
        for (NotePage &page : section->pages) {
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            if (page.loaded) {
                pages.append(page);
            }
        }
    }
    const QSizeF pageSize = m_pageItem != nullptr ? m_pageItem->sheetSize() : QSizeF(760, 1004);
    return [pages, pageSize](QPrinter *printer, QPainter *painter) {
        if (pages.isEmpty()) {
            return;
        }
        const QRectF sheet(0, 0, printer->width(), printer->height());
        const qreal scale = qMin(sheet.width() / pageSize.width(),
                                 sheet.height() / pageSize.height());
        for (int i = 0; i < pages.size(); ++i) {
            if (i > 0) {
                printer->newPage();
            }
            painter->save();
            painter->translate(sheet.center()
                               - QPointF(pageSize.width() * scale / 2.0,
                                         pageSize.height() * scale / 2.0));
            painter->scale(scale, scale);
            renderNotePage(painter, pages.at(i), pageSize);
            painter->restore();
        }
    };
}

} // namespace QindaOffice::Apps::Notes
