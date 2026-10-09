// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PowerPoint's own rules beyond the schema (D-070), checked structurally on
// a written package: sldSz/notesSz present, master and layout ids ≥
// 2147483648 and unique together, every master related to a theme, slide
// ids ≥ 256, every slide related to a layout, shape ids unique per slide,
// notes slides related to the notes master and their slide, an Override
// for every XML part. Shared by the pptx gates (tst_pptx_write and the M75
// ones); returns human-readable problems, empty when the package is fine.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QSet>
#include <QStringList>

#include <functional>

namespace PptxFixture {

namespace Ooxml = QindaOffice::Ooxml;

inline QStringList ruleViolations(const QByteArray &zip)
{
    QStringList problems;
    const auto package = Ooxml::Package::fromData(zip);
    if (!package) {
        return {QStringLiteral("not a package")};
    }
    const QString p = QString::fromUtf16(Ooxml::Ns::P);
    const QString r = QString::fromUtf16(Ooxml::Ns::R);
    const QString main = package->mainPart();
    const auto root = package->xml(main);
    if (!root || root->child(p, u"sldSz") == nullptr || root->child(p, u"notesSz") == nullptr) {
        problems.append(QStringLiteral("sldSz/notesSz missing"));
        return problems;
    }
    QSet<quint64> bigIds;
    for (const auto *master : root->child(p, u"sldMasterIdLst")->elements()) {
        const quint64 id = master->attr(u"id").toULongLong();
        if (id < 2147483648ull || bigIds.contains(id)) {
            problems.append(QStringLiteral("bad master id %1").arg(id));
        }
        bigIds.insert(id);
        const auto rel = package->relationship(main, master->attribute(r, u"id"));
        const QString part = rel ? package->targetPart(main, *rel) : QString();
        const auto masterRoot = package->xml(part);
        if (!masterRoot) {
            problems.append(QStringLiteral("master part %1 missing").arg(part));
            continue;
        }
        for (const auto *layout : masterRoot->child(p, u"sldLayoutIdLst")->elements()) {
            const quint64 lid = layout->attr(u"id").toULongLong();
            if (lid < 2147483648ull || bigIds.contains(lid)) {
                problems.append(QStringLiteral("bad layout id %1").arg(lid));
            }
            bigIds.insert(lid);
        }
        if (!package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::Theme))) {
            problems.append(QStringLiteral("master without theme"));
        }
    }
    QSet<quint64> slideIds;
    if (const auto *list = root->child(p, u"sldIdLst")) {
        for (const auto *sld : list->elements()) {
            const quint64 id = sld->attr(u"id").toULongLong();
            if (id < 256 || id >= 2147483648ull || slideIds.contains(id)) {
                problems.append(QStringLiteral("bad slide id %1").arg(id));
            }
            slideIds.insert(id);
            const auto rel = package->relationship(main, sld->attribute(r, u"id"));
            const QString part = rel ? package->targetPart(main, *rel) : QString();
            if (!package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::SlideLayout))) {
                problems.append(QStringLiteral("%1 has no layout").arg(part));
            }
            // Shape ids unique per slide.
            QSet<QString> ids;
            const std::function<void(const Ooxml::XmlElement &)> walk = [&](const Ooxml::XmlElement &e) {
                if (e.name == u"cNvPr" && e.ns == p) {
                    const QString id = e.attr(u"id");
                    if (ids.contains(id)) {
                        problems.append(QStringLiteral("%1: duplicate shape id %2").arg(part, id));
                    }
                    ids.insert(id);
                }
                for (const auto *c : e.elements()) {
                    if (c->name == u"Fallback") {
                        continue; // the fallback repeats the choice's id on purpose
                    }
                    walk(*c);
                }
            };
            walk(*package->xml(part));
            if (const auto notes = package->firstOfType(part, QString::fromUtf16(Ooxml::RelType::NotesSlide))) {
                const QString notesPart = package->targetPart(part, *notes);
                if (!package->firstOfType(notesPart, QString::fromUtf16(Ooxml::RelType::NotesMaster))
                    || !package->firstOfType(notesPart, QString::fromUtf16(Ooxml::RelType::Slide))) {
                    problems.append(QStringLiteral("%1 lacks its notes master or slide").arg(notesPart));
                }
            }
        }
    }
    for (const QString &name : package->partNames()) {
        if (name.endsWith(QLatin1String(".xml")) && !name.startsWith(QLatin1String("[")) && package->contentType(name) == u"application/xml"
            && !name.startsWith(QLatin1String("customXml"))) {
            problems.append(QStringLiteral("%1 has no content-type override").arg(name));
        }
    }
    return problems;
}

} // namespace PptxFixture
