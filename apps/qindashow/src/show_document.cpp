// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_document.h"

#include <QUuid>

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "libre_office_converter.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "recovery_journal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUndoStack>

namespace QindaOffice::Apps::Show {

namespace {

// Conversion goes through Core::convertBlocking (office_core); see the note
// in format_io.cpp. Intermediates inside the private converter directory are
// cleaned up there, a user file never is.
QString convertAndWait(const QString &input, const QString &targetExt, QString *error)
{
    return Core::convertBlocking(input, targetExt, QStringLiteral(".qindashow-lo"), error);
}

QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// The PowerPoint Open XML family QindaShow reads natively (a template or
// show is a presentation with another main content type).
bool isOpenXml(const QString &suffix)
{
    return suffix == u"pptx" || suffix == u"pptm" || suffix == u"potx" || suffix == u"ppsx";
}

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    // QSaveFile: a failed or truncated write (ENOSPC, quota) must not be
    // reported as a successful save with the dirty flag cleared.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QObject::tr("Could not open %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace

ShowDocument::ShowDocument(QObject *parent)
    : Core::Document(parent)
{
    // A pristine deck is one title slide so the canvas is never empty — and
    // it carries the title placeholder, so the first thing the user sees is
    // something to type into rather than an empty white page.
    ShowModel::Slide &slide = m_presentation.addSlide(tr("Slide 1"));
    slide.layout = QStringLiteral("title");
    ShowModel::Shape title;
    title.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    title.kind = ShowModel::ShapeKind::TextBox;
    title.rect = QRectF(m_presentation.slideSize.width() * 0.1,
                        m_presentation.slideSize.height() * 0.36,
                        m_presentation.slideSize.width() * 0.8,
                        m_presentation.slideSize.height() * 0.2);
    title.strokeWidth = 0;
    ShowModel::TextRun run;
    run.text = tr("Click to add a title");
    run.pointSize = 40;
    run.color = QColor(120, 120, 120);
    ShowModel::TextParagraph paragraph;
    paragraph.alignment = int(Qt::AlignHCenter);
    paragraph.runs.append(run);
    title.paragraphs.append(paragraph);
    slide.shapes.append(title);
}

bool ShowDocument::load(const QString &path, QString *error)
{
    const QString suffix = suffixOf(path);
    ShowModel::Presentation incoming;
    QString readError;
    if (suffix == QLatin1String("fodp")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = tr("Could not open %1: %2").arg(path, file.errorString());
            }
            return false;
        }
        incoming = ShowModel::readFodp(file.readAll(), &readError);
    } else if (isOpenXml(suffix)) {
        // D-070: PowerPoint natively — no LibreOffice in the way.
        incoming = ShowModel::readPptxFile(path, &readError);
    } else {
        // .ppt, .odp and the rest: LibreOffice makes a .pptx in a private
        // directory, the native reader reads that.
        const QString converted = convertAndWait(path, QStringLiteral("pptx"), error);
        if (converted.isEmpty()) {
            return false;
        }
        incoming = ShowModel::readPptxFile(converted, &readError);
        QFile::remove(converted);
    }
    if (!readError.isEmpty()) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }
    m_presentation = incoming;
    // AGENT-GUARD: the undo stack must be cleared before the presentation is
    // ever replaced on load. Shape commands hold raw Slide* into the deck's
    // slide list; a stale command would dereference freed/detached buffers.
    // (Reachable via File ▸ New from Template on an already-edited window.)
    if (undoStack() != nullptr) {
        undoStack()->clear();
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit loaded();
    return true;
}

bool ShowDocument::save(const QString &path, QString *error)
{
    const QString suffix = suffixOf(path);
    QString writeError;
    if (suffix == QLatin1String("fodp")) {
        const QByteArray bytes = ShowModel::writeFodp(m_presentation, &writeError);
        if (bytes.isEmpty()) {
            if (error != nullptr) {
                *error = writeError;
            }
            return false;
        }
        if (!writeBytes(path, bytes, error)) {
            return false;
        }
    } else if (isOpenXml(suffix) || suffix.isEmpty()) {
        const QByteArray bytes = ShowModel::writePptx(m_presentation, &writeError);
        if (bytes.isEmpty()) {
            if (error != nullptr) {
                *error = writeError;
            }
            return false;
        }
        if (!writeBytes(path, bytes, error)) {
            return false;
        }
    } else {
        // .odp / .ppt: the native .pptx, converted by LibreOffice. The
        // intermediate lives in a QTemporaryDir; the target path is the
        // user's file and is overwritten, never deleted as an "intermediate".
        QTemporaryDir temp;
        if (!temp.isValid()) {
            if (error != nullptr) {
                *error = tr("Could not stage the conversion.");
            }
            return false;
        }
        const QString intermediate = temp.path() + QStringLiteral("/deck.pptx");
        if (!ShowModel::writePptxFile(m_presentation, intermediate, &writeError)) {
            if (error != nullptr) {
                *error = writeError.isEmpty() ? tr("Could not stage the conversion.") : writeError;
            }
            return false;
        }
        const QString converted = convertAndWait(intermediate, suffix, error);
        if (converted.isEmpty()) {
            return false;
        }
        // AGENT-GUARD: copy through QSaveFile at the destination — the
        // converted file lives in the QTemporaryDir (often tmpfs) while the
        // target is on the user's filesystem, so QFile::rename fails with
        // EXDEV. The old remove-then-rename destroyed the user's existing
        // file whenever that rename failed.
        QFile convertedFile(converted);
        if (!convertedFile.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = convertedFile.errorString();
            }
            return false;
        }
        const QByteArray payload = convertedFile.readAll();
        if (payload.isEmpty() || !writeBytes(path, payload, error)) {
            if (error != nullptr && error->isEmpty()) {
                *error = tr("Could not move the converted file into place.");
            }
            return false;
        }
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList ShowDocument::mimeTypes() const
{
    // Mirrors the .desktop MimeType list and ShowHost's open filters.
    return {QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"),
            QStringLiteral("application/vnd.ms-powerpoint.presentation.macroEnabled.12"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.template"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.slideshow"),
            QStringLiteral("application/vnd.ms-powerpoint"),
            QStringLiteral("application/vnd.oasis.opendocument.presentation"),
            QStringLiteral("application/vnd.oasis.opendocument.presentation-flat-xml")};
}

QByteArray ShowDocument::recoverySnapshot() const
{
    // AGENT-NOTE (D-070): the journal holds a .pptx, not a .fodp — the
    // pptx mapping round-trips the whole model (tables, charts, preserved
    // Office content, masters and layouts), .fodp only its ODF subset.
    QString error;
    return ShowModel::writePptx(m_presentation, &error);
}

qint64 ShowDocument::recoveryPayloadLimit() const
{
    // AGENT-CONTRACT (document.h): one of the journal's two bounds. A deck's
    // snapshot is a packaged file with its pictures, as QindaWrite's .docx.
    return Core::RecoveryJournal::kMaxBinaryPayload;
}

bool ShowDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    QString readError;
    // Journals written before M62 hold .fodp.
    m_presentation = ShowModel::looksLikeZip(snapshot) ? ShowModel::readPptxData(snapshot, &readError)
                                                       : ShowModel::readFodp(snapshot, &readError);
    if (!readError.isEmpty()) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }
    // Same raw-Slide* hazard as load(): drop commands that reference the
    // pre-recovery deck.
    if (undoStack() != nullptr) {
        undoStack()->clear();
    }
    setDirty(true);
    emit loaded();
    return true;
}

} // namespace QindaOffice::Apps::Show
