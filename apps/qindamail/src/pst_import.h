// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// File ▸ Import ▸ Outlook Data File (M76, D-085): a .pst (or an .ost in the
// same format) into QindaMail — mail into a local folder tree that mirrors
// the file's, contacts and contact groups into People, appointments into the
// local calendar, tasks onto the task board. One import at a time per
// process (MailService::outlookImport, shared by every window).
//
// The job streams: it scans the folder tree a few folders per event-loop
// turn, then imports items in slices of ~40 ms (the mbox importer's rule),
// reporting `progress`, `counts` and `currentFolder` as it goes; cancel()
// stops at the next slice and keeps what was imported. Contacts are merged
// into the address book once, at the end (one write of contacts.json).
//
// AGENT-CONTRACT (PstImportDialog.qml, tst_qindamail_pst): start(url) → a
// request id ("" when an import is already running, with lastError);
// finished(requestId, summary) exactly once, on a later turn, with summary
// {state accepted|rejected|cancelled, file, folder (the local root folder),
// folderKey, folders, messages, contacts, groups, events, tasks, skipped,
// total, error, warnings}. The same file imported again replaces what it
// imported before (outlook_records.h identities; mail by content id) and
// lands in the same local folder (settings key "outlookImports": record key
// → folder).
// AGENT-GUARD: the file is untrusted (pst_reader.h bounds everything); an
// item that cannot be read is skipped and counted, never fatal; only a file
// that cannot be opened fails the import.

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace QindaOffice::Apps::Mail {

class MailService;

Q_MOC_INCLUDE("mail_service.h")

class PstImporter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MailService owns the Outlook import (MailService.outlookImport).")
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    // idle | scanning | importing | done | failed | cancelled
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(int processed READ processed NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    explicit PstImporter(MailService *service);
    ~PstImporter() override;

    [[nodiscard]] bool running() const;
    [[nodiscard]] QString phase() const { return m_phase; }
    [[nodiscard]] QString fileName() const { return m_fileName; }
    [[nodiscard]] QString currentFolder() const { return m_currentFolder; }
    [[nodiscard]] int total() const { return m_total; }
    [[nodiscard]] int processed() const { return m_processed; }
    [[nodiscard]] double progress() const;
    [[nodiscard]] QVariantMap counts() const;
    [[nodiscard]] QVariantMap summary() const { return m_summary; }
    [[nodiscard]] QString lastError() const { return m_lastError; }

    Q_INVOKABLE QString start(const QUrl &file);
    Q_INVOKABLE void cancel();

    // C++ (tests): the time slice per event-loop turn (ms).
    void setSliceMs(int ms) { m_sliceMs = ms; }

signals:
    void stateChanged();
    void progressChanged();
    void finished(const QString &requestId, const QVariantMap &summary);

private:
    struct Job;
    void step();
    void next();   // the next slice on a later turn
    void scanStep();
    void importStep();
    void importItem(quint32 nid);
    void finish(const QString &state, const QString &error);
    void setPhase(const QString &phase);
    [[nodiscard]] QString rootFolderFor(const QByteArray &recordKey, const QString &baseName) const;

    QPointer<MailService> m_service;
    std::unique_ptr<Job> m_job;
    QString m_phase = QStringLiteral("idle");
    QString m_fileName;
    QString m_currentFolder;
    QString m_lastError;
    QVariantMap m_summary;
    int m_total = 0;
    int m_processed = 0;
    int m_sliceMs = 40;
};

} // namespace QindaOffice::Apps::Mail
