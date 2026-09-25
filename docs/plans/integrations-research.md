<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Research: which integrations QindaBooks (and the suite) should add next

Status: research only, 2026-09-25. Nothing here is decided and no code has changed.
The document is written for the lead and the owner. It covers every candidate named
in the brief plus a few more we found. **Every factual claim has a URL next to it.**
There are three kinds of claim without a primary source, and each is labelled:

- **(snippet)**: the official page renders only with JavaScript or refused the
  fetch, so the claim comes from the search engine's copy of that page.
- **(secondary)**: the source is a third party, not the vendor.
- **(unverified)**: we found no source that settles it.

Effort ratings (S/M/L) and risk judgements are ours, not the vendors'.

**Standing constraints this research assumes:**

- QindaBooks is a local-first Qt/C++ desktop app with one SQLite company file per business.
- Secrets live only in the desktop keyring.
- There is no backend of our own and no server that could receive a webhook. Sync pulls data while the app is open.
- The in-progress M48 connection design (D-048, on `work/books-connect`) is the pattern every new connector should follow:
  - everything lands in the **bank feed** first;
  - FITID = the provider's stable id;
  - only **posted** items are imported;
  - there is a per-connection **cursor** with an overlap window;
  - a processor's balance is a **bank-type clearing account**, and its payouts become transfers matched to the bank's deposit;
  - tokens are checked before they are stored and never written to the file.

**The owner's philosophy (added mid-brief) is "bring your own key" (BYOK).** The user
chooses the providers and pastes their own token or key, or their own OAuth client ID.
Nothing ties the user to one ecosystem, explicitly not Microsoft and not Intuit, and a
cheaper alternative counts as a feature. Each candidate therefore gets a **BYOK fit**
rating:

| Rating | Meaning |
|---|---|
| ★★★ | A personal, ideally read-only, token or key the user creates in the provider's own dashboard. File-based imports that need no key at all also count as ★★★. |
| ★★ | OAuth where the **user** can register their own app or client ID, a loopback (or otherwise desktop-usable) redirect works, and there is no business review. |
| ★ | Needs **us** to be an approved partner or aggregator, or needs a hosted backend (what exactly is listed). A BYOK-friendly alternative that gets the same data is named. |

---

## 1. Executive summary

The best next integrations share three traits: a key the owner mints, a pull API with
stable ids, and money flows that land in the existing bank feed. Most of the high-value
work turns out to need **no API at all**: saved import profiles, a 1099 file for the
IRS's free portal, and accountant/990 hand-off. Those are pure BYOK and cost nothing to
run.

### Ranked shortlist (value × feasibility, BYOK weighted heavily)

| # | Integration | BYOK | Effort | Why |
|---|---|---|---|---|
| 1 | **SimpleFIN Bridge** (any US/Canadian bank) | ★★★ | S | The only any-bank feed a user can hold personally. They paste a setup token, and it costs $15/yr for 25 institutions. It covers every bank Mercury doesn't: Chase, BofA, Amex, Relay, Novo, credit unions. |
| 2 | **PayPal** (incl. Pay with Venmo checkout) | ★★★ | M | The user creates their own REST app. Client credentials need no redirect and no review. Gross, fee and withdrawal (T-code) rows fit the Stripe clearing-account model. |
| 3 | **Saved import profiles for no-API sources** (payroll journals, Venmo/Cash App statements, Zeffy/Benevity/PayPal Giving Fund/Every.org exports) | ★★★ (files) | M | One mapping-profile importer turns a dozen providers with no usable API (every ★ payroll and donation platform) into repeatable imports with dedup. It builds on the existing `books_io` CSV import. |
| 4 | **Donation platforms**: Givebutter and Zeffy APIs; Donorbox joined through Stripe | ★★★ | S–M | The community center's income. Givebutter's free admin key includes payouts and fund/campaign codes. Zeffy's API is free. Donorbox donations join to the Stripe charges we already import. |
| 5 | **1099-NEC/MISC file for the IRS IRIS Taxpayer Portal** | ★★★ (file) | S–M | Free, no vendor. FIRE stops accepting files on **2026-11-19**, so tax year 2026 (due 2027-01-31) is IRIS-only. Vendors already carry a 1099 flag. |
| 6 | **Accountant and tax hand-off pack**: tax-line mapping, TB/GL export, Drake TB template, Form 990 Part VIII/IX classes | ★★★ (file) | S–M | Every accountant takes a TB/GL file. The nonprofit must e-file a 990 and needs functional-expense columns that the books must capture from day one. |
| 7 | **Square** | ★★★ | S–M | Personal access token allowed in production. Payout entries link to payments, the cleanest reconciliation of any processor. Relevant if the center takes in-person payments. |
| 8 | **Migration in from export files**: QBO "Export data" zip, Xero/Wave exports, Wave full-access token | ★★★ | M | Getting off QuickBooks is the savings story. Intuit's API needs an HTTPS non-loopback redirect and a production questionnaire (★), but its own export zip does not. |

Just below the line:

- **Stripe Tax reports** through the existing Stripe key (S, ★★★): take it as soon as the studio registers anywhere.
- **Brex / Ramp / BILL** (★★★, S–M each): only if the owner uses them.
- **Plaid Trial** (★★, free, 10 connections): a backstop for banks SimpleFIN lacks.
- **Studio storefronts**: Gumroad, Paddle, Shopify, Apple/Google payout reports.
- **Local OCR receipts plus QindaMail forwarding.**
- **Rippling / Deel payroll tokens.**

### What not to build

- **MX, Finicity/Mastercard, Akoya** (★): approved-partner onboarding, SOC 2 reports, server-held secrets. SimpleFIN already reaches MX on the user's behalf.
- **GoCardless Bank Account Data**: signups are closed.
- **TrueLayer**: no US coverage.
- **OFX Direct Connect**: gone at the big banks.
- **Gusto / ADP / Justworks APIs** (partner-gated; use their journal exports).
- **The QuickBooks Online API** for migration (★; use the export zip).
- **IRIS A2A** (L: TCC plus ATS testing plus JWKS; the portal CSV gives the same result).

---

## 2. BYOK scoreboard (all candidates)

| Category | ★★★ | ★★ | ★ (and the BYOK alternative) |
|---|---|---|---|
| Any-bank feeds | SimpleFIN Bridge | Plaid Trial (own dev account, Hosted Link, 10 items); Teller dev tier (own mTLS cert, 100 enrollments) | MX, Finicity, Akoya → **SimpleFIN**; GoCardless (closed), TrueLayer (no US) → statement files |
| Direct fintech/bank APIs | Mercury, Brex, Ramp, BILL (Spend & Expense; AP/AR sync token), Wise (personal token, statements) | — | Relay, Novo, Bluevine, Found, Lili, Amex, Chase, Capital One → **SimpleFIN / Plaid Trial / QFX-QBO-CSV download** |
| Payments & commerce | PayPal, Square, Shopify (Dev Dashboard client credentials, same org), WooCommerce, Gumroad, Lemon Squeezy, Paddle, Eventbrite, Apple App Store, Google Play, Patreon | Etsy (Seller App, auto-approved, PKCE) | Venmo, Zelle, Cash App, Ko-fi → **bank feed + statement CSV profile** |
| Donations | Givebutter, Zeffy, Donorbox ($17/mo add-on), GoFundMe Pro (self-serve client credentials), Bloomerang, Neon CRM | — | Every.org, Benevity, PayPal Giving Fund (and Meta fundraisers through it), Venmo charity, Little Green Light (approval required) → **CSV profiles** |
| Payroll & contractors | Rippling, Deel; journal/GL files from any payroll | — | Gusto, ADP RUN, Justworks, OnPay (no API) → **payroll journal / GL export profile** |
| 1099 | IRIS Taxpayer Portal CSV (file); Avalara 1099 & W-9 (own client id/secret) | — | IRIS A2A (TCC + ATS testing) → **portal CSV** |
| Sales tax | Stripe Tax (existing key), TaxJar (Professional), Avalara AvaTax | — | — |
| Receipts/OCR | Local Tesseract / PaddleOCR / local VLM (no key); AWS Textract, Azure DI, Google DocAI, Veryfi, Mindee (own keys) | — | — |
| Migration | QBO export zip, QBD IIF/Excel, Wave full-access token, Xero/Wave/FreshBooks exports | Xero own PKCE app (Starter tier), FreshBooks own app (HTTPS redirect trick) | QBO API (HTTPS non-loopback redirect + questionnaire) → **QBO export zip** |
| Suite | CardDAV, Nextcloud WebDAV, GitHub, GitLab, Linear, Jira, Jitsi (none needed), desktop mounts/portal | Google People/Drive/Calendar-Meet (own Desktop client, Testing mode), Microsoft Graph (own Entra app), Zoom (own General app) | — |

---

## 3. Category 1: bank feeds

### 3.1 Comparison: aggregators

| Aggregator | BYOK | Cost to user | Approval | History / freshness | Pending→posted | Effort |
|---|---|---|---|---|---|---|
| SimpleFIN Bridge | ★★★ | $1.50/mo or $15/yr, 25 institutions | none | ≤90 days per request; ~daily refresh | pending flag; id stability across posting undocumented | S |
| Plaid (Trial) | ★★ | free, 10 Items; paid tiers unpublished | identity check, "Personal use" option | 90 days default, up to 730; 1–4 refreshes/day | posted gets a **new id** + `pending_transaction_id` | M |
| Teller (developer) | ★★ | free ≤100 enrollments; production $0.10–$1.75 | none stated | undocumented | new id if not matched | M |
| MX | ★ | sales-led | production approval, IP allowlist | 90 days, ~24 h | update/delete on post | L |
| Finicity (Mastercard) | ★ | sales-led | partner onboarding | 24 months; daily, no automated refresh | — | L |
| Akoya | ★ | unpublished, possible setup fee | questionnaire + SOC 2 + legal | — | — | L |
| GoCardless BAD | ★ | — | **signups disabled** | EU/UK only | — | — |
| TrueLayer | ★ | sales-led | — | UK/EU only | — | — |

### 3.2 SimpleFIN Bridge (★★★, S), rank 1

