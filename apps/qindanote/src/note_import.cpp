// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_import.h"

#include "note_rich_import.h"

#include <qindatk/rich_text_document.h>

#include "docx_model.h"
#include "docx_reader.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextTable>

namespace QindaOffice::Apps::Notes::Import {

namespace {

// The widest picture a note page's text column takes (page 760 − margins).
constexpr qreal kPictureWidth = 660;

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Apps::Notes::Import", text);
}

bool readAll(const QString &path, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    *bytes = file.readAll();
    return true;
}

ImportedPage page(const QString &title, const RichImport::Imported &imported)
{
    return {title, QJsonArray{RichImport::textBlock(imported)}};
}

// The text of `document` between two positions as its own document (the
// pictures keep their qwmedia: names; fromTextDocument resolves them).
QTextDocument *slice(const QTextDocument &document, int from, int to, QObject *owner)
{
    auto *part = new QTextDocument(owner);
    QTextCursor source(const_cast<QTextDocument *>(&document));
    source.setPosition(from);
    source.setPosition(to, QTextCursor::KeepAnchor);
    QTextCursor target(part);
    target.insertFragment(source.selection());
    // AGENT-NOTE: insertFragment() merges the fragment's first paragraph
    // into the empty one there WITHOUT its block format (a leading heading
    // would lose its level), so it is put back afterwards.
    QTextCursor(part->begin()).setBlockFormat(document.findBlock(from).blockFormat());
    return part;
}

bool importWord(const QString &path, QList<ImportedPage> *pages, QString *error)
{
    QTextDocument document;
    Docx::WordParts parts;
    if (!Docx::readDocx(path, &document, &parts, error)) {
        return false;
    }
    const QString name = parts.properties.title.trimmed().isEmpty() ? QFileInfo(path).completeBaseName()
                                                                    : parts.properties.title.trimmed();
    // Heading 1 paragraphs outside tables split the document into pages.
    QList<QTextBlock> splits;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() == 1 && QTextCursor(block).currentTable() == nullptr
            && !block.text().trimmed().isEmpty()) {
            splits.append(block);
        }
    }
    if (splits.size() < 2) {
        pages->append(page(name, RichImport::fromTextDocument(document, parts.media, kPictureWidth)));
        return true;
    }
    QObject owner;
    const int end = document.characterCount() - 1;
    if (splits.first().position() > 0) {
        const QTextDocument *before = slice(document, 0, splits.first().position() - 1, &owner);
        if (!before->toPlainText().trimmed().isEmpty()) {
            pages->append(page(name, RichImport::fromTextDocument(*before, parts.media, kPictureWidth)));
        }
    }
    for (qsizetype i = 0; i < splits.size(); ++i) {
        const QTextBlock heading = splits.at(i);
        const int from = heading.next().isValid() ? heading.next().position() : end;
        const int to = i + 1 < splits.size() ? splits.at(i + 1).position() - 1 : end;
        const QTextDocument *body = slice(document, from, qMax(from, to), &owner);
        pages->append(page(heading.text().trimmed(), RichImport::fromTextDocument(*body, parts.media, kPictureWidth)));
    }
    return true;
}

bool importMarkdown(const QString &path, QList<ImportedPage> *pages, QString *error)
{
    QByteArray bytes;
    if (!readAll(path, &bytes, error)) {
        return false;
    }
    QString markdown = QString::fromUtf8(bytes);
    QString title = QFileInfo(path).completeBaseName();
    // A leading "# Title" names the page instead of repeating on it.
    static const QRegularExpression heading(QStringLiteral("^\\s*#\\s+([^\\n]+)\\n?"));
    if (const QRegularExpressionMatch m = heading.match(markdown); m.hasMatch()) {
        title = m.captured(1).trimmed();
        markdown.remove(0, m.capturedLength());
    }
    pages->append(page(title, RichImport::fromMarkdown(markdown, kPictureWidth)));
    return true;
}

bool importHtml(const QString &path, QList<ImportedPage> *pages, QString *error)
{
    QByteArray bytes;
    if (!readAll(path, &bytes, error)) {
        return false;
    }
    QStringDecoder decoder(QStringConverter::encodingForHtml(bytes).value_or(QStringConverter::Utf8));
    const QString html = decoder(bytes);
    QString title = QFileInfo(path).completeBaseName();
    static const QRegularExpression titleTag(QStringLiteral("<title[^>]*>(.*?)</title>"),
                                             QRegularExpression::CaseInsensitiveOption
                                                 | QRegularExpression::DotMatchesEverythingOption);
    if (const QRegularExpressionMatch m = titleTag.match(html); m.hasMatch()) {
        QTextDocument plain;
        plain.setHtml(m.captured(1));
        if (!plain.toPlainText().trimmed().isEmpty()) {
            title = plain.toPlainText().simplified();
        }
    }
    pages->append(page(title, RichImport::fromHtml(html, kPictureWidth)));
    return true;
}

} // namespace

Kind kindOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("docx") || suffix == QLatin1String("docm") || suffix == QLatin1String("dotx")
        || suffix == QLatin1String("dotm")) {
        return Kind::Word;
    }
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        return Kind::Markdown;
    }
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm") || suffix == QLatin1String("xhtml")) {
        return Kind::Html;
    }
    if (suffix == QLatin1String("one") || suffix == QLatin1String("onepkg") || suffix == QLatin1String("onetoc2")) {
        return Kind::OneNote;
    }
    return Kind::Unknown;
}

QStringList nameFilters()
{
    return {tr("Documents (*.docx *.docm *.dotx *.dotm *.md *.markdown *.html *.htm)"),
            tr("Word documents (*.docx *.docm *.dotx *.dotm)"), tr("Markdown (*.md *.markdown)"),
            tr("Web pages (*.html *.htm *.xhtml)"), tr("OneNote sections (*.one *.onepkg *.onetoc2)")};
}

QString oneNoteExplanation()
{
    return tr("OneNote section files (.one) can't be read here. Their format (MS-ONESTORE) has no open "
              "implementation QindaNote can use. In OneNote, export the pages to a Word document (File ▸ "
              "Export ▸ Word Document) or to PDF, then import the Word document here.");
}

bool importFile(const QString &path, QList<ImportedPage> *pages, QString *error)
{
    switch (kindOf(path)) {
    case Kind::Word:
        return importWord(path, pages, error);
    case Kind::Markdown:
        return importMarkdown(path, pages, error);
    case Kind::Html:
        return importHtml(path, pages, error);
    case Kind::OneNote:
        if (error != nullptr) {
            *error = oneNoteExplanation();
        }
        return false;
    case Kind::Unknown:
        break;
    }
    if (error != nullptr) {
        *error = tr("QindaNote imports Word documents, Markdown and web pages.");
    }
    return false;
}

} // namespace QindaOffice::Apps::Notes::Import
