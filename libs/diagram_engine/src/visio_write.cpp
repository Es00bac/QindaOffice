// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing → Visio package (visio_io.h): the parts around the pages —
// document, masters, theme and the rest of the source, pages.xml, windows,
// properties. Pages are visio_write_page.cpp / visio_write_emit.cpp.
#include "visio_io.h"
#include "visio_write_p.h"

#include "ooxml_namespaces.h"
#include "ooxml_properties.h"
#include "themes.h"
#include "visio_data.h"
#include "visio_write_m87.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace QindaOffice::Diagram::Visio {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("VisioExport", text);
}

QString documentType(Kind kind)
{
    switch (kind) {
    case Kind::MacroDrawing:
        return QString::fromUtf16(Type::DrawingMacro);
    case Kind::Template:
        return QString::fromUtf16(Type::Template);
    case Kind::MacroTemplate:
        return QString::fromUtf16(Type::TemplateMacro);
    case Kind::Drawing:
        break;
    }
    return QString::fromUtf16(Type::Drawing);
}

bool isMacro(Kind kind)
{
    return kind == Kind::MacroDrawing || kind == Kind::MacroTemplate;
}

const SourcePage *sourcePageOf(const Source *source, const Page &page)
{
    if (source == nullptr) {
        return nullptr;
    }
    const QJsonObject vsdx = page.extra.value(QLatin1String("vsdx")).toObject();
    if (vsdx.value(QLatin1String("src")).toString() != source->token) {
        return nullptr;
    }
    const auto it = source->pages.constFind(vsdx.value(QLatin1String("page")).toString());
    return it == source->pages.constEnd() ? nullptr : &*it;
}

// Visio needs every page name unique (case-blind).
QString uniqueName(const QString &wanted, QSet<QString> &taken, int index)
{
    QString base = wanted.trimmed().isEmpty() ? QStringLiteral("Page-%1").arg(index + 1) : wanted.trimmed();
    QString name = base;
    for (int n = 2; taken.contains(name.toLower()); ++n) {
        name = QStringLiteral("%1 (%2)").arg(base).arg(n);
    }
    taken.insert(name.toLower());
    return name;
}

void copyDocumentRelationships(WriteContext &ctx, const QString &documentPart, Kind kind, bool commentsKept, bool dataKept)
{
    const Source &source = *ctx.source;
    for (const Ooxml::Relationship &rel : source.package->relationships(source.documentPart)) {
        if (rel.type == QStringView(Rel::Pages) || rel.type == QStringView(Rel::Masters)
            || rel.type == QStringView(Rel::Windows)) {
            continue; // written below
        }
        // M87: comments written afresh replace the package's own, and the
        // parts that thread them (Visio 365's modern comments, persons).
        const QString targetName = rel.external ? QString() : source.package->targetPart(source.documentPart, rel).toLower();
        if (!commentsKept && (rel.type == QStringView(kCommentsRelType) || targetName.contains(QLatin1String("comment"))
                              || targetName.contains(QLatin1String("person")))) {
            continue;
        }
        // M87: changed external data is written afresh (visio_data.h); the
        // connections Visio made travel either way.
        if (!dataKept && rel.type == QStringView(kRecordsetsRelType)) {
            continue;
        }
        if (rel.type.endsWith(QLatin1String("/vbaProject")) && !isMacro(kind)) {
            continue; // a drawing without macros carries no VBA project
        }
        if (rel.external) {
            ctx.writer->addRelationship(documentPart, rel.type, rel.target, true);
            continue;
        }
        const QString target = source.package->targetPart(source.documentPart, rel);
        if (!target.isEmpty() && source.package->contains(target)) {
            ctx.writer->addRelationship(documentPart, rel.type, ctx.copier->copyPart(target));
        }
    }
}

// The source's windows.xml when every page it names is still written.
bool sourceWindowsFit(const Source &source, const QSet<QString> &pageIds, QString *part)
{
    const auto rel = source.package->firstOfType(source.documentPart, QString::fromUtf16(Rel::Windows));
    if (!rel) {
        return false;
    }
    *part = source.package->targetPart(source.documentPart, *rel);
    const XmlElementPtr windows = source.package->xml(*part);
    if (!windows) {
        return false;
    }
    for (const XmlElement *w : children(*windows, u"Window")) {
        const QString page = w->attribute({}, u"Page");
        if (!page.isEmpty() && !pageIds.contains(page)) {
            return false;
        }
    }
    return true;
}

// Whether every page's comments are the ones read from `source` (and no
// page that had comments is gone).
bool commentsAsRead(const Drawing &drawing, const Source &source, const QVector<const SourcePage *> &sourcePages)
{
    QSet<QString> seen;
    for (int i = 0; i < drawing.pages.size(); ++i) {
        const SourcePage *sp = sourcePages.at(i);
        if (sp == nullptr) {
            if (!drawing.pages.at(i).comments.isEmpty()) {
                return false;
            }
            continue;
        }
        seen.insert(sp->id);
        if (!(drawing.pages.at(i).comments == sp->read.comments)) {
            return false;
        }
    }
    for (auto it = source.pages.cbegin(); it != source.pages.cend(); ++it) {
        if (!seen.contains(it.key()) && !it->read.comments.isEmpty()) {
            return false;
        }
    }
    return true;
}

} // namespace

