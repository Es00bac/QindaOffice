// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support (M76, D-085; header-only): builds Outlook data files. No
// .pst writer exists on Linux, so every fixture of tst_mail_pst and of
// QindaMail's import tests is generated here: the Unicode (wVer 23) and
// ANSI (wVer 14) formats, the three block encodings, a message store, the
// named-property map, a folder tree with hierarchy and contents tables, and
// items with their recipient tables, attachments and embedded messages.
// Large values, row matrices and many attachments go to subnodes and
// SIBLOCKs exactly as MS-PST lays them out (pst_builder_ltp.h,
// pst_builder_ndb.h).
//
// Use: describe folders and items with PstFolderSpec / PstMessageSpec
// (PropList holds MAPI properties; text() writes PtypString or PtypString8
// for the file's format), name named properties with named(), then build().

#include "mapi_named.h"
#include "pst_builder_ndb.h"

#include <QHash>
#include <QUuid>

#include <memory>

namespace QindaOffice::Mail::Test {

using PstBuild::PropList;

struct PstAttachmentSpec;

struct PstMessageSpec {
    PropList props;
    QList<PropList> recipients;
    QList<PstAttachmentSpec> attachments;
};

struct PstAttachmentSpec {
    PropList props;
    std::shared_ptr<PstMessageSpec> embedded;   // with PidTagAttachMethod 5
};

struct PstFolderSpec {
    QString name;
    QString containerClass;
    QList<PstMessageSpec> messages;
    QList<PstFolderSpec> children;
};

class PstBuilder {
public:
    explicit PstBuilder(bool unicode, quint8 encoding = 1) : m_unicode(unicode), m_encoding(encoding) {}

    [[nodiscard]] bool unicode() const { return m_unicode; }
    QByteArray recordKey = QByteArray::fromHex("5e2a9f00c1b2d3e4f5061728394a5b6c");
    QString storeName = QStringLiteral("Personal Folders");
    PstFolderSpec top{QStringLiteral("Top of Personal Folders"), QString(), {}, {}};

    // The id of a named property (allocated on first use, 0x8000 + n).
    quint16 named(const QUuid &set, quint32 lid) { return name({set, lid, {}}); }
    quint16 named(const QUuid &set, const QString &text) { return name({set, 0, text}); }

    // Filled by build(): folder ids by path ("Inbox/Projects"), item ids in
    // the order they were written.
    QHash<QString, quint32> folderNids;
    QList<quint32> messageNids;
    quint32 topNid = 0;

    QByteArray build(PstBuild::PstLayout *layout = nullptr)
    {
        PstBuild::NdbWriter writer(m_unicode, m_encoding);
        folderNids.clear();
        messageNids.clear();
        m_nextFolder = 0x400;
        m_nextMessage = 0x10000;
        topNid = folderNid();
        writer.add(propertyNode(0x21, 0, storeProps()));
        writer.add(propertyNode(0x61, 0, nameMapProps()));
        PropList root;
        root.longValue(0x3602, 0).longValue(0x3603, 0).boolValue(0x360A, true);
        writer.add(propertyNode(0x122, 0x122, root));
        addTables(writer, 0x122, {QPair<quint32, const PstFolderSpec *>(topNid, &top)}, {}, {});
        addFolder(writer, top, topNid, 0x122, QString());
        return writer.build(layout);
    }

private:
    struct Name {
        QUuid set;
        quint32 lid = 0;
        QString text;
    };

    quint16 name(const Name &wanted)
    {
        for (qsizetype i = 0; i < m_names.size(); ++i) {
            const Name &n = m_names.at(i);
            if (n.set == wanted.set && n.lid == wanted.lid && n.text == wanted.text) return quint16(0x8000 + i);
        }
        m_names.append(wanted);
        return quint16(0x8000 + m_names.size() - 1);
    }

    quint32 folderNid() { return (m_nextFolder++ << 5) | 0x02; }

