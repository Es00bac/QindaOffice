// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Stripe payouts reach the bank twice: once in Stripe's balance history (a
// payout, booked as a Transfer from the Stripe clearing account to the
// payout bank account) and once in the bank's own feed (a deposit from
// "STRIPE"). The deposit must be MATCHED to the transfer's bank-side line,
// never categorized as income a second time (D-048).
//
// AGENT-CONTRACT (with stripe_posting.cpp): a payout transfer is a
// DocKind::Transfer document whose `reference` is the Stripe payout id
// ("po_…"); its to-account line is what the bank deposit clears. Both
// import orders converge here: Mercury first leaves the deposit New until
// the transfer exists; Stripe first leaves the transfer's bank line
// uncleared until the deposit arrives. Both syncs call match() at the end.

#include <QtGlobal>

namespace QindaOffice::Books {
class CompanyFile;
}

namespace QindaOffice::Books::Connect {

// Days a bank deposit may differ from the payout transfer (dated when
// Stripe created the payout; banks credit it one to four business days
// later).
inline constexpr int kPayoutMatchDays = 7;

// Matches New feed lines (on `bankAccountId`, or every account when 0)
// whose payee or memo mentions Stripe to uncleared payout-transfer lines
// of the same amount within kPayoutMatchDays, closest date first.
// Returns the number of lines matched.
int matchStripePayouts(CompanyFile &file, qint64 bankAccountId = 0);

} // namespace QindaOffice::Books::Connect
