// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A small read-only element tree built with a namespace-aware
// QXmlStreamReader, for EWS SOAP responses. Navigation matches local names;
// `ns` is kept for the few places that must check it (the SOAP envelope).
// AGENT-NOTE: EWS answers are nested several levels deep with optional
// elements in schema order; walking a tree reads far clearer than a
// streaming state machine per operation, and responses are bounded
// (16 MiB by HttpTransport, depth and node count here).

#include <QList>
#include <QMap>
#include <QString>

namespace QindaOffice::Mail {

struct XmlNode {
    QString ns, name;                       // namespace URI and local name
    QMap<QString, QString> attributes;      // by local name
    QString text;                           // concatenated character data
    QList<XmlNode> children;

    const XmlNode *child(QStringView local) const;
    QList<const XmlNode *> childrenNamed(QStringView local) const;
    QString childText(QStringView local) const;     // trimmed; empty when absent
    const XmlNode *find(QStringView local) const;    // first descendant, depth first
    void findAll(QStringView local, QList<const XmlNode *> *out) const;
    QString attribute(const QString &local) const { return attributes.value(local); }
};

// Parses `bytes` into `root` (the document element). False with `error`
// for malformed XML, a DTD (never expected from EWS; refused so entity
// expansion cannot be abused) or a tree deeper than 64 / larger than 200k nodes.
bool parseXml(const QByteArray &bytes, XmlNode *root, QString *error);

} // namespace QindaOffice::Mail
