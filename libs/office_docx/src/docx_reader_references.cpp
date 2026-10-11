// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_reader_p.h"
#include "docx_reference_fields.h"
#include "docx_merge_fields.h"
#include "docx_format.h"
namespace QindaOffice::Docx::detail {
void BodyReader::bookmarkRange(const Ooxml::XmlElement &marker, ContainerState &state)
{
    const QString id = wAttr(&marker, u"id");
    if (marker.name == u"bookmarkEnd") { m_openBookmarks.remove(id); return; }
    const QString name = wAttr(&marker, u"name");
    if (name.isEmpty() || name == u"_GoBack") return;
    m_openBookmarks.insert(id, name); state.pendingAnchors.append(name);
}
void BodyReader::prepareReferences(QTextCharFormat &format, const InlineState &inl, ContainerState &state)
{
    QVariantList fields;
    auto append = [&](const QVariantMap &field) {
        const QString code = field.value(QStringLiteral("code")).toString();
        // Merge expression evaluation keeps its own nested-code representation.
        if (!code.isEmpty() && !isMergeField(code) && referenceType(code) != u"HYPERLINK") fields.append(field);
    };
    for (auto &field : state.fields) if (field.inResult) {
        field.produced = true;
        append({{QStringLiteral("id"), field.referenceId}, {QStringLiteral("code"), field.instruction.trimmed()},
                {QStringLiteral("beginXml"), field.beginXml}});
    }
    for (const auto &field : inl.simpleReferences) append(field.toMap());
    if (!fields.isEmpty()) format.setProperty(kReferenceFields, fields);
    if (!m_openBookmarks.isEmpty()) format.setProperty(kBookmarkRanges, m_openBookmarks.values());
}
} // namespace QindaOffice::Docx::detail
