// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The mailbox-level Exchange operations behind ExchangeMailbox (M81,
// D-090): automatic replies, the master category list, the server's inbox
// rules (read-only) and a message's or event's categories and follow-up
// flag — once over Microsoft Graph (graph_mailbox.cpp) and once over
// Exchange Web Services (ews_mailbox.cpp). Each run is one sequential HTTP
// dialogue on a worker thread (HttpTransport), like the calendar backends.
// Internal to mail_exchange.
//
// AGENT-CONTRACT (exchange_mailbox.h documents the maps): inputs and
// results are plain QVariantMaps so mail_exchange needs no office_mail
// types; the result carries the provider contract keys (ok, state, error,
// retryable, transcript, [authRejected], [retryAfterSeconds]).

#include "http_transport.h"

#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace QindaOffice::Mail::MailboxJobs {

struct Config {
    QString service;                // graph | ews
    QUrl baseUrl;                   // Graph: https://graph.microsoft.com/v1.0
    QUrl url;                       // EWS: https://<host>/EWS/Exchange.asmx
    QString userName;               // EWS basic
    QString mailbox;                // the mailbox's SMTP address
    QString serverVersion = QStringLiteral("Exchange2013_SP1");
    QString secret;
    QString mechanism = QStringLiteral("bearer");   // bearer | basic
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
    int maxPages = 20;              // Graph paging bound for pullItemStates
};

enum class Op { ReadAutoReplies, WriteAutoReplies, ListCategories, AddCategory, ListRules, SetItemState, PullItemStates };

// Dispatches to the service's implementation; never touches a QObject.
QVariantMap run(Op op, const Config &config, const QVariantMap &input);
QVariantMap graph(Op op, const Config &config, const QVariantMap &input);
QVariantMap ews(Op op, const Config &config, const QVariantMap &input);

// The contract keys from the transport's state, merged over `out`.
QVariantMap finish(const HttpTransport &transport, QVariantMap out, const QString &problem = {});

// A plain reply text as the HTML both servers store, and back.
QString htmlFromText(const QString &text);
QString textFromHtml(const QString &html);

} // namespace QindaOffice::Mail::MailboxJobs
