// SPDX-License-Identifier: GPL-3.0-or-later
#include "notebook.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace QindaOffice::Apps::Notes {

namespace {

// AGENT-GUARD: ids come from notebook.json and are interpolated into file
// paths. A hostile notebook with "../.." in an id must never escape the
// notebook directory; reject anything but the safe identifier shape.
bool idIsSafe(const QString &id)
{
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return safe.match(id).hasMatch();
}

// Atomic write: the earlier release truncate-wrote notebook.json and page
// payloads, so a crash or full disk mid-save destroyed the previous content.
bool writeJsonAtomically(const QString &path, const QByteArray &payload, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace

QString Notebook::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

bool Notebook::create(const QString &path, QString *error)
{
    QDir dir(path);
    if (!dir.mkpath(QStringLiteral("assets"))) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1/assets").arg(path);
        }
        return false;
    }
    // AGENT-GUARD: m_open gates directory(); setting it before mkpath
    // succeeded left a "half-open" notebook that later saves wrote into the
    // failed directory.
    m_dir = dir;
    m_open = true;
    m_name = QFileInfo(path).fileName();
    NoteSection &section = addSection(QStringLiteral("Notes"));
    addPage(section, QStringLiteral("First page"));
    return save(error);
}

bool Notebook::load(const QString &path, QString *error)
{
    QDir dir(path);
    QFile file(dir.filePath(QStringLiteral("notebook.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray payload = file.readAll();
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    // A corrupt file previously loaded as an EMPTY notebook and the next
    // edit overwrote the bytes — unrecoverable data loss. Fail honestly.
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("notebook.json is not valid JSON");
        }
        return false;
    }
    const QJsonObject root = document.object();
    m_dir = dir;
    m_open = true;
    m_name = root.value(QStringLiteral("name")).toString();
    m_sections.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("sections")).toArray()) {
        const QJsonObject object = value.toObject();
        NoteSection section;
        section.id = object.value(QStringLiteral("id")).toString();
        section.name = object.value(QStringLiteral("name")).toString();
        for (const QJsonValue &pageValue : object.value(QStringLiteral("pages")).toArray()) {
            const QJsonObject pageObject = pageValue.toObject();
            NotePage page;
            page.id = pageObject.value(QStringLiteral("id")).toString();
            page.title = pageObject.value(QStringLiteral("title")).toString();
            section.pages.append(page);
        }
        m_sections.append(section);
    }
    return true;
}

bool Notebook::save(QString *error) const
{
    if (!m_open) {
        if (error != nullptr) {
            *error = QStringLiteral("notebook has no directory yet");
        }
        return false;
    }
    // Page payloads are persisted BEFORE the index: a mid-save failure then
    // leaves the previous index intact (referencing pages that still exist),
    // never an index pointing at files that were never written.
    QString lastError;
    for (const NoteSection &section : m_sections) {
        if (!idIsSafe(section.id)) {
            lastError = QStringLiteral("unsafe section id: %1").arg(section.id);
            continue;
        }
        for (const NotePage &page : section.pages) {
            if (!idIsSafe(page.id)) {
                lastError = QStringLiteral("unsafe page id: %1").arg(page.id);
                continue;
            }
            // AGENT-GUARD: never write a page that was never read. Pages load
            // lazily, so an unopened page's envelope is empty in memory —
            // writing it replaced the user's notes with a blank page. Its
            // file on disk is already the current content; leave it alone.
            if (!page.loaded) {
                continue;
            }
            const QString dirPath = QStringLiteral("sections/%1/pages").arg(section.id);
            if (!m_dir.mkpath(dirPath)) {
                lastError = QStringLiteral("cannot create %1").arg(dirPath);
                continue;
            }
            if (!writeJsonAtomically(
                    m_dir.filePath(dirPath + QStringLiteral("/%1.json").arg(page.id)),
                    Ink::pageToJson(page.envelope), &lastError)) {
                continue;
            }
        }
    }
    if (!lastError.isEmpty()) {
        // AGENT-GUARD: the page-write failure must fail the save even when
        // the caller passed no error out-param — returning true here meant
        // silent data loss through the public API.
        if (error != nullptr) {
            *error = lastError;
        }
        return false;
    }
    QJsonArray sections;
    for (const NoteSection &section : m_sections) {
        QJsonArray pages;
        for (const NotePage &page : section.pages) {
            pages.append(QJsonObject{{QStringLiteral("id"), page.id},
                                     {QStringLiteral("title"), page.title}});
        }
        sections.append(QJsonObject{{QStringLiteral("id"), section.id},
                                    {QStringLiteral("name"), section.name},
                                    {QStringLiteral("pages"), pages}});
    }
    return writeJsonAtomically(
        m_dir.filePath(QStringLiteral("notebook.json")),
        QJsonDocument(QJsonObject{{QStringLiteral("name"), m_name},
                                  {QStringLiteral("sections"), sections}})
            .toJson(),
        error);
}

