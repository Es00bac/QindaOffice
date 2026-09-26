// SPDX-License-Identifier: GPL-3.0-or-later
#include "libre_office_converter.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

namespace QindaOffice::Core {

QString LibreOfficeConverter::convertTokenFor(const QString &targetExtension)
{
    const QString ext = targetExtension.toLower();
    // Plan-verified registry names (docx/html/fodp/pptx); everything else
    // converts by bare extension so soffice resolves the default filter.
    if (ext == QLatin1String("docx")) {
        return QStringLiteral("docx:MS Word 2007 XML");
    }
    if (ext == QLatin1String("html")) {
        return QStringLiteral("html:HTML (StarWriter)");
    }
    if (ext == QLatin1String("fodp")) {
        return QStringLiteral("fodp:OpenDocument Presentation Flat XML");
    }
    if (ext == QLatin1String("pptx")) {
        return QStringLiteral("pptx:Impress MS PowerPoint 2007 XML");
    }
    return ext;
}

LibreOfficeConverter::LibreOfficeConverter(QObject *parent)
    : QObject(parent)
{
}

LibreOfficeConverter::~LibreOfficeConverter()
{
    if (m_process != nullptr && m_process->state() == QProcess::Running) {
        m_process->kill();
    }
}

QProcessEnvironment LibreOfficeConverter::headlessEnvironment()
{
    // AGENT-GUARD: soffice must use its headless "svp" VCL backend whatever
    // the session asks for. A QindaQt session exports SAL_USE_VCLPLUGIN=qt6
    // (environment.d, so LibreOffice's own menu joins the global menu), and
    // our apps and tests often carry QT_QPA_PLATFORM=offscreen; soffice
    // inherited both, loaded its Qt VCL plugin on the offscreen platform and
    // aborted ("Unspecified Application Error", exit 134) before converting.
    // Pinning svp and dropping the Qt platform variables keeps every
    // conversion independent of how the calling app was started.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SAL_USE_VCLPLUGIN"), QStringLiteral("svp"));
    env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    env.remove(QStringLiteral("QT_QUICK_BACKEND"));
    return env;
}

bool LibreOfficeConverter::probe(QString *diagnostic)
{
    QProcess check;
    check.setProcessEnvironment(headlessEnvironment());
    check.start(QStringLiteral("soffice"),
                {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!check.waitForStarted(5000)) {
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("soffice could not be started");
        }
        return false;
    }
    if (!check.waitForFinished(15000)) {
        check.kill();
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("soffice --version timed out");
        }
        return false;
    }
    if (check.exitCode() != 0) {
        if (diagnostic != nullptr) {
            *diagnostic = QString::fromUtf8(check.readAllStandardError()).trimmed();
        }
        return false;
    }
    return true;
}

bool LibreOfficeConverter::isAvailable() const
{
    return m_available;
}

int LibreOfficeConverter::convert(const QString &inputPath, const QString &targetExtension,
                                  const QString &outputDir, QString *error)
{
    if (!m_probed) {
        m_probed = true;
        m_available = probe(nullptr);
    }
    if (!m_available) {
        if (error != nullptr) {
            *error = tr("LibreOffice is not available on this system.");
        }
        return 0;
    }
    const QFileInfo input(inputPath);
    if (!input.isFile() || !input.isReadable()) {
        if (error != nullptr) {
            *error = tr("Input file is not readable: %1").arg(inputPath);
        }
        return 0;
    }
    Job job;
    job.id = ++m_nextId;
    job.input = input.absoluteFilePath();
    job.token = convertTokenFor(targetExtension);
    job.outDir = outputDir;
    m_queue.append(std::move(job));
    if (m_process == nullptr) {
        runNext();
    }
    return m_queue.isEmpty() ? m_nextId : m_queue.last().id;
}

