// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Private shared helpers for the bounded graphic modules.
#include "docx_graphics.h"
#include "shape_json.h"
namespace QindaOffice::Docx::Graphics {
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
[[nodiscard]] XmlElement *descendant(XmlElement &root, QStringView ns, QStringView name);
[[nodiscard]] QString related(const std::shared_ptr<const Ooxml::Package> &source,
                               const QString &part, const QString &id);
[[nodiscard]] XmlElementPtr drawingRun(const QString &kind, const QSizeF &size);
[[nodiscard]] XmlElementPtr shapeXml(const Show::Shape &shape, const XmlElement *original = nullptr,
                                    const Show::Shape *previous = nullptr);
[[nodiscard]] Show::Shape defaultShape();
void replaceText(Show::Shape &shape, const QString &text);
[[nodiscard]] QVariantMap readDiagram(const XmlElement &data, const std::shared_ptr<const Ooxml::Package> &source,
                                      const QString &story, const Ooxml::Theme &theme);
[[nodiscard]] QVariantMap newDiagram();
bool editDiagram(QVariantMap &data, const QStringList &texts, QString *error);
void writeDiagram(XmlElement &run, const QVariantMap &data, const std::shared_ptr<const Ooxml::Package> &source,
                  Ooxml::PackageWriter &writer, const QString &story, const QString &sourceStory);
} // namespace QindaOffice::Docx::Graphics
