// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pptx writer's shared state (internal to show_model; D-070): one
// WriteSession per save (the package being built, the copier that carries
// content from the package the deck was read from, deduplicated media) and
// one PartWriter per slide / notes slide (its relationships and the shape
// ids already taken on it).
//
// AGENT-GUARD: preserved XML is rewritten by the copier against its SOURCE
// part BEFORE anything new is inserted into it — the copier resolves every
// r: attribute it meets against the source package, so an id the writer
// just created would be resolved against the wrong relationships file.

#include "pptx_context.h"
#include "pptx_origin.h"

#include "ooxml_copier.h"
#include "ooxml_package.h"

#include <QSet>

#include <memory>

namespace QindaOffice::Show::Pptx {

class WriteSession {
public:
    explicit WriteSession(const Presentation &deck);

    const Presentation &deck;
    Ooxml::PackageWriter writer;
    std::shared_ptr<const Ooxml::Package> source; // null for a deck not read from .pptx
    std::unique_ptr<Ooxml::RelationshipCopier> copier;
    QHash<QString, QString> slideParts; // model slide id → part written

    // The media part holding `bytes` (one part per distinct image).
    QString media(const QByteArray &bytes, const QString &mime);
    // M75: the part holding a video's or sound's bytes ("media<N>.<ext>",
    // its type from the extension); one part per distinct file.
    QString mediaFile(const QByteArray &bytes, const QString &mime, const QString &name);

private:
    QHash<QByteArray, QString> m_media; // content hash → part
};

class PartWriter {
public:
    PartWriter(WriteSession &session, QString part);

    WriteSession &session;
    const QString part;

    // A relationship from this part (internal target = a part name).
    QString relate(const QString &type, const QString &target, bool external = false);
    // Rewrites the relationship ids of XML read from `sourcePart` so they
    // resolve from this part (carrying what they point at).
    void carry(Ooxml::XmlElement &element, const QString &sourcePart);
    // An a:hlinkClick for the model's hyperlink spelling; nullptr for none.
    [[nodiscard]] Ooxml::XmlElementPtr hyperlink(const QString &target);
    // M91 (D-099, pptx_actions.cpp): action settings as `element`
    // (hlinkClick, hlinkHover, hlinkMouseOver): the jump, the program /
    // file / deck a verb names (an external relationship), the ScreenTip,
    // the highlight and the sound (a WAV part); nullptr for none.
    [[nodiscard]] Ooxml::XmlElementPtr action(const ShowAction &action, const QString &element);

    // Shape ids: the ids shapes read from the deck keep are reserved first
    // (reserveShapeIds); claim() then gives a shape its reserved id once,
    // or a fresh one above everything reserved or taken.
    void reserve(quint32 id) { m_reserved.insert(id); }
    [[nodiscard]] bool isFree(quint32 id) const { return id != 0 && !m_reserved.contains(id) && !m_taken.contains(id); }
    quint32 claim(quint32 wanted);
    [[nodiscard]] QSet<quint32> usedIds() const { return m_reserved + m_taken; }

private:
    QSet<quint32> m_reserved{1}; // 1 is the shape tree's own id
    QSet<quint32> m_taken{1};
};

// The relationship type URIs the writer uses.
[[nodiscard]] QString relType(const char *name);
// "image/png" → "png" (and the reverse guess for bytes without a type).
[[nodiscard]] QString extensionForMime(const QString &mime, const QByteArray &bytes);

} // namespace QindaOffice::Show::Pptx
