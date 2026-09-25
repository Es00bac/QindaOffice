// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A scripted HTTP/1.1 server on 127.0.0.1 for the office_mail tests: the
// autoconfig sources, the OAuth authorization and token endpoints. No test
// touches the real network (docs/verification.md, M43 gates). Pattern:
// tests/office_ai/mock_sse_server.

#include <QHash>
#include <QList>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace QindaOffice::Mail::Test {

struct FakeRequest {
    QByteArray method;
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;   // lower-case names
    QByteArray body;

    // The body as application/x-www-form-urlencoded, fully decoded values.
    [[nodiscard]] QString form(const QString &key) const;
};

struct FakeResponse {
    int status = 200;
    QByteArray contentType = "application/json";
    QByteArray body;
    bool hang = false;   // accept the request, never answer
};

class FakeHttpServer final : public QTcpServer {
    Q_OBJECT
public:
    using Handler = std::function<FakeResponse(const FakeRequest &)>;

    explicit FakeHttpServer(QObject *parent = nullptr);

    void route(const QString &path, const FakeResponse &response);
    void route(const QString &path, Handler handler);
    [[nodiscard]] QUrl url(const QString &pathAndQuery) const;
    [[nodiscard]] const QList<FakeRequest> &requests() const { return m_requests; }
    [[nodiscard]] QStringList requestPaths() const;
    void clearRequests() { m_requests.clear(); }

protected:
    void incomingConnection(qintptr handle) override;

private:
    void serve(class QTcpSocket *socket, QByteArray *buffer);

    QHash<QString, Handler> m_routes;
    QList<FakeRequest> m_requests;
};

} // namespace QindaOffice::Mail::Test
