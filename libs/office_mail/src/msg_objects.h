// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Mapi:: property bags over an Outlook .msg compound file (M76, D-085):
// the MS-OXMSG object storages (message, embedded message, recipient,
// attachment) as Mapi::Message / Attachment / PropertyBag, so the mappings
// .msg and .pst share read them (mapi_mail.h, outlook_items.h). Internal to
// office_mail; readMsg() and readOutlookMsg() are the ways in.
//
// AGENT-GUARD: one MsgContext per file owns the CfbReader, the read budget
// (MsgReadBudget: every stream of the whole tree, embedded messages
// included) and the named-property map; every bag keeps the context alive.

#include "cfb_reader.h"
#include "mapi_named.h"
#include "mapi_object.h"
#include "msg_properties.h"

#include <memory>

namespace QindaOffice::Mail::MsgDetail {

struct MsgContext {
    CfbReader cfb;
    MsgReadBudget budget;
    Mapi::NamedPropertyMap named;
};

template <typename Base>
class StorageBag : public Base {
public:
    bool has(quint16 id, quint16 type) const override { return m_bag.has(id, type); }
    std::optional<quint32> longValue(quint16 id) const override { return m_bag.longValue(id); }
    std::optional<double> doubleValue(quint16 id) const override { return m_bag.doubleValue(id); }
    bool boolValue(quint16 id) const override { return m_bag.boolValue(id); }
    QDateTime timeValue(quint16 id) const override { return m_bag.timeValue(id); }
    QString string(quint16 id) override { return m_bag.string(id); }
    QByteArray binary(quint16 id) override { return m_bag.binary(id); }
    QByteArray optionalBinary(quint16 id) override { return m_bag.optionalBinary(id); }
    QStringList strings(quint16 id) override { return m_bag.strings(id); }
    QList<QByteArray> binaries(quint16 id) override { return m_bag.binaries(id); }
    void setString8CodePage(int codePage) override { m_bag.setString8CodePage(codePage); }
    int string8CodePage() const override { return m_bag.codePage(); }
    bool failed() const override { return m_bag.failed(); }
    QString error() const override { return m_bag.error(); }

protected:
    StorageBag(std::shared_ptr<MsgContext> context, MsgPropertyBag bag)
        : m_context(std::move(context))
        , m_bag(std::move(bag))
    {
    }
    std::shared_ptr<MsgContext> m_context;
    MsgPropertyBag m_bag;
};

class MsgMessage final : public StorageBag<Mapi::Message> {
public:
    // The storage as a message object; null with `*error` when its property
    // stream is unreadable.
    static std::unique_ptr<MsgMessage> load(const std::shared_ptr<MsgContext> &context, int storage, MsgObjectKind kind,
                                            QString *error);
    [[nodiscard]] bool hasProperties() const { return m_bag.hasProperties(); }
    bool recipients(std::vector<std::unique_ptr<Mapi::PropertyBag>> *out, QString *error) override;
    bool attachments(std::vector<std::unique_ptr<Mapi::Attachment>> *out, QString *error) override;
    const Mapi::NamedPropertyMap &namedProperties() const override { return m_context->named; }

private:
    using StorageBag::StorageBag;
};

class MsgAttachment final : public StorageBag<Mapi::Attachment> {
public:
    MsgAttachment(std::shared_ptr<MsgContext> context, MsgPropertyBag bag) : StorageBag(std::move(context), std::move(bag)) {}
    std::unique_ptr<Mapi::Message> embeddedMessage(QString *error) override;
};

class MsgRecipient final : public StorageBag<Mapi::PropertyBag> {
public:
    MsgRecipient(std::shared_ptr<MsgContext> context, MsgPropertyBag bag) : StorageBag(std::move(context), std::move(bag)) {}
};

// Opens `file` as a .msg: the compound file, the named-property map and the
// top-level message. Null with a reason ("Cannot read the Outlook message: …")
// on failure, including a compound file that is not an Outlook message.
std::unique_ptr<MsgMessage> openMsgFile(const QByteArray &file, QString *error);

} // namespace QindaOffice::Mail::MsgDetail
