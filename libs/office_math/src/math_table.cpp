// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_table.h"

namespace QindaOffice::Math {

namespace {

// Big-endian reads that fail closed: an out-of-range offset clears ok().
class Reader {
public:
    explicit Reader(const QByteArray &data)
        : m_data(data)
    {
    }

    [[nodiscard]] bool ok() const { return m_ok; }

    quint16 u16(qsizetype offset)
    {
        if (offset < 0 || offset + 2 > m_data.size()) {
            m_ok = false;
            return 0;
        }
        const auto *p = reinterpret_cast<const uchar *>(m_data.constData()) + offset;
        return quint16((p[0] << 8) | p[1]);
    }

    qint16 s16(qsizetype offset) { return qint16(u16(offset)); }

private:
    const QByteArray &m_data;
    bool m_ok = true;
};

constexpr qsizetype kMaxCoverage = 65536;

// The glyphs of a Coverage table in coverage-index order.
QList<quint32> coverage(Reader &r, qsizetype offset)
{
    QList<quint32> out;
    const quint16 format = r.u16(offset);
    const quint16 count = r.u16(offset + 2);
    if (format == 1) {
        for (qsizetype i = 0; i < count && r.ok(); ++i) {
            out.append(r.u16(offset + 4 + 2 * i));
        }
    } else if (format == 2) {
        for (qsizetype i = 0; i < count && r.ok(); ++i) {
            const quint16 start = r.u16(offset + 4 + 6 * i);
            const quint16 end = r.u16(offset + 6 + 6 * i);
            for (quint32 g = start; g <= end && out.size() < kMaxCoverage; ++g) {
                out.append(g);
            }
        }
    }
    return r.ok() ? out : QList<quint32>();
}

// MathItalicsCorrectionInfo / MathTopAccentAttachment: a coverage plus one
// MathValueRecord per covered glyph.
QHash<quint32, int> valuesPerGlyph(Reader &r, qsizetype offset)
{
    QHash<quint32, int> out;
    const QList<quint32> glyphs = coverage(r, offset + r.u16(offset));
    const quint16 count = r.u16(offset + 2);
    for (qsizetype i = 0; i < count && i < glyphs.size(); ++i) {
        const qint16 value = r.s16(offset + 4 + 4 * i);
        if (!r.ok()) {
            return {};
        }
        out.insert(glyphs.at(i), value);
    }
    return out;
}

GlyphConstruction construction(Reader &r, qsizetype offset)
{
    GlyphConstruction c;
    const quint16 assembly = r.u16(offset);
    const quint16 variants = r.u16(offset + 2);
    for (qsizetype i = 0; i < variants && r.ok(); ++i) {
        c.variants.append({r.u16(offset + 4 + 4 * i), r.u16(offset + 6 + 4 * i)});
    }
    if (assembly != 0) {
        const qsizetype a = offset + assembly;
        c.assemblyItalic = r.s16(a);
        const quint16 parts = r.u16(a + 4);
        for (qsizetype i = 0; i < parts && r.ok(); ++i) {
            const qsizetype p = a + 6 + 10 * i;
            GlyphPart part;
            part.glyph = r.u16(p);
            part.startConnector = r.u16(p + 2);
            part.endConnector = r.u16(p + 4);
            part.fullAdvance = r.u16(p + 6);
            part.extender = (r.u16(p + 8) & 0x0001) != 0;
            c.parts.append(part);
        }
    }
    return c;
}

QHash<quint32, GlyphConstruction> constructions(Reader &r, qsizetype variants, qsizetype coverageOffset,
                                               quint16 count, qsizetype firstOffset)
{
    QHash<quint32, GlyphConstruction> out;
    if (coverageOffset == 0 || count == 0) {
        return out;
    }
    const QList<quint32> glyphs = coverage(r, variants + coverageOffset);
    for (qsizetype i = 0; i < count && i < glyphs.size() && r.ok(); ++i) {
        const quint16 at = r.u16(firstOffset + 2 * i);
        if (at != 0) {
            out.insert(glyphs.at(i), construction(r, variants + at));
        }
    }
    return r.ok() ? out : QHash<quint32, GlyphConstruction>();
}

} // namespace

MathTable MathTable::parse(const QByteArray &math)
{
    MathTable t;
    Reader r(math);
    if (math.size() < 10 || r.u16(0) != 1) {
        return t;
    }
    const qsizetype constants = r.u16(4);
    const qsizetype glyphInfo = r.u16(6);
    const qsizetype variants = r.u16(8);
    if (constants == 0) {
        return t;
    }
    // MathConstants: four 16-bit values, 51 MathValueRecords, one int16.
    t.m_constants.at(0) = r.s16(constants);
    t.m_constants.at(1) = r.s16(constants + 2);
    t.m_constants.at(2) = r.u16(constants + 4);
    t.m_constants.at(3) = r.u16(constants + 6);
    for (size_t i = 4; i + 1 < t.m_constants.size(); ++i) {
        t.m_constants.at(i) = r.s16(constants + 8 + 4 * qsizetype(i - 4));
    }
    t.m_constants.back() = r.s16(constants + 8 + 4 * 51);
    if (!r.ok()) {
        return t;
    }
    if (glyphInfo != 0) {
        if (const quint16 italics = r.u16(glyphInfo); italics != 0) {
            t.m_italics = valuesPerGlyph(r, glyphInfo + italics);
        }
        if (const quint16 accents = r.u16(glyphInfo + 2); accents != 0) {
            t.m_accents = valuesPerGlyph(r, glyphInfo + accents);
        }
        if (const quint16 extended = r.u16(glyphInfo + 4); extended != 0) {
            const QList<quint32> glyphs = coverage(r, glyphInfo + extended);
            t.m_extended = QSet<quint32>(glyphs.cbegin(), glyphs.cend());
        }
    }
    if (variants != 0) {
        t.m_minOverlap = r.u16(variants);
        const quint16 vCoverage = r.u16(variants + 2);
        const quint16 hCoverage = r.u16(variants + 4);
        const quint16 vCount = r.u16(variants + 6);
        const quint16 hCount = r.u16(variants + 8);
        t.m_vertical = constructions(r, variants, vCoverage, vCount, variants + 10);
        t.m_horizontal = constructions(r, variants, hCoverage, hCount, variants + 10 + 2 * qsizetype(vCount));
    }
    t.m_valid = r.ok();
    return t;
}

std::optional<int> MathTable::italicCorrection(quint32 glyph) const
{
    const auto it = m_italics.constFind(glyph);
    return it != m_italics.constEnd() ? std::optional<int>(*it) : std::nullopt;
}

std::optional<int> MathTable::topAccentAttachment(quint32 glyph) const
{
    const auto it = m_accents.constFind(glyph);
    return it != m_accents.constEnd() ? std::optional<int>(*it) : std::nullopt;
}

const GlyphConstruction *MathTable::vertical(quint32 glyph) const
{
    const auto it = m_vertical.constFind(glyph);
    return it != m_vertical.constEnd() ? &*it : nullptr;
}

const GlyphConstruction *MathTable::horizontal(quint32 glyph) const
{
    const auto it = m_horizontal.constFind(glyph);
    return it != m_horizontal.constEnd() ? &*it : nullptr;
}

} // namespace QindaOffice::Math
