// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_font.h"

#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <cmath>

namespace QindaOffice::Math {

struct MathFont::Data {
    QString family;
    QRawFont base;
    MathTable table;
    qreal unitsPerEm = 1000;
    mutable QMutex mutex;
    mutable QHash<int, QRawFont> sizes; // key: pixel size × 64
};

namespace {

constexpr int kReferencePixels = 64;

// AGENT-NOTE: the suite's mapping for Word's "Cambria Math" (D-100): the
// math fonts media-fonts/dejavu and media-fonts/noto install first.
const QStringList &suiteMathFamilies()
{
    static const QStringList families{
        QStringLiteral("DejaVu Math TeX Gyre"), QStringLiteral("Noto Sans Math"), QStringLiteral("STIX Two Math"),
        QStringLiteral("Latin Modern Math"),    QStringLiteral("TeX Gyre Termes Math"),
        QStringLiteral("TeX Gyre Pagella Math"), QStringLiteral("Libertinus Math"), QStringLiteral("XITS Math"),
    };
    return families;
}

// Fonts for characters the math font lacks.
const QStringList &fallbackFamilies()
{
    static const QStringList families{
        QStringLiteral("Noto Sans Math"),    QStringLiteral("DejaVu Sans"),        QStringLiteral("DejaVu Serif"),
        QStringLiteral("Noto Sans Symbols"), QStringLiteral("Noto Sans Symbols 2"), QStringLiteral("Noto Sans"),
    };
    return families;
}

int sizeKey(qreal pixelSize)
{
    return int(std::lround(pixelSize * 64.0));
}

QRawFont rawForFamily(const QString &family, bool bold, bool italic)
{
    QFont font(family);
    font.setPixelSize(kReferencePixels);
    font.setBold(bold);
    font.setItalic(italic);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::NoFontMerging);
    return QRawFont::fromFont(font);
}

// Process-wide cache of sized raw fonts by (family, bold, italic, size).
QRawFont cachedRaw(const QString &family, bool bold, bool italic, qreal pixelSize)
{
    static QMutex mutex;
    static QHash<QString, QRawFont> bases;
    static QHash<QString, QRawFont> sized;
    const QString key = family + QLatin1Char('|') + QChar(bold ? u'b' : u'-') + QChar(italic ? u'i' : u'-');
    const QString sizedKey = key + QLatin1Char('|') + QString::number(sizeKey(pixelSize));
    QMutexLocker lock(&mutex);
    if (const auto it = sized.constFind(sizedKey); it != sized.constEnd()) {
        return *it;
    }
    auto base = bases.constFind(key);
    if (base == bases.constEnd()) {
        base = bases.insert(key, QFontDatabase::hasFamily(family) ? rawForFamily(family, bold, italic) : QRawFont());
    }
    QRawFont out = *base;
    if (out.isValid()) {
        out.setPixelSize(pixelSize);
    }
    sized.insert(sizedKey, out);
    return out;
}

} // namespace

quint32 glyphIndex(const QRawFont &font, char32_t c)
{
    if (!font.isValid()) {
        return 0;
    }
    QChar chars[2];
    int count = 1;
    if (c > 0xFFFF) {
        chars[0] = QChar(QChar::highSurrogate(c));
        chars[1] = QChar(QChar::lowSurrogate(c));
        count = 2;
    } else {
        chars[0] = QChar(char16_t(c));
    }
    quint32 glyphs[2] = {0, 0};
    int numGlyphs = 2;
    if (!font.glyphIndexesForChars(chars, count, glyphs, &numGlyphs) || numGlyphs < 1) {
        return 0;
    }
    return glyphs[0];
}

QStringList MathFont::candidateFamilies(const QString &requestedFamily)
{
    QStringList out;
    if (!requestedFamily.isEmpty()) {
        out.append(requestedFamily);
    }
    for (const QString &family : suiteMathFamilies()) {
        if (!out.contains(family, Qt::CaseInsensitive)) {
            out.append(family);
        }
    }
    return out;
}

