// SPDX-License-Identifier: GPL-3.0-or-later
// How QindaWrite shows floating pictures (docx_float.h): Qt's floating
// frames for the wrapped ones, a picture handler that sizes the frames'
// boxes and draws the pictures in them, and a painter for the pictures
// behind and in front of the text.

#include "docx_float.h"

#include "docx_properties.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPaintDevice>
#include <QPainter>
#include <QPixmap>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextObjectInterface>
#include <QUrl>

#include <climits>
#include <cmath>

namespace QindaOffice::Docx {

namespace {

QImage pictureImage(const QTextDocument *document, const QTextImageFormat &format)
{
    const QVariant resource = document->resource(QTextDocument::ImageResource, QUrl(format.name()));
    if (resource.userType() == QMetaType::QImage) {
        return resource.value<QImage>();
    }
    if (resource.userType() == QMetaType::QPixmap) {
        return resource.value<QPixmap>().toImage();
    }
    if (resource.userType() == QMetaType::QByteArray) {
        return QImage::fromData(resource.toByteArray());
    }
    return {};
}

// AGENT-NOTE: the size Qt 6.11's QTextImageHandler gives an inline image
// (qtextimagehandler.cpp getPixmapSize): whole pixels from the format's
// width/height, the picture's own size for a missing side, scaled to the
// layout's paint device, capped by ImageMaxWidth. Kept identical so a
// document's inline pictures lay out the same with this handler.
QSizeF inlineSize(QTextDocument *document, const QTextImageFormat &format)
{
    const bool hasWidth = format.hasProperty(QTextFormat::ImageWidth);
    const bool hasHeight = format.hasProperty(QTextFormat::ImageHeight);
    const int width = qRound(format.width());
    const int height = qRound(format.height());
    QSize size(width, height);
    QImage image;
    if (!hasWidth || !hasHeight) {
        image = pictureImage(document, format);
        const QSizeF own = image.deviceIndependentSize();
        if (!hasWidth) {
            size.setWidth(hasHeight && own.height() > 0 ? qRound(height * (own.width() / own.height())) : qRound(own.width()));
        }
        if (!hasHeight) {
            size.setHeight(hasWidth && own.width() > 0 ? qRound(width * (own.height() / own.width())) : qRound(own.height()));
        }
    }
    if (const QPaintDevice *device = document->documentLayout()->paintDevice()) {
        if (image.isNull()) {
            image = pictureImage(document, format);
        }
        if (!image.isNull()) {
            size *= qreal(device->logicalDpiY()) / 96.0;
        }
    }
    if (format.hasProperty(QTextFormat::ImageMaxWidth)) {
        const QTextLength max = format.maximumWidth();
        const int limit = max.type() == QTextLength::PercentageLength
            ? int((document->pageSize().width() - 2 * document->documentMargin()) * max.value(100) / 100)
            : int(max.rawValue());
        if (size.width() > limit) {
            size.scale(limit, INT_MAX, Qt::KeepAspectRatio);
        }
    }
    return QSizeF(size);
}

qreal columnWidth(const QTextDocument *document)
{
    const QTextFrameFormat root = document->rootFrame()->frameFormat();
    const qreal page = document->pageSize().width() > 0 ? document->pageSize().width() : document->textWidth();
    return qMax<qreal>(24, page - root.leftMargin() - root.rightMargin());
}

// The box a wrapped picture's floating frame takes, and where the picture
// sits in it: its offset from the column's edge (left floats) and below
// its line, and the wrap distances on the text's side.
struct FloatBox {
    QSizeF size;
    QPointF inset;
};

FloatBox floatBox(QTextDocument *document, const QTextImageFormat &format, const PictureFloat &layout)
{
    const QSizeF picture = inlineSize(document, format);
    const qreal column = columnWidth(document);
    const qreal margin = document->rootFrame()->frameFormat().leftMargin();
    FloatBox box;
    const qreal down = layout.movesWithText() && layout.vAlign.isEmpty() ? qMax<qreal>(0, layout.vOffset) : 0;
    if (floatPosition(layout) == QTextFrameFormat::FloatRight) {
        box.inset = QPointF(layout.distLeft, down);
        box.size = QSizeF(layout.distLeft + picture.width(), down + picture.height() + layout.distBottom);
        return box;
    }
    qreal x = 0;
    if (layout.hAlign == u"center") {
        x = (column - picture.width()) / 2;
    } else if (layout.hAlign.isEmpty()) {
        x = layout.hRelative == u"page" || layout.hRelative == u"leftMargin" ? layout.hOffset - margin : layout.hOffset;
    }
    x = qBound<qreal>(0, x, qMax<qreal>(0, column - picture.width()));
    box.inset = QPointF(x, down);
    const qreal width = layout.wrap == u"topAndBottom" ? column : x + picture.width() + layout.distRight;
    box.size = QSizeF(qMin(width, column), down + picture.height() + layout.distBottom);
    return box;
}

// AGENT-NOTE (Qt 6.11 qtextdocumentlayout.cpp, positionFloat and
// floatMargins; qtextobject.cpp QTextFramePrivate::remove_me): a floating
// picture's frame stays in its parent frame's list of floats after its
// character goes (setPictureFloat replaces it, the picture is deleted, an
// undo takes it out). Qt detaches the frame without deleting it, and the
// list only forgets deleted frames, so its last box keeps pushing the text
// aside: an empty gap where the picture was. Dropping a detached frame's
// layout data makes it "dirty", and the layout passes dirty floats by; an
// undo that brings the character back re-attaches the frame and it is laid
// out anew.
void forgetDetachedFloats(QTextDocument *document)
{
    const QTextFrame *root = document->rootFrame();
    for (QTextFrame *frame : document->findChildren<QTextFrame *>(Qt::FindDirectChildrenOnly)) {
        if (frame != root && frame->parentFrame() == nullptr && frame->layoutData() != nullptr
            && frame->frameFormat().position() != QTextFrameFormat::InFlow) {
            frame->setLayoutData(nullptr);
        }
    }
}

const QTextFrame *floatFrame(const QTextDocument &document, const QTextCharFormat &format)
{
    const auto *frame = qobject_cast<const QTextFrame *>(document.objectForFormat(format));
    return frame != nullptr && frame->frameFormat().position() != QTextFrameFormat::InFlow ? frame : nullptr;
}

class PictureHandler final : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit PictureHandler(QObject *parent)
        : QObject(parent)
    {
        setObjectName(QStringLiteral("qqoPictureHandler"));
    }

