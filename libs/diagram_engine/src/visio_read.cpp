// SPDX-License-Identifier: GPL-3.0-or-later
// Visio package → Drawing (visio_io.h): the document, masters and pages;
// shapes are in visio_read_shape.cpp.
#include "visio_data.h"
#include "visio_io.h"
#include "visio_read_p.h"

#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("VisioImport", text, disambiguation, n);
}

void collectMasterShapes(const XmlElement &shapes, MasterInfo &master, bool top)
{
    for (const XmlElement *shape : children(shapes, u"Shape")) {
        const int id = intAttribute(*shape, u"ID", -1);
        master.shapes.insert(id, shape);
        if (top && master.first < 0) {
            master.first = id;
        }
        if (const XmlElement *nested = child(*shape, u"Shapes")) {
            collectMasterShapes(*nested, master, false);
        }
    }
}

void readMasters(ReadContext &ctx, const QString &documentPart)
{
    const auto rel = ctx.package->firstOfType(documentPart, QString::fromUtf16(Rel::Masters));
    if (!rel) {
        return;
    }
    const QString mastersPart = ctx.package->targetPart(documentPart, *rel);
    const XmlElementPtr root = Ooxml::parseXml(ctx.package->data(mastersPart));
    if (!root) {
        return;
    }
    ctx.source->mastersPart = mastersPart;
    for (const XmlElement *master : children(*root, u"Master")) {
        const XmlElement *relElement = child(*master, u"Rel");
        const QString id = relElement != nullptr ? relElement->attribute(Ooxml::Ns::R, u"id") : QString();
        const auto masterRel = ctx.package->relationship(mastersPart, id);
        if (!masterRel) {
            continue;
        }
        MasterInfo info;
        info.id = intAttribute(*master, u"ID", -1);
        info.part = ctx.package->targetPart(mastersPart, *masterRel);
        info.root = Ooxml::parseXml(ctx.package->data(info.part));
        if (!info.root) {
            continue;
        }
        if (const XmlElement *shapes = child(*info.root, u"Shapes")) {
            collectMasterShapes(*shapes, info, true);
        }
        const int masterId = intAttribute(*master, u"ID", -1);
        ctx.source->masterParts.insert(masterId, info.part);
        ctx.source->masterRoots.insert(masterId, info.root);
        ctx.masters.insert(masterId, info);
    }
}

double cellNumber(const Sheet &sheet, QStringView name, double fallback)
{
    const Cell *c = sheet.cell(name);
    return c != nullptr ? c->number().value_or(fallback) : fallback;
}