MathFont MathFont::open(const QString &requestedFamily)
{
    static QMutex mutex;
    static QHash<QString, MathFont> opened;
    QMutexLocker lock(&mutex);
    if (const auto it = opened.constFind(requestedFamily); it != opened.constEnd()) {
        return *it;
    }
    MathFont font;
    for (const QString &family : candidateFamilies(requestedFamily)) {
        if (!QFontDatabase::hasFamily(family)) {
            continue;
        }
        const QRawFont raw = rawForFamily(family, false, false);
        if (!raw.isValid() || raw.familyName().compare(family, Qt::CaseInsensitive) != 0) {
            continue; // fontconfig substituted another family
        }
        MathTable table = MathTable::parse(raw.fontTable("MATH"));
        if (!table.isValid()) {
            continue;
        }
        auto data = std::make_shared<Data>();
        data->family = raw.familyName();
        data->base = raw;
        data->table = std::move(table);
        data->unitsPerEm = raw.unitsPerEm() > 0 ? raw.unitsPerEm() : 1000;
        font.d = std::move(data);
        break;
    }
    opened.insert(requestedFamily, font);
    return font;
}

QString MathFont::family() const
{
    return d ? d->family : QString();
}

const MathTable &MathFont::table() const
{
    static const MathTable empty;
    return d ? d->table : empty;
}

qreal MathFont::unitsPerEm() const
{
    return d ? d->unitsPerEm : 1000;
}

qreal MathFont::designToPixels(qreal units, qreal pixelSize) const
{
    return units * pixelSize / unitsPerEm();
}

qreal MathFont::constant(MathConstant c, qreal pixelSize) const
{
    const int v = table().constant(c);
    switch (c) {
    case MathConstant::ScriptPercentScaleDown:
    case MathConstant::ScriptScriptPercentScaleDown:
    case MathConstant::RadicalDegreeBottomRaisePercent:
        return v;
    default:
        return designToPixels(v, pixelSize);
    }
}

QRawFont MathFont::raw(qreal pixelSize) const
{
    if (!d) {
        return {};
    }
    QMutexLocker lock(&d->mutex);
    const int key = sizeKey(pixelSize);
    if (const auto it = d->sizes.constFind(key); it != d->sizes.constEnd()) {
        return *it;
    }
    QRawFont out = d->base;
    out.setPixelSize(pixelSize);
    d->sizes.insert(key, out);
    return out;
}

GlyphInfo MathFont::describe(const QRawFont &font, quint32 glyph, bool mathFont) const
{
    GlyphInfo info;
    info.font = font;
    info.glyph = glyph;
    info.fromMathFont = mathFont;
    QPointF advance;
    if (font.advancesForGlyphIndexes(&glyph, &advance, 1, QRawFont::UseDesignMetrics)) {
        info.advance = advance.x();
    }
    info.bounds = font.boundingRect(glyph);
    if (mathFont) {
        const qreal px = font.pixelSize();
        if (const std::optional<int> italic = table().italicCorrection(glyph)) {
            info.italic = designToPixels(*italic, px);
        }
        if (const std::optional<int> accent = table().topAccentAttachment(glyph)) {
            info.topAccent = designToPixels(*accent, px);
        }
    }
    return info;
}

GlyphInfo MathFont::glyph(char32_t c, qreal pixelSize) const
{
    const QRawFont font = raw(pixelSize);
    if (const quint32 g = glyphIndex(font, c); g != 0) {
        return describe(font, g, true);
    }
    for (const QString &family : fallbackFamilies()) {
        const QRawFont fallback = cachedRaw(family, false, false, pixelSize);
        if (fallback.isValid() && fallback.supportsCharacter(uint(c))) {
            if (const quint32 g = glyphIndex(fallback, c); g != 0) {
                return describe(fallback, g, false);
            }
        }
    }
    GlyphInfo info = describe(font, 0, false);
    info.missing = true;
    return info;
}

GlyphInfo MathFont::glyphById(quint32 glyph, qreal pixelSize) const
{
    return describe(raw(pixelSize), glyph, true);
}

GlyphInfo MathFont::textGlyph(const QString &family, bool bold, bool italic, char32_t c, qreal pixelSize) const
{
    if (!family.isEmpty()) {
        const QRawFont font = cachedRaw(family, bold, italic, pixelSize);
        if (const quint32 g = glyphIndex(font, c); g != 0) {
            return describe(font, g, false);
        }
    }
    return glyph(c, pixelSize);
}

StretchedGlyph MathFont::single(const GlyphInfo &glyph)
{
    StretchedGlyph out;
    out.font = glyph.font;
    out.glyphs = {glyph.glyph};
    out.positions = {QPointF(0, 0)};
    out.width = glyph.advance;
    out.ascent = -glyph.bounds.top();
    out.descent = glyph.bounds.bottom();
    out.italic = glyph.italic;
    out.topAccent = glyph.topAccent;
    return out;
}