    QSizeF intrinsicSize(QTextDocument *document, int position, const QTextFormat &format) override
    {
        Q_UNUSED(position);
        const QTextImageFormat image = format.toImageFormat();
        const PictureFloat layout = pictureFloat(image);
        if (layout.overText()) {
            return {0, 0}; // drawn over or under the text by drawFloatingPictures()
        }
        if (floatFrame(*document, image) != nullptr) {
            return floatBox(document, image, layout).size;
        }
        return inlineSize(document, image);
    }

    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int position,
                    const QTextFormat &format) override
    {
        Q_UNUSED(position);
        const QTextImageFormat image = format.toImageFormat();
        const PictureFloat layout = pictureFloat(image);
        if (layout.overText()) {
            return;
        }
        QRectF target = rect;
        if (floatFrame(*document, image) != nullptr) {
            const FloatBox box = floatBox(document, image, layout);
            target = QRectF(rect.topLeft() + box.inset, inlineSize(document, image));
        }
        const QImage picture = pictureImage(document, image);
        if (!picture.isNull()) {
            painter->drawImage(target, picture);
        }
    }
};

QSizeF pictureSize(const QTextDocument &document, const QTextImageFormat &format)
{
    return inlineSize(const_cast<QTextDocument *>(&document), format);
}

// A picture behind or in front of the text: where its position puts it on
// the page its paragraph is on.
QRectF overTextRect(const QTextDocument &document, int position, const QTextImageFormat &format, const PictureFloat &p)
{
    const QTextBlock block = document.findBlock(position);
    const QTextLayout *lines = block.layout();
    if (lines == nullptr) {
        return {};
    }
    const QRectF box = document.documentLayout()->blockBoundingRect(block);
    const QTextLine line = lines->lineForTextPosition(position - block.position());
    const qreal lineTop = box.top() + (line.isValid() ? line.y() : 0);
    const QSizeF page = document.pageSize();
    const qreal height = page.height() > 0 ? page.height() : 1e9;
    const qreal pageTop = std::floor(lineTop / height) * height;
    const QTextFrameFormat root = document.rootFrame()->frameFormat();
    const QSizeF size = pictureSize(document, format);
    // Horizontal: the span the position is relative to, then the offset or alignment.
    qreal from = root.leftMargin();
    qreal span = page.width() - root.leftMargin() - root.rightMargin();
    if (p.hRelative == u"page") {
        from = 0;
        span = page.width();
    } else if (p.hRelative == u"leftMargin") {
        from = 0;
        span = root.leftMargin();
    } else if (p.hRelative == u"rightMargin") {
        from = page.width() - root.rightMargin();
        span = root.rightMargin();
    } else if (p.hRelative == u"character" && line.isValid()) {
        from = box.left() + line.cursorToX(position - block.position());
        span = 0;
    }
    qreal x = from + p.hOffset;
    if (p.hAlign == u"center") {
        x = from + (span - size.width()) / 2;
    } else if (p.hAlign == u"right" || p.hAlign == u"outside") {
        x = from + span - size.width();
    } else if (!p.hAlign.isEmpty()) {
        x = from;
    }
    // Vertical.
    qreal top = box.top();
    qreal vspan = 0;
    if (p.vRelative == u"page") {
        top = pageTop;
        vspan = height;
    } else if (p.vRelative == u"margin") {
        top = pageTop + root.topMargin();
        vspan = height - root.topMargin() - root.bottomMargin();
    } else if (p.vRelative == u"topMargin") {
        top = pageTop;
        vspan = root.topMargin();
    } else if (p.vRelative == u"bottomMargin") {
        top = pageTop + height - root.bottomMargin();
        vspan = root.bottomMargin();
    } else if (p.vRelative == u"line") {
        top = lineTop;
    }
    qreal y = top + p.vOffset;
    if (p.vAlign == u"center") {
        y = top + (vspan - size.height()) / 2;
    } else if (p.vAlign == u"bottom" || p.vAlign == u"outside") {
        y = top + vspan - size.height();
    } else if (!p.vAlign.isEmpty()) {
        y = top;
    }
    return {x, y, size.width(), size.height()};
}

