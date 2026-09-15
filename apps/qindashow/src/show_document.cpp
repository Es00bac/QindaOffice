// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_document.h"

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "libre_office_converter.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

namespace QindaOffice::Apps::Show {

namespace {

// Blocks on the async converter; returns the converted output path or {}
// with `error` set. Intermediates land in a per-document converter
// directory next to the input; an input that lives inside that directory
// is removed after conversion (it is ours), a user file is never touched.
QString convertAndWait(const QString &input, const QString &targetExt, QString *error)
{
    Core::LibreOfficeConverter converter;
    const QString outDir =
        QFileInfo(input).absolutePath() + QStringLiteral("/.qindashow-lo");
    QDir().mkpath(outDir);
    QEventLoop loop;
    QString result;
    QString failure;
    QObject::connect(&converter, &Core::LibreOfficeConverter::jobFinished, &loop,
                     [&loop, &result](int, const QString &output) {
                         result = output;
                         loop.quit();
                     });
    QObject::connect(&converter, &Core::LibreOfficeConverter::jobFailed, &loop,
                     [&loop, &failure](int, const QString &message) {
                         failure = message;
                         loop.quit();
                     });
    const int id = converter.convert(input, targetExt, outDir, error);
    if (id == 0) {
        return {};
    }
    QTimer::singleShot(130000, &loop, [&loop] { loop.quit(); });
    loop.exec();
    if (result.isEmpty() && failure.isEmpty() && error != nullptr) {
        *error = QStringLiteral("conversion timed out");
    } else if (!failure.isEmpty() && error != nullptr) {
        *error = failure;
    }
    if (input.startsWith(outDir)) {
        QFile::remove(input);
    }
    return result;
}

QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

} // namespace

ShowDocument::ShowDocument(QObject *parent)
    : Core::Document(parent)
{
    // A pristine deck is one title slide so the canvas is never empty.
    ShowModel::Slide &slide = m_presentation.addSlide(tr("Slide 1"));
    slide.layout = QStringLiteral("title");
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
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = tr("Could not open %1: %2").arg(path, file.errorString());
            }
            return false;
        }
        file.write(bytes);
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
        QFile::remove(path);
        if (!QFile::rename(converted, path)) {
            if (error != nullptr) {
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
    setDirty(true);
    emit loaded();
    return true;
}

} // namespace QindaOffice::Apps::Show
