// SPDX-License-Identifier: GPL-3.0-or-later
#include "odf_dom.h"

#include <QLocale>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QtMath>

namespace QindaOffice::Show::Odf {

namespace {

QString key(QStringView nsUri, QStringView local)
{
    return QLatin1Char('{') + nsUri.toString() + QLatin1Char('}') + local.toString();
}

} // namespace

QString OdfNode::attr(QStringView nsUri, QStringView local, const QString &fallback) const
{
    return attributes.value(key(nsUri, local), fallback);
}

bool OdfNode::hasAttr(QStringView nsUri, QStringView local) const
{
    return attributes.contains(key(nsUri, local));
}

const OdfNode *OdfNode::child(QStringView nsUri, QStringView local) const
{
    for (const OdfNodePtr &c : children) {
        if (c->is(nsUri, local)) {
            return c.get();
        }
    }
    return nullptr;
}

QList<const OdfNode *> OdfNode::elements() const
{
    QList<const OdfNode *> out;
    for (const OdfNodePtr &c : children) {
        if (!c->isText()) {
            out.append(c.get());
        }
    }
    return out;
}

QList<const OdfNode *> OdfNode::elements(QStringView nsUri, QStringView local) const
{
    QList<const OdfNode *> out;
    for (const OdfNodePtr &c : children) {
        if (c->is(nsUri, local)) {
            out.append(c.get());
        }
    }
    return out;
}

OdfNodePtr parseOdf(const QByteArray &data, QString *error)
{
    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(true);
    OdfNodePtr root;
    QList<OdfNode *> stack;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::DTD:
            // AGENT-GUARD: no DTDs, so no entity expansion from hostile files.
            if (error != nullptr) {
                *error = QStringLiteral("the document declares a DTD");
            }
            return nullptr;
        case QXmlStreamReader::StartElement: {
            auto node = std::make_shared<OdfNode>();
            node->ns = reader.namespaceUri().toString();
            node->name = reader.name().toString();
            for (const QXmlStreamAttribute &a : reader.attributes()) {
                node->attributes.insert(key(a.namespaceUri(), a.name()), a.value().toString());
            }
            OdfNode *raw = node.get();
            if (stack.isEmpty()) {
                root = std::move(node);
            } else {
                stack.last()->children.append(std::move(node));
            }
            stack.append(raw);
            break;
        }
        case QXmlStreamReader::EndElement:
            if (!stack.isEmpty()) {
                stack.removeLast();
            }
            break;
        case QXmlStreamReader::Characters:
            if (!stack.isEmpty()) {
                OdfNode *parent = stack.last();
                if (!parent->children.isEmpty() && parent->children.last()->isText()) {
                    parent->children.last()->text += reader.text();
                } else {
                    auto text = std::make_shared<OdfNode>();
                    text->text = reader.text().toString();
                    parent->children.append(std::move(text));
                }
            }
            break;
        default:
            break;
        }
    }
    if (reader.hasError() || !root) {
        if (error != nullptr) {
            *error = reader.hasError() ? QStringLiteral("XML error at line %1: %2")
                                             .arg(reader.lineNumber())
                                             .arg(reader.errorString())
                                       : QStringLiteral("empty document");
        }
        return nullptr;
    }
    return root;
}

qreal lengthToPt(QStringView value)
{
    static const QRegularExpression kLength(
        QStringLiteral("^\\s*([-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?)\\s*([a-z%]*)\\s*$"));
    const QRegularExpressionMatch match = kLength.matchView(value);
    if (!match.hasMatch()) {
        return 0;
    }
    const double number = match.captured(1).toDouble();
    const QString unit = match.captured(2);
    if (unit == u"pt") {
        return number;
    }
    if (unit == u"cm" || unit.isEmpty()) {
        return number * 72.0 / 2.54; // bare = legacy QindaShow centimetres
    }
    if (unit == u"mm") {
        return number * 72.0 / 25.4;
    }
    if (unit == u"in" || unit == u"inch") {
        return number * 72.0;
    }
    if (unit == u"pc") {
        return number * 12.0;
    }
    if (unit == u"px") {
        return number * 72.0 / 96.0;
    }
    return 0; // percentages and unknown units are not lengths
}

