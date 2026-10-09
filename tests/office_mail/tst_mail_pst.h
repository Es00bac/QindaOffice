// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// tst_mail_pst (M76, D-085): the Outlook data file reader — MS-PST's CRC and
// encodings, the NDB, LTP and messaging layers over every builder fixture
// (Unicode and ANSI, no encoding, permute and cyclic), each mapped item
// (mail fields, attachments, an embedded message, a contact, a contact
// group, appointments, tasks), data and subnode trees of every depth, the
// corrupt variants (truncated, a cyclic B-tree, a bad block size, a bad
// CRC, fuzz) failing cleanly, and a 10 000-message file within its time
// bound. One QtTest class over tst_mail_pst.cpp (formats, folders, crypto),
// tst_mail_pst_items.cpp (items) and tst_mail_pst_corrupt.cpp (structure,
// corruption, size). Every fixture is built in-process (pst_fixture.h).

#include "pst_reader.h"

#include <QBuffer>
#include <QObject>

#include <memory>

namespace QindaOffice::Mail::Test {

// A PstFile over bytes in memory (the buffer must outlive the reader).
struct OpenedPst {
    QByteArray bytes;
    QBuffer buffer;
    Pst::PstFile file;

    bool open(const QByteArray &data, QString *error)
    {
        bytes = data;
        buffer.setBuffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        return file.open(&buffer, error);
    }
};

} // namespace QindaOffice::Mail::Test

class TestMailPst : public QObject {
    Q_OBJECT

private slots:
    // tst_mail_pst.cpp
    void cryptTables();
    void checksumsAndSignatures();
    void formats_data();
    void formats();
    void folderTree();
    void ostSignature();
    void refusesOtherFiles();

    // tst_mail_pst_items.cpp
    void mailItems_data();
    void mailItems();
    void embeddedMessage();
    void contactItem_data();
    void contactItem();
    void groupItem();
    void appointmentItems();
    void taskItems();
    void otherItems();
    void msgContactGroup();

    // tst_mail_pst_corrupt.cpp
    void deepTrees();
    void truncatedFiles();
    void cyclicIndex();
    void badBlockSize();
    void badChecksum();
    void corruptionFuzz();
    void tenThousandMessages();
};
