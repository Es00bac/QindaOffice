// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook .msg (MS-OXMSG) → MessageContent (msg_reader.h): the compound file
// as Mapi:: bags (msg_objects.h) through the shared mail mapping (mapi_mail.h).

#include "msg_reader.h"

#include "msg_objects.h"

namespace QindaOffice::Mail {

bool readMsg(const QByteArray &file, MessageContent *out, QString *error)
{
    *out = MessageContent();
    QString why;
    std::unique_ptr<MsgDetail::MsgMessage> message = MsgDetail::openMsgFile(file, &why);
    if (!message) {
        if (error) *error = why;
        return false;
    }
    if (!Mapi::readMailContent(*message, out, &why)) {
        if (error) *error = QStringLiteral("Cannot read the Outlook message: %1").arg(why);
        return false;
    }
    return true;
}

} // namespace QindaOffice::Mail