NoteSection &Notebook::addSection(const QString &name)
{
    NoteSection section;
    section.id = newId();
    section.name = name;
    m_sections.append(section);
    return m_sections.last();
}

NotePage &Notebook::addPage(NoteSection &section, const QString &title)
{
    NotePage page;
    page.id = newId();
    page.title = title;
    page.envelope.id = page.id;
    page.envelope.title = title;
    page.loaded = true; // created in memory: this IS the content
    section.pages.append(page);
    // The section reference may dangle after append; refresh by id.
    for (NoteSection &candidate : m_sections) {
        if (candidate.id == section.id) {
            return candidate.pages.last();
        }
    }
    return section.pages.last();
}

bool Notebook::persistPage(const NoteSection &section, const NotePage &page, QString *error) const
{
    // AGENT-GUARD: an untitled notebook has no directory. QDir()'s path is
    // ".", so this used to write sections/<id>/pages/<id>.json into whatever
    // directory the process was launched from. In-memory content is covered
    // by the recovery journal until the first real save.
    if (!m_open) {
        if (error != nullptr) {
            *error = QStringLiteral("notebook has no directory yet");
        }
        return false;
    }
    if (!idIsSafe(section.id) || !idIsSafe(page.id)) {
        if (error != nullptr) {
            *error = QStringLiteral("unsafe notebook id");
        }
        return false;
    }
    const QString dirPath = QStringLiteral("sections/%1/pages").arg(section.id);
    if (!m_dir.mkpath(dirPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1").arg(dirPath);
        }
        return false;
    }
    // Atomic and checked, like every other document write (decisions D-010).
    return writeJsonAtomically(m_dir.filePath(dirPath + QStringLiteral("/%1.json").arg(page.id)),
                               Ink::pageToJson(page.envelope), error);
}

bool Notebook::loadPage(NoteSection &section, NotePage &page, QString *error) const
{
    if (!m_open || !idIsSafe(section.id) || !idIsSafe(page.id)) {
        if (error != nullptr) {
            *error = QStringLiteral("notebook has no directory yet");
        }
        return false;
    }
    QFile file(m_dir.filePath(
        QStringLiteral("sections/%1/pages/%2.json").arg(section.id, page.id)));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    if (!Ink::pageFromJson(file.readAll(), &page.envelope)) {
        if (error != nullptr) {
            *error = QStringLiteral("page %1 is not valid JSON").arg(page.id);
        }
        return false;
    }
    page.loaded = true;
    return true;
}

NoteSection *Notebook::sectionById(const QString &id)
{
    for (NoteSection &section : m_sections) {
        if (section.id == id) {
            return &section;
        }
    }
    return nullptr;
}

NotePage *Notebook::pageById(const QString &sectionId, const QString &pageId)
{
    NoteSection *section = sectionById(sectionId);
    if (section == nullptr) {
        return nullptr;
    }
    for (NotePage &page : section->pages) {
        if (page.id == pageId) {
            return &page;
        }
    }
    return nullptr;
}

NoteSection *Notebook::sectionOf(const QString &pageId)
{
    for (NoteSection &section : m_sections) {
        for (const NotePage &page : section.pages) {
            if (page.id == pageId) {
                return &section;
            }
        }
    }
    return nullptr;
}

