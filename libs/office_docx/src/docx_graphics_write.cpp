// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics_p.h"
#include "chart_interchange.h"
#include "ooxml_namespaces.h"
#include <QJsonObject>

namespace QindaOffice::Docx {
Ooxml::XmlElementPtr writeGraphic(const QTextCharFormat &format, const WordParts &parts,
                                  Ooxml::PackageWriter &writer, const QString &story, QString *error)
{
    using namespace Graphics;
    const QString kind = format.stringProperty(kGraphicKind);
    const auto current = format.property(kGraphicData).toMap();
    const auto previous = format.property(kGraphicSnapshot).toMap();
    const auto image = format.toImageFormat();
    if (kind.isEmpty() || (current == previous && format.property(kDrawingSize).toSizeF() == QSizeF(image.width(), image.height()))) return {};
    auto run = Ooxml::parseFragment(format.stringProperty(kInlineXml));
    if (!run) { if (error) *error = QStringLiteral("The selected graphic has no valid DrawingML source."); return {}; }
    const QString kR = QString::fromUtf16(Ooxml::Ns::R);
    const QString kC = QString::fromUtf16(Ooxml::Ns::C);
    const QString kWPS = QString::fromUtf16(Ooxml::Ns::WPS);
    const QString sourceStory = format.stringProperty(kInlineSourcePart);
    // New relationships are inserted only AFTER source ids are carried.
    // An independent copier detaches one edited reference from any twins.
    Ooxml::RelationshipCopier copier(parts.source, &writer);
    if (current == previous) {
        if (parts.source) copier.rewrite(*run, sourceStory, story);
    } else if (kind == u"chart") {
        auto *reference = descendant(*run, kC, u"chart");
        if (!reference) { if (error) *error = QStringLiteral("The chart reference is missing."); return {}; }
        reference->removeAttribute(kR, u"id");
        if (parts.source) copier.rewrite(*run, sourceStory, story);
        const QString sourcePart = format.stringProperty(kGraphicSource);
        const QString part = Show::ChartIO::write(writer, sourcePart.isEmpty() ? nullptr : parts.source, sourcePart,
            Show::ChartIO::fromMap(current), Show::ChartIO::fromMap(previous), QStringLiteral("word"), error);
        if (part.isEmpty()) { if (error && error->isEmpty()) *error = QStringLiteral("The chart data could not be saved."); return {}; }
        reference->setAttribute(kR, QStringLiteral("id"), writer.addRelationship(story, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"), part));
    } else if (kind == u"smartArt") {
        writeDiagram(*run, current, format.stringProperty(kGraphicSource).isEmpty() ? nullptr : parts.source, writer, story, sourceStory);
    } else if (kind == u"shape") {
        auto *wsp = descendant(*run, kWPS, u"wsp");
        if (!wsp) { if (error) *error = QStringLiteral("The drawing shape source is missing."); return {}; }
        const auto shape = Show::shapeFromJson(QJsonObject::fromVariantMap(current));
        const auto old = Show::shapeFromJson(QJsonObject::fromVariantMap(previous));
        *wsp = *shapeXml(shape, wsp, previous.isEmpty() ? nullptr : &old);
        if (parts.source) copier.rewrite(*run, sourceStory, story);
    }
    if (kind == u"shape") {
        if (auto *wsp = descendant(*run, kWPS, u"wsp"))
            if (auto *extent = descendant(*wsp, QString::fromUtf16(Ooxml::Ns::A), u"ext")) {
                extent->setAttribute({}, QStringLiteral("cx"), QString::number(qRound64(image.width() * 9525)));
                extent->setAttribute({}, QStringLiteral("cy"), QString::number(qRound64(image.height() * 9525)));
            }
    }
    if (auto *extent = descendant(*run, QString::fromUtf16(Ooxml::Ns::WP), u"extent")) {
        extent->setAttribute({}, QStringLiteral("cx"), QString::number(qRound64(image.width() * 9525)));
        extent->setAttribute({}, QStringLiteral("cy"), QString::number(qRound64(image.height() * 9525)));
    }
    return run;
}
} // namespace QindaOffice::Docx
