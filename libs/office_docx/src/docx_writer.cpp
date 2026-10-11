// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_writer.h"
#include "docx_comments.h"

#include "docx_properties.h"
#include "docx_sections.h"
#include "docx_styles.h"
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

XmlElementPtr sectionMarkup(const WriteEnv &env, int index)
{
    const SectionRef &section = env.sections.at(index);
    const WordParts &parts = *env.parts;
    QString markup;
    SectionSettings was;
    if (section.closing()) {
        markup = parts.sourceSectionXml;
        was = importedClosingSettings(parts);
    } else {
        const QTextBlockFormat format = section.end.blockFormat();
        markup = format.stringProperty(kSectionXml);
        const QVariantMap snapshot = format.property(kSectionSnapshot).toMap();
        was = snapshot.isEmpty() ? sectionSettings(format) : SectionSettings::fromMap(snapshot);
    }
    XmlElementPtr sectPr = Ooxml::parseFragment(markup);
    if (!sectPr) {
        return newSectionElement(section.settings);
    }
    if (!(section.settings == was)) {
        writeSectionSettings(*sectPr, section.settings, was);
    }
    return sectPr;
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
    // Sections (M88, D-096): each section's w:sectPr is its own markup,
    // patched where its settings changed (sectionMarkup), with its own
    // header and footer references. A section whose stories are as read
    // keeps its references (the copier carries their parts byte for byte);
    // otherwise they are written again from its stories, a slot it does not
    // define left out (Word's Link to Previous).
    env.body = &body;
    env.sections = documentSections(body, parts);
    for (qsizetype i = 0; i + 1 < env.sections.size(); ++i) {
        env.sectionEnds.insert(env.sections.at(i).end.blockNumber(), int(i));
    }
    static const char *kSlots[] = {"default", "first", "even"};
    auto storiesChanged = [&](int index) {
        if (!env.copier) {
            return true;
        }
        const SectionRef &section = env.sections.at(index);
        const XmlElementPtr markup = Ooxml::parseFragment(
            section.closing() ? parts.sourceSectionXml : section.end.blockFormat().stringProperty(kSectionXml));
        if (!markup) {
            return true;
        }
        QSet<QString> referenced;
        for (const XmlElement *ref : markup->elements()) {
            if (ref->name == u"headerReference" || ref->name == u"footerReference") {
                referenced.insert(ref->name + wAttr(ref, u"type", QStringLiteral("default")));
            }
        }
        for (int s = 0; s < kHeaderFooterSlots; ++s) {
            for (const bool footer : {false, true}) {
                const HeaderFooter *story = sectionStory(parts, section.key, footer, HeaderFooterSlot(s));
                const bool defines = story != nullptr && story->defines();
                const QString name = (footer ? QStringLiteral("footerReference") : QStringLiteral("headerReference"))
                    + QString::fromLatin1(kSlots[s]);
                if (defines != referenced.contains(name)
                    || (defines && (story->document->isModified() || story->sourcePart.isEmpty()))) {
                    return true;
                }
            }
        }
        return false;
    };
    // The part a story is written as (or copied from the source).
    auto storyPart = [&](const HeaderFooter &story, bool footer) {
        if (!story.document->isModified() && !story.sourcePart.isEmpty() && env.copier) {
            return env.copier->copyPart(story.sourcePart);
        }
        const QString partName = env.writer.uniquePartName(footer ? QStringLiteral("word/footer")
                                                                  : QStringLiteral("word/header"),
                                                           QStringLiteral(".xml"));
        XmlElement root(kW, footer ? QStringLiteral("ftr") : QStringLiteral("hdr"));
        detail::BodyWriter(env, story.document.get(), partName).writeFrame(story.document->rootFrame(), root);
        markIgnorable(root);
        env.writer.addXmlPart(partName, root, type(footer ? "footer+xml" : "header+xml"));
        return partName;
    };
    env.sectionHook = [&](XmlElement &sectPr, const QString &part, int index) {
        const SectionRef &section = env.sections.at(index);
        const bool rebuild = storiesChanged(index);
        if (rebuild) {
            sectPr.removeChildren(kW, u"headerReference");
            sectPr.removeChildren(kW, u"footerReference");
        }
        if (env.copier) {
            env.copier->rewrite(sectPr, env.sourceMain, part);
        }
        if (rebuild) {
            for (const bool footer : {false, true}) {
                for (int s = 0; s < kHeaderFooterSlots; ++s) {
                    const HeaderFooter *story = sectionStory(parts, section.key, footer, HeaderFooterSlot(s));
                    if (story == nullptr || !story->defines()) {
                        continue;
                    }
                    const QString rel = env.writer.addRelationship(
                        kMain, QString::fromUtf16(footer ? Ooxml::RelType::Footer : Ooxml::RelType::Header),
                        storyPart(*story, footer));
                    XmlElement &e = sectPr.appendElement(kW, footer ? QStringLiteral("footerReference")
                                                                    : QStringLiteral("headerReference"));
                    e.setAttribute(kW, QStringLiteral("type"), QString::fromLatin1(kSlots[s]));
                    e.setAttribute(kR, QStringLiteral("id"), rel);
                }
            }
        }
        detail::patchSectionNoteOptions(parts, sectPr, section.closing());
        detail::sortChildren(sectPr);
    };

    // The body.
    XmlElement document(kW, QStringLiteral("document"));
    XmlElement &bodyElement = document.appendElement(kW, QStringLiteral("body"));
    detail::prepareEndnotes(env, body);
    detail::BodyWriter bodyWriter(env, &body, kMain);
    bodyWriter.writeFrame(body.rootFrame(), bodyElement);
    bodyWriter.closeComments(bodyElement);
    const int closingIndex = int(env.sections.size()) - 1;
    XmlElementPtr sectPr = detail::sectionMarkup(env, closingIndex);
    env.sectionHook(*sectPr, kMain, closingIndex);
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
