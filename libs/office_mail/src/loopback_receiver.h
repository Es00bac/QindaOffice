// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The loopback redirect endpoint of a native-app OAuth flow (RFC 8252 §7.3):
// a QTcpServer on 127.0.0.1 with an ephemeral port that waits for the one
// browser request carrying `code` (or `error`) and `state`, answers it with
// a small static page and stops listening.
//
// AGENT-GUARD: the page is static text chosen by the verdict. Never echo
// query parameters into it (reflected XSS on a page the user trusts), and
// never log the request line (it carries the authorization code).

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

class QTcpSocket;

namespace QindaOffice::Mail {

class LoopbackReceiver : public QObject {
    Q_OBJECT
public:
    struct Page {
        bool success = false;
        QString heading;
        QString text;
    };
    // Called once with the callback's query; returns the page to show.
    using Handler = std::function<Page(const QUrlQuery &query)>;

    explicit LoopbackReceiver(Handler handler, QObject *parent = nullptr);
    ~LoopbackReceiver() override;

    // 127.0.0.1, port chosen by the kernel. False (with `error`) on failure.
    [[nodiscard]] bool listen(QString *error);
    // http://127.0.0.1:<port>/
    [[nodiscard]] QUrl redirectUri() const;
    void close();

    static constexpr qsizetype kMaxRequestBytes = 8 * 1024;

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status, const QByteArray &reason, const QByteArray &html);

    Handler m_handler;
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    bool m_done = false;
};

} // namespace QindaOffice::Mail
