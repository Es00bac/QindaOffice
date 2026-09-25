// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A minimal HTTP/1.1 server on 127.0.0.1 for the mail_exchange tests. Each
// request is handed to a handler; every reply closes the connection. The
// server records each request (method, decoded path, query, headers, body)
// so tests can assert what the adapter sent - including that no secret
// travelled anywhere it should not.
// AGENT-NOTE: runs on the test's (GUI) thread; the adapters' requests come
// from a worker thread, and the test waits with QSignalSpy::wait, which
// spins this thread's event loop.

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QTcpServer>
#include <QUrlQuery>
#include <functional>

class FakeHttpServer : public QObject {
public:
    struct Request {
        QByteArray method;
        QString path;                               // percent-decoded
        QString rawTarget;                          // as sent
        QUrlQuery query;
        QMap<QByteArray, QByteArray> headers;       // lower-case names
        QByteArray body;
    };
    struct Reply {
        int status = 200;
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray body;
    };
    using Handler = std::function<Reply(const Request &)>;

    explicit FakeHttpServer(Handler handler, QObject *parent = nullptr);
    quint16 port() const { return m_server.serverPort(); }
    QString origin() const;

    // Required Authorization header value; a mismatch answers 401.
    QByteArray authorization;
    QList<Request> requests;
    int count(const QByteArray &method, const QString &pathPrefix = {}) const;

private:
    void respond(QTcpSocket *socket, const Request &request);
    QTcpServer m_server;
    Handler m_handler;
};
