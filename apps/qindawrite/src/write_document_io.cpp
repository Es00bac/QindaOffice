// SPDX-License-Identifier: GPL-3.0-or-later
// WriteDocument open/save/recovery (D-068): Word packages natively; legacy
// and OpenDocument formats through LibreOffice to and from .docx; HTML,
// Markdown and plain text as before.

#include "write_document.h"

#include "docx_reader.h"
#include "docx_text.h"
#include "docx_writer.h"
#include "format_io.h"
#include "libre_office_converter.h"
#include "ooxml_package.h"
#include "recovery_journal.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextBlock>

namespace QindaOffice::Apps::Write {

namespace {

// Recovery payloads: a .docx after this tag; older journals hold HTML.
const QByteArray kRecoveryTag = QByteArrayLiteral("QINDAWRITE-DOCX/1\n");

QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

bool isWordSuffix(const QString &ext)
{
    return ext == u"docx" || ext == u"docm" || ext == u"dotx" || ext == u"dotm";
}

// Formats LibreOffice converts to and from .docx for us.
bool isConvertedSuffix(const QString &ext)
{
    return ext == u"doc" || ext == u"dot" || ext == u"rtf" || ext == u"odt" || ext == u"ott" || ext == u"wpd"
        || ext == u"fodt" || ext == u"wps";
}

int wordCount(const QTextDocument &text)
{
    int words = 0;
    bool inWord = false;
    for (const QChar c : text.toPlainText()) {
        const bool letter = c.isLetterOrNumber();
        words += letter && !inWord ? 1 : 0;
        inWord = letter;
    }
    return words;
}

Docx::WriteOptions optionsFor(const QTextDocument &text, const QString &ext)
{
    Docx::WriteOptions options;
    options.wordCount = wordCount(text);
    options.pageCount = text.pageCount();
    options.kind = ext == u"dotx" ? Docx::WriteOptions::Template
        : ext == u"docm"          ? Docx::WriteOptions::MacroDocument
        : ext == u"dotm"          ? Docx::WriteOptions::MacroTemplate
                                  : Docx::WriteOptions::Document;
    return options;
}

} // namespace

bool WriteDocument::load(const QString &path, QString *error)
{
    const QString ext = suffixOf(path);
    bool ok = false;
    if (isWordSuffix(ext)) {
        ok = loadWordPackage(path, error);
    } else if (isConvertedSuffix(ext)) {
        ok = loadConverted(path, error);
    } else {
        ok = loadMarkup(path, error);
    }
    if (!ok) {
        return false;
    }
    finishLoad(path);
    return true;
}

bool WriteDocument::loadWordPackage(const QString &path, QString *error)
{
    Docx::WordParts parts;
    if (!Docx::readDocx(path, m_text.get(), &parts, error)) {
        return false;
    }
    adoptParts(std::move(parts));
    return true;
}

bool WriteDocument::loadConverted(const QString &path, QString *error)
{
    // AGENT-NOTE: LibreOffice writes beside its input, so the input is
    // copied into a private directory first — opening from a read-only
    // folder (a CD, a shared drive) must work, and nothing may appear next
    // to the user's file.
    QTemporaryDir scratch;
    const QString input = scratch.filePath(QFileInfo(path).fileName());
    if (!scratch.isValid() || !QFile::copy(path, input)) {
        if (error != nullptr) {
            *error = tr("cannot read %1").arg(path);
        }
        return false;
    }
    const QString docx = Core::convertBlocking(input, QStringLiteral("docx"), QStringLiteral("lo"), error);
    if (docx.isEmpty()) {
        return false;
    }
    if (!loadWordPackage(docx, error)) {
        return false;
    }
    // The converted package is not the user's file: nothing in it is worth
    // carrying back on save except what the model already holds.
    m_parts.properties.title = m_parts.properties.title.isEmpty() ? QFileInfo(path).completeBaseName()
                                                                  : m_parts.properties.title;
    return true;
}

bool WriteDocument::loadMarkup(const QString &path, QString *error)
{
    if (!loadDocument(path, m_text.get(), error)) {
        return false;
    }
    Docx::WordParts parts;
    // Page setup + header/footer travel in HTML metas (page_setup_io.h);
    // QTextDocument::setHtml drops them, so parse the raw source.
    const QString ext = suffixOf(path);
    if (ext == u"html" || ext == u"htm") {
        QFile raw(path);
        if (raw.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const PageSetup setup = pageSetupFromHtml(QString::fromUtf8(raw.readAll()));
            if (setup.hasCustomPageSize) {
                parts.layout = setup.layout;
            }
            parts.footnotes = setup.footnotes;
            QFont font = m_text->defaultFont();
            font.setPointSizeF(9);
            if (!setup.header.isEmpty()) {
                parts.header(Docx::HeaderFooterSlot::Default).document = Docx::headerFooterFromPattern(setup.header, font);
            }
            if (!setup.footer.isEmpty()) {
                parts.footer(Docx::HeaderFooterSlot::Default).document = Docx::headerFooterFromPattern(setup.footer, font);
            }
        }
    } else {
        parts.layout = m_parts.layout; // plain text keeps the current paper
    }
    adoptParts(std::move(parts));
    return true;
}

void WriteDocument::finishLoad(const QString &path)
{
    setCanonicalPath(path);
    refreshByteRevision();
    for (auto *list : {&m_parts.headers, &m_parts.footers}) {
        for (Docx::HeaderFooter &hf : *list) {
            if (hf.document) {
                hf.document->setModified(false);
            }
        }
    }
    // Both flags: ours drives the title and close consent, QTextDocument's
    // drives the signal that sets ours.
    m_text->clearUndoRedoStacks();
    m_text->setModified(false);
    setDirty(false);
    emit contentChanged();
}

bool WriteDocument::save(const QString &path, QString *error)
{
    const QString ext = suffixOf(path);
    bool ok = false;
    if (isWordSuffix(ext)) {
        ok = saveWordPackage(path, error);
    } else if (isConvertedSuffix(ext)) {
        ok = saveConverted(path, error);
    } else if (ext == u"html" || ext == u"htm") {
        ok = saveHtml(path, error);
    } else {
        ok = saveDocument(path, m_text.get(), error);
    }
    if (!ok) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    m_text->setModified(false);
    setDirty(false);
    return true;
}

bool WriteDocument::saveWordPackage(const QString &path, QString *error) const
{
    return Docx::writeDocx(path, *m_text, m_parts, optionsFor(*m_text, suffixOf(path)), error);
}

bool WriteDocument::saveConverted(const QString &path, QString *error) const
{
    QTemporaryDir scratch;
    const QString docx = scratch.filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".docx"));
    if (!scratch.isValid() || !Docx::writeDocx(docx, *m_text, m_parts, optionsFor(*m_text, QStringLiteral("docx")), error)) {
        return false;
    }
    const QString converted = Core::convertBlocking(docx, suffixOf(path), QStringLiteral("lo"), error);
    if (converted.isEmpty()) {
        return false;
    }
    QFile in(converted);
    QSaveFile out(path);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = out.errorString();
        }
        return false;
    }
    const QByteArray bytes = in.readAll();
    if (bytes.isEmpty() || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error != nullptr) {
            *error = tr("converting to %1 produced no usable output").arg(suffixOf(path));
        }
        return false;
    }
    return true;
}