    PropList storeProps() const
    {
        PropList p;
        p.binary(0x0FF9, recordKey);
        p.text(0x3001, storeName, m_unicode);
        p.binary(0x35E0, PstBuild::le32(0) + recordKey + PstBuild::le32(topNid));
        p.longValue(0x67FF, 0);
        return p;
    }

    // MS-PST 2.4.7: the GUID, entry and string streams.
    PropList nameMapProps() const
    {
        QByteArray guids, entries, strings;
        QList<QUuid> sets;
        for (qsizetype i = 0; i < m_names.size(); ++i) {
            const Name &n = m_names.at(i);
            quint16 guidIndex = 0;
            if (n.set == Mapi::kPsMapi) guidIndex = 1;
            else if (n.set == Mapi::kPsPublicStrings) guidIndex = 2;
            else {
                if (!sets.contains(n.set)) {
                    sets.append(n.set);
                    guids += Mapi::guidBytes(n.set);
                }
                guidIndex = quint16(3 + sets.indexOf(n.set));
            }
            const bool isString = !n.text.isEmpty();
            quint32 identifier = n.lid;
            if (isString) {
                identifier = quint32(strings.size());
                const QByteArray utf16 = PstBuild::utf16(n.text);
                strings += PstBuild::le32(quint32(utf16.size())) + utf16;
                while (strings.size() % 4) strings += '\0';
            }
            entries += PstBuild::le32(identifier) + PstBuild::le16(quint16(guidIndex << 1 | (isString ? 1 : 0)))
                + PstBuild::le16(quint16(i));
        }
        PropList p;
        p.longValue(0x0001, 251).binary(0x0002, guids).binary(0x0003, entries).binary(0x0004, strings);
        return p;
    }

    PstBuild::NdbNode propertyNode(quint32 nid, quint32 parent, const PropList &props)
    {
        quint32 next = 0x100;
        QList<PstBuild::ValueSubnode> values;
        PstBuild::NdbNode node{nid, parent, PstBuild::writePropertyContext(props, maxBlock(), &next, &values), {}};
        for (const PstBuild::ValueSubnode &value : std::as_const(values)) node.subnodes.append({value.nid, 0, value.blocks, {}});
        return node;
    }

    PstBuild::NdbNode tableNode(quint32 nid, quint32 parent, const QList<quint32> &columns,
                                const QList<PstBuild::TableRow> &rows)
    {
        quint32 next = 0x100;
        QList<PstBuild::ValueSubnode> values;
        PstBuild::NdbNode node{nid, parent, PstBuild::writeTableContext(columns, rows, m_unicode, maxBlock(), &next, &values), {}};
        for (const PstBuild::ValueSubnode &value : std::as_const(values)) node.subnodes.append({value.nid, 0, value.blocks, {}});
        return node;
    }

    int maxBlock() const { return m_unicode ? 8176 : 8180; }
    quint16 textType() const { return m_unicode ? 0x001F : 0x001E; }

    // The columns are the (id, type) pairs the rows use, in first-seen order.
    static QList<quint32> columnsOf(const QList<PstBuild::TableRow> &rows)
    {
        QList<quint32> tags;
        for (const PstBuild::TableRow &row : rows) {
            for (const PstBuild::Prop &p : row.cells.props) {
                const quint32 tag = quint32(p.id) << 16 | p.type;
                if (!tags.contains(tag)) tags.append(tag);
            }
        }
        return tags;
    }

    void addTables(PstBuild::NdbWriter &writer, quint32 folder, const QList<QPair<quint32, const PstFolderSpec *>> &children,
                   const QList<quint32> &items, const QList<const PstMessageSpec *> &specs)
    {
        QList<PstBuild::TableRow> hierarchy;
        for (const auto &[nid, child] : children) {
            PropList cells;
            cells.text(0x3001, child->name, m_unicode).longValue(0x3602, quint32(child->messages.size()))
                .longValue(0x3603, 0).boolValue(0x360A, !child->children.isEmpty());
            hierarchy.append({nid, cells});
        }
        QList<PstBuild::TableRow> contents;
        for (qsizetype i = 0; i < items.size(); ++i) {
            PropList cells;   // a few columns Outlook's contents tables carry
            for (const PstBuild::Prop &p : specs.at(i)->props.props) {
                if (p.id == 0x001A || p.id == 0x0037 || p.id == 0x0E07 || p.id == 0x0E06) cells.props.append(p);
            }
            contents.append({items.at(i), cells});
        }
        const quint32 base = folder & ~0x1Fu;
        writer.add(tableNode(base | 0x0D, folder, columnsOf(hierarchy), hierarchy));
        writer.add(tableNode(base | 0x0E, folder, columnsOf(contents), contents));
        writer.add(tableNode(base | 0x0F, folder, {}, {}));
    }

