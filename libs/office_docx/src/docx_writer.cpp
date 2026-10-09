// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_writer.h"
#include "docx_comments.h"

#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_properties.h"
#include "ooxml_units.h"

#include <QSaveFile>
#include <QTextFrame>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kMC = QString::fromUtf16(Ooxml::Ns::MC);
const QString kMain = QStringLiteral("word/document.xml");
const QString kTypePrefix = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.");

QString type(const char *suffix)
{
    return detail::wordprocessingType(suffix);
}

XmlElementPtr sourceXml(const Ooxml::Package &package, const QString &main, const char16_t *relType, QString *part)
{
    const auto rel = package.firstOfType(main, QString::fromUtf16(relType));
    if (!rel) {
        return nullptr;
    }
    *part = package.targetPart(main, *rel);
    return part->isEmpty() ? nullptr : package.xml(*part);
}

} // namespace

namespace detail {

QString wordprocessingType(const char *suffix)
{
    return kTypePrefix + QString::fromLatin1(suffix);
}

// Declares every Microsoft extension namespace the part uses as
// mc:Ignorable on its root, as Word does.
void markIgnorable(XmlElement &root)
{
    static const QStringList kExtensions{
        QString::fromUtf16(Ooxml::Ns::W14), QString::fromUtf16(Ooxml::Ns::W15), QString::fromUtf16(Ooxml::Ns::WP14),
        QStringLiteral("http://schemas.microsoft.com/office/word/2015/wordml/symex"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2016/wordml/cid"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2018/wordml"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2018/wordml/cex"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2020/wordml/sdtdatahash"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2023/wordml/word16du")};
    QSet<QString> used;
    std::function<void(const XmlElement &)> walk = [&](const XmlElement &e) {
        used.insert(e.ns);
        for (const Ooxml::XmlAttribute &a : e.attributes) {
            used.insert(a.ns);
        }
        for (const Ooxml::XmlChild &c : e.children) {
            if (c.element) {
                walk(*c.element);
            }
        }
    };
    walk(root);
    QStringList tokens;
    for (const QString &uri : kExtensions) {
        if (used.contains(uri)) {
            tokens.append(QLatin1Char('{') + uri + QLatin1Char('}'));
        }
    }
    root.removeAttribute(kMC, u"Ignorable");
    if (!tokens.isEmpty()) {
        root.setAttribute(kMC, QStringLiteral("Ignorable"), tokens.join(QLatin1Char(' ')));
    }
}

} // namespace detail

using detail::markIgnorable;

QByteArray docxBytes(const QTextDocument &body, const WordParts &parts, const WriteOptions &options, QString *error)
{
    detail::WriteEnv env;
    env.parts = &parts;
    const std::shared_ptr<const Ooxml::Package> &source = parts.source;
    XmlElementPtr sourceStyles;
    XmlElementPtr sourceNumbering;
    XmlElementPtr sourceSettings;
    QString sourceSettingsPart;
    if (source) {
        env.sourceMain = source->mainPart();
        env.copier = std::make_unique<Ooxml::RelationshipCopier>(source, &env.writer);
        QString part;
        sourceStyles = sourceXml(*source, env.sourceMain, Ooxml::RelType::Styles, &part);
        env.sourceStyles = StyleSheet::parse(sourceStyles.get());
        sourceNumbering = sourceXml(*source, env.sourceMain, Ooxml::RelType::Numbering, &part);
        sourceSettings = sourceXml(*source, env.sourceMain, Ooxml::RelType::Settings, &sourceSettingsPart);
        if (const auto rel = source->firstOfType(env.sourceMain, QString::fromUtf16(Ooxml::RelType::Footnotes))) {
            env.sourceFootnotesPart = source->targetPart(env.sourceMain, *rel);
        }
        if (const auto rel = source->firstOfType(env.sourceMain, QString::fromUtf16(Ooxml::RelType::Endnotes))) {
            env.sourceEndnotesPart = source->targetPart(env.sourceMain, *rel);
        }
    }
    if (parts.styles) {
        // The styles as QindaWrite's style commands left them (M73).
        sourceStyles = parts.styles;
        env.sourceStyles = StyleSheet::parse(sourceStyles.get());
    }
    env.numbering = std::make_unique<detail::NumberingBuilder>(sourceNumbering);
    env.layoutChanged = !source || parts.sourceSectionXml.isEmpty() || !(parts.layout == parts.importedLayout);
    env.headersChanged = !source || parts.differentFirstPage != parts.importedDifferentFirstPage;
    for (const auto *list : {&parts.headers, &parts.footers}) {
        for (const HeaderFooter &hf : *list) {
            if (hf.document && (hf.document->isModified() || hf.sourcePart.isEmpty())) {
                env.headersChanged = true;
            }
        }
    }

    // Headers and footers first, so the section hook can point at them.
    struct Reference {
        bool header;
        QString slot;
        QString id;
    };
    QList<Reference> references;
    if (env.headersChanged) {
        static const char *kSlots[] = {"default", "first", "even"};
        for (int s = 0; s < kHeaderFooterSlots; ++s) {
            for (const bool header : {true, false}) {
                const HeaderFooter &hf = header ? parts.headers[size_t(s)] : parts.footers[size_t(s)];
                if (!hf.document || (hf.document->isEmpty() && hf.sourcePart.isEmpty())) {
                    continue;
                }
                QString partName;
                if (!hf.document->isModified() && !hf.sourcePart.isEmpty() && env.copier) {
                    partName = env.copier->copyPart(hf.sourcePart);
                } else {
                    partName = env.writer.uniquePartName(header ? QStringLiteral("word/header")
                                                                : QStringLiteral("word/footer"),
                                                         QStringLiteral(".xml"));
                    XmlElement root(kW, header ? QStringLiteral("hdr") : QStringLiteral("ftr"));
                    detail::BodyWriter(env, hf.document.get(), partName).writeFrame(hf.document->rootFrame(), root);
                    markIgnorable(root);
                    env.writer.addXmlPart(partName, root, type(header ? "header+xml" : "footer+xml"));
                }
                const QString rel = env.writer.addRelationship(
                    kMain, QString::fromUtf16(header ? Ooxml::RelType::Header : Ooxml::RelType::Footer), partName);
                references.append({header, QString::fromLatin1(kSlots[s]), rel});
            }
        }
    }
    // A paragraph-level section whose page is not the document's as read
    // has a page of its own (an envelope before a letter, a label sheet,
    // M77): a new page layout leaves it alone.
    bool closingSection = false;
    auto ownPage = [&](const XmlElement &sectPr) {
        const XmlElement *size = sectPr.child(kW, u"pgSz");
        if (closingSection || size == nullptr) {
            return false;
        }
        const qreal w = Ooxml::Units::twipsToPx(Ooxml::Units::parseTwips(wAttr(size, u"w"), 0));
        const qreal h = Ooxml::Units::twipsToPx(Ooxml::Units::parseTwips(wAttr(size, u"h"), 0));
        return qAbs(w - parts.importedLayout.pageSize.width()) > 1 || qAbs(h - parts.importedLayout.pageSize.height()) > 1;
    };
    env.sectionHook = [&](XmlElement &sectPr, const QString &part) {
        if (env.copier) {
            if (env.headersChanged) {
                sectPr.removeChildren(kW, u"headerReference");
                sectPr.removeChildren(kW, u"footerReference");
            }
            env.copier->rewrite(sectPr, env.sourceMain, part);
        }
        if (env.headersChanged) {
            sectPr.removeChildren(kW, u"headerReference");
            sectPr.removeChildren(kW, u"footerReference");
            for (const Reference &ref : std::as_const(references)) {
                XmlElement &e = sectPr.appendElement(kW, ref.header ? QStringLiteral("headerReference")
                                                                    : QStringLiteral("footerReference"));
                e.setAttribute(kW, QStringLiteral("type"), ref.slot);
                e.setAttribute(kR, QStringLiteral("id"), ref.id);
            }
            sectPr.removeChildren(kW, u"titlePg");
            if (parts.differentFirstPage) {
                sectPr.appendElement(kW, QStringLiteral("titlePg"));
            }
        }
        if (env.layoutChanged && !ownPage(sectPr)) {
            const PageLayout &l = parts.layout;
            XmlElement &size = detail::wChild(sectPr, QStringLiteral("pgSz"));
            const QString code = size.attribute(kW, u"code");
            size.attributes.clear();
            size.setAttribute(kW, QStringLiteral("w"), detail::twips(l.pageSize.width()));
            size.setAttribute(kW, QStringLiteral("h"), detail::twips(l.pageSize.height()));
            if (l.landscape()) {
                size.setAttribute(kW, QStringLiteral("orient"), QStringLiteral("landscape"));
            }
            if (!code.isEmpty() && l.pageSize == parts.importedLayout.pageSize) {
                size.setAttribute(kW, QStringLiteral("code"), code);
            }
            XmlElement &margin = detail::wChild(sectPr, QStringLiteral("pgMar"));
            margin.attributes.clear();
            margin.setAttribute(kW, QStringLiteral("top"), detail::twips(l.margins.top()));
            margin.setAttribute(kW, QStringLiteral("right"), detail::twips(l.margins.right()));
            margin.setAttribute(kW, QStringLiteral("bottom"), detail::twips(l.margins.bottom()));
            margin.setAttribute(kW, QStringLiteral("left"), detail::twips(l.margins.left()));
            margin.setAttribute(kW, QStringLiteral("header"), detail::twips(l.headerDistance));
            margin.setAttribute(kW, QStringLiteral("footer"), detail::twips(l.footerDistance));
            margin.setAttribute(kW, QStringLiteral("gutter"), detail::twips(l.gutter));
        }
        detail::patchSectionNoteOptions(parts, sectPr, closingSection);
        detail::sortChildren(sectPr);
    };

    // The body.
    XmlElement document(kW, QStringLiteral("document"));
    XmlElement &bodyElement = document.appendElement(kW, QStringLiteral("body"));
    detail::prepareEndnotes(env, body);
    detail::BodyWriter bodyWriter(env, &body, kMain);
    bodyWriter.writeFrame(body.rootFrame(), bodyElement);
    bodyWriter.closeComments(bodyElement);
    XmlElementPtr sectPr = Ooxml::parseFragment(parts.sourceSectionXml);
    if (!sectPr) {
        sectPr = std::make_shared<XmlElement>(kW, QStringLiteral("sectPr"));
        XmlElement &cols = sectPr->appendElement(kW, QStringLiteral("cols"));
        cols.setAttribute(kW, QStringLiteral("space"), QStringLiteral("720"));
        sectPr->appendElement(kW, QStringLiteral("docGrid"))
            .setAttribute(kW, QStringLiteral("linePitch"), QStringLiteral("360"));
    }
    closingSection = true;
    env.sectionHook(*sectPr, kMain);
    bodyElement.appendElement(sectPr);

    // Footnotes and endnotes the body references (docx_writer_notes.cpp).
    const detail::NotesWritten notes = detail::writeNotes(env);

    // Comments (docx_comments.h): while none changed, the source's comment
    // parts are carried below byte for byte; otherwise they are written from
    // the model and the source's are left behind.
    const bool rewriteComments = commentsChanged(parts, body);
    if (rewriteComments) {
        QList<Comment> comments = parts.comments;
        for (Comment &c : comments) {
            if (c.paraId.isEmpty() && !c.isUnchanged()) {
                c.paraId = newParaId();
            }
        }
        XmlElementPtr written = commentsPart(comments, parts.sourceCommentIds, body);
        if (!written->children.isEmpty()) {
            // Kept comments may hold links or pictures relative to the source part.
            if (const auto rel = source && env.copier
                    ? source->firstOfType(env.sourceMain, QString::fromUtf16(Ooxml::RelType::Comments))
                    : std::nullopt) {
                env.copier->rewrite(*written, source->targetPart(env.sourceMain, *rel), QStringLiteral("word/comments.xml"));
            }
            markIgnorable(*written);
            env.writer.addXmlPart(QStringLiteral("word/comments.xml"), *written, type("comments+xml"));
            env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Comments),
                                       QStringLiteral("word/comments.xml"));
            XmlElementPtr extended = commentsExtendedPart(comments, parts.sourceCommentIds, body);
            if (!extended->children.isEmpty()) {
                env.writer.addXmlPart(QStringLiteral("word/commentsExtended.xml"), *extended,
                                      type("commentsExtended+xml"));
                env.writer.addRelationship(
                    kMain, QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/commentsExtended"),
                    QStringLiteral("word/commentsExtended.xml"));
            }
            env.usedStyles.insert(QStringLiteral("CommentText"));
            env.usedStyles.insert(QStringLiteral("CommentReference"));
        }
    }