// What a picture's position is measured from, on the page of its line:
// the left edge and the top.
QPointF reference(const QTextDocument &document, int position, const PictureFloat &p)
{
    const QTextBlock block = document.findBlock(position);
    const QTextLayout *lines = block.layout();
    const QRectF box = document.documentLayout()->blockBoundingRect(block);
    const QTextLine line = lines != nullptr ? lines->lineForTextPosition(position - block.position()) : QTextLine();
    const qreal lineTop = box.top() + (line.isValid() ? line.y() : 0);
    const QSizeF page = document.pageSize();
    const qreal height = page.height() > 0 ? page.height() : 1e9;
    const qreal pageTop = std::floor(lineTop / height) * height;
    const QTextFrameFormat root = document.rootFrame()->frameFormat();
    qreal x = root.leftMargin();
    if (p.hRelative == u"page" || p.hRelative == u"leftMargin") {
        x = 0;
    } else if (p.hRelative == u"rightMargin") {
        x = page.width() - root.rightMargin();
    }
    qreal y = box.top();
    if (p.vRelative == u"page" || p.vRelative == u"topMargin") {
        y = pageTop;
    } else if (p.vRelative == u"margin") {
        y = pageTop + root.topMargin();
    } else if (p.vRelative == u"bottomMargin") {
        y = pageTop + height - root.bottomMargin();
    } else if (p.vRelative == u"line") {
        y = lineTop;
    }
    return {x, y};
}

} // namespace

PictureFloat movedPictureFloat(const QTextDocument &document, int position, const QPointF &delta)
{
    PictureFloat p = pictureFloat(characterFormatAt(document, position));
    const QRectF now = floatingPictureRect(document, position);
    if (!p.isFloating() || now.isEmpty()) {
        return p;
    }
    const QPointF from = reference(document, position, p);
    p.hAlign.clear();
    p.vAlign.clear();
    p.hOffset = now.left() + delta.x() - from.x();
    p.vOffset = now.top() + delta.y() - from.y();
    return p;
}

PictureFloat wrappedPictureFloat(const QTextDocument &document, int position, const QString &wrap)
{
    PictureFloat p = pictureFloat(characterFormatAt(document, position));
    const bool wasInline = !p.isFloating();
    if (wasInline && wrap != u"inline") {
        const QRectF now = floatingPictureRect(document, position);
        p.hRelative = QStringLiteral("column");
        p.vRelative = QStringLiteral("paragraph");
        p.hAlign.clear();
        p.vAlign.clear();
        p.wrap = wrap;
        const QPointF from = reference(document, position, p);
        p.hOffset = qMax<qreal>(0, now.left() - from.x());
        p.vOffset = qMax<qreal>(0, now.top() - from.y());
        if (wrap == u"behind" || wrap == u"inFront") {
            p.distLeft = p.distRight = 0;
        }
        return p;
    }
    p.wrap = wrap;
    return p;
}