- **Setup flow.** The user signs up, connects banks in SimpleFIN, and clicks *Create Setup Token* ([Actual Budget guide](https://actualbudget.org/docs/advanced/bank-sync/simplefin/)).
  - The app base64-decodes the token into a claim URL and POSTs to it **once**. The reply is an Access URL with Basic-auth credentials built in ([protocol](https://www.simplefin.org/protocol.html)).
  - The setup token is then spent, so the Access URL must go straight to the keyring ([developers](https://beta-bridge.simplefin.org/info/developers)).
  - No developer account, client secret, webview or redirect is involved. The user can revoke access at any time ([developers](https://beta-bridge.simplefin.org/info/developers)).
- **Data.**
  - Accounts carry `balance`, an optional `available-balance` and `balance-date`. Transactions carry `id`, `posted`, an optional `transacted_at` and a `pending` flag ([protocol](https://www.simplefin.org/protocol.html)).
  - Ids are unique within an account and never reused there ([protocol](https://www.simplefin.org/protocol.html)). **Whether an id survives pending→posted is undocumented**, so import posted only, as M48 does for Mercury.
- **Limits.**
  - At most 90 days per request. SimpleFIN recommends a ~5-day overlap. Keep to about 24 requests a day or fewer: tokens get warnings, then are disabled ([developers](https://beta-bridge.simplefin.org/info/developers)).
  - Banks refresh roughly daily ([Actual Budget](https://actualbudget.org/docs/advanced/bank-sync/simplefin/)).
  - Backfill beyond 90 days comes from statement files.
- **Cost.** $1.50/mo or $15/yr, 25 institutions and 25 apps ([bridge](https://beta-bridge.simplefin.org/)). No developer fee or approval ([developers](https://beta-bridge.simplefin.org/info/developers)).
- **Coverage and risk.**
  - US and Canada only ([institution search](https://beta-bridge.simplefin.org/search-institutions)).
  - Bank logins are held by **MX**, not SimpleFIN ([security](https://beta-bridge.simplefin.org/info/security)).
  - In May 2026 an MX incident exposed up to 39 users' transactions to each other for about 4 hours ([security](https://beta-bridge.simplefin.org/info/security)). Disclose this in the setup dialog.
- **Fit.** It is one GET with Basic auth. FITID = `simplefin:<account-id>:<txn-id>`. Link each SimpleFIN account to a books account exactly like Mercury.

### 3.3 Plaid (★★, M): the backstop

- **Trial plan.**
  - For US/Canada teams created on or after 2026-04-15, *Trial* replaced Limited Production. It is capped at **10 production Items** ([billing](https://plaid.com/docs/account/billing/)).
  - It includes Transactions and Balance and OAuth banks such as Chase, BofA, Wells, Capital One, Citi and Amex. There is no business registration or security questionnaire, and a "Personal use" option exists. `/item/remove` does not free a slot ([Trial FAQ](https://support.plaid.com/hc/en-us/articles/39994173227159-What-is-the-Plaid-Trial-plan), snippet).
  - Plaid frames Trial as for "hobbyist use, or evaluating" ([billing](https://plaid.com/docs/account/billing/)). **Whether ongoing business bookkeeping fits the Trial terms is unverified.**
- **Auth for a desktop app.**
  - `client_id` and `secret` are server-side credentials ([glossary](https://plaid.com/docs/quickstart/glossary/)). In BYOK the user's own secret sits in their own keyring, so nothing is distributed.
  - Production redirect URIs must be registered HTTPS. HTTP localhost works in Sandbox only, and custom schemes are not supported ([OAuth guide](https://plaid.com/docs/link/oauth/)). Webview Link is deprecated ([webview](https://plaid.com/docs/link/webview/)).
  - **Hosted Link** avoids all of that. Open `hosted_link_url` in the system browser, then poll `/link/token/get` for the `public_token` (available for 6 hours). No webhook is needed ([Hosted Link](https://plaid.com/docs/link/hosted-link/)).
- **Data.**
  - `/transactions/sync` is cursor-based, with 90 days by default and up to 730 via `days_requested`. Webhooks are optional ([transactions](https://plaid.com/docs/transactions/)).
  - A posted transaction gets a **new id** with `pending_transaction_id`, which can be null ([data](https://plaid.com/docs/transactions/transactions-data/)).
  - Limits: `/transactions/sync` 50/min per Item; `/transactions/refresh` is billed ([rate limits](https://plaid.com/docs/errors/rate-limit-exceeded/), [transactions](https://plaid.com/docs/transactions/)).
- **Cost.** Trial is free. Pay-as-you-go bills Transactions monthly per Item, and rates are unpublished ([billing](https://plaid.com/docs/account/billing/), [pricing](https://plaid.com/pricing/)).

### 3.4 Teller (★★, M)

- **Auth.** The user's own certificate and key (`teller.zip`), with mTLS required. Teller says never to distribute the private key ([auth](https://teller.io/docs/api/authentication)).
- **Enrollment.** Through the Teller Connect JS widget ([connect guide](https://teller.io/docs/guides/connect)). QindaBooks would have to host a local page (QtWebEngine or a loopback page in the browser). **Whether Teller restricts the widget's origin is unverified.**
- **Cost and coverage.** Free for 100 live enrollments; production $0.10–$1.75 per transaction or enrollment; more than 7,000 institutions ([teller.io](https://teller.io/)).
- **Rate limits.** Not published ([api](https://teller.io/docs/api)).
- **Data.** Transaction `status` is pending or posted. A posting that changes a lot gets a new id. Teller advises re-fetching 7–10 days ([transactions](https://teller.io/docs/api/account/transactions)).
- **Verdict.** Viable, but the web widget and mTLS make it heavier than SimpleFIN for the same banks.

### 3.5 MX, Finicity, Akoya (★): skip; the alternative is SimpleFIN or Plaid Trial

- **MX.**
  - The Connect widget URL comes from a server call with secret credentials, and IP whitelisting is required. Production is by request ([docs](https://docs.mx.com/connect/), [checklist](https://docs.mx.com/resources/integration-checklist/)).
  - OAuth banks need production access plus a registration form, and "Do not open the OAuth window in a WebView" ([OAuth guide](https://docs.mx.com/resources/oauth-guide/)).
  - The free developer account is limited to 100 users ([signup](https://dashboard.mx.com/sign_up)).
- **Finicity / Mastercard Open Finance.**
  - Partner ID, secret and app key; free "Test Drive" ([README](https://github.com/Mastercard/open-banking-us-openapi/blob/main/README.md)).
  - Daily refresh, and automated Refresh calls are not permitted ([API reference](https://developer.mastercard.com/open-finance-us/documentation/api-reference/)).
  - Pricing through sales ([G2](https://www.g2.com/products/mastercard-open-banking/pricing), secondary).
- **Akoya.** Production needs a questionnaire, a SOC 2 report or security questionnaire, and a legal agreement ([production access](https://docs.akoya.com/docs/guide-for-production-access)). Registered HTTPS redirects; localhost is sandbox only ([redirects](https://docs.akoya.com/docs/redirect-uris-explained)).

### 3.6 Non-US: GoCardless Bank Account Data and TrueLayer (★)

- **GoCardless Bank Account Data** (formerly Nordigen): "New signups … are currently disabled" ([GoCardless](https://bankaccountdata.gocardless.com/new-signups-disabled)). Disabled since July 2025, EU/UK only ([Actual Budget](https://actualbudget.org/docs/advanced/bank-sync/gocardless/)).
- **TrueLayer:** UK/EU coverage ([truelayer.com/data](https://truelayer.com/data/)). Production through sales ([console](https://support.truelayer.com/hc/en-us/categories/360000239517-Console)). 90-day reconfirmation ([connections](https://docs.truelayer.com/docs/connections)).
- **Alternative:** statement files. Wise's statement endpoint also emits CAMT.053, MT940 and QIF (§3.8).

### 3.7 OFX Direct Connect and the CFPB §1033 outlook

- **OFX Direct Connect is effectively gone.**
  - GnuCash's wiki (2025-11-30) calls it "historical interest only" ([wiki](https://wiki.gnucash.org/wiki/OFX_Direct_Connect_Bank_Settings)).
  - Quicken says Chase, Amex and Schwab moved to Express Web Connect+ ([Quicken](https://www.quicken.com/support/how-quicken-connects-your-bank)).
  - Bank of America ended OFX on 2025-09-30 ([Quicken community](https://community.quicken.com/discussion/7966609/9-30-25-bank-of-america-ofx-discontinued), secondary).
  - OFX Home shut down in 2022 ([gnucash-user](https://lists.gnucash.org/pipermail/gnucash-user/2021-April/096354.html)).
  - **Keep the existing QFX/OFX/QBO file import; don't build Direct Connect.**
- **§1033 won't bring free direct access soon.**
  - The rule's compliance dates would have started 2026-04-01 ([CFPB](https://www.consumerfinance.gov/personal-financial-data-rights/)).
  - The CFPB opened a reconsideration on 2025-08-22 that includes fees ([Federal Register](https://www.federalregister.gov/documents/2025/08/22/2025-16139/personal-financial-data-rights-reconsideration)).
  - A federal court enjoined enforcement on 2025-10-29 ([ABA Banking Journal](https://bankingjournal.aba.com/2025/11/kentucky-federal-court-enjoins-cfpb-from-enforcing-current-1033-final-rule/), secondary).
  - A rewrite went to OIRA on 2026-08-06 and is expected to allow fees above a free threshold ([Consumer Finance Monitor](https://www.consumerfinancemonitor.com/2026/08/06/cfpb-sends-new-section-1033-open-banking-proposal-to-oira-for-review/), [outlook](https://www.consumerfinancemonitor.com/2026/06/26/open-banking-regulation-in-2026-federal-regulation-resurfaces-as-states-bring-data-sharing-into-focus/), secondary).
  - JPMorgan and Plaid signed a pricing deal in September 2025 ([Open Banking Tracker](https://www.openbankingtracker.com/guides/open-banking-data-access-fees), secondary). Expect aggregator prices to rise, not fall.

### 3.8 Direct-API banks and fintechs

| Provider | BYOK | Token | Read-only? | Pending? | Receipts / GL | Rate limit | Effort |
|---|---|---|---|---|---|---|---|
| Mercury (done) | ★★★ | dashboard token | yes | yes | GL code, attachments | not found | — |
| Brex | ★★★ | admin user token, scoped | yes (`*.readonly`) | **settled only** | receipts (15-min URLs) | 1,000/60 s | S–M |
| Ramp | ★★★ | own developer app, client credentials | scoped | yes (`state`) | receipts, GL fields | 200/10 s per IP | M |
| BILL Spend & Expense | ★★★ | admin API token | not found | AUTH vs CLEAR | custom fields, receipts | 60/min | S–M |
| BILL AP/AR | ★★★ | **sync token** (cannot pay/approve) | effectively | — | bills, invoices, docs | 20k/h, 3 concurrent | M |
| Wise Business | ★★★ | personal token (2FA) | no | — | statement formats | unverified | M |
| Relay, Novo, Bluevine, Found, Lili | ★ | no public API | — | — | — | — | file/SimpleFIN |
| Amex, Chase, Capital One | ★ | partners only | — | — | — | — | file/SimpleFIN/Plaid |

**Mercury** (baseline)

- **Tokens.** Read Only tokens need no IP allowlist ([getting started](https://docs.mercury.com/docs/getting-started.md)).
- **Token lifetime.** A token unused for **45 days is deleted**, and an over-privileged one is downgraded ([token policy](https://docs.mercury.com/docs/api-token-security-policies.md)). QindaBooks should warn when a connection hasn't synced in about 30 days (see §11).
- **Partner OAuth.** Available by application only ([OAuth](https://docs.mercury.com/docs/integrations-with-oauth2.md)).

**Brex**

- **Token.** An admin creates a user token with read-only scopes ([auth](https://developer.brex.com/guides/authentication), [scopes](https://developer.brex.com/guides/roles_permissions_scopes)). It expires after 90 days unused ([FAQ](https://developer.brex.com/docs/faq)).
- **Transactions.**
  - **Settled transactions only**, which fits the import-posted-only rule ([FAQ](https://developer.brex.com/docs/faq)).
  - Card transactions have a stable `id` and a `posted_at_start` filter. Brex itself recommends overlapping poll windows and deduping by id ([list card transactions](https://developer.brex.com/openapi/transactions_api/transactions/listprimarycardtransactions.md)).
- **Receipts.** Via `/v1/expenses?expand[]=receipts.download_uris`; the links expire after 15 minutes ([expenses](https://developer.brex.com/openapi/expenses_api/expenses/listexpenses.md)).
- **Limits and gating.** 1,000 requests per 60 s ([rate limits](https://developer.brex.com/guides/rate_limits)). The Accounting API is Alpha and invite-only ([accounting](https://developer.brex.com/openapi/accounting_api.md)).

**Ramp**

- **Credentials.** The user creates a developer app with *Client credentials* and scopes such as `transactions:read` ([support](https://support.ramp.com/accessing-the-developer-api)).
- **Token lifetime.** About 10 days with no refresh token; request a new one ([authorization](https://docs.ramp.com/llms-guides/authorization.txt)).
- **Why client credentials.** The authorization-code flow requires HTTPS redirects, so client credentials is the BYOK path ([authorization](https://docs.ramp.com/llms-guides/authorization.txt)).
- **Data.** Transactions have a UUID `id`, `state` (PENDING / CLEARED / …), `accounting_field_selections`, `receipts[]` and `statement_id` ([OpenAPI](https://docs.ramp.com/openapi/developer-api.json)).
- **Limits.** 200 requests per rolling 10 s per IP ([rate limiting](https://docs.ramp.com/llms-guides/rate-limiting.txt)).

**BILL**

- **Spend & Expense.**
  - An ADMIN-generated `apiToken` ([auth](https://developer.bill.com/docs/authentication-with-api-token.md), [production](https://developer.bill.com/docs/get-started-in-production.md)).
  - Transactions are AUTHORIZATION / DECLINE / CLEAR ([transactions](https://developer.bill.com/docs/transaction-management.md)).
  - 60 calls/min per token ([limits](https://developer.bill.com/docs/api-rate-limits.md)).
- **AP/AR.**
  - The **sync token** can read bills, invoices, vendors, customers and payments, but cannot pay, void or approve ([token sign-in](https://developer.bill.com/docs/token-based-sign-in.md)).
  - 3 concurrent requests per organization ([limits](https://developer.bill.com/docs/api-rate-limits.md)).

**Wise Business**

- **Token and gating.** A personal token requires 2FA to create. Anything beyond it needs a partnership ([personal token](https://docs.wise.com/guides/developer/auth-and-security/personal-api-token.md)).
- **Statements.** Available for US-based accounts in JSON, CSV, PDF, XLSX, CAMT.053, MT940 or QIF, up to 469 days per request, with `referenceNumber` as the id ([balance statement](https://docs.wise.com/api-reference/balance-statement/balancestatementget.md)).

**No-API banks (★)**

- **Relay:** "Relay does not currently offer a public API" (2026-08-11); it offers Plaid/Yodlee connections ([Relay](https://relayfi.com/hc/en-us/articles/48653415496468-Does-Relay-have-public-APIs/), [integrations](https://relayfi.com/integrations/)).
- **Novo:** CSV export ([Novo](https://www.novo.co/help/how-do-i-export-my-transaction-data)).
- **Bluevine:** CSV has **no transaction id** ([Bluevine](https://support.bluevine.com/s/article/How-do-I-export-my-account-transactions-to-a-CSV-file)); apps connect through Plaid ([help](https://www.bluevine.com/help-center/sending-payments/connecting-financial-apps)).
- **Found:** CSV by email ([Found](https://found.com/help/taxes/how-do-i-export-my-bookkeeping-details)).
- **Lili:** CSV with no id column ([Lili](https://support.lili.co/hc/en-us/articles/7003715263642-How-do-I-export-my-account-s-transaction-history)).
- **Amex:** the account API is PSD2-only ([developer](https://developer.americanexpress.com/products/account-and-transaction-api-public/overview), snippet). Cards download as QFX/QBO/CSV ([Amex FAQ](https://www.americanexpress.com/us/customer-service/faq.download-export-transactions-software.html)).
- **Chase:** FDX API for existing partners only ([Chase developer](https://developer.chase.com/products/aggregation-fdx/), snippet).
- **Capital One:** DevExchange for approved partners ([developer](https://developer.capitalone.com/), snippet).
- **Alternative for all of these:** SimpleFIN (whether each bank is covered is unverified; check its institution search), or file import. CSVs without ids fall back to the existing (date, amount, payee, occurrence) dedup.

**MCP servers (speculative)**

- Lili's, Mercury's and Ramp's MCP servers publish OAuth metadata with dynamic client registration and PKCE ([Lili metadata](https://mcp.lilibanking.com/.well-known/oauth-authorization-server), [Mercury metadata](https://mcp.mercury.com/.well-known/oauth-authorization-server)).
- In theory a desktop client could register itself there. MCP tools are not a stable data API, and loopback acceptance is untested. **Not recommended.**

---

## 4. Category 2: payments, commerce and software sales

### 4.1 Comparison

| Provider | BYOK | Credential | Payout ↔ txn link | History / limits | Fees (US) | Effort |
|---|---|---|---|---|---|---|
| PayPal | ★★★ | own REST app, client credentials | T0400 withdrawal rows; no member list | 31-day windows, 3 years back, ≤3 h lag | Checkout / Venmo 3.49% + 49¢ | M |
| Square | ★★★ | personal access token (prod OK) | **payout entries → payment_id** | payouts from 2021 | online 2.9% + 30¢ | S–M |
| Shopify | ★★★ | Dev Dashboard app, client credentials (24 h token, same org) | **balance txns → payout, order** | cost-based GraphQL | — | M |
| Etsy | ★★ | Seller App + OAuth PKCE | ledger disbursement entries | per-key daily/sec limits | 6.5% + processing | M–L |
| WooCommerce + WooPayments | ★★★ | consumer key + secret | **deposit_id** | host-dependent | 2.9% + 30¢ | S |
| Gumroad | ★★★ | "Generate access token" | **payout → transactions** | — | 10% + 50¢ (MoR) | S |
| Lemon Squeezy | ★★★ | API key | no payouts API | 300/min | 5% + 50¢ (MoR) | S, sunset risk |
| Paddle | ★★★ | API key (≤1 yr) | payout reconciliation report | 240/min | 5% + 50¢ (MoR) | M |
| Eventbrite | ★★★ | private token | no payout API | 2,000/h | — | S–M |
| Apple App Store | ★★★ | .p8 key → ES256 JWT | monthly finance reports | — | — | M |
| Google Play | ★★★ | service account → GCS CSVs | monthly earnings | — | — | M |
| Patreon | ★★★ | creator token | none | — | — | S, low value |
| Venmo, Zelle, Cash App, Ko-fi | ★ | none usable | bank feed | — | — | CSV profile |

### 4.2 PayPal (★★★, M), rank 2

- **Credentials.** The owner creates an app under *Apps & Credentials* in the developer dashboard; live use needs a Business account ([REST](https://developer.paypal.com/api/rest/)). The app exchanges its client ID and secret for a bearer token with client credentials ([auth](https://developer.paypal.com/api/rest/authentication/)). No redirect, no review.
- **Transaction search.**
  - `GET /v1/reporting/transactions` (scope `…/reporting/search/read`) returns `transaction_id`, `paypal_reference_id`, `transaction_event_code`, gross `transaction_amount`, `fee_amount` and `ending_balance` ([OpenAPI](https://github.com/paypal/paypal-rest-api-specifications/blob/main/openapi/reporting_transactions_v1.json)).
  - Limits: 31 days per request, 3 years back, `page_size` ≤ 500 ([OpenAPI](https://github.com/paypal/paypal-rest-api-specifications/blob/main/openapi/reporting_transactions_v1.json)); up to 3 hours before a transaction appears ([docs](https://developer.paypal.com/docs/api/transaction-search/v1/)).
  - History older than 3 years: statements only ([help](https://www.paypal.com/us/cshelp/article/why-cant-i-access-transaction-history-greater-than-3-years--ts2241)).
- **Mapping to the M48 model.**
  - A "PayPal (USD)" clearing account.
  - T00xx payments become +gross/−fee.
  - T04xx withdrawals (T0400) become transfers to the bank, matched to the bank deposit.
  - T11xx refunds and reversals, and T12xx adjustments, go to review.
  - T15xx/T21xx holds and releases ([T-codes](https://developer.paypal.com/reports/reference/t-codes)).
  - **An unknown T-code goes to review, never dropped.**
- **Fees** (page updated 2026-09-01): PayPal Checkout and Pay with Venmo 3.49% + 49¢; charity 1.99% + 49¢; standard transfer to bank free ([fees](https://www.paypal.com/us/business/paypal-business-fees)).
- **Unverified.**
  - Whether *Transaction Search* must be ticked on the app.
  - Whether Venmo-checkout payments appear in Transaction Search (expected, since they are priced as PayPal Checkout).

### 4.3 Square (★★★, S–M), rank 7

- **Token.** A personal access token is "suitable for production use" for your own account. It is full-access and unscoped, so store it in the keyring and say so ([access tokens](https://developer.squareup.com/docs/build-basics/access-tokens)).
- **OAuth.** Desktop OAuth would need PKCE, and production redirects must be HTTPS ([OAuth](https://developer.squareup.com/docs/oauth-api/overview)). That makes the personal token the BYOK path.
- **Payouts.** `ListPayouts`, `GetPayout` and `ListPayoutEntries` return gross, fee and net per entry, with status SENT → PAID / FAILED, and no data before 2021 ([Payouts API](https://developer.squareup.com/docs/payouts-api/overview)).
  - Entry types include CHARGE, REFUND, FEE and DISPUTE ([PayoutEntry](https://developer.squareup.com/reference/square/objects/PayoutEntry)).
  - A charge entry carries `payment_id` ([ChargeDetail](https://developer.squareup.com/reference/square/objects/PaymentBalanceActivityChargeDetail)).
- **Payments and orders.**
  - Payments carry `processing_fee` and `order_id` and are eventually consistent ([ListPayments](https://developer.squareup.com/reference/square/payments-api/list-payments)).
  - Orders carry `total_tax_money`, so Square gives us sales tax collected ([Order](https://developer.squareup.com/reference/square/objects/Order)).
- **Limits.** Dynamic; back off on 429 ([errors](https://developer.squareup.com/docs/build-basics/general-considerations/handling-errors)).
- **Fees.** In person 2.6% + 15¢, online 2.9% + 30¢, invoices 3.3% + 30¢ ([fees](https://squareup.com/help/us/en/article/5068-what-are-square-s-fees)).

### 4.4 Shopify (★★★ with caveats, M)

- **Creating the app.** Since 2026-01-01, new custom apps can't be created in the admin; existing ones keep working ([changelog](https://changelog.shopify.com/posts/legacy-custom-apps-can-t-be-created-after-january-1-2026)). A new Dev Dashboard app fetches a token by `client_credentials` that lasts 86,399 s, **only when the app and the store are in the same organization** ([guide](https://shopify.dev/docs/apps/build/dev-dashboard/get-api-access-tokens)).
  - **Whether that works on a paid live store (not just a dev store) is unverified. Test it first.**
- **Review.** Custom apps skip the public review ([help](https://help.shopify.com/en/manual/apps/app-types/custom-apps)).
- **Data.**
  - `ShopifyPaymentsBalanceTransaction` has `amount`, `fee`, `net`, `associatedPayout` and `associatedOrder` ([object](https://shopify.dev/docs/api/admin-graphql/latest/objects/ShopifyPaymentsBalanceTransaction)).
  - Balance transactions can be filtered by `payments_transfer_id` ([account](https://shopify.dev/docs/api/admin-graphql/latest/objects/ShopifyPaymentsAccount)), and payouts need `read_shopify_payments_payouts` ([payout](https://shopify.dev/docs/api/admin-graphql/latest/objects/ShopifyPaymentsPayout)).
  - This is a textbook fit for the clearing-account model.

### 4.5 Etsy (★★, M–L)

- **App tier.** The "Seller App" tier (own shop only) is "approved within minutes, with no manual review queue" ([docs](https://developers.etsy.com/documentation)).
- **Auth.**
  - OAuth 2.0 with PKCE is mandatory, with exact-match redirects. Access tokens last 1 hour and refresh tokens 90 days. Every call carries `x-api-key: keystring:shared_secret` ([authentication](https://developers.etsy.com/documentation/essentials/authentication)).
  - Loopback redirects are reported to work only in community threads ([discussion](https://github.com/etsy/open-api/discussions/1217), secondary).
- **Data.** Payment-account ledger entries have `entry_id`, `sequence_number`, running `balance`, `reference_id` and `parent_entry_id`, under scope `transactions_r` ([OpenAPI](https://www.etsy.com/openapi/generated/oas/3.0.0.json)).
- **Fees.** $0.20 listing + 6.5% transaction fee + processing ([fees](https://help.etsy.com/hc/en-us/articles/115014483627-What-are-the-Fees-and-Taxes-for-Selling-on-Etsy)).

### 4.6 WooCommerce (★★★, S)

- **Keys.** A read key is made under *Settings ▸ Advanced ▸ REST API*. It uses Basic auth over HTTPS, or OAuth 1.0a one-legged over HTTP ([docs](https://woocommerce.github.io/woocommerce-rest-api-docs/#authentication)).
- **Data.** WooPayments adds deposits and a transactions report with fees, net and `deposit_id` ([WooPayments API](https://automattic.github.io/woocommerce-payments/)).
- **Fees.** 2.9% + 30¢ ([fees](https://woocommerce.com/document/woopayments/fees-and-debits/fees/)).

### 4.7 Software-sales platforms (for the studio)

**Merchant of record (MoR).** Gumroad, Lemon Squeezy and Paddle are MoR, which means *they* collect and remit sales tax. For the books that makes tax a pass-through, and it is a strong reason to route the studio's sales through one ([Gumroad pricing](https://gumroad.com/pricing), [Lemon Squeezy pricing](https://www.lemonsqueezy.com/pricing), [Paddle pricing](https://www.paddle.com/pricing)).

- **Gumroad** (★★★, S).
  - "Generate access token" on the owner's own application ([API](https://gumroad.com/api); text read from the open-source UI, [source](https://github.com/antiwork/gumroad/tree/main/app/javascript/components/ApiDocumentation)).
  - Sales carry fees and tax. The Payouts API can list a payout's transactions. Fees are 10% + 50¢, and Gumroad has been MoR since 2025-01-01 ([pricing](https://gumroad.com/pricing)).
- **Lemon Squeezy** (★★★, S, but not recommended).
  - API key from Settings, 300 req/min, no payouts endpoint ([API](https://docs.lemonsqueezy.com/api), [getting started](https://docs.lemonsqueezy.com/guides/developer-guide/getting-started)).
  - Stripe acquired it (2024-07-26), and the 2026-01-28 update describes a migration path to **Stripe Managed Payments** ([acquisition](https://www.lemonsqueezy.com/blog/stripe-acquires-lemon-squeezy), [2026 update](https://www.lemonsqueezy.com/blog/2026-update), [Managed Payments](https://docs.stripe.com/payments/managed-payments)). Stripe Managed Payments reaches QindaBooks through the existing Stripe connection.
- **Paddle** (★★★, M).
  - Scoped keys that expire after 90 days by default, at most 1 year ([auth](https://developer.paddle.com/api-reference/about/authentication)). QindaBooks needs a re-key reminder.
  - Transactions carry totals including fee and earnings ([transactions](https://developer.paddle.com/api-reference/transactions/overview)). Payouts come only through the async `payout_reconciliation` CSV report ([reports](https://developer.paddle.com/api-reference/reports/create-report), [guide](https://developer.paddle.com/build/finance/generate-reports)).
  - 240 requests/min per IP ([rate limiting](https://developer.paddle.com/api-reference/about/rate-limiting)).
- **Apple App Store** (★★★, M). A Team API key (.p8) signs an ES256 JWT that expires within 20 minutes ([keys](https://developer.apple.com/documentation/appstoreconnectapi/creating-api-keys-for-app-store-connect-api), [tokens](https://developer.apple.com/documentation/appstoreconnectapi/generating-tokens-for-api-requests)). `GET /v1/financeReports` returns monthly FINANCIAL / FINANCE_DETAIL reports ([financeReports](https://developer.apple.com/documentation/appstoreconnectapi/get-v1-financereports)).
- **Google Play** (★★★, M). Monthly earnings CSVs sit in the developer's GCS bucket and are read by a service account with "View financial data". Don't rely on a fixed column count ([help](https://support.google.com/googleplay/android-developer/answer/6135870)).
- **Eventbrite** (★★★, S–M; ticketing for the center).
  - A private token from the API Keys page ([auth](https://www.eventbrite.com/platform/docs/authentication)).
  - Order `costs` are split into gross, eventbrite_fee, payment_fee and tax ([orders](https://www.eventbrite.com/platform/docs/order-lookup)); 2,000 calls/h ([rate limits](https://www.eventbrite.com/platform/docs/rate-limits)).
  - No payouts API found. The text of these three pages was read from the site's docs bundle rather than the rendered page.
- **Patreon** (★★★, low value): no payouts or fees endpoints ([docs](https://docs.patreon.com/)).
- **Ko-fi** (★): webhook out only ([help](https://help.ko-fi.com/hc/en-us/articles/360004162298-Does-Ko-fi-have-an-API-or-webhook)).

### 4.8 No-API payment apps (★): the alternative is the bank feed plus a statement profile

- **Zelle.** No merchant API. Money lands directly in the enrolled bank account, so SimpleFIN or Mercury already sees it ([Zelle FAQ](https://www.zellepay.com/faq/using-zelle-small-business)). Nothing to build.
- **Venmo business profile.** Monthly CSV statements ([help](https://help.venmo.com/cs/articles/transaction-history-vhel281)). Fees 1.9% + 10¢ ([fees](https://venmo.com/resources/our-fees/)).
- **Cash App for Business.** 2.6% + 15¢ ([fees](https://cash.app/help/us/en-us/6521-cash-for-business-fees), snippet). No public API found. Statement export format unverified ([statements](https://cash.app/help/us/en-us/3055-view-account-statements)).

---

## 5. Category 2b: nonprofit donations (the community center)

### 5.1 Comparison

| Platform | BYOK | API cost | Fees / tips / payouts in API | Rate limit | Fallback | Effort |
|---|---|---|---|---|---|---|
| Givebutter | ★★★ admin key | free | fee, fee_covered, payout_id, fund/campaign codes; `/payouts` | 500/min | transaction + payout CSVs | S–M |
| Zeffy | ★★★ admin key (beta) | free | gross = net (no fees); fund code; **no payouts** | 100/min | payout CSV | S + CSV |
| Donorbox | ★★★ email + key | **$17/mo add-on** | processing fee, `stripe_charge_id`; no payouts | none published | Stripe join | S |
| GoFundMe Pro (Classy) | ★★★ self-serve client credentials | needs a GFM Pro subscription | full fee breakdown, `/payouts/{paypal\|stripe}` | 1,800/min | — | M |
| Every.org | ★ push webhook only | — | CSV | — | CSV | S |
| Benevity | ★ no charity API | — | rich disbursement CSV | — | CSV | S |
| PayPal Giving Fund / Meta | ★ | — | dashboard/CSV | — | CSV | S |
| Bloomerang / Neon CRM | ★★★ keys | unverified | CRM gifts, not payouts | Neon: 5 concurrent | — | M |
| Little Green Light | ★ (approval, terms) | — | CRM | 300/5 min | — | M, gated |
| Kindful | retired | — | — | — | — | skip |

### 5.2 Notes

- **Givebutter** (★★★, rank 4).
  - Key: admin-only, under *Settings ▸ Developers ▸ API*, shown once ([help](https://help.givebutter.com/en/articles/5489015-how-to-access-the-givebutter-public-api-key)); Bearer auth ([auth](https://docs.givebutter.com/api-reference/authentication.md)).
  - Transactions carry `amount`, `fee`, `fee_covered`, `donated`, `payout`, `payout_id`, `fund_code`, `campaign_code`, `tax_deductible_amount`, `fair_market_value_amount` and `check_deposited_at`, and filter on `transactedAfter` / `updatedAfter` ([transactions](https://docs.givebutter.com/api-reference/transactions/list-all-transactions.md)).
  - Payouts have their own endpoint ([payouts](https://docs.givebutter.com/api-reference/payouts/list-all-payouts.md)); 500 requests/min ([rate limits](https://docs.givebutter.com/api-reference/rate-limits.md)).
  - Platform fee: 0% with tips on, 3% with tips off, plus 2.9% + 30¢ processing ([pricing](https://givebutter.com/pricing)).
  - **The fee fields aren't documented semantically; test against a real account.**
- **Zeffy** (★★★).
  - Only the admin can create keys ([help](https://support.zeffy.com/get-started-with-the-zeffy-api-yourg)). The API is free, beta and read-only except contacts and offline payments ([API](https://www.zeffy.com/integration/api)).
  - Payments carry `eligible_amount` (the receiptable part), `fund.code` and refunds ([docs](https://www.zeffy.com/api/docs)). Zeffy takes no fees; donor tips go to Zeffy ([pricing](https://www.zeffy.com/pricing)).
  - Payouts are weekly, and a payout CSV lists each payout's transactions ([help](https://support.zeffy.com/zeffy-payouts-schedules-amounts-and-reports-ajuec)). Import the API for gifts and the CSV profile for deposits.
- **Donorbox** (★★★).
  - Basic auth with the org email and a key; the add-on costs $17/mo ([help](https://donorbox.zendesk.com/hc/en-us/articles/360061902812-How-to-set-up-your-Donorbox-API-for-the-API-Zapier-Integration)).
  - It processes through the org's **own Stripe/PayPal** and deducts its platform fee from Stripe donations ([fees](https://donorbox.zendesk.com/hc/en-us/articles/360020292992-How-much-does-Donorbox-cost-What-are-its-fees)).
  - **BYOK alternative at $0:** skip the add-on and read Donorbox gifts from the existing Stripe balance transactions. The designation or campaign comes from the Stripe charge's metadata. Whether Donorbox puts it there is unverified; check a live charge.
- **GoFundMe Pro** (★★★).
  - Self-serve *Apps & Integrations ▸ API* gives a client ID and secret ([help](https://prosupport.gofundme.com/hc/en-us/articles/37288767751707-Intro-the-GoFundMe-Pro-API)). The spec defines a client-credentials flow, so no redirect is needed ([OpenAPI](https://developers.gofundme.com/pro/docs/specs/apiv2-public.json)).
  - Transactions carry `gross_amount`, `classy_fees_amount`, `processor_fee_amount`, `fee_on_top` and `payout_id`; payouts are queried by date; 1,800 requests/min ([OpenAPI](https://developers.gofundme.com/pro/docs/specs/apiv2-public.json)).
  - Pricing is a custom subscription ([pricing](https://pro.gofundme.com/c/pricing/)).
- **Every.org** (★).
  - "does not provide programmatic donations or disbursements via API" ([intro](https://docs.every.org/docs/intro)); the webhook needs an HTTPS endpoint ([webhook](https://docs.every.org/docs/webhooks/nonprofit-webhook)).
  - Gifts arrive as grants from a DAF: give Every.org hard credit and the donor soft credit ([support](https://support.every.org/hc/en-us/articles/5711616989843-How-do-we-record-and-acknowledge-donations-received-from-Every-org-donors)).
  - Disbursement options vary; Network for Good takes 1.50% ([disbursements](https://support.every.org/hc/en-us/articles/360061887233-Disbursements-overview)). Use a CSV profile.
- **Benevity** (★).
  - The developer API is for corporate clients ([developer](https://developer.benevity.org/)).
  - The Causes Portal CSV has Disbursement ID, Transaction ID, gross, match, Cause Support Fee and Merchant Fee ([glossary](https://causeshelp.benevity.org/hc/en-us/articles/47969079553300-Reporting-glossary)).
  - The format changed in 2026 ([help](https://causeshelp.benevity.org/hc/en-us/articles/47967654396180-How-do-I-create-a-disbursement-report-and-download-it)). Use a CSV profile.
- **PayPal Giving Fund / Meta.**
  - The dashboard's Activity tab lists donors who opted in ([PayPal](https://www.paypal.com/us/cshelp/article/how-do-i-access-my-charity-dashboard-and-view-donor-information-help208)). The 30-day CSV windows are a snippet.
  - US Meta fundraisers require PayPal Giving Fund enrollment: 1.99% + 49¢, paid out 15–90 days later ([Meta](https://www.facebook.com/help/android-app/286231418422971)).
- **Venmo charity profile.** 1.9% + 10¢ ([Venmo](https://help.venmo.com/cs/articles/charity-profile-donation-fees-vhel159)); requires a PayPal Business account ([setup](https://help.venmo.com/cs/articles/venmo-charity-profile-setup-guide-vhel366)).
- **CRMs.**
  - Bloomerang: admin private key ([REST](https://bloomerang.com/product/integrations-data-management/api/rest-api/)). Kindful is being wound down into Bloomerang ([Bloomerang](https://help.bloomerang.com/en/articles/13382594-kindful)).
  - Neon: Basic auth with org ID and key; limits are 5 concurrent requests per endpoint ([auth](https://developer.neoncrm.com/authentication/), [limits](https://developer.neoncrm.com/api-v2-resources/rate-limits/)).
  - Little Green Light: the key is issued only after LGL approves, and credentials must not be embedded in open-source code ([terms](https://www.littlegreenlight.com/api-terms/)). **Review that clause against our GPL licence** before any work.
  - Blackbaud SKY: unverified (the docs are JavaScript-only, and the public docs repository is archived).

### 5.3 What the books must carry for Form 990 and donors

- **Part VIII** splits contributions by type: federated campaigns, membership dues, fundraising events, related organizations, government grants, all other, and noncash ([i990](https://www.irs.gov/instructions/i990)).
  - Every donation import should therefore set a **revenue line code** and keep **fund/campaign** and **restriction** tags.
- **Schedule B** lists contributors at ≥ $5,000. Organizations meeting the 509(a)(1)/170(b)(1)(A)(vi) test list only gifts above the greater of $5,000 or 2% of line 1h ([i990sb](https://www.irs.gov/instructions/i990sb)).
  - Donor identity must survive the import.
  - For DAF and intermediary platforms (Every.org, PayPal Giving Fund, Benevity), the contributor of record is the intermediary.
- **Written acknowledgment** is required for gifts of $250 or more, stating any goods or services received ([IRS](https://www.irs.gov/charities-non-profits/charitable-organizations/charitable-contributions-written-acknowledgments)). Keep `fair_market_value_amount` (Givebutter) and `eligible_amount` (Zeffy).

---

## 6. Category 3: payroll, contractors and 1099s

### 6.1 Comparison

| Provider | BYOK | Customer API? | Fallback the books can import | Rate limit | Effort |
|---|---|---|---|---|---|
| Gusto | ★ | no: "do not support API access for Gusto customers" | Payroll Journal CSV / General Ledger Mapper | 200/min per grant (partners) | CSV S–M |
| ADP RUN | ★ | none found; the API Central listing names Workforce Now | GL file (Excel), 6 years of reports | — | M |
| Rippling | ★★★ | customer API tokens | — | unverified | M |
| OnPay | ★ | no public API found | GL Summary xlsx, IIF | — | S–M |
| Justworks | ★ | partner-only OAuth | pushes to QBO/Xero only; no CSV found | — | blocked |
| Deel | ★★★ | org / personal tokens | — | 5 req/s per org | M |

**The shape of a payroll journal entry.** Gusto's GL report shows it. Each payroll has a `check_date`, with rows such as `RegularWages`, `EmployerTax`, `DebitNetPay` and `DebitTax`.

- Debits: wages plus employer taxes.
- Credits: net pay plus a tax debit that combines employee withholdings and employer tax. The two credits are the two bank withdrawals.
- Source: [Gusto GL report](https://docs.gusto.com/embedded-payroll/docs/retrieve-a-general-ledger-report.md).

That is exactly one balanced journal entry per payroll. The two credit lines are the two bank debits that the M48 matcher should pair up.

- **Gusto.**
  - No customer API ([app integrations intro](https://docs.gusto.com/app-integrations/docs/introduction)). Partners go through pre-approval plus security and QA reviews ([auth](https://docs.gusto.com/app-integrations/docs/authentication)).
  - The fallback is the *General Ledger Mapper* CSV mapped to the chart ([support](https://support.gusto.com/article/221020155120720/map-payroll-data-and-download-reports-with-the-general-ledger-mapper), snippet).
  - Gusto runs an MCP server with dynamic client registration and scopes `payrolls:read` and `company_reports:read` ([metadata](https://mcp.api.gusto.com/.well-known/oauth-authorization-server)). Its terms for third-party desktop clients are unverified. **Not a stable path.**
- **ADP RUN.**
  - The Marketplace requires sales, security and legal review ([partner PDF](https://marketplace.adp.com/downloads/becoming-a-partner.pdf)).
  - The RUN GL export (per employee or per company, journal on the check date) is the fallback ([RUN GL guide](https://support.adp.com/adp_payroll/content/hybrid/GL/RUN_GL_Guide_QBO.pdf)).
- **Rippling.**
  - Customer tokens under *Tools ▸ Developer ▸ API Tokens*; the v2 API has a Payroll category ([llms.txt](https://developer.rippling.com/llms.txt)).
  - **A token unused for 30 days is revoked**, which clashes with syncing only while the app is open; warn the user.
- **Deel.**
  - Organization and personal tokens are created in the Developer Center ([API tokens](https://developer.deel.com/docs/api-tokens-1), [auth](https://developer.deel.com/api/authentication)).
  - Invoices and a gross-to-net payroll report are available ([invoices](https://developer.deel.com/api/reference/endpoints/accounting/retrieve-deel-invoices), [payroll report](https://developer.deel.com/api/reference/endpoints/global-payroll/get-payroll-report-v-2026-01-01)).
  - Limit: 5 req/s per organization, shared with the org's other tools ([rate limits](https://developer.deel.com/api/rate-limits)).
- **OnPay.** GL Summary as .xlsx, plus an IIF file for QuickBooks Desktop ([GL Summary](https://help.onpay.com/hc/en-us/articles/201811379-How-to-run-a-GL-Summary), [QBD guide](https://help.onpay.com/hc/en-us/articles/203557689-QuickBooks-Desktop-App-setup-guide), snippets). **An IIF *import* in QindaBooks would cover OnPay and many other QuickBooks Desktop-oriented exports.**
- **Justworks.** "OAuth application creation is not self-service" ([public API](https://public-api.justworks.com/v1/docs)). It posts each payroll invoice to QBO or Xero ([help](https://help.justworks.com/hc/en-us/articles/10279095459355-QuickBooks-Online-Integration), snippet). The BYOK route is keying the invoice as a bill from its PDF, which is poor.

### 6.2 1099 e-filing (rank 5)

**Rules**

- **FIRE is closing.** The last day to file is **2026-11-19, 3 p.m. ET**, and every form moves to IRIS in 2027 ([IR-2026-99](https://www.irs.gov/newsroom/irs-reminder-information-return-e-file-system-transitioning-to-a-new-platform)).
- **1099-NEC / 1099-MISC threshold** is **$2,000** for payments after 2025-12-31, inflation-adjusted from 2027 ([i1099mec](https://www.irs.gov/instructions/i1099mec), [Notice 2025-62](https://www.irs.gov/pub/irs-drop/n-25-62.pdf)).
- **1099-K** is back to more than $20,000 **and** more than 200 transactions ([IRS FAQ](https://www.irs.gov/newsroom/irs-issues-faqs-on-form-1099-k-threshold-under-the-one-big-beautiful-bill-dollar-limit-reverts-to-20000)).
- **E-filing is mandatory at 10 or more returns** of all types combined, and 1099-NEC is due January 31 ([i1099mec](https://www.irs.gov/instructions/i1099mec)).

**Filing paths**

| Path | BYOK | Cost | Notes | Effort |
|---|---|---|---|---|
| **IRIS Taxpayer Portal** CSV | ★★★ (file) | free | TCC required (ID.me sign-in); CSV upload of ≤100 returns; IRS template per form/year | S–M |
| Avalara 1099 & W-9 (formerly Track1099) | ★★★ | $3.10/form first 15, down to $0.63 above 500; state $1.49 | own client id/secret; subscription | M |
| Tax1099 | unverified | from $2.99/form; API needs the $349/yr Scale plan | auth not checked | M |
| IRIS A2A | ★ (L) | free | TCC + API client + JWKS + JWT; ATS testing | L |

Sources:

- IRIS Taxpayer Portal: [IRIS](https://www.irs.gov/filing/e-file-information-returns-with-iris), [TCC application](https://www.irs.gov/tax-professionals/iris-application-for-tcc), CSV rules in [working-group Q&A](https://www.irs.gov/pub/irs-efile/iris-working-group-qa-02-12-2025.pdf) and [Pub 5717](https://www.irs.gov/pub/irs-pdf/p5717.pdf).
- Avalara 1099 & W-9: [developer](https://developer.avalara.com/products/avalara-1099-and-w9/api/), [pricing](https://www.avalara.com/us/en/products/1099/pricing.html).
- Tax1099: [pricing](https://www.tax1099.com/tax-1099-efile-pricing).
- IRIS A2A: [Pub 5718](https://www.irs.gov/pub/irs-pdf/p5718.pdf), [ATS](https://www.irs.gov/e-file-providers/iris-assurance-testing-system-ats).

**Recommendation.** QindaBooks already flags 1099 vendors. Add:

- W-9 fields (TIN, stored in the keyring or encrypted, never in logs);
- a year-end "1099 worksheet" that excludes card and PayPal payments, which are reported on the processor's 1099-K;
- **export of the IRS's own IRIS CSV template**;
- recipient copies printed through the existing `PagePainter`.

One-time setup for the owner: apply for an IRIS TCC through ID.me well before January.

---

## 7. Category 4: sales tax

| Provider | BYOK | Cost | What we'd pull | Effort |
|---|---|---|---|---|
| **Stripe Tax** | ★★★ (existing `rk_` key + *Tax Transaction Reports* permission) | Basic 0.5%/txn (no-code) or $0.50/txn (API), only where registered; monitoring free | per-jurisdiction `filing_total_sales`, `…_taxable_sales`, `…_tax_collected`; registrations | S–M |
| TaxJar | ★★★ (dashboard token) | Starter $39/mo has **no API**; Professional $99/mo; AutoFile $50–55/return | orders, nexus regions; no liability-report API found | M |
| Avalara AvaTax | ★★★ (account id + license key) | "from $69/mo" (Select); otherwise custom | transactions; filing status unverified | M |

**Stripe Tax**

- Permission: *Tax Transaction Reports* on the restricted key ([tax reports](https://docs.stripe.com/tax/reports), [keys](https://docs.stripe.com/keys)).
- Reports are async runs (create, poll, download) ([Reports API](https://docs.stripe.com/reports/api)). Types are `tax.summarized_export.1` and itemized exports ([report types](https://docs.stripe.com/reports/report-types/tax)).
- Registrations: `GET /v1/tax/registrations` ([registrations](https://docs.stripe.com/api/tax/registrations/all)). Stripe files through partners ([filing](https://docs.stripe.com/tax/filing)).
- Pricing: [Stripe Tax pricing](https://stripe.com/tax/pricing).

**TaxJar.** Still selling as of 2026-06-12 ([plans](https://support.taxjar.com/article/139-how-much-does-taxjar-cost)); [API reference](https://developers.taxjar.com/api/reference/).

**Avalara AvaTax.** Resetting the license key breaks every connection ([auth](https://developer.avalara.com/avatax/authentication-in-rest/)); [pricing](https://www.avalara.com/us/en/products/pricing.html).

**Does the studio need it?**

- 25 states tax SaaS, plus 7 more if the software is downloaded ([Stripe guide, 2025-11-24](https://stripe.com/guides/introduction-to-saas-taxability-in-the-us)).
- Stripe's threshold monitoring is free ([monitoring](https://docs.stripe.com/tax/monitoring)).
- **Recommendation:**
  1. Turn on free monitoring.
  2. Import Stripe Tax summaries once the studio registers anywhere. The collected tax posts to Sales Tax Payable per jurisdiction through the existing `SalesTax` module.
  3. Sell through a merchant of record (Paddle or Gumroad) if the owner would rather not register at all.

---

## 8. Category 5: receipts and expenses

| Option | BYOK | Cost | Fit | Effort |
|---|---|---|---|---|
| **Tesseract 5.5.3** (Apache-2.0) | no key | free, local | plain OCR only; ≥300 DPI, PSM 11/12 for receipts; no field extraction | S |
| PaddleOCR 3.7 (Apache-2.0) | no key | free, local | PP-OCRv5 plus key-information extraction; Python runtime | M |
| docTR 1.1 (Apache-2.0) | no key | free, local | now maintained by t2k | M |
| Local VLM (llama.cpp `libmtmd`, Qwen3-VL) | no key | free, local (GPU helps) | field extraction by prompt; suite already has `office_ai` | M–L |
| AWS Textract AnalyzeExpense | ★★★ | $0.01/page (first 1M) | fields | S–M |
| Azure Document Intelligence receipt | ★★★ | $10/1,000 pages; F0 500 pages/mo free | fields | S–M |
| Google Document AI Expense | ★★★ | $0.10 per document (1–10 pages) | fields | S–M |
| Veryfi | ★★★ | $0.08/receipt; 100/mo free | fields | S–M |
| Mindee V2 | ★★★ | from €44/mo; V1 closed to new accounts | fields | S–M |

Sources:

- Local engines:
  - Tesseract: [releases](https://github.com/tesseract-ocr/tesseract/releases), [ImproveQuality](https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html)
  - PaddleOCR: [releases](https://github.com/PaddlePaddle/PaddleOCR/releases), [paper](https://arxiv.org/abs/2507.05595)
  - docTR: [repo](https://github.com/mindee/doctr)
  - llama.cpp: [multimodal](https://github.com/ggml-org/llama.cpp/blob/master/docs/multimodal.md)
  - Qwen3-VL: [repo](https://github.com/QwenLM/Qwen3-VL)
- Cloud services:
  - AWS: [Textract pricing](https://aws.amazon.com/textract/pricing/)
  - Azure: [DI pricing](https://azure.microsoft.com/en-us/pricing/details/ai-document-intelligence/)
  - Google: [DocAI pricing](https://cloud.google.com/document-ai/pricing)
  - Veryfi: [pricing](https://www.veryfi.com/pricing/)
  - Mindee: [plans](https://docs.mindee.com/account-management/plans.md), [V1 status](https://docs.mindee.com/v1/get-started/readme.md)

**Recommendation.** Keep it local-first and BYOK:

1. Attach the receipt (image/PDF) to a transaction and store it content-addressed.
2. Run Tesseract for searchable text (S).
3. Optionally let the suite's existing AI provider, or a local VLM, extract vendor, date, total and tax into a *suggestion* that the user confirms.
4. Offer cloud OCR only as an opt-in BYOK key.

**QindaMail forwarding** fits with no server: a rule files messages sent to a "receipts" folder or alias into QindaBooks' inbox. PDF and image attachments go to OCR, and HTML receipts are parsed as text (our assessment).

**IRS record-keeping for scanned receipts.** Rev. Proc. 97-22 requires:

- integrity and accuracy controls;
- prevention and detection of unauthorized changes;
- an indexing system;
- legible reproduction;
- an audit trail from the ledger to the source documents.

Sources: [IRB 1997-13](https://www.irs.gov/pub/irs-irbs/irb97-13.pdf), [Pub 583](https://www.irs.gov/publications/p583).

**Design implication.** Receipt files are immutable and hashed, with an index table and a link from each posting to its document (a `documents` table; we'd propose a D-0xx).

---

## 9. Category 6: migration in and out

| Source | BYOK | Route | Effort | Notes |
|---|---|---|---|---|
| **QuickBooks Online: export zip** | ★★★ | *Settings ▸ Tools ▸ Export data* gives a zip of Excel files (P&L, BS, TB, GL, lists) | S–M | recommended default |
| QuickBooks Online API | ★ | OAuth: production redirect must be HTTPS, not localhost or an IP; self-attested questionnaire even for private apps | L | Builder tier $0 for 500k CorePlus reads/month |
| QuickBooks Desktop | ★★★ | IIF for lists; import-only for transactions; Excel reports out | S–M | new sales ended 2024-09-30 (Enterprise excepted) |
| Xero | ★★ | own PKCE app (Starter: free, 5 connections) or Custom Connection (paid) | M | `/Journals` gated; AI-training clause |
| Wave | ★★★ | Full Access Token for your own businesses; **Pro subscription required** | M | GraphQL |
| FreshBooks | ★★ | own app; HTTPS redirect (`https://localhost` copy-code trick) | M | one refresh token per user/app |

**QuickBooks Online**

- **Production keys.**
  - Need a self-attested assessment questionnaire, even for private apps ([App Partner Program guide v1.2, 03/2026](https://static.developer.intuit.com/resources/Intuit_App_Partner_Program_Guide.pdf); [FAQ](https://help.developer.intuit.com/s/article/New-app-assessment-process-FAQ), snippet).
  - Production redirects cannot be localhost or an IP ([community answer](https://help.developer.intuit.com/s/question/0D54R00007Bf8sfSAB/why-does-production-redirect-uri-not-allow-localhost), snippet).
  - Intuit says OAuth 2 is supported only for server-side web apps ([help](https://help.developer.intuit.com/s/article/OAuth2-for-desktop-apps-automated-apps-single-page-applications), snippet).
- **Fees.**
  - Core (data-in) calls are free. CorePlus (data-out) calls are metered.
  - Tiers: Builder $0 / 500k; Silver $300/mo; Gold $1,700/mo; Platinum $4,500/mo ([program guide](https://static.developer.intuit.com/resources/Intuit_App_Partner_Program_Guide.pdf)).
  - Fees charged from 2025-11-01 ([Apideck](https://www.apideck.com/blog/quickbooks-api-pricing-and-the-intuit-app-partner-program), secondary).
- **Tokens.** Refresh tokens rotate and have a 5-year maximum lifetime ([Intuit blog](https://blogs.intuit.com/2025/11/12/important-changes-to-refresh-token-policy), snippet).
- **Export zip.** The file route: [export data](https://quickbooks.intuit.com/learn-support/en-us/help-article/list-management/export-reports-lists-data-quickbooks-online/L1xleDrLp_US_en_US), [reports to Excel](https://quickbooks.intuit.com/learn-support/en-us/help-article/report-management/export-reports-excel-quickbooks-online/L7iAoP97n_US_en_US).
- **API reference** for later (★, reached through the OAuth Playground token paste; unverified): [Customer entity](https://developer.intuit.com/app/developer/qbo/docs/api/accounting/all-entities/customer), [TrialBalance](https://developer.intuit.com/app/developer/qbo/docs/api/accounting/all-entities/trialbalance).

**QuickBooks Desktop**

- **Stop-sell.** Took effect 2024-09-30 ([Intuit](https://quickbooks.intuit.com/r/whats-new/quickbooks-desktop-stop-sell/)).
- **IIF.** Imports and exports lists, but only imports transactions, and they arrive unlinked ([IIF](https://quickbooks.intuit.com/learn-support/en-us/help-article/import-export-data-files/export-import-edit-iif-files/L56LT9Z0Q_US_en_US), [import kit](https://quickbooks.intuit.com/learn-support/en-us/help-article/list-management/iif-overview-import-kit-sample-files-headers/L5CZIpJne_US_en_US)).
- **qbXML / Web Connector.** Windows only ([Web Connector](https://developer.intuit.com/app/developer/qbdesktop/docs/get-started/get-started-with-quickbooks-web-connector)).
- QindaBooks already *exports* IIF (D-046). **Adding IIF *import* for lists and journals** would also cover OnPay's payroll IIF.

**Xero**

- **Auth.** PKCE with no secret ([PKCE flow](https://developer.xero.com/documentation/guides/oauth2/pkce-flow)). Loopback redirect is unverified in the official docs. Refresh tokens expire after 60 days unused ([FAQ](https://developer.xero.com/faq)).
- **Pricing from 2026-03-02.** Starter free with 5 connections; Core A$35/mo ([pricing](https://developer.xero.com/pricing)). The pricing page and FAQ **disagree** on whether Starter requires certification.
- **Terms.** The new terms **prohibit using API data to train AI/ML models** ([policy FAQ](https://developer.xero.com/faq/pricing-and-policy-updates)). That matters because the suite has AI features. Never route Xero data to an LLM for training; confirm that inference-only use is allowed.
- **Journals.** Connections from 2026-04-29 have no journal access ([custom integration FAQ](https://developer.xero.com/faq/custom-integration)).

**Wave**

- **Access.** A Full Access Token for your own businesses; OAuth only for apps offered to others ([permitted use](https://developer.waveapps.com/hc/en-us/articles/360020596571-Permitted-Use-Wave-Business-Owners), snippet).
- **Plan.** Pro or Wave Advisor required ([OAuth guide](https://developer.waveapps.com/hc/en-us/articles/360019493652-OAuth-Guide)).

**FreshBooks**

- **Auth.** Redirect URIs must be HTTPS; client secret required; one refresh token at a time ([authentication](https://www.freshbooks.com/api/authentication), [start](https://www.freshbooks.com/api/start)).
- **Local workaround.** The `https://localhost` copy-code trick is secondary ([Apideck](https://www.apideck.com/blog/how-to-get-your-freshbooks-api-key)).

**Exchange formats**

- **OFX.** Banking 2.3, maintained by FDX ([spec](https://financialdataexchange.org/wp-content/uploads/2025/12/OFX-Banking-Specification-v2.3.pdf)). Already imported.
- **SIE** (Swedish, [sie.se](https://sie.se/in-english/)) and **SAF-T** (OECD, no US adoption; [VATCalc](https://www.vatcalc.com/global/which-countries-have-introduced-oecds-saf-t-2/), secondary) are not relevant to the US.

**Migration design.** Import the chart of accounts, then lists, then an **opening trial balance** as of the cut-over date. Optionally follow with the GL detail as journal entries for history. Map QBO's account types to ours with a review screen.

---

## 10. Category 7: accountant and tax hand-off (rank 6)

- **Common denominator.** A trial balance (account number, name, type, debit, credit, **tax-line code**) plus GL detail, as CSV and XLSX. QindaCalc can already write XLSX.
- **Drake Tax.** Trial Balance Import supports 1065, 1120, 1120-S **and 990**, not Schedule C, through Drake's own Excel template ([Drake KB](https://kb.drakesoftware.com/kb/Drake-Tax/11166.htm)). Emitting that template is S.
- **TurboTax Business.** Covers 1120-S and 1065 by spreadsheet upload or QuickBooks connection; no trial-balance import is documented ([TurboTax](https://turbotax.intuit.com/small-business-taxes/)).
- **Lacerte, UltraTax CS, ProSeries, CCH Axcess.** Not verified; the search budget ran out. Ask the owner's accountant which one they use.
- **GIFI.** Canadian CRA only ([CRA](https://www.canada.ca/en/revenue-agency/services/forms-publications/publications/rc4088/general-index-financial-information-gifi.html)). Not relevant.

**Form 990 for the center**

- **Thresholds.** 990-N if gross receipts are normally ≤ $50k; 990-EZ if receipts < $200k **and** assets < $500k; otherwise the 990 ([IRS](https://www.irs.gov/charities-non-profits/form-990-series-which-forms-do-exempt-organizations-file-filing-phase-in)).
- **E-filing.** A 2025 Form 990 must be e-filed ([i990](https://www.irs.gov/instructions/i990)), and so must the 990-EZ ([i990ez](https://www.irs.gov/instructions/i990ez)).
- **What the books must capture.**
  - **Part VIII** revenue lines: 1a–1h, then 2, 3, 6–11.
  - **Part IX** functional expense columns: B program, C management & general, D fundraising. These are required for 501(c)(3) and (c)(4) organizations ([i990](https://www.irs.gov/instructions/i990)).
  - **Design implication:**
    - every expense posting needs a functional class (the existing *class* dimension can carry it);
    - every revenue account needs a Part VIII line;
    - a "990 worksheet" report groups the lot.

**The studio**

- **Schedule C** lines 8–27b / 30 and Part V ([i1040sc](https://www.irs.gov/instructions/i1040sc)).
- **1120-S** Schedules L, M-1, M-2 ([i1120s](https://www.irs.gov/instructions/i1120s)).
- **Design implication:** a per-account `tax_line` map per form. Ship starter maps with the chart templates.

---

## 11. Living without webhooks

Webhooks are optional for every recommended ★★★ provider:

- Plaid's `/transactions/sync` can be polled ([transactions](https://plaid.com/docs/transactions/)).
- Hosted Link can be polled through `/link/token/get` ([Hosted Link](https://plaid.com/docs/link/hosted-link/)).
- Stripe and Paddle reports are create-then-poll ([Stripe Reports API](https://docs.stripe.com/reports/api), [Paddle reports](https://developer.paddle.com/api-reference/reports/create-report)).

The only webhook-only sources are Every.org and Ko-fi, and both have CSV exports. The rules that make pull sync safe:

1. **The cursor is a date with an overlap, not "since last call".**
   - Store per connection and per account: the newest posted date seen, and the oldest still-pending date.
   - Next window = min(last posted − overlap, oldest pending).
   - Overlaps the providers themselves recommend:

     | Provider | Overlap |
     |---|---|
     | Mercury (M48) | 7 days |
     | SimpleFIN | ~5 days ([developers](https://beta-bridge.simplefin.org/info/developers)) |
     | Teller | 7–10 days ([transactions](https://teller.io/docs/api/account/transactions)) |
     | Brex | "a few days" ([list transactions](https://developer.brex.com/openapi/transactions_api/transactions/listprimarycardtransactions.md)) |
     | PayPal | at least 3 hours for lag ([docs](https://developer.paypal.com/docs/api/transaction-search/v1/)) |

   - The FITID dedup makes overlap free.
2. **Idempotency by namespaced provider id.** FITID = `<provider>:<account>:<id>`, with a derived `:fee` line as M48 does for Stripe. Re-import is a no-op, and import and posting commit in one transaction.
3. **Posted only.** Pending ids are unstable at Plaid ([data](https://plaid.com/docs/transactions/transactions-data/)) and Teller ([transactions](https://teller.io/docs/api/account/transactions)), and not documented at SimpleFIN.
   - Pending items are shown as a non-posting preview at most.
   - A posted item whose provider later *changes* it (an amount correction) goes to review, never a silent edit.
4. **Respect windows and budgets.**
   - Window caps: PayPal 31 days per request; SimpleFIN 90 days per request, ~24 requests/day.
   - Batch backfills as a resumable job whose progress is stored in the cursor, so closing the app mid-backfill resumes cleanly.
   - Back off on 429 using `Retry-After` where given (e.g. Givebutter, [rate limits](https://docs.givebutter.com/api-reference/rate-limits.md)).
5. **Credentials that die when idle are the real webhook-less risk.**

   | Provider | What happens | Source |
   |---|---|---|
   | Mercury | deleted after 45 days unused | [policy](https://docs.mercury.com/docs/api-token-security-policies.md) |
   | Rippling | revoked after 30 days unused | [llms.txt](https://developer.rippling.com/llms.txt) |
   | Brex | expires after 90 days unused | [FAQ](https://developer.brex.com/docs/faq) |
   | Paddle | keys last at most 1 year | [auth](https://developer.paddle.com/api-reference/about/authentication) |
   | Etsy | refresh token lasts 90 days | [auth](https://developers.etsy.com/documentation/essentials/authentication) |
   | Xero | refresh token dies after 60 days unused | [FAQ](https://developer.xero.com/faq) |
   | Square | PKCE refresh token lasts 90 days | [OAuth](https://developer.squareup.com/docs/oauth-api/overview) |
   | Google | Testing-mode tokens last 7 days | [Google](https://support.google.com/cloud/answer/15549945?hl=en) |

   - Each connection should record its provider's idle limit.
   - The Connections dialog (and the dashboard) should show "sync within N days or this key is revoked".
   - Keys with an expiry date get a re-key reminder.
6. **Optional headless sync with no server.**
   - A `qindabooks --sync <company>` entry point run by a **systemd user timer** keeps tokens alive and the books current while the app is closed. It reuses the same library and keyring (via the Secret Service, which needs an unlocked session).
   - This is a desktop-local convenience, not a backend. It is the owner's call (see §14).
7. **Matching across sources replaces webhook "events".**
   - A processor payout becomes a transfer that waits for the bank line, whichever arrives first (M48).
   - Donation-platform payouts (Givebutter `payout_id`, Zeffy payout CSV, GoFundMe Pro payouts), PayPal T0400 withdrawals, Square and Shopify payouts use the same matcher.

---

## 12. Category 8: the rest of the suite (brief)

| Need | ★★★ route | ★★ route | Effort |
|---|---|---|---|
| Contacts (QindaMail) | **CardDAV** with app passwords: iCloud (2FA app-specific password), Fastmail (not on the Basic plan), Nextcloud | Google People (own Desktop client, Testing mode); Microsoft Graph `Contacts.ReadWrite` (own Entra app) | S–M |
| Files (all apps) | **XDG portal FileChooser plus the desktop's own mounts** (GVfs/KIO, rclone), with no credentials in the suite; **Nextcloud WebDAV** via Login Flow v2 app passwords | Google Drive `drive.file` (non-sensitive; only files the app created or opened); OneDrive via Graph `Files.ReadWrite` | S / M |
| Issues (QindaPlan) | GitHub fine-grained PAT (GA 2025-03-18); GitLab PAT (≤365 days); Linear personal key; Jira scoped API token | GitHub App device flow; GitLab/Linear PKCE | S each |
| Meeting links (QindaMail) | **Jitsi** URL (no API; room creator must sign in since 2023) | Google Meet via Calendar `conferenceData.createRequest`; Teams via Graph `isOnlineMeeting` (work/school only); Zoom General app with PKCE on `http://127.0.0.1` | S–M |

**OAuth rules for the ★★ routes**

- **Google.**
  - Desktop clients keep loopback redirects on 127.0.0.1 or [::1], and custom schemes are no longer supported ([native apps](https://developers.google.com/identity/protocols/oauth2/native-app), [loopback migration](https://developers.google.com/identity/protocols/oauth2/resources/loopback-migration)).
  - Unverified apps with sensitive scopes are capped at 100 users ([unverified](https://support.google.com/cloud/answer/7454865?hl=en)).
  - **Testing-mode refresh tokens expire after 7 days** ([testing](https://support.google.com/cloud/answer/15549945?hl=en)).
  - Personal use is exempt from verification ([sensitive scopes](https://developers.google.com/identity/protocols/oauth2/production-readiness/sensitive-scope-verification)).
  - The restricted full `drive` scope needs a yearly third-party security assessment when data passes through a third-party server ([restricted scopes](https://developers.google.com/identity/protocols/oauth2/production-readiness/restricted-scope-verification)). **Use `drive.file` ([Drive scopes](https://developers.google.com/workspace/drive/api/guides/api-specific-auth)).**
- **Microsoft.**
  - The loopback port is ignored when matching, and 127.0.0.1 is preferred ([redirect rules](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url)).
  - Public client flows must be switched on ([desktop configuration](https://learn.microsoft.com/en-us/entra/identity-platform/scenario-desktop-app-configuration)).
  - **A personal-account user must create a free Azure tenant to register their own app** ([quickstart](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app)).
  - Multi-tenant consent to an unverified publisher is often blocked ([publisher verification](https://learn.microsoft.com/en-us/entra/identity-platform/publisher-verification-overview)). BYOK sidesteps that, because each user owns their own registration.
- **Zoom.**
  - PKCE with `http://127.0.0.1`, not `localhost`; an unpublished app works inside the developer's own account ([OAuth](https://developers.zoom.us/docs/integrations/oauth/), [private apps](https://developers.zoom.us/docs/distribute/sharing-private-and-beta-apps/)).
  - 100 meeting creates or updates per user per day ([rate limits](https://developers.zoom.us/docs/api/rate-limits/)).

**Other providers**

- CardDAV: [iCloud](https://support.apple.com/en-us/102654), [Fastmail](https://www.fastmail.help/hc/en-us/articles/360058752854-App-passwords), [Nextcloud](https://docs.nextcloud.com/server/stable/admin_manual/groupware/contacts.html), [Google CardDAV (OAuth only)](https://developers.google.com/people/carddav).
- Nextcloud: [Login Flow v2](https://docs.nextcloud.com/server/stable/developer_manual/client_apis/LoginFlow/index.html).
- XDG portal: [FileChooser](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html).
- rclone: [mount](https://rclone.org/commands/rclone_mount/).
- GitHub: [fine-grained PAT GA](https://github.blog/changelog/2025-03-18-fine-grained-pats-are-now-generally-available/), [REST limits](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api).
- GitLab: [PATs](https://docs.gitlab.com/user/profile/personal_access_tokens/).
- Linear: [rate limiting](https://linear.app/developers/rate-limiting), [OAuth](https://linear.app/developers/oauth-2-0-authentication).
- Jira: [API tokens](https://support.atlassian.com/atlassian-account/docs/manage-api-tokens-for-your-atlassian-account/).
- Jitsi: [auth change](https://jitsi.org/blog/authentication-on-meet-jit-si/).
- Google Meet: [create events](https://developers.google.com/workspace/calendar/api/guides/create-events).
- Teams: [online meetings](https://learn.microsoft.com/en-us/graph/outlook-calendar-online-meetings).

**Suite verdict**

- **Files:** portal plus desktop mounts first, then Nextcloud WebDAV.
- **Contacts:** CardDAV first.
- **Meetings:** Jitsi links first.
- **Issues:** GitHub, GitLab and Linear with a PAT.
- Google and Microsoft remain optional, bring-your-own-client-ID paths.

---

## 13. Cost comparison

US list prices as fetched on 2026-09-25. Several vendors were running intro promos
(QuickBooks 50% off for 3 months, Xero 90% off for 6 months, Google Workspace varying
between page loads). **We compare list prices, never promos.** Processing fees
(Stripe, PayPal, Square) are the same whichever bookkeeping app is used, so they are
not counted as savings.

### 13.1 What the incumbents cost

| Product | Plan | Price | Terms | Source |
|---|---|---|---|---|
| Microsoft 365 | Business Basic (web/mobile apps + mail) | $7.00/user/mo | annual; $8.40 month-to-month | [MS](https://www.microsoft.com/en-us/microsoft-365/business/microsoft-365-business-basic) |
| | Business Standard (desktop Office + mail) | $14.00/user/mo | annual; $16.80 month-to-month | [MS](https://www.microsoft.com/en-us/microsoft-365/business/microsoft-365-business-standard) |
| | Business Premium | $22.00/user/mo | annual; $26.40 month-to-month | [MS](https://www.microsoft.com/en-us/microsoft-365/business/microsoft-365-business-premium) |
| | Apps for business (desktop Office, no mail) | $10.00/user/mo | annual; $12.00 month-to-month | [MS](https://www.microsoft.com/en-us/microsoft-365/business/microsoft-365-apps-for-business) |
| | Price change | increase effective 2026-07-01, at renewal | — | [MS blog 2025-12-04](https://www.microsoft.com/en-us/microsoft-365/blog/2025/12/04/advancing-microsoft-365-new-capabilities-and-pricing-update) |
| | Nonprofit | Business Basic grant free (≤300 users); Business Premium $5.50/user/mo | paid yearly | [MS nonprofits](https://www.microsoft.com/en-us/nonprofits/offers-for-nonprofits) |
| Google Workspace | Starter / Standard / Plus | $7 / $14 / $22 per user/mo | "save 16%" with a 1-year commitment | [Google](https://workspace.google.com/pricing) |
| | Nonprofits | $0; Standard $3.50/user/mo annual | 1-year for the discount | [Google nonprofits](https://www.google.com/nonprofits/offerings/workspace/) |
| QuickBooks Online | Solopreneur | $20/mo | monthly | [Intuit](https://quickbooks.intuit.com/solopreneur/) |
| | Simple Start (1 user) / Essentials / Plus / Advanced | **$38 / $85 / $140 / $340 per mo** | monthly, no contract | [Intuit pricing](https://quickbooks.intuit.com/pricing/) |
| | Payroll bundles | Workforce Payroll + Simple Start $88/mo + $7/employee; + Essentials $135/mo + $7/employee | monthly; 10% off annual | [Intuit payroll](https://quickbooks.intuit.com/payroll/pricing/) |
| | Nonprofit via TechSoup | QBO Plus 1 year (5 users) **$80 admin fee** | 1-year | [TechSoup](https://www.techsoup.org/intuit) |
| QuickBooks Desktop | Enterprise Gold / Platinum / Diamond | $2,210 / $2,717 / $5,363 per yr | annual | [Intuit](https://quickbooks.intuit.com/desktop/enterprise/pricing/) |
| Xero (for context) | Early / Growing / Established | $25 / $55 / $90 → **$27 / $59 / $97 from 2026-10-01** | monthly | [Xero](https://www.xero.com/us/pricing-plans/), [update](https://www.xero.com/us/pricing-plans/update/) |
| Wave / Zoho Books / FreshBooks | Pro / Standard / Lite | $19 / $20 / $23 per mo (Wave Starter and Zoho Free are $0) | monthly | [Wave](https://www.waveapps.com/pricing), [Zoho](https://www.zoho.com/us/books/pricing/), [FreshBooks](https://www.freshbooks.com/pricing) |

**Price history caveats.** The old Microsoft prices ($6 / $12.50 / $22 before July 2026)
come from a third party ([Red River](https://redriver.com/collaboration/microsoft-365-price-increase-2026), secondary), because
Microsoft's own table is an image. The QuickBooks Online 2025→2026 increase history
is unverified. On the QuickBooks payroll page, two page loads showed different
per-employee rates.

### 13.2 What the BYOK providers cost

| Provider | Price | Source |
|---|---|---|
| QindaOffice + QindaBooks | $0 (GPL-3.0-or-later) | this repository |
| SimpleFIN Bridge | $15/yr (≈ $1.25/mo) for 25 institutions | [SimpleFIN](https://beta-bridge.simplefin.org/) |
| Mercury | $0 base plan | [Mercury](https://mercury.com/pricing) |
| Relay | Starter $0 | [Relay](https://relayfi.com/pricing) |
| Plaid Trial | $0 for 10 Items | [Plaid billing](https://plaid.com/docs/account/billing/) |
| IRS IRIS Taxpayer Portal | $0 | [IRS](https://www.irs.gov/filing/e-file-information-returns-with-iris) |
| Avalara 1099 (if preferred) | $3.10/form (first 15) | [Avalara](https://www.avalara.com/us/en/products/1099/pricing.html) |
| Gusto Simple / OnPay | $49/mo + $6/person | [Gusto](https://gusto.com/product/price/simple), [OnPay](https://onpay.com/pricing/) |
| Stripe Tax Basic | 0.5% or $0.50 per transaction, only where registered | [Stripe](https://stripe.com/tax/pricing) |
| TaxJar Starter / Professional (API) | $39 / $99 per mo | [TaxJar](https://www.taxjar.com/pricing) |
| Zeffy (donations) | $0 platform fee (donor tips fund it) | [Zeffy](https://www.zeffy.com/pricing) |
| Givebutter (donations) | 0% with tips on, 3% with tips off, + processing | [Givebutter](https://givebutter.com/pricing) |
| Donorbox API add-on | $17/mo (skip it and read through Stripe) | [Donorbox](https://donorbox.zendesk.com/hc/en-us/articles/360061902812-How-to-set-up-your-Donorbox-API-for-the-API-Zapier-Integration) |
| Local OCR (Tesseract) | $0 | [Tesseract](https://github.com/tesseract-ocr/tesseract/releases) |
| Mail/contacts/calendar host (the suite has none of its own) | Fastmail Business Standard $5/user/mo annual (Basic lacks CardDAV); self-hosted Nextcloud $0 | [Fastmail pricing](https://www.fastmail.com/pricing/), [Fastmail app passwords](https://www.fastmail.help/hc/en-us/articles/360058752854-App-passwords), [Nextcloud](https://nextcloud.com/pricing/) |

Plaid's public pricing page still describes a "Limited Production" tier with 200 calls
per product ([Plaid pricing](https://plaid.com/pricing/)). The billing docs say Trial
replaced it for teams created on or after 2026-04-15
([billing](https://plaid.com/docs/account/billing/)). Treat the pricing page as stale.

### 13.3 Worked examples (monthly, list prices, annual-commitment rates where offered)

The suite does not host mail. Each example therefore keeps a mail host on both sides,
so the comparison stays honest.

**The studio: 2 users, no employees, Mercury + Stripe.** QuickBooks Online Simple Start
allows 1 user ([pricing](https://quickbooks.intuit.com/pricing/)), so 2 users means
Essentials.

| | Incumbent | BYOK |
|---|---|---|
| Office + mail | M365 Business Standard 2 × $14 = $28 | QindaOffice $0 + Fastmail Business Standard 2 × $5 = $10 |
| Books | QBO Essentials $85 | QindaBooks $0 |
| Bank feeds | included (Intuit's aggregators) | Mercury API $0 + SimpleFIN $1.25 for other banks |
| **Total** | **$113/mo ≈ $1,356/yr** | **$11.25/mo ≈ $135/yr** |

Saving: **≈ $1,220/yr**.

**The studio with 3 W-2 employees.**

| | Incumbent | BYOK |
|---|---|---|
| Payroll + books | QBO Workforce Payroll + Essentials $135 + 3 × $7 = $156 | Gusto Simple $49 + 3 × $6 = $67; QindaBooks $0 (imports Gusto's journal CSV) |
| Office + mail | $28 | $10 |
| SimpleFIN | — | $1.25 |
| **Total** | **$184/mo ≈ $2,208/yr** | **$78.25/mo ≈ $939/yr** |

Saving: **≈ $1,270/yr**.

**The community center: 3 users, nonprofit pricing.** Here the incumbents are already
cheap, so the case is independence, not money:

- Incumbent: the M365 Business Basic grant ($0), or Google for Nonprofits ($0), plus TechSoup QBO Plus at $80/yr (≈ $6.67/mo). With desktop Office through Business Premium nonprofit at 3 × $5.50 = $16.50/mo, the total is **≈ $23/mo**.
- BYOK: QindaOffice + QindaBooks $0, SimpleFIN $1.25, and mail on the free nonprofit Google/Microsoft grant or on Fastmail/Nextcloud. The total is **≈ $1.25/mo**, plus mail if it moves off the free grant.

The larger nonprofit lever is the **donation platform**: Zeffy charges no platform fee,
Givebutter charges 0% only while donor tips stay on, and Donorbox's API costs $17/mo
(see §5).

**Caveats**

- These examples leave out what QuickBooks bundles and we don't: QBO's hosted
  multi-user access, its mobile app and the accountant's direct login.
- Processing, payroll-tax and 1099 filing fees are similar on both sides, except
  that IRIS is free.
- For an owner who pays for M365 mainly for Outlook/Exchange mail, the office-suite
  saving is the $14 − $5 gap per user, not the whole $14.

---

## 14. Open questions for the owner

1. **Banks.** Which banks and cards does each business use besides Mercury?
   - Are any on the ★ list (Chase, BofA, Amex, Relay, Novo, Bluevine, a credit union)?
   - This decides whether SimpleFIN's $15/yr is needed, and for which company.
2. **PayPal / Venmo / Cash App / Zelle.** Which does each business actually receive money through, and roughly how much?
3. **Donations.** Which platform(s) does the center use: Givebutter, Zeffy, Donorbox, GoFundMe Pro, Every.org, Benevity/employer matching, PayPal Giving Fund / Facebook?
   - Does it track **restricted funds** or grants that need their own balances?
4. **In-person payments.** Square, Stripe Terminal, or something else at the center's front desk or events? Eventbrite for tickets?
5. **Payroll.** Does either business have W-2 employees? Through which provider (Gusto, OnPay, ADP, Justworks)? Contractors through Deel?
6. **1099s.** How many contractors per year per business? Is there already an IRIS TCC? (If not, start the ID.me/TCC application before December.)
7. **Studio sales.** How does the studio sell software:
   - Stripe directly (then Stripe Tax monitoring matters),
   - a merchant of record (Paddle, Gumroad, Lemon Squeezy, where sales tax is their problem),
   - app stores, Shopify or Etsy?
8. **Sales tax.** Is either business registered to collect sales tax in any state today?
9. **Spend cards.** Any Brex, Ramp, BILL or Divvy use?
10. **Existing books.** Is there history in QuickBooks Online/Desktop, Xero, Wave or FreshBooks to bring in? From what date should QindaBooks be the book of record?
11. **Accountant.** Who prepares the returns, and in which software (Drake, Lacerte, UltraTax, ProSeries, CCH)? What form does the center file (990-N, 990-EZ, 990), and what entity/return is the studio (Schedule C, 1120-S, 1065)?
12. **Background sync.** Is a systemd user timer that syncs while the app is closed acceptable? It keeps idle-expiring keys alive and needs the keyring unlocked at login.
13. **Cloud OCR.** Is sending receipt images to a cloud OCR service with the owner's own key acceptable, or local-only (Tesseract / local model)?
14. **Suite.** Contacts and files: iCloud, Fastmail, Nextcloud, Google or Microsoft? Issues: GitHub, GitLab or Linear? Meetings: Jitsi, Zoom, Meet or Teams?

---

## 15. Claims we could not verify

- **Bank feeds**
  - SimpleFIN: whether a transaction id is stable when a pending transaction posts; which specific banks (and Mercury) it covers.
  - Plaid: whether the Trial terms permit ongoing business bookkeeping, and its paid rates.
  - Teller: widget origin restrictions, rate limits, history depth.
  - MX / Finicity / Akoya: prices.
- **Fintech APIs**
  - Mercury: API rate limits.
  - Wise and BILL Spend & Expense: whether read-only tokens exist.
  - Relay, Novo, Bluevine, Found: OFX/QBO downloads.
  - Amex / Chase / Capital One developer portal contents (snippets only).
- **Payments**
  - PayPal: whether Transaction Search must be enabled per app; whether Venmo-checkout rows appear.
  - Shopify: whether client credentials work on a paid live store.
  - Etsy: loopback redirect in production; rate numbers; money format.
  - Cash App: statement format.
  - Paddle: payouts entity.
  - Eventbrite: payout data.
- **Donations**
  - Givebutter: fee-field semantics.
  - Donorbox: whether the add-on is available on the Standard plan; whether Stripe metadata carries the designation.
  - GoFundMe Pro: which plans include the API.
  - Blackbaud SKY: pricing and auth.
  - Neon / Bloomerang: plan gating.
- **Payroll and tax**
  - ADP RUN: any customer API.
  - Rippling: rate limits and payroll fields.
  - Gusto / OnPay: exact CSV columns.
  - Gusto MCP: terms.
  - Tax1099: API auth.
  - AvaTax: filing-status API.
  - TaxJar: liability-report API.
  - Marketplace-facilitator treatment of app-store sales.
- **Migration**
  - Intuit's production-redirect rule is from community answers and snippets; the official page would not render.
  - The Intuit OAuth Playground token-paste workaround.
  - Xero: loopback redirect; the Custom Connection US price; the Starter certification conflict.
  - Wave: the full-access-token page (snippet).
  - FreshBooks: whether private apps are reviewed.
- **Hand-off**
  - Trial-balance import in Lacerte, UltraTax CS, ProSeries and CCH Axcess (the search budget ran out).
  - 1120-S Schedule L/M-1 thresholds.
- **Suite**
  - Microsoft Teams links on personal accounts.
  - Atlassian PKCE for public clients.
  - A C++ MSAL on Linux.
  - Google Calendar/Meet scope sensitivity class.
