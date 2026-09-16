// SPDX-License-Identifier: GPL-3.0-or-later
#include "notebook.h"

#include <QFile>
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
    const QString dirPath = QStringLiteral("sections/%1/pages").arg(section.id);
    if (!m_dir.mkpath(dirPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1").arg(dirPath);
        }
        return false;
    }
    QFile file(m_dir.filePath(dirPath + QStringLiteral("/%1.json").arg(page.id)));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(Ink::pageToJson(page.envelope));
    return true;
}

bool Notebook::loadPage(NoteSection &section, NotePage &page, QString *error) const
{
    QFile file(m_dir.filePath(
        QStringLiteral("sections/%1/pages/%2.json").arg(section.id, page.id)));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return Ink::pageFromJson(file.readAll(), &page.envelope);
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

} // namespace QindaOffice::Apps::Notes