void readPages(ReadContext &ctx, const QString &documentPart)
{
    const auto rel = ctx.package->firstOfType(documentPart, QString::fromUtf16(Rel::Pages));
    if (!rel) {
        return;
    }
    const QString pagesPart = ctx.package->targetPart(documentPart, *rel);
    const XmlElementPtr pages = Ooxml::parseXml(ctx.package->data(pagesPart));
    if (!pages) {
        return;
    }
    QHash<QString, QString> backPages; // model page id → Visio ID of its background page
    for (const Ooxml::XmlChild &c : pages->children) {
        if (!c.element || !c.element->is(kNs, u"Page")) {
            continue;
        }
        const XmlElement &entry = *c.element;
        const XmlElement *relElement = child(entry, u"Rel");
        const auto pageRel =
            ctx.package->relationship(pagesPart, relElement != nullptr ? relElement->attribute(Ooxml::Ns::R, u"id") : QString());
        if (!pageRel) {
            continue;
        }
        SourcePage source;
        source.id = entry.attribute({}, u"ID");
        source.part = ctx.package->targetPart(pagesPart, *pageRel);
        source.entry = c.element;
        source.background = Ooxml::Units::parseOnOff(entry.attribute({}, u"Background"), false);
        const XmlElement *pageSheetElement = child(entry, u"PageSheet");
        const Sheet sheet = pageSheetElement != nullptr ? Sheet::merge(*pageSheetElement, nullptr) : Sheet();
        const double pageScale = cellNumber(sheet, u"PageScale", 1);
        const double drawingScale = cellNumber(sheet, u"DrawingScale", 1);
        source.scale = pageScale > 0 && drawingScale > 0 ? pageScale / drawingScale : 1.0;
        const double widthIn = cellNumber(sheet, u"PageWidth", 8.5);
        const double heightIn = cellNumber(sheet, u"PageHeight", 11);
        const double k = kPx * source.scale;
        Page page;
        page.id = ctx.drawing->newId(QStringLiteral("p"));
        page.name = entry.attribute({}, u"Name", entry.attribute({}, u"NameU"));
        page.size = QSizeF(std::clamp(widthIn * k, 48.0, 20000.0), std::clamp(heightIn * k, 48.0, 20000.0));
        page.extra.insert(QStringLiteral("vsdx"),
                          QJsonObject{{QStringLiteral("src"), ctx.source->token}, {QStringLiteral("page"), source.id}});
        source.name = page.name;
        source.size = page.size;
        // M87 (D-095): layers, line jumps, print setup, Auto Size, and a
        // background page as one (BackPage resolved once every page is read).
        readPageSettings(ctx.doc, sheet, page);
        page.backgroundPage = source.background;
        if (entry.hasAttribute({}, u"BackPage")) {
            backPages.insert(page.id, entry.attribute({}, u"BackPage"));
        }
        int variation = int(cellNumber(sheet, u"VariationColorIndex", 0));
        if (variation < 0 || variation > 64) {
            variation = 0; // 65534: "use the document's"
        }
        const XmlElementPtr contents = Ooxml::parseXml(ctx.package->data(source.part));
        // Inches (drawing units, y up from the page bottom) → page units.
        const QTransform toPage(k, 0, 0, -k, 0, heightIn * k);
        if (contents) {
            PageReader reader(ctx, source, page, toPage, variation);
            const XmlElement *connects = child(*contents, u"Connects");
            reader.prescan(connects); // a group glued to keeps a shape of its own
            reader.readShapes(child(*contents, u"Shapes"));
            reader.readConnects(connects);
            reader.snapshot();
        }
        // Layer indexes past the page's layers name nothing.
        const int layerCount = int(page.layers.size());
        for (Shape &shape : page.shapes) {
            shape.layers.removeIf([layerCount](int i) { return i >= layerCount; });
        }
        for (Connector &connector : page.connectors) {
            connector.layers.removeIf([layerCount](int i) { return i >= layerCount; });
        }
        source.read = page;
        source.read.shapes.clear();
        source.read.connectors.clear();
        ctx.source->pages.insert(source.id, source);
        ctx.drawing->pages.append(page);
    }
    for (auto it = backPages.cbegin(); it != backPages.cend(); ++it) {
        for (const Page &back : std::as_const(ctx.drawing->pages)) {
            if (back.extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("page")).toString() == it.value()) {
                Page *page = ctx.drawing->page(it.key());
                page->backPageId = back.id;
                ctx.source->pages[page->extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("page")).toString()]
                    .read.backPageId = back.id;
            }
        }
    }
}

QStringList warningsFor(const ReadContext &ctx)
{
    QStringList out;
    if (ctx.metafiles > 0) {
        out << tr("%n picture(s) in Windows metafile format (EMF/WMF) are kept in the file but shown as frames.", nullptr,
                  ctx.metafiles);
    }
    if (ctx.objects > 0) {
        out << tr("%n embedded object(s) are kept in the file but shown as frames.", nullptr, ctx.objects);
    }
    if (ctx.kept > 0) {
        out << tr("%n shape(s) QindaDiagram does not show (guides, ink) are kept in the file.", nullptr, ctx.kept);
    }
    if (ctx.richText > 0) {
        out << tr("%n shape(s) have text in several formats; each is shown in one until you edit it.", nullptr,
                  ctx.richText);
    }
    return out;
}

