// SPDX-License-Identifier: GPL-3.0-or-later
// Kept MSPDI elements: capture, replay, stamps and schema-ordered output
// (mspdi_keep.h).
#include "mspdi_keep.h"

#include "mspdi_p.h"

#include <QJsonValue>

#include <algorithm>

namespace QindaOffice::Plan::Mspdi {

QJsonArray keptToJson(const QList<Kept> &kept)
{
    QJsonArray out;
    for (const Kept &k : kept) {
        QJsonObject o{{QStringLiteral("name"), k.name}, {QStringLiteral("xml"), k.xml}};
        if (!k.after.isEmpty()) {
            o.insert(QStringLiteral("after"), k.after);
        }
        if (k.derived) {
            o.insert(QStringLiteral("derived"), true);
        }
        out.append(o);
    }
    return out;
}

QList<Kept> keptFromJson(const QJsonValue &value)
{
    QList<Kept> out;
    for (const QJsonValue &v : value.toArray()) {
        const QJsonObject o = v.toObject();
        Kept k{o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("after")).toString(),
               o.value(QStringLiteral("xml")).toString(), o.value(QStringLiteral("derived")).toBool()};
        if (!k.name.isEmpty() && !k.xml.isEmpty()) {
            out << k;
        }
    }
    return out;
}

QString capture(QXmlStreamReader &in)
{
    QString out;
    QXmlStreamWriter w(&out);
    int depth = 0;
    while (true) {
        switch (in.tokenType()) {
        case QXmlStreamReader::StartElement: {
            w.writeStartElement(in.qualifiedName().toString());
            // AGENT-NOTE: the MSPDI default namespace is the document's;
            // the fragment stays unqualified so it lands in it on replay.
            // Any other declaration travels as an attribute.
            for (const QXmlStreamNamespaceDeclaration &d : in.namespaceDeclarations()) {
                if (d.prefix().isEmpty() && d.namespaceUri() == kNamespace) {
                    continue;
                }
                w.writeAttribute(d.prefix().isEmpty() ? QStringLiteral("xmlns")
                                                      : QStringLiteral("xmlns:") + d.prefix().toString(),
                                 d.namespaceUri().toString());
            }
            for (const QXmlStreamAttribute &a : in.attributes()) {
                w.writeAttribute(a.qualifiedName().toString(), a.value().toString());
            }
            ++depth;
            break;
        }
        case QXmlStreamReader::EndElement:
            w.writeEndElement();
            --depth;
            break;
        case QXmlStreamReader::Characters:
            if (in.isCDATA()) {
                w.writeCDATA(in.text().toString());
            } else if (!in.isWhitespace()) {
                w.writeCharacters(in.text().toString());
            }
            break;
        case QXmlStreamReader::Comment: w.writeComment(in.text().toString()); break;
        case QXmlStreamReader::EntityReference: w.writeEntityReference(in.name().toString()); break;
        case QXmlStreamReader::ProcessingInstruction:
            w.writeProcessingInstruction(in.processingInstructionTarget().toString(),
                                         in.processingInstructionData().toString());
            break;
        default: break;
        }
        if (depth == 0 || in.atEnd()) {
            break;
        }
        in.readNext();
    }
    return out;
}

void replay(QXmlStreamWriter &out, const QString &fragment)
{
    QXmlStreamReader in(fragment);
    // AGENT-GUARD: namespaces stay as written — the fragment's prefixes
    // and declarations are plain names and attributes here.
    in.setNamespaceProcessing(false);
    while (!in.atEnd()) {
        switch (in.readNext()) {
        case QXmlStreamReader::StartElement:
            out.writeStartElement(in.qualifiedName().toString());
            for (const QXmlStreamAttribute &a : in.attributes()) {
                out.writeAttribute(a.qualifiedName().toString(), a.value().toString());
            }
            break;
        case QXmlStreamReader::EndElement: out.writeEndElement(); break;
        case QXmlStreamReader::Characters:
            if (in.isCDATA()) {
                out.writeCDATA(in.text().toString());
            } else if (!in.isWhitespace()) {
                out.writeCharacters(in.text().toString());
            }
            break;
        case QXmlStreamReader::Comment: out.writeComment(in.text().toString()); break;
        case QXmlStreamReader::EntityReference: out.writeEntityReference(in.name().toString()); break;
        case QXmlStreamReader::ProcessingInstruction:
            out.writeProcessingInstruction(in.processingInstructionTarget().toString(),
                                           in.processingInstructionData().toString());
            break;
        default: break;
        }
    }
}