    // Parts carried over verbatim from the source document.
    if (source && env.copier) {
        static const QStringList kCarry{
            QString::fromUtf16(Ooxml::RelType::Theme), QString::fromUtf16(Ooxml::RelType::FontTable),
            QString::fromUtf16(Ooxml::RelType::WebSettings), QString::fromUtf16(Ooxml::RelType::Comments),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml"),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/glossaryDocument"),
            QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/stylesWithEffects"),
            QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/people"),
            QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/commentsExtended"),
            QStringLiteral("http://schemas.microsoft.com/office/2016/09/relationships/commentsIds"),
            QStringLiteral("http://schemas.microsoft.com/office/2018/08/relationships/commentsExtensible")};
        static const QString kVba = QStringLiteral("http://schemas.microsoft.com/office/2006/relationships/vbaProject");
        const bool macros = options.kind == WriteOptions::MacroDocument || options.kind == WriteOptions::MacroTemplate;
        static const QStringList kCommentParts{
            QString::fromUtf16(Ooxml::RelType::Comments),
            QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/commentsExtended"),
            QStringLiteral("http://schemas.microsoft.com/office/2016/09/relationships/commentsIds"),
            QStringLiteral("http://schemas.microsoft.com/office/2018/08/relationships/commentsExtensible")};
        for (const Ooxml::Relationship &rel : source->relationships(env.sourceMain)) {
            if (rel.external || (!kCarry.contains(rel.type) && !(macros && rel.type == kVba))) {
                continue;
            }
            if (rewriteComments && kCommentParts.contains(rel.type)) {
                continue; // written from the model above
            }
            const QString target = source->targetPart(env.sourceMain, rel);
            if (target.isEmpty()) {
                continue;
            }
            env.writer.addRelationship(kMain, rel.type, env.copier->copyPart(target));
        }
        for (const Ooxml::Relationship &rel : source->relationships(QString())) {
            if (!rel.external && rel.type.endsWith(u"/custom-properties")) {
                env.writer.addRelationship(QString(), rel.type, env.copier->copyPart(source->targetPart(QString(), rel)));
            }
        }
    }

    // Styles and numbering, now that every reference is known.
    XmlElementPtr styles = detail::stylesPart(env, body.defaultFont(), sourceStyles);
    env.numbering->patchStyles(*styles);
    env.writer.addXmlPart(QStringLiteral("word/styles.xml"), *styles, type("styles+xml"));
    env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Styles), QStringLiteral("word/styles.xml"));
    if (XmlElementPtr numbering = env.numbering->result()) {
        markIgnorable(*numbering);
        env.writer.addXmlPart(QStringLiteral("word/numbering.xml"), *numbering, type("numbering+xml"));
        env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Numbering),
                                   QStringLiteral("word/numbering.xml"));
    }

    // Settings: the source's, kept consistent with what was written.
    detail::writeSettings(env, sourceSettings, sourceSettingsPart, notes);

    markIgnorable(document);
    static const char *kMainTypes[] = {
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.template.main+xml",
        "application/vnd.ms-word.document.macroEnabled.main+xml",
        "application/vnd.ms-word.template.macroEnabledTemplate.main+xml"};
    env.writer.addXmlPart(kMain, document, QString::fromLatin1(kMainTypes[options.kind]));
    env.writer.addRelationship(QString(), QString::fromUtf16(Ooxml::RelType::OfficeDocument), kMain);
    Ooxml::CoreProperties properties = parts.properties;
    properties.modified = QDateTime::currentDateTimeUtc();
    if (!properties.created.isValid()) {
        properties.created = properties.modified;
    }
    Ooxml::writeProperties(env.writer, properties, options.application, options.pageCount, options.wordCount);
    return env.writer.toZip(error);
}

bool writeDocx(const QString &path, const QTextDocument &body, const WordParts &parts, const WriteOptions &options,
               QString *error)
{
    const QByteArray bytes = docxBytes(body, parts, options, error);
    if (bytes.isEmpty()) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Docx
