// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_math.h"
#include "docx_float.h"
#include "docx_properties.h"
#include "math_layout.h"
#include "math_omml.h"
#include <QPainter>
#include <QTextDocument>
#include <QTextInlineObject>
#include <QTextObjectInterface>
#include <QtGui/private/qtextdocumentlayout_p.h>
#include <atomic>

namespace QindaOffice::Docx {
namespace {
Math::EquationLayout layout(const QTextDocument *document, const QTextFormat &format)
{
    const auto equation = Math::readOmmlFragment(format.stringProperty(kInlineXml));
    Math::LayoutOptions options;
    const QTextCharFormat text = format.toCharFormat();
    options.fontSizePt = text.fontPointSize() > 0 ? text.fontPointSize() : document->defaultFont().pointSizeF();
    if (options.fontSizePt <= 0) options.fontSizePt = 11;
    if (text.foreground().style() != Qt::NoBrush) options.color = text.foreground().color();
    options.placeholders = false;
    return equation ? Math::EquationLayout(*equation, options) : Math::EquationLayout();
}
class MathHandler : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    using QObject::QObject;
    QSizeF intrinsicSize(QTextDocument *document, int, const QTextFormat &format) override
    {
        const auto math = layout(document, format);
        return math.isValid() ? QSizeF(math.width(), math.ascent() + math.descent()) : QSizeF(120, 24);
    }
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int,
                    const QTextFormat &format) override
    {
        const auto math = layout(document, format);
        if (math.isValid()) math.paint(*painter, rect.topLeft() + QPointF(0, math.ascent()));
        else { painter->save(); painter->setPen(Qt::black); painter->drawText(rect, Qt::AlignCenter, tr("Equation unavailable")); painter->restore(); }
    }
};
class MathDocumentLayout : public QTextDocumentLayout {
    Q_OBJECT
public:
    explicit MathDocumentLayout(QTextDocument *document) : QTextDocumentLayout(document)
    {
        registerHandler(kMathObject, new MathHandler(this));
    }
protected:
    void resizeInlineObject(QTextInlineObject item, int position, const QTextFormat &format) override
    {
        if (!isEquation(format.toCharFormat())) {
            QTextDocumentLayout::resizeInlineObject(item, position, format); return;
        }
        // AGENT-CONTRACT: Qt's default custom-object layout assigns zero
        // descent. Equations must reserve their real depth below the text
        // baseline; both screen and print use this same layout and painter.
        const auto math = layout(document(), format);
        item.setWidth(math.isValid() ? math.width() : 120);
        item.setAscent(math.isValid() ? math.ascent() : 20);
        item.setDescent(math.isValid() ? math.descent() : 4);
    }
};
}
void installMathLayout(QTextDocument *document)
{
    if (!document || qobject_cast<MathDocumentLayout *>(document->documentLayout())) return;
    document->setDocumentLayout(new MathDocumentLayout(document));
    installPictureHandler(document);
}
bool isEquation(const QTextCharFormat &format)
{
    return format.objectType() == kMathObject && !format.stringProperty(kInlineXml).isEmpty();
}
QTextCursor equationAt(const QTextCursor &cursor)
{
    if (cursor.isNull()) return {};
    if (cursor.hasSelection()) {
        return cursor.selectionEnd() - cursor.selectionStart() == 1 && isEquation(cursor.charFormat())
            ? cursor : QTextCursor();
    }
    for (int at : {cursor.position(), cursor.position() - 1}) {
        if (at < 0 || at >= cursor.document()->characterCount() - 1) continue;
        QTextCursor selected(cursor.document()); selected.setPosition(at);
        selected.setPosition(at + 1, QTextCursor::KeepAnchor);
        if (isEquation(selected.charFormat())) return selected;
    }
    return {};
}
QTextCharFormat equationFormat(const QString &xml, const QTextCharFormat &surrounding,
                               const QString &sourcePart, int serial)
{
    // Reader-assigned island ids start at one and are bounded by package size.
    // Keep newly authored islands disjoint, including adjacent identical formulas.
    static std::atomic<int> next{0x40000000};
    QTextCharFormat format = surrounding;
    format.setObjectType(kMathObject);
    format.setVerticalAlignment(QTextCharFormat::AlignNormal);
    format.setProperty(kInlineXml, xml);
    format.setProperty(kInlineSourcePart, sourcePart);
    format.setProperty(kInlineSerial, serial != 0 ? serial : next.fetch_add(1));
    format.setProperty(kInlineLabel, QStringLiteral("Equation"));
    return format;
}
}
#include "docx_math.moc"
