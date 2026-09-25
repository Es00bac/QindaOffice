// SPDX-License-Identifier: GPL-3.0-or-later
// OFX / QFX: OFX 1.x (SGML: leaf elements have no closing tag) and OFX 2.x
// (XML). One tokenizer serves both.
//
// AGENT-NOTE: an element's value is the text between its start tag and
// the next '<', which is exactly how SGML leaves work and is also true of
// XML leaves; aggregates (STMTTRN, LEDGERBAL) have explicit end tags in
// both versions. So we never need a DTD or an XML parser — and bank files
// that are "almost XML" still import.
#include "bank_import.h"

#include "csv_util.h"

#include <QRegularExpression>

namespace QindaOffice::Books::BankImport {

namespace {

QString decodeEntities(QString s)
{
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    static const QRegularExpression numeric(QStringLiteral("&#(x?)([0-9A-Fa-f]+);"));
    QRegularExpressionMatch m;
    while ((m = numeric.match(s)).hasMatch()) {
        bool ok = false;
        const uint code = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
        s.replace(m.capturedStart(), m.capturedLength(), ok ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1) : QString());
    }
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return s.simplified();
}

// DTPOSTED: YYYYMMDD[HHMMSS[.XXX]][[-5:EST]] — the calendar date is what
// the statement means; the time zone never moves it.
QDate ofxDate(const QString &value)
{
    return QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
}

struct Txn {
    QString type;
    QString posted;
    QString user;
    QString amount;
    QString fitid;
    QString name;
    QString memo;
    QString check;
    QString ref;
};

} // namespace

BankParseResult parseOfx(const QByteArray &data, int exponent)
{
    BankParseResult result;
    result.format = QStringLiteral("ofx");
    const QString text = Csv::decode(data);
    const qsizetype start = text.indexOf(QStringLiteral("<OFX"), 0, Qt::CaseInsensitive);
    if (start < 0) {
        result.errors << BankParseError{0, QStringLiteral("not an OFX file (no <OFX> element)")};
        return result;
    }
    static const QRegularExpression token(QStringLiteral("<([^<>]+)>([^<]*)"));
    auto it = token.globalMatch(text, start);
    std::optional<Txn> txn;
    bool inLedger = false;
    int txnNo = 0;
    auto finish = [&] {
        if (!txn) {
            return;
        }
        ++txnNo;
        BankLine line;
        line.date = ofxDate(txn->posted.isEmpty() ? txn->user : txn->posted);
        const std::optional<qint64> amount = parseAmount(txn->amount, exponent, false);
        if (!line.date.isValid() || !amount) {
            result.errors << BankParseError{txnNo, QStringLiteral("transaction %1 has no readable date or amount")
                                                       .arg(txn->fitid.isEmpty() ? QString::number(txnNo) : txn->fitid)};
        } else {
            line.amount = *amount;
            line.fitid = txn->fitid;
            line.payee = txn->name.isEmpty() ? txn->memo : txn->name;
            line.memo = txn->name.isEmpty() ? QString() : txn->memo;
            line.checkNumber = txn->check.isEmpty() ? txn->ref : txn->check;
            result.lines << line;
        }
        txn.reset();
    };
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString tag = m.captured(1).trimmed();
        if (tag.startsWith(QLatin1Char('?')) || tag.startsWith(QLatin1Char('!'))) {
            continue;
        }
        const QString value = decodeEntities(m.captured(2));
        if (tag.startsWith(QLatin1Char('/'))) {
            const QString name = tag.mid(1).trimmed().toUpper();
            if (name == QLatin1String("STMTTRN")) {
                finish();
            } else if (name == QLatin1String("LEDGERBAL")) {
                inLedger = false;
            }
            continue;
        }
        const QString name = tag.section(QLatin1Char(' '), 0, 0).toUpper();
        if (name == QLatin1String("STMTTRN")) {
            finish(); // SGML files may omit </STMTTRN> in the wild
            txn = Txn{};
            continue;
        }
        if (name == QLatin1String("BANKTRANLIST") || name == QLatin1String("LEDGERBAL")
            || name == QLatin1String("AVAILBAL")) {
            finish();
        }
        if (name == QLatin1String("LEDGERBAL")) {
            inLedger = true;
        } else if (name == QLatin1String("CCSTMTRS")) {
            result.creditCard = true;
        }
        if (value.isEmpty()) {
            continue;
        }
        if (txn) {
            if (name == QLatin1String("TRNTYPE")) txn->type = value;
            else if (name == QLatin1String("DTPOSTED")) txn->posted = value;
            else if (name == QLatin1String("DTUSER")) txn->user = value;
            else if (name == QLatin1String("TRNAMT")) txn->amount = value;
            else if (name == QLatin1String("FITID")) txn->fitid = value;
            else if (name == QLatin1String("NAME")) txn->name = value;
            else if (name == QLatin1String("MEMO")) txn->memo = value;
            else if (name == QLatin1String("CHECKNUM")) txn->check = value;
            else if (name == QLatin1String("REFNUM")) txn->ref = value;
            continue;
        }
        if (name == QLatin1String("ACCTID")) {
            result.accountId = value;
        } else if (name == QLatin1String("CURDEF")) {
            result.currency = value;
        } else if (inLedger && name == QLatin1String("BALAMT")) {
            result.ledgerBalance = parseAmount(value, exponent, false);
        } else if (inLedger && name == QLatin1String("DTASOF")) {
            result.ledgerBalanceDate = ofxDate(value);
        }
    }
    finish();
    return result;
}

} // namespace QindaOffice::Books::BankImport