bool Notebook::copyStructure(const QString &targetPath, QString *error)
{
    Notebook replica = *this;
    // AGENT-GUARD: read every page from the SOURCE first. save() only writes
    // loaded pages, so a Save As of a notebook where most pages were never
    // opened would otherwise create a target holding just the one page the
    // user happened to look at.
    if (m_open) {
        for (NoteSection &section : replica.m_sections) {
            for (NotePage &page : section.pages) {
                if (!page.loaded) {
                    static_cast<void>(loadPage(section, page, nullptr));
                }
            }
        }
    }
    replica.m_dir = QDir(targetPath);
    replica.m_open = true;
    if (!replica.m_dir.mkpath(QStringLiteral("assets"))) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1/assets").arg(targetPath);
        }
        return false;
    }
    if (!replica.save(error)) {
        return false;
    }
    m_dir = replica.m_dir;
    return true;
}

QByteArray Notebook::toRecoveryJson() const
{
    QJsonArray sections;
    for (const NoteSection &section : m_sections) {
        QJsonArray pages;
        for (const NotePage &page : section.pages) {
            QJsonObject entry{{QStringLiteral("id"), page.id},
                              {QStringLiteral("title"), page.title},
                              {QStringLiteral("loaded"), page.loaded}};
            if (page.loaded) {
                // Only a loaded page carries content worth recovering; an
                // unloaded one is still exactly what its file holds.
                entry.insert(QStringLiteral("page"),
                             QJsonDocument::fromJson(Ink::pageToJson(page.envelope)).object());
            }
            pages.append(entry);
        }
        sections.append(QJsonObject{{QStringLiteral("id"), section.id},
                                    {QStringLiteral("name"), section.name},
                                    {QStringLiteral("pages"), pages}});
    }
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("version"), 1},
                             {QStringLiteral("name"), m_name},
                             {QStringLiteral("dir"), m_open ? m_dir.path() : QString()},
                             {QStringLiteral("sections"), sections}})
        .toJson(QJsonDocument::Compact);
}

bool Notebook::fromRecoveryJson(const QByteArray &json, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return fail(QStringLiteral("recovery payload is not a JSON object"));
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(0) != 1) {
        return fail(QStringLiteral("unsupported notebook recovery format"));
    }
    QVector<NoteSection> restored;
    for (const QJsonValue &value : root.value(QStringLiteral("sections")).toArray()) {
        const QJsonObject object = value.toObject();
        NoteSection section;
        section.id = object.value(QStringLiteral("id")).toString();
        section.name = object.value(QStringLiteral("name")).toString();
        for (const QJsonValue &pageValue : object.value(QStringLiteral("pages")).toArray()) {
            const QJsonObject pageObject = pageValue.toObject();
            NotePage page;
            page.id = pageObject.value(QStringLiteral("id")).toString();
            page.title = pageObject.value(QStringLiteral("title")).toString();
            page.loaded = pageObject.value(QStringLiteral("loaded")).toBool(false);
            if (page.loaded) {
                static_cast<void>(Ink::pageFromJson(
                    QJsonDocument(pageObject.value(QStringLiteral("page")).toObject())
                        .toJson(QJsonDocument::Compact),
                    &page.envelope));
            }
            section.pages.append(page);
        }
        restored.append(section);
    }
    if (restored.isEmpty()) {
        return fail(QStringLiteral("recovery payload has no sections"));
    }
    m_name = root.value(QStringLiteral("name")).toString();
    m_sections = std::move(restored);
    // Re-attach to the source directory when it is still there, so pages the
    // session never opened lazily load from disk instead of coming back
    // blank. A notebook that was never saved stays untitled.
    const QString dir = root.value(QStringLiteral("dir")).toString();
    m_open = !dir.isEmpty()
        && QFile::exists(QDir(dir).filePath(QStringLiteral("notebook.json")));
    m_dir = m_open ? QDir(dir) : QDir();
    return true;
}

} // namespace QindaOffice::Apps::Notes