QTextCharFormat characterFormatAt(const QTextDocument &document, int position)
{
    const QTextBlock block = document.findBlock(position);
    for (auto it = block.begin(); block.isValid() && !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (position >= fragment.position() && position < fragment.position() + fragment.length()) {
            return fragment.charFormat();
        }
    }
    return {};
}

void installPictureHandler(QTextDocument *document)
{
    QAbstractTextDocumentLayout *layout = document != nullptr ? document->documentLayout() : nullptr;
    if (layout == nullptr || layout->findChild<PictureHandler *>(QString(), Qt::FindDirectChildrenOnly) != nullptr) {
        return;
    }
    auto *handler = new PictureHandler(layout);
    layout->registerHandler(QTextFormat::ImageObject, handler);
    // contentsChange comes before QTextDocumentLayout::documentChanged()
    // relays the text out (Qt 6.11 QTextDocumentPrivate::finishEdit).
    QObject::connect(document, &QTextDocument::contentsChange, handler, [document] { forgetDetachedFloats(document); });
}

bool hasFloatingPictures(const QTextDocument &document)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isImageFormat() && pictureFloat(format).isFloating()) {
                return true;
            }
        }
    }
    return false;
}

int setPictureFloat(QTextDocument *document, int position, const PictureFloat &layout)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    if (cursor.selectedText() != QString(QChar::ObjectReplacementCharacter) || !cursor.charFormat().isImageFormat()) {
        return -1;
    }
    QTextImageFormat image = cursor.charFormat().toImageFormat();
    if (layout.isFloating() || image.hasProperty(kPictureFloatSnapshot)) {
        image.setProperty(kPictureFloat, layout.toMap());
    } else {
        image.clearProperty(kPictureFloat);
    }
    image.setObjectIndex(-1);
    if (layout.isFloating()) {
        installPictureHandler(document);
    }
    cursor.removeSelectedText();
    const QTextFrameFormat::Position where = floatPosition(layout);
    if (where == QTextFrameFormat::InFlow) {
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), image);
    } else {
        cursor.insertImage(image, where);
    }
    return position;
}

QRectF floatingPictureRect(const QTextDocument &document, int position)
{
    const QTextCharFormat format = characterFormatAt(document, position);
    if (!format.isImageFormat() || document.characterAt(position) != QChar::ObjectReplacementCharacter) {
        return {};
    }
    const QTextImageFormat image = format.toImageFormat();
    const PictureFloat layout = pictureFloat(image);
    if (layout.overText()) {
        return overTextRect(document, position, image, layout);
    }
    if (const QTextFrame *frame = floatFrame(document, image)) {
        const QRectF box = document.documentLayout()->frameBoundingRect(const_cast<QTextFrame *>(frame));
        const FloatBox inner = floatBox(const_cast<QTextDocument *>(&document), image, layout);
        return {box.topLeft() + inner.inset, pictureSize(document, image)};
    }
    const QTextBlock block = document.findBlock(position);
    const QTextLayout *lines = block.layout();
    const QTextLine line = lines != nullptr ? lines->lineForTextPosition(position - block.position()) : QTextLine();
    if (!line.isValid()) {
        return {};
    }
    // Qt sits an inline picture on the baseline.
    const QSizeF size = pictureSize(document, image);
    const QPointF origin = document.documentLayout()->blockBoundingRect(block).topLeft();
    const qreal baseline = origin.y() + line.y() + line.ascent();
    return {origin.x() + line.cursorToX(position - block.position()), baseline - size.height(), size.width(), size.height()};
}

void drawFloatingPictures(QPainter *painter, const QTextDocument &document, const QRectF &clip, bool behind)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            if (!format.isImageFormat()) {
                continue;
            }
            const PictureFloat layout = pictureFloat(format);
            if (!layout.overText() || (layout.wrap == u"behind") != behind) {
                continue;
            }
            for (int i = 0; i < fragment.length(); ++i) {
                const QRectF rect = overTextRect(document, fragment.position() + i, format.toImageFormat(), layout);
                const QImage picture = pictureImage(&document, format.toImageFormat());
                if (rect.intersects(clip) && !picture.isNull()) {
                    painter->drawImage(rect, picture);
                }
            }
        }
    }
}

} // namespace QindaOffice::Docx

#include "docx_float_display.moc"
