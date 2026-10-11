// SPDX-License-Identifier: GPL-3.0-or-later
// Create a Video and Export Slides as Pictures (show_export.h).

#include "show_export.h"

#include "action_model.h"
#include "menu_tree.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_renderer.h"
#include "video_export.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

ShowExport::ShowExport(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    m_available = !ShowModel::ffmpegPath(&m_reason).isEmpty();
    Quick::ActionModel *model = host->actions();
    // AGENT-NOTE: without ffmpeg the command stays off and its own text says
    // why — in the menu, on the ribbon and in the panel export alike.
    m_video = model->add(QString::fromLatin1(ExportActionId::kVideo),
                         m_available ? tr("Create a &Video…") : tr("Create a &Video (needs ffmpeg)"), {}, QStringLiteral("film"));
    connect(m_video, &ActionEntry::triggered, this, &ShowExport::videoDialogRequested);
    m_pictures = model->add(QString::fromLatin1(ExportActionId::kPictures), tr("Export Slides as &Pictures…"), {},
                            QStringLiteral("images"));
    connect(m_pictures, &ActionEntry::triggered, this, &ShowExport::picturesDialogRequested);
    const auto sync = [this] {
        const bool slides = m_host->slideCount() > 0;
        m_video->setEnabled(m_available && slides && m_thread == nullptr);
        m_pictures->setEnabled(slides);
    };
    connect(host, &ShowHost::deckChanged, this, sync);
    connect(this, &ShowExport::exportingChanged, this, sync);
    sync();
}

ShowExport::~ShowExport()
{
    m_cancel = true;
    waitForVideo();
}

QList<MenuNode> ShowExport::menuItems() const
{
    return {MenuNode::action(QString::fromLatin1(ExportActionId::kVideo)), MenuNode::action(QString::fromLatin1(ExportActionId::kPictures))};
}

QUrl ShowExport::suggestedFile(const QString &extension) const
{
    const QString path = m_host->showDocument()->canonicalPath();
    const QString base = path.isEmpty() ? QDir::home().filePath(tr("Untitled")) : path.section(QLatin1Char('.'), 0, -2);
    return QUrl::fromLocalFile(base + QLatin1Char('.') + extension);
}

void ShowExport::setProgress(qreal progress)
{
    if (!qFuzzyCompare(m_progress + 1.0, progress + 1.0)) {
        m_progress = progress;
        emit progressChanged();
    }
}

bool ShowExport::startVideo(const QString &resolution, int secondsPerSlide, bool useTimings, const QUrl &file)
{
    if (!m_available) {
        m_host->showStatusMessage(m_reason, 8000);
        return false;
    }
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (m_thread != nullptr || path.isEmpty() || m_host->slideCount() == 0) {
        return false;
    }
    ShowModel::VideoSettings settings;
    settings.size = resolution == u"720p" ? QSize(1280, 720) : resolution == u"4k" ? QSize(3840, 2160) : QSize(1920, 1080);
    settings.secondsPerSlide = qBound(1, secondsPerSlide, 3600);
    settings.useTimings = useTimings;
    // The worker draws from its own copy: the deck may change meanwhile.
    const ShowModel::Presentation deck = *m_host->showDocument()->presentation();
    m_cancel = false;
    setProgress(0);
    auto result = std::make_shared<std::pair<bool, QString>>(false, QString());
    m_thread = QThread::create([this, deck, settings, path, result] {
        QString error;
        const bool ok = ShowModel::exportVideo(
            deck, settings, path,
            [this](int frame, int frames) {
                QMetaObject::invokeMethod(this, [this, frame, frames] { setProgress(qreal(frame) / qMax(1, frames)); },
                                          Qt::QueuedConnection);
            },
            &m_cancel, &error);
        *result = {ok, error};
    });
    connect(m_thread, &QThread::finished, this, [this, result, path] {
        m_thread->deleteLater();
        m_thread = nullptr;
        const QString message = result->first ? tr("Saved the video %1").arg(QFileInfo(path).fileName()) : result->second;
        m_host->showStatusMessage(message, 8000);
        emit exportingChanged();
        emit videoFinished(result->first, message);
    });
    m_thread->start(QThread::LowPriority);
    emit exportingChanged();
    return true;
}

void ShowExport::cancelVideo()
{
    m_cancel = true;
}

void ShowExport::waitForVideo()
{
    if (m_thread != nullptr) {
        m_thread->wait();
        QCoreApplication::sendPostedEvents(this); // the progress and the finish
    }
}

int ShowExport::exportPictures(const QString &format, bool all, int width, const QUrl &file)
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty() || deck->slides.isEmpty()) {
        return 0;
    }
    const bool jpeg = format == u"jpeg" || format == u"jpg";
    const QString extension = jpeg ? QStringLiteral("jpg") : QStringLiteral("png");
    const int pixelsWide = qBound(160, width, 7680);
    const QSize pixels(pixelsWide, qMax(1, qRound(pixelsWide * deck->slideSize.height() / deck->slideSize.width())));
    QList<std::pair<int, QString>> targets;
    const QFileInfo info(path);
    if (all) {
        // As PowerPoint: a folder named after the file, a picture per slide.
        const QString folder = info.absoluteDir().filePath(info.completeBaseName());
        if (!QDir().mkpath(folder)) {
            m_host->showStatusMessage(tr("Cannot make the folder %1").arg(QDir::toNativeSeparators(folder)), 8000);
            return 0;
        }
        for (qsizetype i = 0; i < deck->slides.size(); ++i) {
            targets.append({int(i), QDir(folder).filePath(QStringLiteral("Slide%1.%2").arg(i + 1).arg(extension))});
        }
    } else if (m_host->currentSlideRow() >= 0) {
        const QString target = info.suffix().isEmpty() ? path + QLatin1Char('.') + extension : path;
        targets.append({m_host->currentSlideRow(), target});
    }
    int written = 0;
    for (const auto &[row, target] : std::as_const(targets)) {
        const QImage image = ShowModel::renderSlideToImage(*deck, deck->slides.at(row), pixels);
        written += image.save(target, jpeg ? "JPG" : "PNG", jpeg ? 92 : -1) ? 1 : 0;
    }
    m_host->showStatusMessage(written == targets.size() ? tr("Saved %n picture(s)", nullptr, written)
                                                        : tr("Saved %1 of %2 pictures").arg(written).arg(targets.size()),
                              8000);
    return written;
}

} // namespace QindaOffice::Apps::Show
