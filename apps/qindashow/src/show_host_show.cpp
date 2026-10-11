// SPDX-License-Identifier: GPL-3.0-or-later
// ShowHost's slide show (M91, D-099): the Slide Show tab's controllers,
// starting a show (From Beginning, From Current Slide, a custom show, a
// rehearsal) and ending it (a rehearsal's timings offered for keeping),
// and what an action asks of the world outside the show.
//
// AGENT-GUARD (safety, show_action.h): nothing here runs a program. Web
// and e-mail addresses go to the desktop's URL handler; a file opens only
// after the presenter agrees (fileConsentRequested → openFileConfirmed),
// and never when it is a program or a script.

#include "show_host.h"

#include "show_action_settings.h"
#include "show_document.h"
#include "show_effect_options.h"
#include "show_export.h"
#include "show_media_actions.h"
#include "show_rehearsal.h"
#include "show_slide_show.h"
#include "standard_actions.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>

namespace QindaOffice::Apps::Show {

namespace {

// A file the desktop would RUN rather than open: refused.
bool isProgram(const QFileInfo &info)
{
    static const QStringList kRunnable{QStringLiteral("desktop"), QStringLiteral("sh"),   QStringLiteral("bash"),
                                       QStringLiteral("zsh"),     QStringLiteral("csh"),  QStringLiteral("exe"),
                                       QStringLiteral("bat"),     QStringLiteral("cmd"),  QStringLiteral("com"),
                                       QStringLiteral("msi"),     QStringLiteral("jar"),  QStringLiteral("py"),
                                       QStringLiteral("pl"),      QStringLiteral("rb"),   QStringLiteral("appimage"),
                                       QStringLiteral("run"),     QStringLiteral("bin"),  QStringLiteral("ps1"),
                                       QStringLiteral("vbs"),     QStringLiteral("js"),   QStringLiteral("scr"),
                                       QStringLiteral("lnk"),     QStringLiteral("command"), QStringLiteral("flatpakref")};
    return (info.isExecutable() && !info.isDir()) || kRunnable.contains(info.suffix().toLower());
}

} // namespace

void ShowHost::createSlideShow()
{
    m_slideShow = new ShowSlideShow(this);
    m_actionSettings = new ShowActionSettings(this);
    m_export = new ShowExport(this);
    m_effects = new ShowEffectOptions(this);
}

void ShowHost::startShow()
{
    PresentationController::Options options;
    options.startSlide = qMax(0, m_currentRow);
    startShow(options);
}

void ShowHost::startShowFromBeginning()
{
    startShow(PresentationController::Options());
}

void ShowHost::startShow(const PresentationController::Options &options)
{
    if (slideCount() == 0 || m_player != nullptr) {
        return;
    }
    if (ShowModel::showPlaylist(*m_document->presentation(), options.customShow).isEmpty()) {
        showStatusMessage(tr("There are no visible slides in this show"), 6000);
        return;
    }
    m_canvas->commitTextEdit(false);
    m_player = new PresentationController(m_document->presentation(), options, this);
    // An action's sound plays from a file, like the slides' media.
    m_player->setSoundResolver([this](const QByteArray &bytes, const QString &name) {
        ShowModel::MediaInfo sound;
        sound.kind = ShowModel::MediaKind::Audio;
        sound.data = bytes;
        sound.mime = QStringLiteral("audio/wav");
        sound.name = name;
        return m_media->urlFor(sound);
    });
    // Queued: the controller is still in its action when it asks.
    connect(m_player, &PresentationController::endRequested, this, &ShowHost::endShow, Qt::QueuedConnection);
    connect(m_player, &PresentationController::openRequested, this, &ShowHost::openActionTarget);
    emit playerChanged();
}

void ShowHost::endShow()
{
    if (m_player == nullptr) {
        return;
    }
    // The editor lands on the slide the show ended on.
    const int row = m_player->slide();
    QHash<int, qint64> timings;
    qint64 total = 0;
    const bool rehearsed = m_player->rehearsal() != nullptr;
    if (rehearsed) {
        m_player->rehearsal()->finish();
        timings = m_player->rehearsal()->timings();
        total = m_player->rehearsal()->totalMs();
    }
    m_player->deleteLater();
    m_player = nullptr;
    m_presenterView = false;
    emit playerChanged();
    selectSlideRow(row);
    if (rehearsed) {
        m_slideShow->rehearsalFinished(timings, total); // "keep the new timings?"
    }
}

void ShowHost::openActionTarget(const QString &address, bool file)
{
    const QString scheme = ShowModel::webScheme(address);
    if (!file && (scheme == u"http" || scheme == u"https" || scheme == u"mailto")) {
        QDesktopServices::openUrl(QUrl(address));
        return;
    }
    if (!scheme.isEmpty()) {
        emit showNotice(tr("QindaShow opens web, e-mail and file links only, not %1").arg(address));
        return;
    }
    // Another file: a file: URL or a path, relative to the presentation.
    QString path = address.startsWith(QLatin1String("file:"), Qt::CaseInsensitive) ? QUrl(address).toLocalFile() : address;
    if (QDir::isRelativePath(path)) {
        const QString deck = m_document->canonicalPath();
        path = QDir(deck.isEmpty() ? QDir::currentPath() : QFileInfo(deck).absolutePath()).filePath(path);
    }
    const QFileInfo info(QDir::cleanPath(path));
    if (!info.exists()) {
        emit showNotice(tr("%1 is not there").arg(QDir::toNativeSeparators(info.filePath())));
        return;
    }
    if (isProgram(info)) {
        emit showNotice(tr("%1 is a program. QindaShow never runs programs.").arg(info.fileName()));
        return;
    }
    emit fileConsentRequested(info.absoluteFilePath());
}

void ShowHost::openFileConfirmed(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || isProgram(info)) {
        return; // asked again: still never a program
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}

} // namespace QindaOffice::Apps::Show
