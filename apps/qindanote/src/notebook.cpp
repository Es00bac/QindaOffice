// SPDX-License-Identifier: GPL-3.0-or-later
#include "notebook.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace QindaOffice::Apps::Notes {

QString Notebook::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

bool Notebook::create(const QString &path, QString *error)
{
    m_dir = QDir(path);
    m_open = true;
    if (!m_dir.mkpath(QStringLiteral("assets"))) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create %1/assets").arg(path);
        }
        return false;
    }
    m_name = QFileInfo(path).fileName();
    NoteSection &section = addSection(QStringLiteral("Notes"));
    addPage(section, QStringLiteral("First page"));
    return save(error);
}

bool Notebook::load(const QString &path, QString *error)
{
    m_dir = QDir(path);
    m_open = true;
    QFile file(m_dir.filePath(QStringLiteral("notebook.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
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
    QFile file(m_dir.filePath(QStringLiteral("notebook.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(
                   QJsonObject{{QStringLiteral("name"), m_name}, {QStringLiteral("sections"), sections}})
                   .toJson());
    // Page payloads live beside the index.
    for (const NoteSection &section : m_sections) {
        for (const NotePage &page : section.pages) {
            QString pageError;
            if (!persistPage(section, page, &pageError) && error != nullptr) {
                *error = pageError;
                return false;
            }
        }
    }
    return true;
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

} // namespace QindaOffice::Apps::Notes
