// SPDX-License-Identifier: GPL-3.0-or-later
// MailboxJobs: dispatch, results and reply text (mailbox_jobs.h).
#include "mailbox_jobs.h"

#include <QRegularExpression>

namespace QindaOffice::Mail::MailboxJobs {

using namespace Qt::StringLiterals;
using Failure = QindaTK::Protocol::Failure;

QVariantMap run(Op op, const Config &config, const QVariantMap &input)
{
    return config.service == "ews"_L1 ? ews(op, config, input) : graph(op, config, input);
}

QVariantMap finish(const HttpTransport &transport, QVariantMap out, const QString &problem)
{
    const Failure failure = transport.failure();
    const bool ok = failure == Failure::None && problem.isEmpty();
    out.insert(u"ok"_s, ok);
    out.insert(u"state"_s, ok ? u"accepted"_s
                              : QindaTK::Protocol::failureState(failure == Failure::None ? Failure::Rejected : failure));
    out.insert(u"error"_s, ok ? QString() : transport.redact(problem.isEmpty() ? transport.error() : problem));
    out.insert(u"retryable"_s, failure == Failure::Offline);
    out.insert(u"transcript"_s, transport.transcript());
    if (transport.authRejected()) out.insert(u"authRejected"_s, true);
    if (transport.retryAfterSeconds() >= 0) out.insert(u"retryAfterSeconds"_s, transport.retryAfterSeconds());
    return out;
}

QString htmlFromText(const QString &text)
{
    if (text.trimmed().isEmpty()) return {};
    QString escaped = text.toHtmlEscaped();
    escaped.replace("\r\n"_L1, "\n"_L1).replace(u'\n', "<br>"_L1);
    return u"<html><body><div>"_s + escaped + u"</div></body></html>"_s;
}

QString textFromHtml(const QString &html)
{
    if (!html.contains(u'<')) return html.trimmed();
    // Line breaks and paragraphs first, then every tag goes; entities decode.
    QString text = html;
    text.remove(QRegularExpression(u"<(script|style|head)[^>]*>.*?</\\1>"_s,
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    text.replace(QRegularExpression(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption), u"\n"_s);
    text.replace(QRegularExpression(u"</(p|div|li|tr|h[1-6])\\s*>"_s, QRegularExpression::CaseInsensitiveOption), u"\n"_s);
    text.remove(QRegularExpression(u"<[^>]*>"_s));
    text.replace("&nbsp;"_L1, " "_L1).replace("&lt;"_L1, "<"_L1).replace("&gt;"_L1, ">"_L1)
        .replace("&quot;"_L1, "\""_L1).replace("&#39;"_L1, "'"_L1).replace("&amp;"_L1, "&"_L1);
    text.replace(QRegularExpression(u"\\n{3,}"_s), u"\n\n"_s);
    return text.trimmed();
}

} // namespace QindaOffice::Mail::MailboxJobs