void LibreOfficeConverter::runNext()
{
    if (m_queue.isEmpty()) {
        return;
    }
    const Job job = m_queue.takeFirst();
    m_currentId = job.id;

    m_profile = std::make_unique<QTemporaryDir>();
    m_profile->setAutoRemove(true);

    auto *process = new QProcess(this);
    m_process = process;
    // AGENT-GUARD: verify the SENDER is the current job's process before
    // finishing. A crashed (or killed) job emits errorOccurred AND finished,
    // and a killed job's signals arrive after the next job already started —
    // without the guard one stale signal failed the next job (cascading the
    // whole queue) or double-emitted jobFailed.
    connect(process, &QProcess::errorOccurred, this,
            [this, process, job](QProcess::ProcessError error) {
                if (m_process != process) {
                    return; // stale signal from a superseded process
                }
                // FailedToStart never emits finished() — finish here. Other
                // error kinds (Crashed) are handled by the finished() handler
                // with the full diagnostics.
                if (error == QProcess::FailedToStart) {
                    finishCurrent(false, tr("soffice failed to start for %1").arg(job.input));
                }
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, job](int exitCode, QProcess::ExitStatus status) {
                if (m_process != process) {
                    return; // stale signal from a superseded process
                }
                if (status != QProcess::NormalExit || exitCode != 0) {
                    finishCurrent(false, tr("soffice exited with code %1").arg(exitCode));
                    return;
                }
                const QFileInfo input(job.input);
                const QString output =
                    job.outDir + QLatin1Char('/') + input.completeBaseName() + QLatin1Char('.')
                    + job.token.section(QLatin1Char(':'), 0, 0);
                if (!QFileInfo::exists(output)) {
                    finishCurrent(false, tr("soffice produced no output for %1").arg(job.input));
                    return;
                }
                finishCurrent(true, output);
            });

    // AGENT-GUARD: one job at a time, private profile per run (probe #8);
    // --norestore keeps a crashed instance from blocking this run.
    process->setProcessEnvironment(headlessEnvironment());
    process->start(QStringLiteral("soffice"),
                   {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                    QStringLiteral("--nologo"),
                    QStringLiteral("-env:UserInstallation=file://%1").arg(m_profile->path()),
                    QStringLiteral("--convert-to"), job.token,
                    QStringLiteral("--outdir"), job.outDir, job.input});

    QTimer::singleShot(kJobTimeoutMs, process, [this, process] {
        if (m_process == process && process->state() == QProcess::Running) {
            process->kill();
            finishCurrent(false, tr("conversion timed out"));
        }
    });
}

void LibreOfficeConverter::finishCurrent(bool ok, const QString &message)
{
    QProcess *process = m_process;
    const int jobId = m_currentId;
    m_process = nullptr;
    m_profile.reset();
    if (process != nullptr) {
        process->deleteLater();
    }
    if (ok) {
        emit jobFinished(jobId, message);
    } else {
        emit jobFailed(jobId, message);
    }
    runNext(); // next queued job, if any
}

QString convertBlocking(const QString &inputPath, const QString &targetExtension,
                        const QString &scratchDirName, QString *error)
{
    LibreOfficeConverter converter;
    const QString outDir =
        QFileInfo(inputPath).absolutePath() + QLatin1Char('/') + scratchDirName;
    QDir().mkpath(outDir);
    QEventLoop loop;
    QString result;
    QString failure;
    QObject::connect(&converter, &LibreOfficeConverter::jobFinished, &loop,
                     [&loop, &result](int, const QString &output) {
                         result = output;
                         loop.quit();
                     });
    QObject::connect(&converter, &LibreOfficeConverter::jobFailed, &loop,
                     [&loop, &failure](int, const QString &message) {
                         failure = message;
                         loop.quit();
                     });
    const int id = converter.convert(inputPath, targetExtension, outDir, error);
    if (id == 0) {
        return {};
    }
    // A conversion that never answers must not hang the window. The guard
    // sits just past the converter's own per-job timeout.
    QTimer::singleShot(LibreOfficeConverter::kJobTimeoutMs + 10000, &loop,
                       [&loop] { loop.quit(); });
    loop.exec();
    if (result.isEmpty() && failure.isEmpty() && error != nullptr) {
        *error = QStringLiteral("conversion timed out");
    } else if (!failure.isEmpty() && error != nullptr) {
        *error = failure;
    }
    // AGENT-GUARD: only intermediates we produced (inside the private
    // converter directory) may be removed — on the load path `inputPath` is
    // the user's own document.
    if (inputPath.startsWith(outDir)) {
        QFile::remove(inputPath);
    }
    return result;
}

} // namespace QindaOffice::Core
