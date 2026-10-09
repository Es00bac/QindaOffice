// SPDX-License-Identifier: GPL-3.0-or-later
// Mapi:: property bags over MS-OXMSG storages (msg_objects.h).

#include "msg_objects.h"

#include "mapi_mail.h"

namespace QindaOffice::Mail::MsgDetail {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

// One stream of the "__nameid_version1.0" storage, or empty.
QByteArray nameStream(const CfbReader &cfb, int storage, const QString &name)
{
    const int id = cfb.child(storage, name);
    const CfbReader::Entry *entry = cfb.entry(id);
    QByteArray bytes;
    if (entry && entry->type == CfbReader::Entry::Stream && entry->size <= 16 * 1024 * 1024) cfb.read(id, &bytes, nullptr);
    return bytes;
}

} // namespace

std::unique_ptr<MsgMessage> MsgMessage::load(const std::shared_ptr<MsgContext> &context, int storage, MsgObjectKind kind,
                                             QString *error)
{
    MsgPropertyBag bag;
    if (!bag.load(context->cfb, storage, kind, &context->budget, error)) return nullptr;
    return std::unique_ptr<MsgMessage>(new MsgMessage(context, std::move(bag)));
}

bool MsgMessage::recipients(std::vector<std::unique_ptr<Mapi::PropertyBag>> *out, QString *error)
{
    out->clear();
    const QList<int> storages = m_bag.numberedStorages(QStringLiteral("__recip_version1.0_#"));
    if (storages.size() > Mapi::kMaxRecipients)
        return fail(error, QStringLiteral("A message has more than %1 recipients").arg(Mapi::kMaxRecipients));
    for (const int id : storages) {
        MsgPropertyBag bag;
        if (!bag.load(m_context->cfb, id, MsgObjectKind::Recipient, &m_context->budget, error)) return false;
        out->push_back(std::make_unique<MsgRecipient>(m_context, std::move(bag)));
    }
    return true;
}

bool MsgMessage::attachments(std::vector<std::unique_ptr<Mapi::Attachment>> *out, QString *error)
{
    out->clear();
    const QList<int> storages = m_bag.numberedStorages(QStringLiteral("__attach_version1.0_#"));
    if (storages.size() > Mapi::kMaxAttachments)
        return fail(error, QStringLiteral("A message has more than %1 attachments").arg(Mapi::kMaxAttachments));
    for (const int id : storages) {
        MsgPropertyBag bag;
        if (!bag.load(m_context->cfb, id, MsgObjectKind::Attachment, &m_context->budget, error)) return false;
        out->push_back(std::make_unique<MsgAttachment>(m_context, std::move(bag)));
    }
    return true;
}

std::unique_ptr<Mapi::Message> MsgAttachment::embeddedMessage(QString *error)
{
    // MS-OXMSG 2.2.2.1: the attached message is the PT_OBJECT storage
    // "__substg1.0_3701000D", a message object with a 24-byte header.
    const int object = m_bag.childStorage(QStringLiteral("__substg1.0_3701000D"));
    if (object < 0) return nullptr;
    std::unique_ptr<MsgMessage> message = MsgMessage::load(m_context, object, MsgObjectKind::Embedded, error);
    if (message && !message->hasProperties()) {
        fail(error, QStringLiteral("An embedded message has no properties"));
        return nullptr;
    }
    return message;
}

std::unique_ptr<MsgMessage> openMsgFile(const QByteArray &file, QString *error)
{
    auto context = std::make_shared<MsgContext>();
    context->budget.remaining = Mapi::kMaxContentBytes;
    QString why;
    if (!context->cfb.open(file, &why)) {
        fail(error, QStringLiteral("Cannot read the Outlook message: %1").arg(why));
        return nullptr;
    }
    // MS-OXMSG 2.2.3: the named-property map of the whole file (embedded
    // messages use the top level's).
    const int names = context->cfb.child(context->cfb.root(), QStringLiteral("__nameid_version1.0"));
    const CfbReader::Entry *namesEntry = context->cfb.entry(names);
    if (namesEntry && namesEntry->type == CfbReader::Entry::Storage) {
        context->named.parse(nameStream(context->cfb, names, QStringLiteral("__substg1.0_00020102")),
                             nameStream(context->cfb, names, QStringLiteral("__substg1.0_00030102")),
                             nameStream(context->cfb, names, QStringLiteral("__substg1.0_00040102")));
    }
    std::unique_ptr<MsgMessage> root = MsgMessage::load(context, context->cfb.root(), MsgObjectKind::TopLevel, &why);
    if (!root) {
        fail(error, QStringLiteral("Cannot read the Outlook message: %1").arg(why));
        return nullptr;
    }
    if (!root->hasProperties()) {
        fail(error, QStringLiteral("Cannot read the Outlook message: The file is a compound file but not an Outlook message"));
        return nullptr;
    }
    return root;
}

} // namespace QindaOffice::Mail::MsgDetail
