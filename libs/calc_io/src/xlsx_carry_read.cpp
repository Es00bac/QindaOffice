// SPDX-License-Identifier: GPL-3.0-or-later
// The read side of the carry stage (xlsx_carry.h, M66, D-075): the reduced
// copy of the source package a save carries from, and the notes on what
// cannot be carried.
//
// AGENT-NOTE: the copy keeps what a save may need and nothing else — the
// workbook part, every worksheet part with its <sheetData> emptied (the
// cells live in the engine), every chart sheet part (M69, D-078: its
// drawing and chart travel through the same drawing branch below), their
// relationships, every part a carried
// relationship reaches, the drawing and legacy drawing of a sheet with
// objects the engine does not model, the notes a threaded comment stands
// behind, and the table parts a query table hangs from. Cell data, shared
// strings, styles and the pictures and charts the engine models are left
// out, so the copy is small enough to ride undo snapshots and the crash-
// recovery journal (WorkbookSource is document state).

#include "xlsx_carry.h"

#include "xlsx_carry_types.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QObject>
#include <QSet>

#include <functional>

namespace QindaOffice::Calc::IO {

namespace {

using Ooxml::Package;
using Ooxml::Relationship;
using namespace Carry;

class ReducedPackage {
public:
    explicit ReducedPackage(std::shared_ptr<const Package> source)
        : m_source(std::move(source))
    {
    }

    // The part and every relationship it has, but none of their targets.
    void addShallow(const QString &part, const QByteArray &data)
    {
        if (m_added.contains(part)) {
            return;
        }
        m_added.insert(part);
        m_writer.addPart(part, data, m_source->contentType(part));
        for (const Relationship &rel : m_source->relationships(part)) {
            m_writer.addRelationshipWithId(part, rel.id, rel.type,
                                           rel.external ? rel.target : m_source->targetPart(part, rel), rel.external);
        }
    }

    // The part and everything it reaches. False (nothing added) when that
    // would take a part the writer makes itself — a worksheet, the styles —
    // into the copy: such a part cannot be carried.
    bool addClosure(const QString &part)
    {
        QSet<QString> reach;
        if (!collect(part, &reach)) {
            return false;
        }
        for (const QString &name : std::as_const(reach)) {
            addShallow(name, m_source->data(name));
        }
        return true;
    }

    // A relationship of the package itself (_rels/.rels).
    void addPackageRelationship(const Relationship &rel)
    {
        m_writer.addRelationshipWithId(QString(), rel.id, rel.type,
                                       rel.external ? rel.target : m_source->targetPart(QString(), rel), rel.external);
    }

    [[nodiscard]] QByteArray zip() const { return m_writer.toZip(); }

private:
    bool collect(const QString &part, QSet<QString> *reach) const
    {
        if (part.isEmpty() || reach->contains(part) || !m_source->contains(part)) {
            return true;
        }
        const QString type = m_source->contentType(part);
        if (type.contains(QLatin1String("worksheet+xml")) || type.contains(QLatin1String("chartsheet+xml"))
            || type.contains(QLatin1String("styles+xml")) || type.contains(QLatin1String("sharedStrings+xml"))
            || type.contains(QLatin1String("sheet.main+xml")) || type.contains(QLatin1String("macroEnabled.main+xml"))) {
            return false;
        }
        reach->insert(part);
        for (const Relationship &rel : m_source->relationships(part)) {
            if (!rel.external && !collect(m_source->targetPart(part, rel), reach)) {
                return false;
            }
        }
        return true;
    }

