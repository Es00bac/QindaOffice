// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Export ▸ Create a Video and Export ▸ Slides as Pictures in QindaShow
// (M91, D-099).
//  - Create a Video… (VideoExportDialog.qml): 720p, 1080p or 4K, seconds
//    per slide for slides without a timing, the rehearsed timings or not;
//    show_model's exportVideo (video_export.h) runs on a worker thread over
//    a copy of the deck — the frames the show draws, piped to the system's
//    ffmpeg — with progress and Cancel; a cancelled or failed export leaves
//    no file. Without ffmpeg (/usr/bin/ffmpeg, from Portage) the command is
//    off and says why in its own text.
//  - Export Slides as Pictures… (ExportPicturesDialog.qml): PNG or JPEG,
//    every slide (a folder named after the file, Slide1.png …, as
//    PowerPoint makes it) or the current one, at a chosen width.
//
// AGENT-CONTRACT: the command ids (ExportActionId) are the dbusmenu
// activation identity (AGENTS.md rule 5).

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>

class QThread;

namespace QindaOffice::Quick {
struct MenuNode;
class ActionEntry;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace ExportActionId {
inline const char kVideo[] = "showExportVideoAction";
inline const char kPictures[] = "showExportPicturesAction";
} // namespace ExportActionId

class ShowExport final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    Q_PROPERTY(bool videoAvailable READ videoAvailable CONSTANT)
    Q_PROPERTY(QString videoUnavailableReason READ videoUnavailableReason CONSTANT)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    // 0..1 of the frames written.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
public:
    explicit ShowExport(ShowHost *host);
    ~ShowExport() override;

    [[nodiscard]] bool videoAvailable() const { return m_available; }
    [[nodiscard]] QString videoUnavailableReason() const { return m_reason; }
    [[nodiscard]] bool exporting() const { return m_thread != nullptr; }
    [[nodiscard]] qreal progress() const { return m_progress; }
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // Create a Video's answer: "720p", "1080p" or "4k"; false when it could
    // not start (no ffmpeg, no slides, one running).
    Q_INVOKABLE bool startVideo(const QString &resolution, int secondsPerSlide, bool useTimings, const QUrl &file);
    Q_INVOKABLE void cancelVideo();
    // Export Slides as Pictures' answer: "png" or "jpeg", every slide or the
    // current one, the width in pixels; how many pictures were written.
    Q_INVOKABLE int exportPictures(const QString &format, bool all, int width, const QUrl &file);
    // The deck's path with `extension` ("Untitled.<extension>" when unsaved).
    Q_INVOKABLE QUrl suggestedFile(const QString &extension) const;
    // Blocks until a running export ends (tests, and the window closing).
    void waitForVideo();

signals:
    void videoDialogRequested();
    void picturesDialogRequested();
    void exportingChanged();
    void progressChanged();
    void videoFinished(bool ok, const QString &message);

private:
    void setProgress(qreal progress);

    ShowHost *m_host;
    bool m_available = false;
    QString m_reason;
    QThread *m_thread = nullptr;
    std::atomic_bool m_cancel{false};
    qreal m_progress = 0;
    Quick::ActionEntry *m_video = nullptr;
    Quick::ActionEntry *m_pictures = nullptr;
};

} // namespace QindaOffice::Apps::Show