QString ptToLength(qreal pt)
{
    // AGENT-NOTE: points are the model's own unit, so writing them (with the
    // shortest digits that read back bit-exact) makes .fodp lossless; ODF
    // forbids exponents, hence 'f'.
    QString number = QString::number(pt, 'f', QLocale::FloatingPointShortest);
    if (number == u"-0") {
        number = QStringLiteral("0");
    }
    return number + QStringLiteral("pt");
}

QTransform parseTransform(QStringView value, bool *ok)
{
    static const QRegularExpression kStep(QStringLiteral("([a-zA-Z]+)\\s*\\(([^)]*)\\)"));
    QTransform total;
    bool parsed = false;
    QRegularExpressionMatchIterator it = kStep.globalMatchView(value);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString op = match.captured(1);
        const QStringList args = match.captured(2).split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                                         Qt::SkipEmptyParts);
        QTransform step;
        if (op == u"rotate" && !args.isEmpty()) {
            // ODF rotate() is counter-clockwise on a y-down page; QTransform's
            // rotate is clockwise there, hence the sign.
            step.rotateRadians(-args.at(0).toDouble());
        } else if (op == u"translate" && !args.isEmpty()) {
            step.translate(lengthToPt(args.at(0)), args.size() > 1 ? lengthToPt(args.at(1)) : 0.0);
        } else if (op == u"scale" && !args.isEmpty()) {
            const double sx = args.at(0).toDouble();
            step.scale(sx, args.size() > 1 ? args.at(1).toDouble() : sx);
        } else if (op == u"skewX" && !args.isEmpty()) {
            step.shear(std::tan(args.at(0).toDouble()), 0);
        } else if (op == u"skewY" && !args.isEmpty()) {
            step.shear(0, std::tan(args.at(0).toDouble()));
        } else if (op == u"matrix" && args.size() == 6) {
            step = QTransform(args.at(0).toDouble(), args.at(1).toDouble(), args.at(2).toDouble(),
                              args.at(3).toDouble(), lengthToPt(args.at(4)), lengthToPt(args.at(5)));
        } else {
            continue;
        }
        // AGENT-NOTE: LibreOffice applies the list LEFT TO RIGHT (the first
        // step acts on the shape first) — the reverse of SVG. QTransform's
        // `a * b` means "a, then b", which is that order.
        total = total * step;
        parsed = true;
    }
    if (ok != nullptr) {
        *ok = parsed;
    }
    return total;
}

QString rotationTransform(const QRectF &rect, qreal clockwiseDegrees)
{
    // ODF angle: counter-clockwise radians in (-π, π].
    qreal degrees = std::fmod(-clockwiseDegrees, 360.0);
    if (degrees <= -180.0) {
        degrees += 360.0;
    } else if (degrees > 180.0) {
        degrees -= 360.0;
    }
    const qreal alpha = qDegreesToRadians(degrees);
    // rotate(α) maps (x, y) → (x cosα + y sinα, −x sinα + y cosα); the box
    // centre must land on the model rect's centre.
    const qreal hw = rect.width() / 2.0;
    const qreal hh = rect.height() / 2.0;
    const qreal rx = hw * std::cos(alpha) + hh * std::sin(alpha);
    const qreal ry = -hw * std::sin(alpha) + hh * std::cos(alpha);
    const QPointF centre = rect.center();
    return QStringLiteral("rotate (%1) translate (%2 %3)")
        .arg(QString::number(alpha, 'f', QLocale::FloatingPointShortest), ptToLength(centre.x() - rx),
             ptToLength(centre.y() - ry));
}

} // namespace QindaOffice::Show::Odf
