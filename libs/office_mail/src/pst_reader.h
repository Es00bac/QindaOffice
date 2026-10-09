// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// An Outlook data file (.pst, and an .ost in the same format) as folders
// and items (M76, D-085; MS-PST 2.4, the messaging layer): the message
// store, the folder hierarchy from its hierarchy tables, each folder's
// items from its contents table, and each item as a Mapi::Message (its
// property context, recipient table, attachments and embedded messages)
// that the shared mappings read (mapi_mail.h for mail, outlook_items.h for
// contacts, groups, appointments and tasks).
//
// AGENT-CONTRACT (QindaMail's Outlook import, pst_import.cpp): folders are
// walked from ipmSubtree() — the "Top of Personal Folders" the store names,
// or the root folder when it names none — and items are opened one at a
// time with openMessage(), so a large file streams: nothing but the folder
// being read and the item being opened is in memory.
// AGENT-GUARD: a folder that appears twice in the hierarchy (a loop) is the
// importer's to skip (it tracks visited nids); everything below is bounded
// by NodeDatabase and the LTP layer.
//
// Threading: one thread at a time.

#include "mapi_named.h"
#include "mapi_object.h"
#include "pst_ndb.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <memory>

class QIODevice;

namespace QindaOffice::Mail::Pst {

class PropertyContext;

struct Folder {
    quint32 nid = 0;
    QString name;
    QString containerClass;      // PidTagContainerClass: IPF.Note, IPF.Contact, IPF.Appointment, …
    QList<quint32> subfolders;   // hierarchy table rows that are ordinary folders
    QList<quint32> items;        // contents table rows that are ordinary messages
};

class PstFile {
public:
    PstFile();
    ~PstFile();
    PstFile(const PstFile &) = delete;
    PstFile &operator=(const PstFile &) = delete;

    bool open(const QString &path, QString *error);
    bool open(QIODevice *device, QString *error);
    [[nodiscard]] NodeDatabase &database() { return m_db; }
    [[nodiscard]] QString displayName() const { return m_displayName; }
    // PidTagRecordKey of the store: the identity of this file (16 bytes, or
    // empty when the store has none).
    [[nodiscard]] QByteArray recordKey() const { return m_recordKey; }
    [[nodiscard]] quint32 ipmSubtree() const { return m_ipmSubtree; }
    [[nodiscard]] const Mapi::NamedPropertyMap &namedProperties() const { return m_named; }

    bool readFolder(quint32 nid, Folder *out, QString *error);
    // A message (or contact, appointment, task …) by its node id.
    std::unique_ptr<Mapi::Message> openMessage(quint32 nid, QString *error);

    // Internal (pst_objects.cpp): a subnode as a property context.
    bool loadProperties(const Subnode &node, PropertyContext *out, QString *error);

private:
    bool loadStore(QString *error);
    bool loadNode(quint32 nid, PropertyContext *out, QString *error);
    bool loadTableRows(quint32 nid, QList<quint32> *rows, QString *error);

    NodeDatabase m_db;
    Mapi::NamedPropertyMap m_named;
    QString m_displayName;
    QByteArray m_recordKey;
    quint32 m_ipmSubtree = kNidRootFolder;
};

} // namespace QindaOffice::Mail::Pst