    void addFolder(PstBuild::NdbWriter &writer, const PstFolderSpec &folder, quint32 nid, quint32 parent, const QString &path)
    {
        folderNids.insert(path, nid);
        PropList props;
        props.text(0x3001, folder.name, m_unicode).longValue(0x3602, quint32(folder.messages.size()))
            .longValue(0x3603, 0).boolValue(0x360A, !folder.children.isEmpty());
        if (!folder.containerClass.isEmpty()) props.text(0x3613, folder.containerClass, m_unicode);
        writer.add(propertyNode(nid, parent, props));
        QList<QPair<quint32, const PstFolderSpec *>> children;
        for (const PstFolderSpec &child : folder.children) children.append({folderNid(), &child});
        QList<quint32> items;
        QList<const PstMessageSpec *> specs;
        for (const PstMessageSpec &message : folder.messages) {
            const quint32 messageNid = (m_nextMessage++ << 5) | 0x04;
            items.append(messageNid);
            specs.append(&message);
            messageNids.append(messageNid);
            writer.add(messageNode(message, messageNid, nid));
        }
        addTables(writer, nid, children, items, specs);
        for (const auto &[childNid, child] : children)
            addFolder(writer, *child, childNid, nid, path.isEmpty() ? child->name : path + QLatin1Char('/') + child->name);
    }

    // A message (or an embedded message): its property context, recipient
    // and attachment tables, attachments and large values as subnodes.
    PstBuild::NdbNode messageNode(const PstMessageSpec &message, quint32 nid, quint32 parent)
    {
        PstBuild::NdbNode node = propertyNode(nid, parent, message.props);
        node.parent = parent;
        QList<PstBuild::TableRow> recipients;
        for (qsizetype i = 0; i < message.recipients.size(); ++i) recipients.append({quint32(i), message.recipients.at(i)});
        PstBuild::NdbNode recipientTable = tableNode(0x692, 0, columnsOf(recipients), recipients);
        node.subnodes.append(recipientTable);
        QList<PstBuild::TableRow> attachmentRows;
        for (qsizetype i = 0; i < message.attachments.size(); ++i) {
            const PstAttachmentSpec &spec = message.attachments.at(i);
            const quint32 attachmentNid = (quint32(0x8001 + i) << 5) | 0x05;
            PropList props = spec.props;
            PstBuild::NdbNode embedded;
            if (spec.embedded) {
                embedded = messageNode(*spec.embedded, (0x4001u << 5) | 0x04, 0);
                embedded.parent = 0;
                props.object(0x3701, embedded.nid, 0);
            }
            PstBuild::NdbNode attachment = propertyNode(attachmentNid, 0, props);
            attachment.parent = 0;
            if (spec.embedded) attachment.subnodes.append(embedded);
            node.subnodes.append(attachment);
            PropList cells;
            for (const PstBuild::Prop &p : spec.props.props) {
                if (p.id == 0x3705 || p.id == 0x3707 || p.id == 0x370B) cells.props.append(p);
            }
            attachmentRows.append({attachmentNid, cells});
        }
        if (!message.attachments.isEmpty()) node.subnodes.append(tableNode(0x671, 0, columnsOf(attachmentRows), attachmentRows));
        return node;
    }

    bool m_unicode;
    quint8 m_encoding;
    QList<Name> m_names;
    quint32 m_nextFolder = 0x400;
    quint32 m_nextMessage = 0x10000;
};

} // namespace QindaOffice::Mail::Test
