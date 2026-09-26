// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class QTemporaryDir;

namespace QindaOffice::Core {

// Headless LibreOffice conversion bridge (plan section 5.1): async, one job
// at a time per converter instance, 120 s per-job timeout, a private
// -env:UserInstallation profile per run so converters in different processes
// never clash (parallel-run probe #8). Availability is probed once at
// startup; every filter name below was verified against the LO registry.
//
// Usage: int id = converter.convert(in, "docx", outDir); the result arrives
// as jobFinished(id, outputPath) or jobFailed(id, error). Callers own
// cleaning the output; the per-run profile is removed after each job.
class LibreOfficeConverter final : public QObject {
    Q_OBJECT
public:
    static constexpr int kJobTimeoutMs = 120000;

    // Target extension -> --convert-to token (verified names; unknown
    // extensions convert by bare extension and let soffice pick the filter).
    [[nodiscard]] static QString convertTokenFor(const QString &targetExtension);

    explicit LibreOfficeConverter(QObject *parent = nullptr);
    ~LibreOfficeConverter() override;

    // soffice --version; false (with a diagnostic) when unavailable. Cached
    // per process - LibreOffice is an optional runtime dependency.
    [[nodiscard]] static bool probe(QString *diagnostic = nullptr);
    [[nodiscard]] bool isAvailable() const;

    // The environment every soffice child runs in: the caller's, with the
    // VCL backend pinned to the headless one (see the AGENT-GUARD in the .cpp).
    [[nodiscard]] static QProcessEnvironment headlessEnvironment();

    // Queues a conversion; returns a job id >= 1, or 0 when the queue is
    // rejected (unavailable converter or unreadable input).
    int convert(const QString &inputPath, const QString &targetExtension,
                const QString &outputDir, QString *error = nullptr);

signals:
    void jobFinished(int jobId, QString outputPath);
    void jobFailed(int jobId, QString error);

private:
    void runNext();
    void finishCurrent(bool ok, const QString &message);

    struct Job {
        int id = 0;
        QString input;
        QString token;
        QString outDir;
    };

    QList<Job> m_queue;
    QProcess *m_process = nullptr;
    std::unique_ptr<QTemporaryDir> m_profile;
    int m_nextId = 0;
    int m_currentId = 0;
    bool m_available = false;
    bool m_probed = false;
};

// Blocking one-shot conversion for document load/save paths (the three apps
// each grew a byte-identical private copy of this; it belongs next to the
// converter). Runs a nested event loop — the conversion is a local
// subprocess, so the UI stays responsive — bounded by a hard timeout so a
// wedged soffice can never hang the window.
//
// `scratchDirName` is the private intermediate directory created beside the
// input (".qindacalc-lo" and friends). Files INSIDE it are removed
// afterwards; the user's own document is never touched.
//
// Returns the converted file's path, or {} with `error` set.
[[nodiscard]] QString convertBlocking(const QString &inputPath,
                                      const QString &targetExtension,
                                      const QString &scratchDirName, QString *error);

} // namespace QindaOffice::Core