bool readPackage(const QByteArray &zip, ReadResult *result, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    QString packageError;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::fromData(zip, &packageError);
    if (!package) {
        return fail(tr("This is not a Visio drawing (%1).").arg(packageError));
    }
    const auto documentRel = package->firstOfType(QString(), QString::fromUtf16(Rel::Document));
    const QString documentPart = documentRel ? package->targetPart(QString(), *documentRel) : QString();
    QString xmlError;
    const XmlElementPtr document = documentPart.isEmpty() ? nullptr : package->xml(documentPart, &xmlError);
    if (!document || !document->is(kNs, u"VisioDocument")) {
        return fail(tr("This is not a Visio 2013 or later drawing: it has no Visio document part."));
    }
    auto source = std::make_shared<Source>();
    source->token = QString::fromLatin1(QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex().left(16));
    source->package = package;
    source->documentPart = documentPart;
    source->document = document;
    source->properties = Ooxml::readCoreProperties(*package);
    ReadContext ctx;
    ctx.package = package;
    ctx.source = source.get();
    const auto themeRel = package->firstOfType(documentPart, QString::fromUtf16(Ooxml::RelType::Theme));
    const XmlElementPtr theme = themeRel ? package->xml(package->targetPart(documentPart, *themeRel)) : nullptr;
    ctx.doc.load(*document, theme.get());
    source->lineStyle = ctx.doc.defaultLineStyle;
    source->fillStyle = ctx.doc.defaultFillStyle;
    source->textStyle = ctx.doc.defaultTextStyle;
    source->connectorStyle = ctx.doc.styles.idNamed(u"Connector");
    Drawing drawing;
    drawing.title = source->properties.title;
    ctx.drawing = &drawing;
    readMasters(ctx, documentPart);
    readPages(ctx, documentPart);
    readComments(ctx, documentPart); // M87: after the pages, whose items they name
    readRecordsets(ctx, documentPart);
    if (drawing.pages.isEmpty()) {
        return fail(tr("The Visio drawing has no pages."));
    }
    result->drawing = drawing;
    result->warnings = warningsFor(ctx);
    result->source = source;
    return true;
}

} // namespace

const Sheet *MasterInfo::sheet(int id)
{
    if (id < 0) {
        id = first;
    }
    const auto cached = sheets.constFind(id);
    if (cached != sheets.constEnd()) {
        return &*cached;
    }
    const XmlElement *shape = shapes.value(id);
    if (shape == nullptr) {
        return nullptr;
    }
    return &*sheets.insert(id, Sheet::merge(*shape, nullptr));
}

const XmlElement *masterShape(const Source &source, int masterId, int shapeId)
{
    const XmlElementPtr root = source.masterRoots.value(masterId);
    const XmlElement *shapes = root ? child(*root, u"Shapes") : nullptr;
    if (shapes == nullptr) {
        return nullptr;
    }
    if (shapeId < 0) {
        const QList<const XmlElement *> top = children(*shapes, u"Shape");
        return top.isEmpty() ? nullptr : top.first();
    }
    // Depth-first through nested groups.
    QList<const XmlElement *> stack = children(*shapes, u"Shape");
    while (!stack.isEmpty()) {
        const XmlElement *shape = stack.takeFirst();
        if (intAttribute(*shape, u"ID", -1) == shapeId) {
            return shape;
        }
        if (const XmlElement *nested = child(*shape, u"Shapes")) {
            stack = children(*nested, u"Shape") + stack;
        }
    }
    return nullptr;
}

bool isVisioPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("vsdx") || suffix == QLatin1String("vsdm") || suffix == QLatin1String("vstx")
        || suffix == QLatin1String("vstm");
}

bool isLegacyVisioPath(const QString &path)
{
    static const QStringList legacy{QStringLiteral("vsd"), QStringLiteral("vss"), QStringLiteral("vst"),
                                    QStringLiteral("vdx"), QStringLiteral("vsx"), QStringLiteral("vtx")};
    return legacy.contains(QFileInfo(path).suffix().toLower());
}

QString legacyFormatMessage(const QString &path)
{
    return tr("%1 is in a Visio 2003–2010 format, which QindaDiagram cannot open. In Visio, choose File ▸ Save As "
              "▸ Visio Drawing (*.vsdx) and open that file instead.")
        .arg(QFileInfo(path).fileName());
}

Kind kindForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("vsdm")) {
        return Kind::MacroDrawing;
    }
    if (suffix == QLatin1String("vstx")) {
        return Kind::Template;
    }
    if (suffix == QLatin1String("vstm")) {
        return Kind::MacroTemplate;
    }
    return Kind::Drawing;
}

bool read(const QString &path, ReadResult *result, QString *error)
{
    if (isLegacyVisioPath(path)) {
        if (error != nullptr) {
            *error = legacyFormatMessage(path);
        }
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = tr("Could not open %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return readData(file.readAll(), result, error);
}

bool readData(const QByteArray &zip, ReadResult *result, QString *error)
{
    return readPackage(zip, result, error);
}

QString sourceToken(const Source &source)
{
    return source.token;
}

QString referencedToken(const Drawing &drawing)
{
    for (const Page &page : drawing.pages) {
        const QString token = page.extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("src")).toString();
        if (!token.isEmpty()) {
            return token;
        }
    }
    return {};
}

} // namespace QindaOffice::Diagram::Visio