QByteArray write(const Drawing &drawing, const Source *source, Kind kind, QString *error)
{
    Ooxml::PackageWriter writer;
    std::unique_ptr<Ooxml::RelationshipCopier> copier;
    if (source != nullptr) {
        copier = std::make_unique<Ooxml::RelationshipCopier>(source->package, &writer);
    }
    WriteContext ctx;
    ctx.drawing = &drawing;
    ctx.source = source;
    ctx.writer = &writer;
    ctx.copier = copier.get();
    ctx.defaultFont = theme(drawing.themeId).font;
    if (source != nullptr) {
        ctx.shapeStyle = source->lineStyle >= 0 ? source->lineStyle : 0;
        ctx.connectorStyle = source->connectorStyle >= 0 ? source->connectorStyle : ctx.shapeStyle;
    } else {
        ctx.shapeStyle = 3;     // Normal (visio_write_document.cpp)
        ctx.connectorStyle = 5; // Connector
    }
    const QString documentPart = QStringLiteral("visio/document.xml");
    const XmlElementPtr document = source != nullptr ? source->document->clone() : freshDocument();
    writer.addXmlPart(documentPart, *document, documentType(kind));
    writer.addRelationship(QString(), QString::fromUtf16(Rel::Document), documentPart);
    if (source != nullptr && !source->mastersPart.isEmpty()) {
        // Masters are carried whole (their parts keep their relationship
        // ids, so masters.xml needs no change).
        writer.addRelationship(documentPart, QString::fromUtf16(Rel::Masters), copier->copyPart(source->mastersPart));
        for (auto it = source->masterParts.cbegin(); it != source->masterParts.cend(); ++it) {
            ctx.masterParts.insert(it.key(), copier->copyPart(it.value()));
        }
    }
    // Pages: source pages keep their IDs, new pages take the next free ones
    // (decided first: a page names its background page by ID, M87).
    int nextPageId = 0;
    for (const Page &page : drawing.pages) {
        if (const SourcePage *sp = sourcePageOf(source, page)) {
            nextPageId = std::max(nextPageId, sp->id.toInt() + 1);
        }
    }
    QVector<const SourcePage *> sourcePages;
    QHash<QString, QString> pageIds; // model page id → Visio page ID
    QSet<QString> writtenIds;
    for (const Page &page : drawing.pages) {
        const SourcePage *sp = sourcePageOf(source, page);
        if (sp != nullptr && writtenIds.contains(sp->id)) {
            sp = nullptr; // a duplicated page is a new page
        }
        const QString id = sp != nullptr ? sp->id : QString::number(nextPageId++);
        writtenIds.insert(id);
        sourcePages.append(sp);
        pageIds.insert(page.id, id);
    }
    // M87: comments as read travel with the package's own part; changed
    // ones are written afresh (visio_write_m87.cpp).
    const bool commentsKept = source != nullptr && source->comments && commentsAsRead(drawing, *source, sourcePages);
    const bool dataKept = source != nullptr && recordsetsAsRead(drawing, *source);
    if (source != nullptr) {
        copyDocumentRelationships(ctx, documentPart, kind, commentsKept, dataKept);
    }
    const QString pagesPart = QStringLiteral("visio/pages/pages.xml");
    XmlElementPtr pages = makeElement(QStringLiteral("Pages"));
    pages->setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
    QSet<QString> names;
    QString firstPageId;
    QSizeF firstPageInches;
    QHash<QString, QHash<QString, int>> shapeIds; // model page id → model item → Visio ID
    for (int i = 0; i < drawing.pages.size(); ++i) {
        const Page &page = drawing.pages.at(i);
        const SourcePage *sp = sourcePages.at(i);
        const QString id = pageIds.value(page.id);
        const double scale = sp != nullptr ? sp->scale : 1.0;
        const QSizeF inches(page.size.width() / kPx / scale, page.size.height() / kPx / scale);
        const QString part = QStringLiteral("visio/pages/page%1.xml").arg(i + 1);
        PageWriter pageWriter(ctx, page, sp, part);
        const XmlElementPtr contents = pageWriter.build();
        shapeIds.insert(page.id, pageWriter.visioIds());
        writer.addXmlPart(part, *contents, QString::fromUtf16(Type::Page));
        const QString rel = writer.addRelationship(pagesPart, QString::fromUtf16(Rel::Page), part);
        QList<int> masters = pageWriter.mastersUsed().values();
        std::sort(masters.begin(), masters.end());
        for (const int master : std::as_const(masters)) {
            if (ctx.masterParts.contains(master)) {
                writer.addRelationship(part, QString::fromUtf16(Rel::Master), ctx.masterParts.value(master));
            }
        }
        const QString name = uniqueName(page.name, names, i);
        XmlElementPtr entry;
        if (sp != nullptr) {
            entry = sp->entry->clone();
            if (name != sp->name) {
                entry->setAttribute({}, QStringLiteral("Name"), name);
                entry->setAttribute({}, QStringLiteral("NameU"), name);
            }
            XmlElement *sheet = child(*entry, u"PageSheet");
            if (sheet == nullptr && (page.size != sp->size || pageSettingsDiffer(page, sp->read))) {
                sheet = &appendElement(*entry, QStringLiteral("PageSheet"));
            }
            if (page.size != sp->size) {
                setCell(*sheet, QStringLiteral("PageWidth"), numberText(inches.width()));
                setCell(*sheet, QStringLiteral("PageHeight"), numberText(inches.height()));
            }
            if (pageSettingsDiffer(page, sp->read)) {
                writePageSettings(*sheet, page, &sp->read); // M87
            }
            entry->removeChildren(kNs, u"Rel");
        } else {
            entry = freshPageEntry(id.toInt(), name, inches);
            writePageSettings(*child(*entry, u"PageSheet"), page, nullptr); // M87
        }
        // M87: a background page, and the one shown behind this page.
        const Page *back = drawing.page(page.backPageId);
        if (page.backgroundPage) {
            entry->setAttribute({}, QStringLiteral("Background"), QStringLiteral("1"));
        } else {
            entry->removeAttribute({}, u"Background");
        }
        if (back != nullptr && back->backgroundPage && pageIds.contains(back->id)) {
            entry->setAttribute({}, QStringLiteral("BackPage"), pageIds.value(back->id));
        } else {
            entry->removeAttribute({}, u"BackPage");
        }
        appendElement(*entry, QStringLiteral("Rel")).setAttribute(QString::fromUtf16(Ooxml::Ns::R), QStringLiteral("id"), rel);
        pages->appendElement(entry);
        if (firstPageId.isEmpty()) {
            firstPageId = id;
            firstPageInches = inches;
        }
    }
    if (!dataKept) {
        writeRecordsets(writer, documentPart, drawing, pageIds, shapeIds); // M87: external data
    }
    if (!commentsKept) {
        if (const XmlElementPtr comments = commentsElement(drawing, pageIds, shapeIds)) {
            writer.addXmlPart(QStringLiteral("visio/comments.xml"), *comments, QString::fromUtf16(kCommentsType));
            writer.addRelationship(documentPart, QString::fromUtf16(kCommentsRelType), QStringLiteral("visio/comments.xml"));
        }
    }
    writer.addXmlPart(pagesPart, *pages, QString::fromUtf16(Type::Pages));
    writer.addRelationship(documentPart, QString::fromUtf16(Rel::Pages), pagesPart);
    QString windowsPart;
    if (source != nullptr && sourceWindowsFit(*source, writtenIds, &windowsPart)) {
        writer.addRelationship(documentPart, QString::fromUtf16(Rel::Windows), copier->copyPart(windowsPart));
    } else {
        writer.addXmlPart(QStringLiteral("visio/windows.xml"), *freshWindows(firstPageId, firstPageInches),
                          QString::fromUtf16(Type::Windows));
        writer.addRelationship(documentPart, QString::fromUtf16(Rel::Windows), QStringLiteral("visio/windows.xml"));
    }
    // Properties: the source's kept (title follows the drawing), the
    // custom ones with RecalcDocument (visio_write_p.h).
    Ooxml::CoreProperties properties = source != nullptr ? source->properties : Ooxml::CoreProperties();
    properties.title = drawing.title;
    properties.modified = QDateTime::currentDateTimeUtc();
    if (!properties.created.isValid()) {
        properties.created = properties.modified;
    }
    Ooxml::writeProperties(writer, properties, QStringLiteral("QindaDiagram"), int(drawing.pages.size()));
    XmlElementPtr sourceCustom;
    if (source != nullptr) {
        if (const auto rel = source->package->firstOfType(QString(), QString::fromUtf16(Rel::CustomProperties))) {
            sourceCustom = source->package->xml(source->package->targetPart(QString(), *rel));
        }
    }
    writer.addXmlPart(QStringLiteral("docProps/custom.xml"), *customProperties(sourceCustom.get()),
                      QString::fromUtf16(Type::CustomProperties));
    writer.addRelationship(QString(), QString::fromUtf16(Rel::CustomProperties), QStringLiteral("docProps/custom.xml"));
    QString zipError;
    const QByteArray zip = writer.toZip(&zipError);
    if (zip.isEmpty() && error != nullptr) {
        *error = tr("The Visio file could not be written: %1").arg(zipError);
    }
    return zip;
}

bool save(const QString &path, const Drawing &drawing, const Source *source, QString *error)
{
    const QByteArray zip = write(drawing, source, kindForPath(path), error);
    if (zip.isEmpty()) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(zip) != zip.size() || !file.commit()) {
        if (error != nullptr) {
            *error = tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Diagram::Visio