StretchedGlyph MathFont::stretch(const GlyphInfo &base, Qt::Orientation orientation, qreal target,
                                 qreal pixelSize, StretchRule rule) const
{
    if (!d || !base.fromMathFont) {
        return single(base);
    }
    const GlyphConstruction *c = orientation == Qt::Vertical ? table().vertical(base.glyph)
                                                             : table().horizontal(base.glyph);
    if (c == nullptr) {
        return single(base);
    }
    const qreal targetUnits = target * unitsPerEm() / pixelSize;
    if (rule == StretchRule::AtMost) {
        if (c->variants.isEmpty()) {
            return single(base);
        }
        quint32 pick = c->variants.first().glyph;
        for (const GlyphVariant &v : c->variants) {
            if (v.advance > targetUnits) {
                break;
            }
            pick = v.glyph;
        }
        return single(glyphById(pick, pixelSize));
    }
    for (const GlyphVariant &v : c->variants) {
        if (v.advance >= targetUnits) {
            return single(glyphById(v.glyph, pixelSize));
        }
    }
    if (!c->parts.isEmpty()) {
        return assemble(*c, orientation, targetUnits, pixelSize);
    }
    return c->variants.isEmpty() ? single(base) : single(glyphById(c->variants.last().glyph, pixelSize));
}

// OpenType MATH glyph assembly: repeat the extenders the fewest times that
// can reach the target with the minimum connector overlap, then share the
// excess as one overlap, bounded by every joint's connector lengths.
StretchedGlyph MathFont::assemble(const GlyphConstruction &c, Qt::Orientation orientation, qreal targetUnits,
                                  qreal pixelSize) const
{
    const int minOverlap = table().minConnectorOverlap();
    int fixedCount = 0;
    int extCount = 0;
    qreal fixedSum = 0;
    qreal extSum = 0;
    for (const GlyphPart &p : c.parts) {
        (p.extender ? extCount : fixedCount) += 1;
        (p.extender ? extSum : fixedSum) += p.fullAdvance;
    }
    int repeats = 0;
    while (extCount > 0 && repeats < 400) {
        const int n = fixedCount + repeats * extCount;
        const qreal reach = fixedSum + repeats * extSum - qreal(qMax(0, n - 1)) * minOverlap;
        if (reach >= targetUnits) {
            break;
        }
        ++repeats;
    }
    QList<GlyphPart> seq;
    for (const GlyphPart &p : c.parts) {
        for (int k = 0; k < (p.extender ? repeats : 1); ++k) {
            seq.append(p);
        }
    }
    if (seq.isEmpty()) {
        return single(glyphById(c.variants.isEmpty() ? 0 : c.variants.first().glyph, pixelSize));
    }
    qreal total = 0;
    for (const GlyphPart &p : seq) {
        total += p.fullAdvance;
    }
    qreal overlap = minOverlap;
    if (seq.size() > 1) {
        qreal maxOverlap = 1e9;
        for (qsizetype j = 0; j + 1 < seq.size(); ++j) {
            maxOverlap = qMin(maxOverlap, qreal(qMin(seq.at(j).endConnector, seq.at(j + 1).startConnector)));
        }
        maxOverlap = qMax(maxOverlap, qreal(minOverlap));
        overlap = qBound(qreal(minOverlap), (total - targetUnits) / qreal(seq.size() - 1), maxOverlap);
    }
    StretchedGlyph out;
    out.font = raw(pixelSize);
    out.assembled = true;
    out.italic = designToPixels(c.assemblyItalic, pixelSize);
    qreal start = 0;
    qreal length = 0;
    for (const GlyphPart &p : seq) {
        const GlyphInfo part = glyphById(p.glyph, pixelSize);
        const qreal at = designToPixels(start, pixelSize);
        out.glyphs.append(p.glyph);
        if (orientation == Qt::Vertical) {
            // Parts stack bottom-up from y = 0; each part's ink bottom sits at its start.
            out.positions.append(QPointF(0, -at - part.bounds.bottom()));
            out.width = qMax(out.width, part.advance);
        } else {
            out.positions.append(QPointF(at, 0));
            out.ascent = qMax(out.ascent, -part.bounds.top());
            out.descent = qMax(out.descent, part.bounds.bottom());
        }
        length = start + p.fullAdvance;
        start += p.fullAdvance - overlap;
    }
    if (orientation == Qt::Vertical) {
        out.ascent = designToPixels(length, pixelSize);
        out.descent = 0;
    } else {
        out.width = designToPixels(length, pixelSize);
    }
    return out;
}

} // namespace QindaOffice::Math
