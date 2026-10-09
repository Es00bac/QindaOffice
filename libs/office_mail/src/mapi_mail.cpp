// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook mail properties → MessageContent (mapi_mail.h). Moved from the
// M64 .msg reader in M76 so .msg and .pst share it unchanged.

#include "mapi_mail.h"

#include "msg_headers.h"
#include "rtf_codepage.h"
#include "rtf_compressed.h"
#include "rtf_text.h"

namespace QindaOffice::Mail::Mapi {
namespace {

using namespace MsgDetail;

// MS-OXPROPS property ids.
enum : quint16 {
    PidSubject = 0x0037,
    PidSubjectPrefix = 0x003D,
    PidNormalizedSubject = 0x0E1D,
    PidClientSubmitTime = 0x0039,
    PidMessageDeliveryTime = 0x0E06,
    PidCreationTime = 0x3007,
    PidSentRepresentingName = 0x0042,
    PidSentRepresentingAddressType = 0x0064,
    PidSentRepresentingEmailAddress = 0x0065,
    PidSentRepresentingSmtpAddress = 0x5D02,
    PidSenderName = 0x0C1A,
    PidSenderAddressType = 0x0C1E,
    PidSenderEmailAddress = 0x0C1F,
    PidSenderSmtpAddress = 0x5D01,
    PidTransportMessageHeaders = 0x007D,
    PidDisplayBcc = 0x0E02,
    PidDisplayCc = 0x0E03,
    PidDisplayTo = 0x0E04,
    PidInternetMessageId = 0x1035,
    PidInternetReferences = 0x1039,
    PidInReplyToId = 0x1042,
    PidBody = 0x1000,
    PidRtfCompressed = 0x1009,
    PidHtml = 0x1013,
    PidInternetCodepage = 0x3FDE,
    PidMessageCodepage = 0x3FFD,
    PidRecipientType = 0x0C15,
    PidDisplayName = 0x3001,
    PidAddressType = 0x3002,
    PidEmailAddress = 0x3003,
    PidSmtpAddress = 0x39FE,
    PidRecipientDisplayName = 0x5FF6,
    PidAttachDataBinary = 0x3701,
    PidAttachFilename = 0x3704,
    PidAttachMethod = 0x3705,
    PidAttachLongFilename = 0x3707,
    PidAttachMimeTag = 0x370E,
    PidAttachContentId = 0x3712,
    PidAttachContentLocation = 0x3713,
    PidAttachFlags = 0x3714,
    PidAttachmentHidden = 0x7FFE,
};

constexpr quint32 kAttachEmbeddedMessage = 5;   // MS-OXCMSG 2.2.2.9 afEmbeddedMessage
constexpr quint32 kAttachOle = 6;               // afStorage
constexpr quint32 kAttMhtmlRef = 0x4;           // PidTagAttachFlags ATT_MHTML_REF

QString mimeTag(const QString &text)
{
    QString mime = oneLine(text).toLower();
    const qsizetype semicolon = mime.indexOf(u';');
    if (semicolon >= 0) mime.truncate(semicolon);
    mime = mime.trimmed();
    const qsizetype slash = mime.indexOf(u'/');
    if (slash <= 0 || slash == mime.size() - 1 || mime.contains(u' ')) return {};
    return mime;
}

QString mimeForName(const QString &name)
{
    static const struct {
        const char *extension;
        const char *mime;
    } kTypes[] = {
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
        {"bmp", "image/bmp"}, {"pdf", "application/pdf"}, {"txt", "text/plain"},
        {"html", "text/html"}, {"htm", "text/html"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"doc", "application/msword"}, {"xls", "application/vnd.ms-excel"},
        {"ppt", "application/vnd.ms-powerpoint"}, {"zip", "application/zip"},
        {"ics", "text/calendar"}, {"vcf", "text/vcard"}, {"eml", "message/rfc822"},
        {"msg", "application/vnd.ms-outlook"}, {"csv", "text/csv"},
    };
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot >= 0) {
        const QString extension = name.mid(dot + 1).toLower();
        for (const auto &type : kTypes) {
            if (extension == QLatin1String(type.extension)) return QLatin1String(type.mime);
        }
    }
    return QStringLiteral("application/octet-stream");
}

// PidTagDisplayTo/Cc/Bcc: display names separated by ';' (no addresses).
QString displayList(const QString &names)
{
    QList<MailAddress> list;
    for (const QString &name : names.split(u';')) {
        const QString clean = oneLine(name);
        if (!clean.isEmpty()) list.append({clean, {}});
    }
    return formatAddressList(list);
}

QString headerList(const HeaderBlock &headers, QStringView field)
{
    return formatAddressList(parseAddressList(headers.value(field)));
}

class MailReader {
public:
    bool readMessage(Message &bag, int depth, int parentCodePage, MessageContent *out);
    QString error() const { return m_error; }

private:
    bool fail(const QString &message)
    {
        if (m_error.isEmpty()) m_error = message;
        return false;
    }
    void readEnvelope(Message &bag, const HeaderBlock &headers, MessageContent *out);
    void readBodies(Message &bag, int internetCodePage, MessageContent *out);
    bool readRecipients(Message &bag, const HeaderBlock &headers, int codePage, MessageContent *out);
    bool readAttachment(Attachment &bag, int depth, int codePage, const QString &html, MessageAttachment *out);