const QStringList &stampElements(Kind kind)
{
    static const QStringList task{QStringLiteral("UID"),         QStringLiteral("Start"),
                                  QStringLiteral("Finish"),      QStringLiteral("Duration"),
                                  QStringLiteral("PercentComplete"), QStringLiteral("ActualStart"),
                                  QStringLiteral("ActualFinish")};
    static const QStringList resource{QStringLiteral("UID"), QStringLiteral("Work"), QStringLiteral("MaxUnits"),
                                      QStringLiteral("StandardRate"), QStringLiteral("CostPerUse")};
    static const QStringList assignment{QStringLiteral("UID"),   QStringLiteral("TaskUID"),
                                        QStringLiteral("ResourceUID"), QStringLiteral("Start"),
                                        QStringLiteral("Finish"), QStringLiteral("Units"), QStringLiteral("Work")};
    static const QStringList none;
    switch (kind) {
    case Kind::Task: return task;
    case Kind::Resource: return resource;
    case Kind::Assignment: return assignment;
    case Kind::Project:
    case Kind::Calendar: break;
    }
    return none;
}

QString stampPart(const QString &element, const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return {};
    }
    if (element == QLatin1String("Start") || element == QLatin1String("Finish") || element.startsWith(QLatin1String("Actual"))) {
        const Minutes m = parseWireDateTime(t);
        return m == kNoDate ? t : wireDateTime(m);
    }
    if (element == QLatin1String("Duration") || element == QLatin1String("Work")) {
        const std::optional<Minutes> m = parseWireDuration(t);
        return m ? wireDuration(*m) : t;
    }
    const std::optional<qint64> n = scaledDecimal(t, 2);
    return n ? decimalText(*n, 2) : t;
}

Children::Children(Kind kind, const QJsonObject &extra, const QString &stampKey, const QString &keptKey)
    : m_kind(kind)
    , m_kept(keptFromJson(extra.value(keptKey)))
    , m_stampRead(extra.value(stampKey).toObject())
{
}

bool Children::stampHolds(const QString &element) const
{
    if (m_stampRead.isEmpty()) {
        return false;
    }
    static const QStringList rates{QStringLiteral("UID"), QStringLiteral("StandardRate"), QStringLiteral("CostPerUse")};
    const QStringList &basis = m_kind == Kind::Resource && element == QLatin1String("Rates") ? rates : stampElements(m_kind);
    return std::all_of(basis.cbegin(), basis.cend(), [this](const QString &e) {
        return m_stampRead.value(e).toString() == m_stampNow.value(e);
    });
}

int Children::rankOf(const QString &name, int depth) const
{
    const int rank = schemaRank(m_kind, name);
    if (rank >= 0 || depth > 64) {
        return rank >= 0 ? rank : 1 << 20;
    }
    // An element Project's list does not name goes where the file had it:
    // right after the element before it.
    for (const Kept &k : m_kept) {
        if (k.name == name) {
            return k.after.isEmpty() ? -1 : rankOf(k.after, depth + 1);
        }
    }
    return 1 << 20;
}

void Children::add(const QString &name, std::function<void()> write)
{
    m_slots << Slot{rankOf(name), 0, m_seq++, std::move(write)};
}

void Children::text(QXmlStreamWriter &out, const QString &name, const QString &value)
{
    if (stampElements(m_kind).contains(name)) {
        m_stampNow.insert(name, stampPart(name, value));
    }
    add(name, [&out, name, value] { out.writeTextElement(name, value); });
}

bool Children::kept(const QString &name) const
{
    return std::any_of(m_kept.cbegin(), m_kept.cend(), [&name](const Kept &k) { return k.name == name; });
}

bool Children::replays(const QString &name) const
{
    return std::any_of(m_kept.cbegin(), m_kept.cend(),
                       [&](const Kept &k) { return k.name == name && (!k.derived || stampHolds(k.name)); });
}

void Children::drop(const QString &name)
{
    m_kept.removeIf([&name](const Kept &k) { return k.name == name; });
}

void Children::write(QXmlStreamWriter &out)
{
    int seq = 0;
    for (const Kept &k : std::as_const(m_kept)) {
        if (k.derived && !stampHolds(k.name)) {
            continue;
        }
        const QString xml = k.xml;
        m_slots << Slot{rankOf(k.name), 1, seq++, [&out, xml] { replay(out, xml); }};
    }
    std::stable_sort(m_slots.begin(), m_slots.end(), [](const Slot &a, const Slot &b) {
        if (a.rank != b.rank) {
            return a.rank < b.rank;
        }
        return a.group != b.group ? a.group < b.group : a.seq < b.seq;
    });
    for (const Slot &slot : std::as_const(m_slots)) {
        slot.write();
    }
}

} // namespace QindaOffice::Plan::Mspdi
