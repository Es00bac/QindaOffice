// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// An in-process Exchange Web Services endpoint for tst_mail_exchange_ews,
// shaped after the request and response examples of the public EWS
// reference (FindItem with CalendarView, GetItem incl. RecurringMasterItemId,
// CreateItem, UpdateItem with ChangeKey / NeverOverwrite, DeleteItem,
// GetFolder, FindFolder). It keeps a small calendar in memory:
// - a series master lists its pattern occurrences explicitly
//   (`occurrenceStarts`, UTC), which InstanceIndex addresses 1-based;
// - FindItem shows a Single as itself and a series as one Occurrence
//   ("occ:<masterId>"), as a CalendarView does;
// - changing a master's Start drops its exceptions, as Exchange does.
// Every accepted write bumps the item's ChangeKey ("CK<n>").

#include "fake_http_server.h"
#include "xml_tree.h"

#include <QDateTime>
#include <QXmlStreamWriter>

class EwsFakeServer {
public:
    struct Occurrence {
        QString itemId;
        QDateTime originalStart, start, end;
        QString subject, location, body;
        bool cancelled = false;
    };
    struct Item {
        QString id, uid, subject, body, location, type = QStringLiteral("Single");
        QString zone = QStringLiteral("Mountain Standard Time");
        bool allDay = false, legacyZone = false, canModify = true, online = false;
        QString joinUrl, organizer;
        QStringList attendees;                 // "Name <email>"
        QDateTime start, end;                  // UTC
        int changeKey = 1;
        QindaOffice::Mail::XmlNode recurrence; // empty name: not recurring
        QList<QDateTime> occurrenceStarts;     // UTC, pattern order
        QList<QDateTime> deleted;
        QList<Occurrence> modified;
    };

    EwsFakeServer();
    FakeHttpServer http;
    QMap<QString, Item> items;
    QMap<QString, QindaOffice::Mail::XmlNode> lastOperation;   // operation name -> request element
    QStringList operations;
    bool throttle = false;                     // HTTP 503 + Retry-After: 12
    bool busy = false;                         // SOAP fault ErrorServerBusy, BackOffMilliseconds 30000

    QString url() const { return http.origin() + QStringLiteral("/EWS/Exchange.asmx"); }
    QString changeKey(const QString &id) const { return QStringLiteral("CK%1").arg(items.value(id).changeKey); }
    void bump(const QString &id) { ++items[id].changeKey; }
    int count(const QString &operation) const { return int(operations.count(operation)); }

private:
    FakeHttpServer::Reply handle(const FakeHttpServer::Request &request);
    void findItem(QXmlStreamWriter &w);
    void getItem(QXmlStreamWriter &w, const QindaOffice::Mail::XmlNode &operation);
    void createItem(QXmlStreamWriter &w, const QindaOffice::Mail::XmlNode &operation);
    void updateItem(QXmlStreamWriter &w, const QindaOffice::Mail::XmlNode &operation);
    void deleteItem(QXmlStreamWriter &w, const QindaOffice::Mail::XmlNode &operation);
    void folders(QXmlStreamWriter &w, const QString &operation);
    void writeItem(QXmlStreamWriter &w, const Item &item, bool full);
    void writeOccurrenceItem(QXmlStreamWriter &w, const Occurrence &occurrence);
    // Applies SetItemField/DeleteItemField updates to an item or an occurrence.
    void apply(const QindaOffice::Mail::XmlNode &updates, Item *item, Occurrence *occurrence);
    // Finds the master owning exception `itemId`; -1 when none.
    QString masterOfException(const QString &itemId, int *index) const;
    int m_nextId = 1;
};