    qint64 m_rtfBudget = kMaxContentBytes;   // decompressed RTF of the whole tree
    QString m_error;
};

bool MailReader::readMessage(Message &bag, int depth, int parentCodePage, MessageContent *out)
{
    // AGENT-NOTE: MS-OXPROPS — PidTagMessageCodepage is the code page of the
    // PT_STRING8 properties; PidTagInternetCodepage is the body's (it can be
    // e.g. KOI8-R while the strings are windows-1251), so it only comes
    // second here and is what PidTagHtml bytes decode with.
    const int internetCodePage = int(bag.longValue(PidInternetCodepage).value_or(0));
    int codePage = int(bag.longValue(PidMessageCodepage).value_or(0));
    if (codePage <= 0) codePage = internetCodePage > 0 ? internetCodePage : parentCodePage;
    bag.setString8CodePage(codePage);

    const HeaderBlock headers(bag.string(PidTransportMessageHeaders));
    readEnvelope(bag, headers, out);
    readBodies(bag, internetCodePage, out);
    if (!readRecipients(bag, headers, codePage, out)) return false;

    std::vector<std::unique_ptr<Attachment>> attachments;
    QString why;
    if (!bag.attachments(&attachments, &why)) return fail(why);
    if (attachments.size() > size_t(kMaxAttachments))
        return fail(QStringLiteral("A message has more than %1 attachments").arg(kMaxAttachments));
    for (const std::unique_ptr<Attachment> &attachment : attachments) {
        MessageAttachment part;
        if (!readAttachment(*attachment, depth, codePage, out->html, &part)) return false;
        out->attachments.append(part);
    }
    if (bag.failed()) return fail(bag.error());
    return true;
}

void MailReader::readEnvelope(Message &bag, const HeaderBlock &headers, MessageContent *out)
{
    out->subject = oneLine(bag.string(PidSubject));
    if (out->subject.isEmpty())
        out->subject = oneLine(bag.string(PidSubjectPrefix) + bag.string(PidNormalizedSubject));

    out->fromName = oneLine(bag.string(PidSentRepresentingName));
    out->fromAddress = smtpAddress(bag, PidSentRepresentingSmtpAddress, PidSentRepresentingAddressType,
                                   PidSentRepresentingEmailAddress);
    out->senderName = oneLine(bag.string(PidSenderName));
    out->senderAddress = smtpAddress(bag, PidSenderSmtpAddress, PidSenderAddressType, PidSenderEmailAddress);
    if (out->fromAddress.isEmpty()) {
        const QList<MailAddress> from = parseAddressList(headers.value(u"from"));
        if (!from.isEmpty() && !from.first().address.isEmpty()) {
            out->fromAddress = from.first().address;
            if (out->fromName.isEmpty()) out->fromName = from.first().name;
        }
    }
    // An Exchange sender shows only a name in From; when the Sender is
    // visibly the same person (or From is empty) borrow its SMTP address.
    // AGENT-GUARD: a nameless Sender is NOT assumed to be From — that would
    // put a delegate's address under the principal's name.
    const bool sameName = out->fromName.isEmpty()
        || (!out->senderName.isEmpty() && out->fromName.compare(out->senderName, Qt::CaseInsensitive) == 0);
    if (out->fromAddress.isEmpty() && sameName) {
        out->fromAddress = out->senderAddress;
        if (out->fromName.isEmpty()) out->fromName = out->senderName;
    }
    // MessageContent keeps Sender only when it differs from From.
    const bool sameSender = out->senderAddress.isEmpty()
        ? (out->senderName.isEmpty() || out->senderName == out->fromName)
        : out->senderAddress.compare(out->fromAddress, Qt::CaseInsensitive) == 0;
    if (sameSender) {
        out->senderName.clear();
        out->senderAddress.clear();
    }
    out->replyTo = headerList(headers, u"reply-to");

    out->date = bag.timeValue(PidClientSubmitTime);
    out->received = bag.timeValue(PidMessageDeliveryTime);
    if (!out->date.isValid()) out->date = parseRfc5322Date(headers.value(u"date"));
    if (!out->date.isValid()) out->date = out->received;
    if (!out->date.isValid()) out->date = bag.timeValue(PidCreationTime);

    out->messageId = stripAngles(bag.string(PidInternetMessageId));
    if (out->messageId.isEmpty()) out->messageId = stripAngles(headers.value(u"message-id"));
    out->inReplyTo = stripAngles(bag.string(PidInReplyToId));
    if (out->inReplyTo.isEmpty()) out->inReplyTo = stripAngles(headers.value(u"in-reply-to"));
    out->references = oneLine(bag.string(PidInternetReferences));
    if (out->references.isEmpty()) out->references = oneLine(headers.value(u"references"));
}

void MailReader::readBodies(Message &bag, int internetCodePage, MessageContent *out)
{
    out->plain = bag.string(PidBody);
    if (bag.has(PidHtml, kPtBinary)) {
        QByteArray bytes = bag.binary(PidHtml);
        while (bytes.endsWith('\0')) bytes.chop(1);
        if (internetCodePage > 0) out->html = decodeCodePage(bytes, internetCodePage);
        else out->html = decodeCodePage(bytes, bytes.isValidUtf8() ? 65001 : 1252);   // a UTF-8 BOM is dropped
    } else {
        out->html = bag.string(PidHtml);
    }
    if (!out->html.isEmpty()) return;

    // AGENT-NOTE: RTF-only bodies (older Outlook, rich-text mail). A broken
    // or over-budget RTF never fails the message: what was read is kept.
    const QByteArray compressed = bag.optionalBinary(PidRtfCompressed);
    QByteArray rtf;
    if (compressed.isEmpty() || !decompressRtf(compressed, &rtf, nullptr)) return;
    if (rtf.size() > m_rtfBudget) return;
    m_rtfBudget -= rtf.size();
    const RtfBodyKind kind = rtfEncapsulation(rtf);
    if (kind == RtfBodyKind::Html) {
        QString html;
        if (rtfToHtml(rtf, &html, nullptr)) {
            out->html = html;
            return;
        }
    }
    if (kind != RtfBodyKind::None && out->plain.isEmpty()) out->plain = rtfToPlainText(rtf);
}

bool MailReader::readRecipients(Message &bag, const HeaderBlock &headers, int codePage, MessageContent *out)
{
    std::vector<std::unique_ptr<PropertyBag>> recipients;
    QString why;
    if (!bag.recipients(&recipients, &why)) return fail(why);
    if (recipients.size() > size_t(kMaxRecipients))
        return fail(QStringLiteral("A message has more than %1 recipients").arg(kMaxRecipients));
    if (recipients.empty()) {
        // AGENT-NOTE: some exporters drop the recipient objects. The original
        // headers carry addresses; PidTagDisplayTo/Cc/Bcc only names.
        out->to = headerList(headers, u"to");
        if (out->to.isEmpty()) out->to = displayList(bag.string(PidDisplayTo));
        out->cc = headerList(headers, u"cc");
        if (out->cc.isEmpty()) out->cc = displayList(bag.string(PidDisplayCc));
        out->bcc = headerList(headers, u"bcc");
        if (out->bcc.isEmpty()) out->bcc = displayList(bag.string(PidDisplayBcc));
        return true;
    }
    QList<MailAddress> to, cc, bcc;
    for (const std::unique_ptr<PropertyBag> &recipient : recipients) {
        recipient->setString8CodePage(codePage);
        MailAddress address;
        address.name = oneLine(recipient->string(PidDisplayName));
        if (address.name.isEmpty()) address.name = oneLine(recipient->string(PidRecipientDisplayName));
        address.address = smtpAddress(*recipient, PidSmtpAddress, PidAddressType, PidEmailAddress);
        if (recipient->failed()) return fail(recipient->error());
        if (address.name.isEmpty() && address.address.isEmpty()) continue;
        // MS-OXOMSG 2.2.3.1: MAPI_TO 1, MAPI_CC 2, MAPI_BCC 3; high bits are flags.
        switch (recipient->longValue(PidRecipientType).value_or(1) & 0x0FFFFFFFu) {
        case 2: cc.append(address); break;
        case 3: bcc.append(address); break;
        default: to.append(address); break;
        }
    }
    out->to = formatAddressList(to);
    out->cc = formatAddressList(cc);
    out->bcc = formatAddressList(bcc);
    return true;
}

bool MailReader::readAttachment(Attachment &bag, int depth, int codePage, const QString &html, MessageAttachment *out)
{
    bag.setString8CodePage(codePage);
    const quint32 method = bag.longValue(PidAttachMethod).value_or(1);
    QString name = oneLine(bag.string(PidAttachLongFilename));
    if (name.isEmpty()) name = oneLine(bag.string(PidAttachFilename));
    const QString displayName = oneLine(bag.string(PidDisplayName));
    out->contentId = stripAngles(bag.string(PidAttachContentId));
    out->contentLocation = oneLine(bag.string(PidAttachContentLocation));
    QString mime = mimeTag(bag.string(PidAttachMimeTag));

    if (method == kAttachEmbeddedMessage) {
        QString why;
        const std::unique_ptr<Message> object = bag.embeddedMessage(&why);
        if (object) {
            if (depth >= kMaxEmbeddingDepth)
                return fail(QStringLiteral("Attached messages are nested more than %1 deep").arg(kMaxEmbeddingDepth));
            auto embedded = std::make_shared<MessageContent>();
            if (!readMessage(*object, depth + 1, codePage, embedded.get())) return false;
            if (name.isEmpty()) {
                const QString base = !embedded->subject.isEmpty() ? embedded->subject
                    : !displayName.isEmpty() ? displayName : QStringLiteral("message");
                name = base + QStringLiteral(".eml");
            }
            out->embedded = embedded;
        } else if (!why.isEmpty()) {
            return fail(why);
        } else if (name.isEmpty()) {
            name = displayName;
        }
        mime = QStringLiteral("message/rfc822");
    } else {
        if (name.isEmpty()) name = displayName;
        // By value (1) and the reference methods carry PidTagAttachDataBinary
        // when there is data; OLE (6) only sometimes does.
        out->data = bag.binary(PidAttachDataBinary);
        if (method == kAttachOle) mime = QStringLiteral("application/octet-stream");
        else if (mime.isEmpty()) mime = mimeForName(name);
    }
    out->name = name;
    out->mimeType = mime;
    out->inlinePart = bag.boolValue(PidAttachmentHidden)
        || (bag.longValue(PidAttachFlags).value_or(0) & kAttMhtmlRef) != 0
        || (!out->contentId.isEmpty() && html.contains(QStringLiteral("cid:") + out->contentId, Qt::CaseInsensitive));
    if (bag.failed()) return fail(bag.error());
    return true;
}

} // namespace

bool usableAddress(const QString &address)
{
    return !address.isEmpty() && address.contains(u'@') && !address.startsWith(u'/');
}

QString smtpAddress(PropertyBag &bag, quint16 smtpId, quint16 typeId, quint16 addressId)
{
    const QString smtp = oneLine(bag.string(smtpId));
    if (usableAddress(smtp)) return smtp;
    if (oneLine(bag.string(typeId)).compare(QLatin1String("SMTP"), Qt::CaseInsensitive) != 0) return {};
    const QString address = oneLine(bag.string(addressId));
    return usableAddress(address) ? address : QString();
}

bool readMailContent(Message &message, MessageContent *out, QString *error)
{
    *out = MessageContent();
    MailReader reader;
    MessageContent content;
    if (!reader.readMessage(message, 0, 1252, &content)) {
        if (error) *error = reader.error();
        return false;
    }
    *out = std::move(content);
    return true;
}

} // namespace QindaOffice::Mail::Mapi
