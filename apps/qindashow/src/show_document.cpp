// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_document.h"

#include <QUuid>

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "libre_office_converter.h"

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
    if (suffix == QLatin1String("fodp")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = tr("Could not open %1: %2").arg(path, file.errorString());
            }
            return false;
        }
        incoming = ShowModel::readFodp(file.readAll(), error);
        if (error != nullptr && !error->isEmpty()) {
            return false;
        }
    } else {
        const QString fodp = convertAndWait(path, QStringLiteral("fodp"), error);
        if (fodp.isEmpty()) {
            return false;
        }
        QFile file(fodp);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = tr("Could not read converted file.");
            }
            return false;
        }
        incoming = ShowModel::readFodp(file.readAll(), error);
        if (error != nullptr && !error->isEmpty()) {
            return false;
        }
        QFile::remove(fodp);
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
    if (suffix == QLatin1String("fodp")) {
        QString writeError;
        const QByteArray bytes = ShowModel::writeFodp(m_presentation, &writeError);
        if (bytes.isEmpty()) {
            if (error != nullptr) {
                *error = writeError;
            }
            return false;
        }
        // QSaveFile: a failed or truncated write (ENOSPC, quota) must not be
        // reported as a successful save with the dirty flag cleared.
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = tr("Could not open %1: %2").arg(path, file.errorString());
            }
            return false;
        }
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
    } else {
        // Write the native intermediate, convert, move the result into
        // place. The intermediate is created inside the converter
        // directory so convertAndWait removes it; the target path is the
        // user's file and is overwritten, never deleted as an
        // "intermediate".
        QTemporaryDir temp;
        if (!temp.isValid()) {
            if (error != nullptr) {
                *error = tr("Could not stage the conversion.");
            }
            return false;
        }
        const QString intermediate = temp.path() + QStringLiteral("/deck.fodp");
        QString writeError;
        const QByteArray bytes = ShowModel::writeFodp(m_presentation, &writeError);
        QFile staging(intermediate);
        if (bytes.isEmpty() || !staging.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = bytes.isEmpty() ? writeError : tr("Could not stage the conversion.");
            }
            return false;
        }
        staging.write(bytes);
        staging.close();
        const QString converted =
            convertAndWait(intermediate, suffix.isEmpty() ? QStringLiteral("odp") : suffix, error);
        if (converted.isEmpty()) {
            return false;
        }
        // AGENT-GUARD: copy through QSaveFile at the destination — the
        // converted file lives in the QTemporaryDir (often tmpfs) while the
        // target is on the user's filesystem, so QFile::rename fails with
        // EXDEV. The old remove-then-rename destroyed the user's existing
        // file whenever that rename failed.
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = out.errorString();
            }
            return false;
        }
        QFile convertedFile(converted);
        if (!convertedFile.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = convertedFile.errorString();
            }
            out.cancelWriting();
            return false;
        }
        const QByteArray payload = convertedFile.readAll();
        if (payload.isEmpty() || out.write(payload) != payload.size() || !out.commit()) {
            if (error != nullptr) {
                *error = tr("Could not move the converted file into place.");
            }
            out.cancelWriting();
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
    return {QStringLiteral("application/vnd.oasis.opendocument.presentation"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation")};
}

QByteArray ShowDocument::recoverySnapshot() const
{
    QString error;
    return ShowModel::writeFodp(m_presentation, &error);
}

bool ShowDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    m_presentation = ShowModel::readFodp(snapshot, error);
    if (error != nullptr && !error->isEmpty()) {
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