bool WriteDocument::saveHtml(const QString &path, QString *error) const
{
    PageSetup setup;
    setup.layout = m_parts.layout;
    setup.header = headerText();
    setup.footer = footerText();
    setup.footnotes = m_parts.footnotes;
    const QString html = injectPageSetupMeta(Docx::htmlWithEmbeddedPictures(*m_text, m_parts.media), setup);
    QSaveFile file(path);
    const QByteArray bytes = html.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

QStringList WriteDocument::mimeTypes() const
{
    return {QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.template"),
            QStringLiteral("application/vnd.ms-word.document.macroEnabled.12"),
            QStringLiteral("application/vnd.ms-word.template.macroEnabled.12"),
            QStringLiteral("application/msword"),
            QStringLiteral("application/vnd.oasis.opendocument.text"),
            QStringLiteral("application/rtf"),
            QStringLiteral("text/html"),
            QStringLiteral("text/markdown"),
            QStringLiteral("text/plain")};
}

qint64 WriteDocument::recoveryPayloadLimit() const
{
    return Core::RecoveryJournal::kMaxBinaryPayload;
}

QByteArray WriteDocument::recoverySnapshot() const
{
    // The whole document — layout, rich headers, footnotes, pictures and
    // the carried source parts — not just the body's HTML.
    const QByteArray docx = Docx::docxBytes(*m_text, m_parts);
    return docx.isEmpty() ? m_text->toHtml().toUtf8() : kRecoveryTag + docx;
}

bool WriteDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    if (snapshot.startsWith(kRecoveryTag)) {
        Docx::WordParts parts;
        const auto package = Ooxml::Package::fromData(snapshot.mid(kRecoveryTag.size()), error);
        if (!package || !Docx::readDocx(package, m_text.get(), &parts, error)) {
            return false;
        }
        adoptParts(std::move(parts));
    } else {
        m_text->setHtml(QString::fromUtf8(snapshot)); // a journal written before D-068
    }
    m_text->setModified(true);
    setDirty(true);
    emit contentChanged();
    return true;
}

} // namespace QindaOffice::Apps::Write