    std::shared_ptr<const Package> m_source;
    Ooxml::PackageWriter m_writer;
    QSet<QString> m_added;
};

// A worksheet part without its cell data (the engine has the cells).
QByteArray strippedSheet(const QByteArray &xml)
{
    const qsizetype open = xml.indexOf("sheetData");
    if (open < 0) {
        return xml;
    }
    const qsizetype tagEnd = xml.indexOf('>', open);
    if (tagEnd < 0 || xml.at(tagEnd - 1) == '/') {
        return xml; // <sheetData/>: nothing to strip
    }
    const qsizetype close = xml.indexOf("sheetData>", tagEnd);
    const qsizetype closeStart = close < 0 ? -1 : xml.lastIndexOf('<', close);
    if (closeStart < 0) {
        return xml;
    }
    return xml.left(tagEnd + 1) + xml.mid(closeStart);
}

// The relationship ids anything in `xml` names (r:id, r:embed, o:relid, …).
QSet<QString> idsIn(const QByteArray &xml)
{
    QSet<QString> ids;
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(xml);
    if (!root) {
        return ids;
    }
    std::function<void(const Ooxml::XmlElement &)> walk = [&](const Ooxml::XmlElement &e) {
        for (const Ooxml::XmlAttribute &a : e.attributes) {
            if (a.ns == QStringView(Ooxml::Ns::R) || (a.ns == QStringView(Ooxml::Ns::O) && a.name == u"relid")) {
                ids.insert(a.value);
            }
        }
        for (const Ooxml::XmlChild &child : e.children) {
            if (child.element) {
                walk(*child.element);
            }
        }
    };
    walk(*root);
    return ids;
}

} // namespace

QByteArray buildCarriedPackage(const QString &packagePath, const Workbook &wb, QStringList *warnings)
{
    QString error;
    const std::shared_ptr<const Package> source = Package::load(packagePath, &error);
    if (!source) {
        return {};
    }
    const QString workbook = source->mainPart();
    if (workbook.isEmpty()) {
        return {};
    }
    ReducedPackage copy(source);
    copy.addShallow(workbook, source->data(workbook));
    bool richData = false;
    QStringList unreachable;
    for (const Relationship &rel : source->relationships(workbook)) {
        if (rel.external || workbookRelWritten(rel.type)) {
            continue;
        }
        if (isRichDataRel(rel.type)) {
            richData = true;
        } else if (!copy.addClosure(source->targetPart(workbook, rel))) {
            unreachable.append(relKind(rel.type));
        }
    }
    bool signature = false;
    for (const Relationship &rel : source->relationships(QString())) {
        signature = signature || isSignatureRel(rel.type);
        const bool main = relKind(rel.type) == QLatin1String("officeDocument");
        if ((main || packageRelCarried(rel.type)) && !rel.external) {
            copy.addPackageRelationship(rel);
            if (!main) {
                copy.addClosure(source->targetPart(QString(), rel));
            }
        }
    }
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const QString part = wb.sheetAt(s).source().part;
        if (part.isEmpty()) {
            continue;
        }
        const QByteArray sheetXml = source->data(part);
        copy.addShallow(part, strippedSheet(sheetXml));
        for (const Relationship &rel : source->relationships(part)) {
            const QString target = rel.external ? QString() : source->targetPart(part, rel);
            const QString kind = relKind(rel.type);
            if (rel.external || target.isEmpty()) {
                continue;
            }
            if (kind == QLatin1String("drawing") || kind == QLatin1String("vmlDrawing")) {
                // The objects the engine does not model are carried from
                // these; only what those objects name comes along.
                const QByteArray xml = source->data(target);
                copy.addShallow(target, xml);
                for (const QString &id : idsIn(xml)) {
                    if (const auto inner = source->relationship(target, id); inner && !inner->external) {
                        copy.addClosure(source->targetPart(target, *inner));
                    }
                }
            } else if (kind == QLatin1String("comments") || kind == QLatin1String("table")) {
                copy.addShallow(target, source->data(target));
                for (const Relationship &inner : source->relationships(target)) {
                    if (!inner.external) {
                        copy.addClosure(source->targetPart(target, inner));
                    }
                }
            } else if (!sheetRelWritten(rel.type) && !copy.addClosure(target)) {
                unreachable.append(kind);
            }
        }
    }
    if (warnings != nullptr) {
        if (richData) {
            warnings->append(QObject::tr("Pictures placed in cells and linked data types are not kept when the workbook is saved."));
        }
        if (signature) {
            warnings->append(QObject::tr("The workbook's digital signature is removed on save: an edited workbook is no longer the one that was signed."));
        }
        if (!unreachable.isEmpty()) {
            unreachable.removeDuplicates();
            warnings->append(QObject::tr("Not kept on save (they reach into parts QindaCalc rewrites): %1.")
                                 .arg(unreachable.join(QStringLiteral(", "))));
        }
    }
    return copy.zip();
}

} // namespace QindaOffice::Calc::IO
