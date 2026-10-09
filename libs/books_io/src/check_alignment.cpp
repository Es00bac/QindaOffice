// SPDX-License-Identifier: GPL-3.0-or-later
// Per-printer check alignment (check_alignment.h): validation, points, and
// the per-user JSON store.
#include "check_alignment.h"

#include "decimal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace QindaOffice::Books {

namespace {

constexpr qreal kPointsPerInch = 72.0;
constexpr qreal kMillimetresPerInch = 25.4;

// The typed value in points; the text was validated by make().
qreal points(const QString &text, CheckAlignment::Unit unit)
{
    const std::optional<Decimal> d = Decimal::parse(text);
    if (!d) {
        return 0;
    }
    qreal value = qreal(d->mantissa());
    for (int i = 0; i < d->scale(); ++i) {
        value /= 10.0;
    }
    return unit == CheckAlignment::Unit::Millimetre ? value * kPointsPerInch / kMillimetresPerInch
                                                    : value * kPointsPerInch;
}

// One coordinate: decimal text, at most `decimals` places and `limit`
// (in the unit's own steps: 100 hundredths of an inch, 254 tenths of a mm).
std::optional<QString> coordinate(const QString &raw, int decimals, qint64 limit, QString *error)
{
    const QString text = raw.trimmed().isEmpty() ? QStringLiteral("0") : raw.trimmed();
    const std::optional<Decimal> d = Decimal::parse(text);
    if (!d || text.endsWith(QLatin1Char('%')) || d->scale() > decimals) {
        if (error != nullptr) {
            *error = QStringLiteral("\"%1\" is not an offset (up to %2 decimals)").arg(raw).arg(decimals);
        }
        return std::nullopt;
    }
    const qint64 steps = d->mantissa() * pow10i(decimals - d->scale());
    if (steps > limit || steps < -limit) {
        if (error != nullptr) {
            *error = QStringLiteral("an offset is at most one inch (25.4 mm) either way");
        }
        return std::nullopt;
    }
    return d->toString();
}

} // namespace

CheckOffset CheckAlignment::offset() const
{
    return CheckOffset{points(x, unit), points(y, unit)};
}

bool CheckAlignment::isZero() const
{
    const CheckOffset o = offset();
    return o.dx == 0 && o.dy == 0;
}

QString CheckAlignment::describe() const
{
    const QString unitText = unit == Unit::Millimetre ? QStringLiteral("mm") : QStringLiteral("in");
    QStringList parts;
    const auto part = [&](const QString &value, const QString &plus, const QString &minus) {
        const std::optional<Decimal> d = Decimal::parse(value);
        if (!d || d->isZero()) {
            return;
        }
        const Decimal magnitude(d->isNegative() ? -d->mantissa() : d->mantissa(), d->scale());
        parts << QStringLiteral("%1 %2 %3").arg(magnitude.toString(), unitText, d->isNegative() ? minus : plus);
    };
    part(x, QStringLiteral("right"), QStringLiteral("left"));
    part(y, QStringLiteral("down"), QStringLiteral("up"));
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QStringLiteral(", "));
}

std::optional<CheckAlignment> CheckAlignment::make(const QString &printer, const QString &x, const QString &y,
                                                   const QString &unitKey, QString *error)
{
    CheckAlignment a;
    a.printer = printer.trimmed();
    a.unit = unitKey == QLatin1String("mm") ? Unit::Millimetre : Unit::Inch;
    const int decimals = a.unit == Unit::Millimetre ? 1 : 2;
    const qint64 limit = a.unit == Unit::Millimetre ? 254 : 100;
    const std::optional<QString> cx = coordinate(x, decimals, limit, error);
    const std::optional<QString> cy = cx ? coordinate(y, decimals, limit, error) : std::nullopt;
    if (!cx || !cy) {
        return std::nullopt;
    }
    a.x = *cx;
    a.y = *cy;
    return a;
}

CheckAlignmentStore::CheckAlignmentStore(const QString &path)
    : m_path(path.isEmpty() ? defaultPath() : path)
{
}

QString CheckAlignmentStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/qindaoffice/books/check-alignment.json");
}

QHash<QString, CheckAlignment> CheckAlignmentStore::load() const
{
    QHash<QString, CheckAlignment> out;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const QJsonObject printers = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("printers")).toObject();
    for (auto it = printers.begin(); it != printers.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        // Re-validated on the way in: a hand-edited file never moves a check by a mile.
        if (const std::optional<CheckAlignment> a =
                CheckAlignment::make(it.key(), o.value(QStringLiteral("x")).toString(),
                                     o.value(QStringLiteral("y")).toString(), o.value(QStringLiteral("unit")).toString())) {
            out.insert(it.key(), *a);
        }
    }
    return out;
}

CheckAlignment CheckAlignmentStore::alignment(const QString &printer) const
{
    CheckAlignment fallback;
    fallback.printer = printer;
    return load().value(printer.trimmed(), fallback);
}

bool CheckAlignmentStore::save(const CheckAlignment &alignment, QString *error)
{
    QHash<QString, CheckAlignment> all = load();
    if (alignment.isZero()) {
        all.remove(alignment.printer);
    } else {
        all.insert(alignment.printer, alignment);
    }
    QJsonObject printers;
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        printers.insert(it.key(), QJsonObject{{QStringLiteral("x"), it->x},
                                              {QStringLiteral("y"), it->y},
                                              {QStringLiteral("unit"), it->unitKey()}});
    }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("could not create %1").arg(QFileInfo(m_path).absolutePath());
        }
        return false;
    }
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)
        || f.write(QJsonDocument(QJsonObject{{QStringLiteral("printers"), printers}}).toJson()) < 0 || !f.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("could not write %1").arg(m_path);
        }
        return false;
    }
    return true;
}

QStringList CheckAlignmentStore::printers() const
{
    QStringList names = load().keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

} // namespace QindaOffice::Books
