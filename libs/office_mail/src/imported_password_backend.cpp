// SPDX-License-Identifier: GPL-3.0-or-later
#include "imported_password_backend.h"
#include "imported_password_reader.h"

#include <QPointer>

namespace QindaOffice::Mail {
namespace {
class ImportedPasswordBackend final : public QObject, public SecretBackend {
public:
    ImportedPasswordBackend(std::shared_ptr<SecretBackend> primary, QDBusConnection bus)
        : m_primary(std::move(primary)), m_bus(std::move(bus)) {}

    ~ImportedPasswordBackend() override
    {
        // A resource-creating reply already in flight must still be drained,
        // so an owned session/prompt can be closed after callback cancellation.
        const auto readers = findChildren<ImportedPassword::Reader *>(QString(), Qt::FindDirectChildrenOnly);
        for (auto *reader : readers) reader->cancel();
    }

    void read(const QString &key, Callback done) override
    {
        QPointer<ImportedPasswordBackend> self(this);
        m_primary->read(key, [self, key, done](const SecretOutcome &outcome) {
            if (!self) return;
            // AGENT-GUARD: locked, denied, unavailable and failed native reads
            // are not a reason to switch credential sources or start prompts.
            if (outcome.status != SecretOutcome::Status::NotFound) return done(outcome);
            auto *reader = new ImportedPassword::Reader(self->m_bus, key, false,
                [done, outcome](const SecretOutcome &imported) {
                    done(imported.status == SecretOutcome::Status::NotFound ? outcome : imported);
                }, self);
            reader->start();
        });
    }

    void write(const QString &key, const QString &value, Callback done) override
    {
        QPointer<ImportedPasswordBackend> self(this);
        m_primary->write(key, value, [self, done](const SecretOutcome &outcome) { if (self) done(outcome); });
    }

    void remove(const QString &key, Callback done) override
    {
        QPointer<ImportedPasswordBackend> self(this);
        m_primary->remove(key, [self, key, done](const SecretOutcome &outcome) {
            if (!self) return;
            if (outcome.status != SecretOutcome::Status::Ok) return done(outcome);
            // No silent resurrection or deletion of an imported original:
            // presence is checked without opening a session or unlocking it.
            auto *reader = new ImportedPassword::Reader(self->m_bus, key, true,
                [done](const SecretOutcome &presence) {
                    if (presence.status == SecretOutcome::Status::NotFound)
                        return done({SecretOutcome::Status::Ok, {}, {}});
                    if (presence.status == SecretOutcome::Status::Ok)
                        return done({SecretOutcome::Status::Failed, {}, QStringLiteral(
                            "An imported credential is preserved. Remove it in Passwords and Keys to complete cleanup.")});
                    done(presence);
                }, self);
            reader->start();
        });
    }

    QString name() const override { return m_primary->name(); }

private:
    std::shared_ptr<SecretBackend> m_primary;
    QDBusConnection m_bus;
};
} // namespace

std::shared_ptr<SecretBackend> withImportedPasswords(std::shared_ptr<SecretBackend> primary,
                                                   const QDBusConnection &bus)
{
    Q_ASSERT(primary);
    return std::make_shared<ImportedPasswordBackend>(std::move(primary), bus);
}
} // namespace QindaOffice::Mail
