// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_metrics.h"

#include "docx_format.h"
#include "docx_properties.h"

#include <QHash>
#include <QRawFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QtEndian>

#include <cmath>

namespace QindaOffice::Docx {

namespace {

struct Metrics {
    qreal word = 0;
    qreal qt = 0;
};

quint16 u16(const QByteArray &table, int offset)
{
    return offset + 2 <= table.size() ? qFromBigEndian<quint16>(table.constData() + offset) : 0;
}

qint16 s16(const QByteArray &table, int offset)
{
    return offset + 2 <= table.size() ? qFromBigEndian<qint16>(table.constData() + offset) : 0;
}

QString keyOf(const QFont &font)
{
    return font.family() + QLatin1Char('|') + QString::number(font.pointSizeF(), 'f', 2) + QLatin1Char('|')
        + QString::number(font.weight()) + QLatin1Char('|') + QString::number(int(font.italic()));
}

const Metrics &metricsOf(const QFont &font)
{
    // AGENT-NOTE: QRawFont is expensive to open; a document uses a handful
    // of fonts. Bounded: a pathological document cannot grow it unduly.
    static QHash<QString, Metrics> cache;
    const QString key = keyOf(font);
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return it.value();
    }
    if (cache.size() > 256) {
        cache.clear();
    }
    Metrics m;
    const QRawFont raw = QRawFont::fromFont(font);
    if (raw.isValid()) {
        m.qt = std::ceil(raw.ascent() + raw.descent() + raw.leading());
        const QByteArray os2 = raw.fontTable("OS/2");
        const QByteArray hhea = raw.fontTable("hhea");
        const QByteArray head = raw.fontTable("head");
        const int unitsPerEm = u16(head, 18);
        const int winAscent = u16(os2, 74);
        const int winDescent = u16(os2, 76);
        if (unitsPerEm > 0 && winAscent + winDescent > 0) {
            const int hheaHeight = s16(hhea, 4) - s16(hhea, 6);
            const int external = qMax(0, s16(hhea, 8) - ((winAscent + winDescent) - hheaHeight));
            m.word = qreal(winAscent + winDescent + external) / unitsPerEm * raw.pixelSize();
        }
    }
    return *cache.insert(key, m);
}

// The font a paragraph is set in: its first run's, else its mark's.
QFont paragraphFont(const QTextBlock &block, const QFont &base)
{
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextCharFormat format = it.fragment().charFormat();
        if (!format.isImageFormat() && !it.fragment().text().trimmed().isEmpty()) {
            return format.font().resolve(base);
        }
    }
    return block.charFormat().font().resolve(base);
}

} // namespace

qreal wordSingleLineHeight(const QFont &font)
{
    return metricsOf(font).word;
}

qreal qtLineHeight(const QFont &font)
{
    return metricsOf(font).qt;
}

void applyWordLineMetrics(QTextDocument *document)
{
    const QFont base = document->defaultFont();
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (!format.hasProperty(kParagraphSnapshot) || format.hasProperty(kLineSpacingOwn)) {
            continue;
        }
        const int type = format.lineHeightType();
        if (type != QTextBlockFormat::SingleHeight && type != QTextBlockFormat::ProportionalHeight) {
            continue; // exact and at-least heights are Word's already
        }
        const qreal multiple = type == QTextBlockFormat::ProportionalHeight ? format.lineHeight() / 100.0 : 1.0;
        const Metrics &m = metricsOf(paragraphFont(block, base));
        if (m.word <= 0 || m.qt <= 0) {
            continue;
        }
        const qreal delta = m.word * multiple - m.qt;
        if (qAbs(delta) < 0.05) {
            continue;
        }
        format.setProperty(kLineSpacingOwn, QVariantList{type, format.lineHeight()});
        format.setLineHeight(delta, QTextBlockFormat::LineDistanceHeight);
        QTextCursor(block).setBlockFormat(format);
    }
}

} // namespace QindaOffice::Docx
