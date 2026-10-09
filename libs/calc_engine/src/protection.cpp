// SPDX-License-Identifier: GPL-3.0-or-later
// Sheet and workbook protection (protection.h, M66, D-075).

#include "protection.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <optional>

namespace QindaOffice::Calc {

namespace {

std::optional<QCryptographicHash::Algorithm> algorithmNamed(const QString &name)
{
    const QString upper = name.trimmed().toUpper();
    if (upper == QLatin1String("SHA-512")) {
        return QCryptographicHash::Sha512;
    }
    if (upper == QLatin1String("SHA-384")) {
        return QCryptographicHash::Sha384;
    }
    if (upper == QLatin1String("SHA-256")) {
        return QCryptographicHash::Sha256;
    }
    if (upper == QLatin1String("SHA-1")) {
        return QCryptographicHash::Sha1;
    }
    if (upper == QLatin1String("MD5")) {
        return QCryptographicHash::Md5;
    }
    return std::nullopt; // RIPEMD-160, WHIRLPOOL, …: Qt has no implementation
}

QByteArray iteratedHash(QCryptographicHash::Algorithm algorithm, const QString &password, const QByteArray &salt,
                        qint32 spinCount)
{
    QByteArray utf16le;
    utf16le.reserve(password.size() * 2);
    for (const QChar c : password) {
        utf16le.append(char(c.unicode() & 0xff));
        utf16le.append(char(c.unicode() >> 8));
    }
    QCryptographicHash first(algorithm);
    first.addData(salt);
    first.addData(utf16le);
    QByteArray hash = first.result();
    // AGENT-NOTE: the iterator FOLLOWS the previous hash for sheet and
    // workbook protection ("append"); agile encryption puts it first. The
    // order is the whole difference between a password Excel accepts and
    // one it rejects.
    QByteArray round(hash.size() + 4, '\0');
    for (qint32 i = 0; i < spinCount; ++i) {
        std::copy(hash.cbegin(), hash.cend(), round.begin());
        round[hash.size()] = char(i & 0xff);
        round[hash.size() + 1] = char((i >> 8) & 0xff);
        round[hash.size() + 2] = char((i >> 16) & 0xff);
        round[hash.size() + 3] = char((i >> 24) & 0xff);
        hash = QCryptographicHash::hash(round, algorithm);
    }
    return hash;
}

QByteArray freshSalt()
{
    QByteArray salt(16, '\0');
    for (char &byte : salt) {
        byte = char(QRandomGenerator::system()->bounded(256));
    }
    return salt;
}

} // namespace

PasswordHash excelPasswordHash(const QString &password, const QByteArray &salt, qint32 spinCount)
{
    const QByteArray hash = iteratedHash(QCryptographicHash::Sha512, password, salt, spinCount);
    return {QStringLiteral("SHA-512"), QString::fromLatin1(hash.toBase64()), QString::fromLatin1(salt.toBase64()),
            spinCount};
}

QString legacyPasswordHash(const QString &password)
{
    // ECMA-376 Part 4 §3.3.1.81 (Apache POI's createXorVerifier1): fold the
    // password's bytes from the LAST one, rotating left within 15 bits
    // before each, then the length the same way, then 0xCE4B.
    if (password.isEmpty()) {
        return QStringLiteral("0");
    }
    const auto rotate = [](quint16 v) { return quint16(((v & 0x4000) != 0 ? 1 : 0) | ((v << 1) & 0x7fff)); };
    const QByteArray bytes = password.toLatin1();
    quint16 verifier = 0;
    for (qsizetype i = bytes.size() - 1; i >= 0; --i) {
        verifier = rotate(verifier);
        verifier ^= quint16(quint8(bytes.at(i)));
    }
    verifier = rotate(verifier);
    verifier ^= quint16(bytes.size());
    verifier ^= 0xCE4B;
    return QString::number(verifier, 16).toUpper();
}

bool passwordMatches(const QString &password, const QString &algorithmName, const QString &hashValue,
                     const QString &saltValue, qint32 spinCount, const QString &legacyHash)
{
    if (!hashValue.isEmpty()) {
        const auto algorithm = algorithmNamed(algorithmName);
        if (!algorithm) {
            return false;
        }
        const QByteArray expected = QByteArray::fromBase64(hashValue.toLatin1());
        return iteratedHash(*algorithm, password, QByteArray::fromBase64(saltValue.toLatin1()), qMax(0, spinCount))
            == expected;
    }
    if (!legacyHash.isEmpty()) {
        bool ok = false;
        const uint stored = legacyHash.trimmed().toUInt(&ok, 16);
        return ok && stored == legacyPasswordHash(password).toUInt(nullptr, 16);
    }
    return true; // no password: unprotecting needs none
}

bool passwordMatches(const SheetProtection &protection, const QString &password)
{
    return passwordMatches(password, protection.algorithmName, protection.hashValue, protection.saltValue,
                           protection.spinCount, protection.legacyHash);
}

bool passwordMatches(const WorkbookProtection &protection, const QString &password)
{
    return passwordMatches(password, protection.algorithmName, protection.hashValue, protection.saltValue,
                           protection.spinCount, protection.legacyHash);
}

void setPassword(SheetProtection *protection, const QString &password)
{
    protection->legacyHash.clear();
    if (password.isEmpty()) {
        protection->algorithmName.clear();
        protection->hashValue.clear();
        protection->saltValue.clear();
        protection->spinCount = 0;
        return;
    }
    const PasswordHash hash = excelPasswordHash(password, freshSalt());
    protection->algorithmName = hash.algorithmName;
    protection->hashValue = hash.hashValue;
    protection->saltValue = hash.saltValue;
    protection->spinCount = hash.spinCount;
}

void setPassword(WorkbookProtection *protection, const QString &password)
{
    protection->legacyHash.clear();
    if (password.isEmpty()) {
        protection->algorithmName.clear();
        protection->hashValue.clear();
        protection->saltValue.clear();
        protection->spinCount = 0;
        return;
    }
    const PasswordHash hash = excelPasswordHash(password, freshSalt());
    protection->algorithmName = hash.algorithmName;
    protection->hashValue = hash.hashValue;
    protection->saltValue = hash.saltValue;
    protection->spinCount = hash.spinCount;
}

} // namespace QindaOffice::Calc
