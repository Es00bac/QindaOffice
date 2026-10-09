<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Mail accounts (`office_mail`)

> Contacts, vCard/CSV, safe HTML reading, printing and message files
> (`.eml`, `.mbox`, Outlook `.msg`) are in
> [mail-interchange.md](mail-interchange.md) (M64, D-072), as are Outlook
> data files, contact groups and CardDAV address books (M76, D-085).

`libs/office_mail` (M43, [D-043](decisions.md#d-043-hosted-accounts-sign-in-with-pkce-over-loopback-and-keep-secrets-in-the-keyring))
is the account layer of QindaMail: what an account is, where its servers are,
how it signs in, and where its secrets live. The protocols themselves are the
QindaTK providers (`SmtpMailProvider`, `ImapMailProvider`, `CalDavProvider`;
QindaTK `docs/productivity-providers.md`); Exchange EWS and Microsoft Graph
calendars are the M43 exchange lane. The QindaMail app (M44) builds its
account dialog and host over this library. Plan:
[`plans/ribbon-and-mail.md`](plans/ribbon-and-mail.md).

```
office_mail → office_core (AtomicFileStore, Paths), Qt Core/Network/Qml,
              qt6keychain, QindaTK   (Qt Gui only for QDesktopServices)
```

No Quick, no desktop archives. Namespace `QindaOffice::Mail`; QML module
`QindaOffice.Mail` 1.0 (C++ types below; the app lane adds its QML files to
the same module). Consumers link `office_mail` **and** `office_mailplugin`.

| Type | Kind | Job |
| --- | --- | --- |
| `MailAccount` | value | one account's configuration (JSON ⇄ struct ⇄ QVariantMap), validation |
| `AccountStore` | QML type | the account list in `accounts.json` |
| `ProviderPresets` | C++ | built-in server settings per domain |
| `ClientConfigParser` | C++ | Thunderbird `clientConfig` v1.1 documents |
| `AutoConfig` | QML type | presets → autoconfig.&lt;domain&gt; → .well-known → ISPDB |
| `OAuth2Session` | QML type | Authorization Code + PKCE over a loopback redirect, tokens |
| `OAuthClientRegistry` | C++ | where an OAuth client ID comes from |
| `MailSecrets` | QML type | keyring secrets through QtKeychain |
| `XOAuth2` | C++ | SASL XOAUTH2 wire format helpers |

Every asynchronous operation follows the toolkit's result contract: a
`requestId` in, exactly one `…Finished(requestId, result)` /
`finished(requestId, result)` out, `result.state` ∈ `accepted | offline |
rejected | conflict`, `retryable`, `error`. Invalid input is rejected on the
next event-loop turn through the same signal.

## Accounts JSON

`$XDG_CONFIG_HOME/qindaoffice/mail/accounts.json`, written atomically
(`Core::AtomicFileStore`), schema `qindaoffice.mail.accounts/1`:

```json
{
  "schema": "qindaoffice.mail.accounts/1",
  "accounts": [{
    "id": "5f0c…", "kind": "gmail", "displayName": "Work", "enabled": true,
    "emailAddress": "ada@gmail.com", "fullName": "Ada Lovelace", "signature": "",
    "syncIntervalMinutes": 15,
    "incoming": {"protocol": "imap", "host": "imap.gmail.com", "port": 993,
                 "security": "tls", "userName": "ada@gmail.com",
                 "authMechanism": "xoauth2", "leaveOnServer": true, "deleteAfterDays": 0},
    "outgoing": {"host": "smtp.gmail.com", "port": 465, "security": "tls",
                 "userName": "ada@gmail.com", "authMechanism": "xoauth2",
                 "sameCredentialsAsIncoming": true},
    "calendar": {"protocol": "caldav", "authMechanism": "bearer", "userName": "ada@gmail.com",
                 "url": "https://apidata.googleusercontent.com/caldav/v2/ada@gmail.com/events/",
                 "calendars": [{"id": "primary", "url": "", "name": "Ada", "color": "#3366cc", "enabled": true}]},
    "oauth": {"provider": "google", "clientId": "", "tenant": "", "scopes": []},
    "specialFolders": {"sent": "[Gmail]/Sent Mail", "drafts": "[Gmail]/Drafts",
                       "trash": "[Gmail]/Trash", "archive": "[Gmail]/All Mail", "junk": "[Gmail]/Spam"}
  }]
}
```

- **Address books** (M76, D-085): an account may carry
  `"contacts": {"protocol": "carddav", "url": "https://contacts.icloud.com/",
  "userName": "ada@icloud.com", "authMechanism": "basic", "addressBooks":
  [{"id": "book1", "url": "https://…/card/", "name": "Contacts",
  "enabled": true}]}` — CardDAV beside the account's mail or calendar, or
  alone (kind `carddav`). The key is written only for an account that uses
  it (or already had it).
- **Unknown fields are preserved** at every level (each struct keeps the
  object it was read from and writes the known keys over it), so a newer
  build's or a hand-added field survives a load/save round trip.
- **No secrets, ever.** Validation refuses any key that names one
  (`password`, `secret`, `token`, `accessToken`, `refreshToken`,
  `clientSecret`, `appPassword`, `apiKey`, `bearer`, in any case or
  `_`/`-` spelling) anywhere in the object — unknown-field preservation
  would otherwise write it to disk.
- A file with another schema is loaded **read-only** (never downgraded); an
  unparseable file is moved to `accounts.json.corrupt` and the store starts
  empty (the `Core::JsonSettings` rule).
- Ids are UUIDs generated by `AccountStore::add`; `[A-Za-z0-9._-]{1,128}`.

### Kinds and validation

| Kind | Incoming | Outgoing | Calendar protocol | Sign-in |
| --- | --- | --- | --- | --- |
| `gmail` | IMAP | SMTP | `caldav` (bearer) or `none` | OAuth `google`, or an app password (mail only) |
| `imap` | IMAP | SMTP | `caldav` or `none` | password |
| `pop3` | POP3 | SMTP | `none` | password |
| `caldav` | — | — | `caldav` | password or bearer |
| `ics` | — | — | `ics` (`https:`, `webcal:`, `file:`) | none / basic |
| `exchange` | optional IMAP | optional SMTP | `ews` | password (Basic over TLS) or bearer |
| `microsoft365` | IMAP, `xoauth2` only | SMTP, `xoauth2` only | `graph` (bearer) or `none` | OAuth `microsoft` |
| `carddav` (M76) | — | — | `none` | password or bearer; `contacts.protocol` must be `carddav` |

Contacts (`contacts.protocol` `carddav` or `none`) may be added to every
kind but `ics`; with `carddav` a server URL (`https:`, or `http:` to
loopback), `authMechanism` `basic` (needs a user name, filled from the
incoming user or the address) or `bearer` (needs `oauth.provider`, Google),
and address books with unique ids and https (or relative) URLs are required.

`MailAccount::validate()` returns one line per problem, prefixed with the
field path (`incoming.port: must be 1–65535`), so the dialog can put it next
to the field. Beyond the table: ASCII addresses only (the toolkit has no
SMTPUTF8); `security: none` only to a loopback host (a local bridge);
`xoauth2`/`bearer` need `oauth.provider`; calendar URLs `https:` (or `http:`
to loopback); calendar entries need unique ids and `#rrggbb` colours;
`syncIntervalMinutes` 0 (manual)–1440; signatures ≤ 64 KiB.
`AccountStore::normalized` fills what the dialog need not ask: display name
and user names from the address, ports from protocol + security.

### `AccountStore` (QML `AccountStore`)

| Member | |
| --- | --- |
| `path` | empty = the default path; setting it reloads |
| `accounts` (list of maps), `count`, `lastError`, `readOnly` | |
| `secrets: MailSecrets` | used by `remove()`; without one, a private `MailSecrets` over the default backend |
| `kinds`, `presets` | constants for the dialog |
| `add(map)`, `update(map)` → `{ok, id, error, errors}` | validate → write → then change the list |
| `remove(id)` → `{ok, id, error, secretsRequestId}` | writes the file, then removes every keyring secret of the account; `secretsRemoved(id, result)` reports it |
| `accountById(id)`, `validate(map)`, `draftFor(kind, address, fullName)`, `reload()` | |
| `contactsUrlFor(address)` (M76) | where CardDAV discovery starts: Google's principal URL for Gmail addresses, `https://contacts.icloud.com/`, `https://carddav.fastmail.com/`, else `https://<domain>/` |
| `addressBooks()` (M76) | every enabled CardDAV book of every account: [{accountId, accountName, id, url, name}] (People's "Save to") |
| signals `accountsChanged`, `accountAdded/Updated/Removed(id)`, `secretsRemoved(id, result)` | |

## Presets

`ProviderPresets` matches the address's domain exactly (case-insensitive).
`auth` tells the dialog how the user signs in; `note` is shown with it.

| Preset | Domains | Kind | IMAP | SMTP | Calendar | Auth |
| --- | --- | --- | --- | --- | --- | --- |
| `gmail` | gmail.com, googlemail.com | gmail | imap.gmail.com:993 tls | smtp.gmail.com:465 tls | Google CalDAV (bearer) | oauth-preferred |
| `outlook` | outlook.com, hotmail.com, live.com, msn.com | microsoft365 | outlook.office365.com:993 tls | smtp-mail.outlook.com:587 starttls | Graph | oauth-required |
| `microsoft365` | (by kind) | microsoft365 | outlook.office365.com:993 tls | smtp.office365.com:587 starttls | Graph | oauth-required |
| `yahoo` | yahoo.com, ymail.com, rocketmail.com | imap | imap.mail.yahoo.com:993 tls | smtp.mail.yahoo.com:465 tls | — | app-password |
| `icloud` | icloud.com, me.com, mac.com | imap | imap.mail.me.com:993 tls | smtp.mail.me.com:587 starttls | https://caldav.icloud.com/ | app-password |
| `fastmail` | fastmail.com, fastmail.fm | imap | imap.fastmail.com:993 tls | smtp.fastmail.com:465 tls | https://caldav.fastmail.com/dav/calendars/user/&lt;address&gt;/ | app-password |
| `aol` | aol.com, aim.com | imap | imap.aol.com:993 tls | smtp.aol.com:465 tls | — | app-password |
| `zoho` | zoho.com, zohomail.com | imap | imap.zoho.com:993 tls | smtp.zoho.com:465 tls | — | password |
| `gmx-com` | gmx.com | imap | imap.gmx.com:993 tls | mail.gmx.com:587 starttls | — | password |
| `gmx-net` | gmx.net, gmx.de, gmx.at, gmx.ch | imap | imap.gmx.net:993 tls | mail.gmx.net:587 starttls | — | password |
| `mailbox-org` | mailbox.org | imap | imap.mailbox.org:993 tls | smtp.mailbox.org:465 tls | — | password |
| `proton` | proton.me, protonmail.com, protonmail.ch, pm.me | imap | 127.0.0.1:1143 starttls | 127.0.0.1:1025 starttls | — | bridge (Proton Mail Bridge) |

Google Workspace and Microsoft 365 on an organization's own domain are
chosen by kind (`draftFor("gmail", …)` / `("microsoft365", …)`); MX-based
detection is not part of M43. Folder names are the providers' English IMAP
names; once IMAP folder listing lands (QindaTK D-299) SPECIAL-USE flags
should win.

## Autoconfig

`AutoConfig.lookup(requestId, address)` tries, in order, and stops at the
first source that yields a usable **encrypted** incoming (IMAP/POP3) + SMTP
pair:

1. the presets (no network);
2. `https://autoconfig.<domain>/mail/config-v1.1.xml?emailaddress=<address>`;
3. `https://<domain>/.well-known/autoconfig/mail/config-v1.1.xml`;
4. the Mozilla ISPDB, `https://autoconfig.thunderbird.net/v1.1/<domain>`.

Only the provider's own server (step 2) sees the full address. Every fetch
is `https:` only (redirects too; `http:` only to loopback for tests), capped
at 256 KiB and `timeoutMs` (10 s default) per step, without cookies or
cache. The parser reads Thunderbird's clientConfig v1.1: `incomingServer`
`imap`/`pop3`, `outgoingServer` `smtp`, `socketType` SSL/STARTTLS/plain →
`tls`/`starttls`/`none`, `%EMAILADDRESS%` / `%EMAILLOCALPART%` /
`%EMAILDOMAIN%`, `authentication` (`password-cleartext` → `auto`, only
`OAuth2` → `xoauth2`). Other server types (exchange, ews) are ignored;
plain-text servers are skipped. A discovered Google or Microsoft host gets
the full gmail / microsoft365 preset (OAuth, calendar, folders) with the
discovered hosts.

Result keys: `emailAddress`, `domain`, `source` (`preset`, `autoconfig`,
`well-known`, `ispdb`, or empty), `sourceUrl`, `presetId`, `providerName`,
`auth`, `note`, `account` (a candidate account map without id), `incoming` /
`outgoing` (every listed server), `documentationUrl`, and `attempts`
(`[{source, url, outcome, error}]`, outcomes `found`, `not-found`,
`invalid`, `offline`, `refused`, `cancelled`). Nothing found is `rejected`,
or `offline` + `retryable` when no step reached a server.
`presetFor(address)` answers the preset step synchronously.

## OAuth 2.0

`OAuth2Session` implements the native-app flow of RFC 8252 with PKCE
(RFC 7636, S256 only):

1. `configure(account)` resolves the client ([below](#oauth-client-ids)),
   the tenant, the login hint, the account id (keyring key) and the
   **resources** the account needs: `mail` (IMAP/SMTP), `calendar` (Graph,
   or Google CalDAV with bearer), `ews` (Exchange Online EWS), `contacts`
   (Google CardDAV with bearer, M76), `mailbox` (Microsoft 365's mailbox
   settings and message tags over Graph, M81).
2. `authorize(requestId)` listens on **127.0.0.1**, ephemeral port, generates
   a 43-character verifier and a `state` from the system CSPRNG, and opens
   the authorization URL (`QDesktopServices::openUrl`, injectable). The
   request consents to the scopes of every resource at once.
   `authorizationUrl` stays readable while waiting (a "copy link" fallback).
3. The browser returns to `http://127.0.0.1:<port>/?code=…&state=…`. A
   wrong or missing `state` rejects the flow without exchanging the code; an
   `error` (e.g. `access_denied`) rejects it with the reason. The browser
   gets a static "you can close this tab" page (never the query echoed) and
   the port closes.
4. The code is redeemed with the verifier; the refresh token goes to the
   keyring (`<accountId>/oauth-refresh-<provider>`), then
   `authorizeFinished`. The wait ends after `timeoutMs` (5 min) or
   `cancel()`.

| Provider | Authorize / token | Scopes |
| --- | --- | --- |
| Google | `https://accounts.google.com/o/oauth2/v2/auth`, `https://oauth2.googleapis.com/token`; `access_type=offline&prompt=consent`; the desktop client's `client_secret` is sent (Google requires it and documents it as not confidential) | `https://mail.google.com/ https://www.googleapis.com/auth/calendar`, plus `https://www.googleapis.com/auth/carddav` when the account syncs contacts (M76; an account made before signs in once more to grant it) — ONE token for mail, calendar and contacts |
| Microsoft | `https://login.microsoftonline.com/<tenant\|common>/oauth2/v2.0/authorize` / `…/token` | mail: `offline_access https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send`; calendar: `offline_access Calendars.ReadWrite`; ews: `offline_access https://outlook.office365.com/EWS.AccessAsUser.All`; mailbox (M81): `offline_access MailboxSettings.ReadWrite Mail.ReadWrite` — ONE TOKEN PER RESOURCE |

Microsoft issues a token for one resource at a time, so the code redemption
asks for the primary resource and `ensureToken(requestId, "calendar")`
performs a refresh-token grant with the calendar scopes using the same
refresh token. Refreshes of one resource are coalesced (Microsoft rotates
refresh tokens); a rotated refresh token replaces the stored one.

| Call | Result |
| --- | --- |
| `ensureToken(requestId, resource)` | the cached token while valid (`cached: true`; valid = more than 60 s left), else a refresh; loads the refresh token from the keyring when none is in memory |
| `refresh(requestId, resource)` | always a refresh-token grant |
| `applyAccessToken(provider, "xoauth2" \| "bearer", resource)` | calls the provider's write-only `setSecret(token, mechanism)` |

Token-endpoint errors map as: `invalid_grant` → `rejected` +
`reauthorizeRequired` (the refresh token is forgotten and wiped from the
keyring; run `authorize()` again) — except a *consent gap* (M81): an
`invalid_grant` with `AADSTS65001` for a resource other than the primary
one means the user never granted that resource's scopes (an account signed
in before M81 asking for `mailbox`), so the refresh token is kept, mail and
calendar go on working, and the result carries `consentRequired: true`; `invalid_client` / `unauthorized_client`
→ `rejected` (check the client ID); `temporarily_unavailable`,
`server_error`, HTTP 5xx and unreachable → `offline` + `retryable`. Result
keys add `provider`, `resource`, `expiresAt` (ISO UTC), `scope`,
`hasRefreshToken`, `reauthorizeRequired`, `refreshTokenStored`,
`cancelled`, `cached`.

**Tokens are never logged, never in a result and never a QML property.**
`accessToken(resource)` is C++ only; QML passes tokens to providers with
`applyAccessToken`. `tst_mail_oauth` captures every message (with all debug
categories on) through a full flow, a second-resource grant, a rotation and
an `invalid_grant`, and searches them and every result for the tokens.

### OAuth client IDs

QindaOffice **ships no client ID** (nor does the toolkit, QindaTK D-189).
`OAuthClientRegistry` resolves one per provider, in order:

1. the account's `oauth.clientId` (+ `oauth.tenant`);
2. `$XDG_CONFIG_HOME/qindaoffice/mail/oauth-clients.json` (the user's);
3. `/usr/share/qindaoffice/oauth-clients.json` (a distributor's).

```json
{"schema": "qindaoffice.mail.oauth-clients/1",
 "google":    {"clientId": "1234-abc.apps.googleusercontent.com", "clientSecret": "GOCSPX-…"},
 "microsoft": {"clientId": "00000000-0000-0000-0000-000000000000", "tenant": "common"}}
```

Register a **desktop / native** client (Google: "Desktop app"; Microsoft:
"Mobile and desktop applications" with redirect `http://localhost`, public
client flows allowed) and enable the scopes above. A client secret is read
from the files only; an account-level client ID borrows the secret of a file
entry with the same ID. A file that exists but does not parse stops
resolution with an error naming it. With nothing configured the error says
so plainly — "No OAuth client ID is configured for Google sign-in … For
Gmail mail alone, an app password works instead." — and the dialog offers
the app-password path.

## Secrets

`MailSecrets` keeps secrets **only** in the desktop keyring through
QtKeychain: service `qindaoffice-mail`, key `<accountId>/<service>`.
On the native QindaQt keyring, a missing QtKeychain lookup can read a
preserved imported KWallet **Password** record through the standard Secret
Service API ([D-067](decisions.md#d-067-native-imported-mail-passwords-have-a-typed-read-only-client-bridge)).

| Service | Holds |
| --- | --- |
| `imap`, `pop3`, `smtp` | mail passwords / app passwords / bridge passwords |
| `caldav`, `ews`, `ics` | calendar passwords (feeds with basic auth) |
| `carddav` | an address-book password when it differs from the account's (M76; without it the runtime uses the incoming mail password, else the CalDAV one) |
| `oauth-refresh-google`, `oauth-refresh-microsoft` | OAuth refresh tokens (written by `OAuth2Session`) |

- QML: `write(requestId, accountId, service, secret)`, `remove(…)`,
  `removeAccount(requestId, accountId)` (every service above), `check(…)`
  (`exists`, never the value) and `applySecret(requestId, accountId, service,
  provider, mechanism)`, which hands the secret straight to a provider's
  `setSecret`. No QML call returns a secret. C++ adapters use
  `read/store/erase` with callbacks.
- **No plaintext fallback.** QtKeychain's `insecureFallback` stays off and
  there is no file backend. Without a secret service the result says so
  (`unavailable: true`, "No desktop keyring is available … start a Secret
  Service provider such as KWallet or GNOME Keyring").
- `QQO_MAIL_SECRET_BACKEND=memory` selects one process-wide in-memory
  backend — for tests and headless UI runs that must not touch the user's
  keyring. Tests may also inject `InMemorySecretBackend` directly.
- **Imported Password compatibility.** The normal QtKeychain credential
  wins. Only `NotFound` triggers the bridge; denied, locked, unavailable and
  failed primary lookups keep their original result. An absent native
  `org.qindaqt.Keyring1` name leaves a normal missing lookup unchanged on
  other desktops. When present, that name and `org.freedesktop.secrets` must
  resolve to the same pinned unique owner. The bridge queries exactly
  `qindaqt.import.kwallet.folder=qindaoffice-mail`,
  `qindaqt.import.kwallet.key=<accountId>/<service>` and
  `qindaqt.import.kwallet.type=1`. It rejects duplicate matches, rechecks
  decrypted attributes after unlocking, and decodes one complete bounded
  big-endian QDataStream QString. Stream/Map records, wrong content types,
  null/empty/NUL text, malformed UTF-16 and incomplete/trailing data fail
  closed. No labels, other accounts or private keyring storage are searched.
- A locked imported record uses the native standard `Unlock`/`Prompt`
  flow. Cancellation/refusal or a still-locked item is reported as denied;
  the bridge does not open KWallet or request credentials itself. Calls have
  two-second timeouts and a read has a seventy-second deadline. Provider
  changes reject the read, and a final owner check precedes publication.
  The client-owned plain Secret Service session operates on the same-user
  bus: this is ordinary client assurance, without raw reply-sender or
  executable attestation. Passwords exist only in the necessary application
  callback/provider memory; no secret is logged, exposed to QML or persisted
  outside the keyring. The wrapper retains its primary backend and belongs
  to one event-loop thread. Destruction cancels callbacks, dismisses its
  own prompt and closes its own session on the original pinned owner;
  in-flight resource-creating replies are drained within their call bound.
- Removing an account writes `accounts.json` first, then removes all of its
  secrets (`AccountStore::secretsRemoved`); a keyring failure can orphan a
  secret, never an account. New writes and ordinary removals stay with
  QtKeychain. Imported originals are never changed or deleted by the
  bridge. If one remains after ordinary removal, cleanup returns a clear
  failure directing the user to **Passwords and Keys**. The Mail service
  displays an **Account removed; credential cleanup incomplete** warning;
  complete cleanup emits no warning. There are no filesystem tombstones or
  automatic credential migrations.

The focused codec and private-bus gates can build without desktop archives:
Qt 6.11, Qt6Keychain with libsecret support and `dbus-run-session` are required.

```sh
cmake -S tests/office_mail/native_secrets -B .cache/native-secrets-build -G Ninja
cmake --build .cache/native-secrets-build
ctest --test-dir .cache/native-secrets-build --output-on-failure
```

They use synthetic records and temporary user paths; CTest starts a private
`dbus-run-session`. Coverage includes the actual installed QtKeychain/libsecret
plaintext/base64 schema miss and imported recovery, actual native precedence,
Unicode framing and hostile lengths, mismatch/duplicates/malformed records,
locked/prompt/cancelled reads, owner mismatch/replacement, destruction during
session/prompt creation and honest read-only cleanup. `tst_qindamail_accounts`
covers the visible cleanup-failure notification and successful cleanup silence.

## XOAUTH2 and the toolkit providers

The QindaTK SMTP and IMAP providers build the SASL XOAUTH2 initial response
(`base64("user=<userName>\x01auth=Bearer <token>\x01\x01")`) themselves from
their `userName` and the secret set with `setSecret(token, "xoauth2")`
(CalDAV: `setSecret(token, "bearer")`). Hosts therefore pass the **raw
access token** (`OAuth2Session::applyAccessToken`), never a prebuilt
payload. `XOAuth2::initialResponse` pins the same format (tested against
Google's published example) for protocols the toolkit does not speak yet,
and `decodeErrorChallenge` reads a server's JSON error challenge.

## Not in this library

MX-record discovery; CalDAV principal/home-set discovery (the presets give
the CalDAV root where one is published); SPECIAL-USE folder detection; the
Exchange/Graph calendar adapters (exchange lane); the account dialog and
the mail host ([QindaMail (app)](#qindamail-app), M44). Real hosted accounts are not exercised by the
automated tests (QindaTK WP-089/WP-090, deferred).

## Exchange and Microsoft 365 calendars

`libs/mail_exchange` (M43, [D-044](decisions.md#d-044-exchange-and-microsoft-365-calendars-are-host-adapters-over-the-store))
syncs an Exchange or Microsoft 365 calendar with the toolkit's
`LocalProductivityStore`. It is a *host adapter* in the sense of QindaTK
D-189: `EwsCalendarProvider` (Exchange Web Services, on-premises Exchange)
and `GraphCalendarProvider` (Microsoft Graph, Microsoft 365) expose the
invokables, signals and record fields of `QindaTK::CalDavProvider`, so the
calendar views, the projection and the host treat every calendar alike.

```
mail_exchange → QindaTK (LocalProductivityStore, RecurrenceService, ICal::digest,
                Protocol::startWorker/redact), Qt Core/Network
```

No Quick code, no desktop archives, no dependency on `office_mail`: the
host passes the password or OAuth token with `setSecret()`, exactly as for
the toolkit providers. Namespace `QindaOffice::Mail`; both adapters carry
`QML_ELEMENT` for the app's QML module.

### API (both adapters)

| Kind | Name | Notes |
| --- | --- | --- |
| property | `store` | `QindaTK::LocalProductivityStore*` |
| property | `accountId`, `calendarId` | a record is this adapter's when `remoteAccount == accountId` and its `calendarId` is empty or equal; give each remote calendar its own `calendarId` |
| property | `defaultZone` | IANA zone for items whose zone cannot be read |
| property | `timeoutMs` (15000), `syncPastDays` (30), `syncFutureDays` (365), `autoSave` (true), `allowInsecureAuth` (false) | the window is whole UTC days, at most ten years |
| property (read) | `hasSecret`, `busy`, `state`, `lastError`, `conflicts` | `conflicts`: `[{collection, id, remoteId, remoteDeleted, remoteVersion, remoteFields, reason?}]` |
| invokable | `setSecret(secret, mechanism)`, `clearSecret()` | EWS `basic` (with `userName`) or `bearer`; Graph `bearer` only |
| invokable | `sync(requestId)` → `syncFinished(requestId, result)` | pull the window, then push local edits and new records |
| invokable | `removeRemote(requestId, "events", id)` → `changeFinished` | conditional delete, then local |
| invokable | `resolveConflict(requestId, "events", id, "local" \| "remote")` → `changeFinished` | `local` writes over the version the conflict recorded |
| invokable | `listCalendars(requestId)` → `calendarsFinished` | `result.calendars: [{id, name, color, readOnly, isDefault}]` |

EWS adds `url` (`https://<host>/EWS/Exchange.asmx`), `userName`,
`serverVersion` (`Exchange2013_SP1`, sent as `RequestServerVersion`),
`impersonate` (SMTP address → `ExchangeImpersonation`) and `folderId`
(empty = the distinguished `calendar` folder). Graph adds `baseUrl`
(`https://graph.microsoft.com/v1.0`, injectable for tests and national
clouds) and `graphCalendarId` (empty = the default calendar).

The result map follows QindaTK's provider contract: `requestId`, `ok`,
`state` (`accepted | offline | rejected | conflict`), `error`, `retryable`,
`transcript` (credentials shown as `[credentials redacted]`, bodies as byte
counts), the counts `fetched`, `created`, `updated`, `deleted`, `conflicts`,
and `conflictCount`. HTTP 401/403 is `rejected` with `authRejected: true` (the
host refreshes the token and retries once); 429/503 and EWS
`ErrorServerBusy` are `offline`, `retryable`, with `retryAfterSeconds` from
`Retry-After` or `BackOffMilliseconds`; an unreachable server is `offline`.
Invalid input is rejected before any request and still arrives through the
signal on the next event-loop turn.

### Record fields

Records carry the fields `CalDavProvider` writes, in the same text forms
(`startLocal` `yyyy-MM-dd HH:mm`, date-only with an exclusive end when
`allDay`; `exceptions` and override `recurrenceId` as original local starts
`yyyy-MM-ddTHH:mm:ss`):

| Field | EWS | Graph |
| --- | --- | --- |
| `title`, `description`, `location` | `Subject`, `Body` (text), `Location` | `subject`, `body` (text), `location.displayName` |
| `startLocal`, `endLocal`, `zone`, `allDay` | `Start`/`End` (UTC) in `StartTimeZoneId` (or `StartTimeZone@Id`), `IsAllDayEvent` | `start`/`end` (UTC) in `originalStartTimeZone` (or `recurrence.range.recurrenceTimeZone`), `isAllDay` |
| `recurrenceRule` / `recurrenceRaw` | `Recurrence` | `recurrence` |
| `exceptions` | `DeletedOccurrences`, cancelled exceptions | occurrences the rule makes in the window without an instance, `isCancelled` instances |
| `overrides` | `ModifiedOccurrences` + the exception items' texts | `type: exception` instances |
| `remoteId`, `remoteChangeKey` / `remoteEtag` | `ItemId`, `ChangeKey` | `id` (immutable), `@odata.etag` |
| `remoteUid`, `remoteItemType`, `remoteOccurrences` | `UID`, `CalendarItemType`, exception `ItemId`s | `iCalUId`, `type`, instance ids |
| `readOnly` | `EffectiveRights/Modify` is false | the calendar's `canEdit` is false |
| `organizer`, `attendees`, `isOnlineMeeting`, `onlineMeetingUrl` | `Organizer`, `Required/OptionalAttendees`, `Resources`, `IsOnlineMeeting`, `JoinOnlineMeetingUrl` | `organizer`, `attendees`, `isOnlineMeeting`, `onlineMeeting.joinUrl` |
| `categories` (M81, read; written by `ExchangeMailbox.setItemState`) | `Categories` | `categories` |

Adapter bookkeeping: `remoteAccount`, `remoteBaseline` (the mapped fields
at the last sync, so a write sends only what changed and never rewrites an
HTML body, attendees or categories), `syncDigest` and `syncState`
(`synced | conflict`). Display-only fields are never written back. The
calendar adapters never write `categories`; QindaMail sends a change
through the mailbox adapter and marks the record `categoriesPending` until
the server has it, and a sync keeps the local `categories` of such a
record instead of the server's (M81).

### Sync semantics

- **Discovery.** EWS: `FindItem` with a `CalendarView` of the window
  (`MaxEntriesReturned` 1000; more fails instead of syncing half), singles
  directly and each series through `GetItem RecurringMasterItemId`; full
  items by `GetItem` only when their `ChangeKey` moved. Graph:
  `{calendar}/events` (`$select`, `$filter` for series masters and the
  window's singles) and `{calendar}/calendarView`, every page through
  `@odata.nextLink` (at most 50 pages, same origin only).
- **Local edits** are a changed `QindaTK::ICal::digest` of the mapped fields
  against `syncDigest`; store operations (`upsert`, `shiftEventLocal`,
  `setOccurrenceExcluded`, `setOccurrenceOverride`) need no marking.
- **Writes are conditional.** EWS `UpdateItem` with the stored `ChangeKey`
  and `ConflictResolution="NeverOverwrite"` (`ErrorIrresolvableConflict`,
  `ErrorChangeKeyRequiredForWriteOperations`, `ErrorStaleObject` →
  conflict); Graph `PATCH`/`DELETE` with `If-Match` (412 → conflict). After a
  write the item is read back and its canonical mapping adopted, so the next
  sync is quiet.
- **Conflicts.** Changed on both sides → `syncState: "conflict"`, the server
  copy in `conflicts`, nothing overwritten; `resolveConflict` decides. A
  read-only item edited here, or a change the server cannot express (see
  below), is a conflict with a `reason`.
- **Deletions.** A record missing from the window listing is looked up by id
  when it matters (edited here, recurring, or expected in the window): gone →
  removed here, or a conflict when edited here. `removeRemote` deletes
  conditionally (EWS `MoveToDeletedItems`).
- **Creates.** A record of this adapter without `remoteId` is created
  (EWS `CreateItem`, Graph `POST` with a `transactionId`) and takes the new
  identity even if a later request of the same sync fails.
- **Invitations.** EWS writes use `SendToNone`: nobody is notified. Graph
  has no such switch: moving or deleting a meeting *you organize* with
  attendees makes Microsoft 365 send the update or cancellation. The host
  should say so before writing such a meeting (`organizer`, `attendees` are
  on the record).
- **Occurrences.** EWS addresses an exception by its own `ItemId` and a plain
  occurrence by `OccurrenceItemId` + `InstanceIndex` (its position in the
  pattern, computed with `RecurrenceService`); Graph by the instance id.
  Excluding deletes the occurrence; an override updates it; removing an
  override restores the series values. Neither server can bring back a
  deleted occurrence: that is a conflict.

### Time zones

Exchange stores Windows zone names; records carry IANA ids. The mapping is
Qt's own CLDR `windowsZones` table (`QTimeZone::windowsIdToDefaultIanaId` /
`ianaIdToWindowsId`, the call QindaTK's CalDAV codec uses), preferring the
record's current zone when it shares the Windows zone (Europe/Zurich stays
Zurich although CLDR's default for W. Europe is Berlin). Times travel as UTC
instants (EWS `...Z`; Graph with `Prefer: outlook.timezone="UTC"`) and are
converted to wall time in the event's zone, so a weekly 09:00 stays 09:00
across DST. Writes send the Windows name (EWS `StartTimeZone`/`EndTimeZone`,
Graph `timeZone`); a zone without one is refused. The end is expressed in the
start's zone.

### Recurrence

| Exchange pattern | Record |
| --- | --- |
| Daily, Weekly (`DaysOfWeek`, `Weekday`, `WeekendDay`), AbsoluteMonthly day 1–28, AbsoluteYearly (not 29 Feb) + NoEnd / EndDate / Numbered | `recurrenceRule` (`daily`/`weekly`/`monthly`/`yearly`, `interval`, `weekdays`, `endMode`, `count`, `until`) |
| AbsoluteMonthly day 29–31, AbsoluteYearly 29 Feb | `recurrenceRaw` (`BYMONTHDAY=28,…,31;BYSETPOS=-1`): Exchange uses the last day of short months, RecurrenceService skips them |
| RelativeMonthly / RelativeYearly ("second Tuesday") | `recurrenceRaw` (`BYDAY=TU;BYSETPOS=2`), read-only pattern |
| Weekly, interval > 1, several days, week starting on a day other than Monday (Sunday without Sunday in the set is equivalent) | `recurrenceRaw` (`WKST=…`) |

Local rules are written as the matching absolute pattern; a monthly rule on
day 29–31, a 29 February yearly rule and (EWS only) a yearly interval above
1 cannot be expressed and are refused. A series with a raw pattern can
change its texts and its time of day, not its pattern or its day.

### Credentials and transport

`setSecret` keeps the secret in memory; nothing returns it. Credentials go
only to `https:`, a loopback host, or with `allowInsecureAuth`; redirects are
never followed and a `@odata.nextLink` to another origin is refused, so a
token cannot leave for another host. The secret (and its base64 forms) is
redacted from results, transcripts and errors; the transcript is mirrored to
the `qindaoffice.mail.exchange` debug category (off by default). Requests run
on a worker thread over a job-local `QNetworkAccessManager` (HTTP/2, system
proxy); results are applied to the store on the GUI thread.

### Limits

- EWS needs Exchange 2010 SP1 or later (`StartTimeZone`, `FirstDayOfWeek`);
  the legacy `TimeZone` display string is not mapped.
- Graph knows deleted occurrences and exceptions only inside the sync window
  (it has no `DeletedOccurrences`); an exception moved out of the window is
  found by looking a year to either side.
- EWS `InstanceIndex` counts deleted occurrences (pattern position), as
  Exchange documents it; qualification against real servers belongs to
  WP-089/WP-090 (deferred), as for the toolkit's hosted accounts.
- Bodies are read as text; an HTML body is rewritten only when the
  description is edited here.
- A 403 is treated like a 401 (sign-in problem), per the provider contract.

Tests (`ctest -R exchange`, local servers on 127.0.0.1 only):
`tst_mail_exchange_mapping`, `tst_mail_exchange_ews`,
`tst_mail_exchange_ews_writes`, `tst_mail_exchange_graph`,
`tst_mail_exchange_graph_writes`, `tst_mail_exchange_mailbox` (M81); gates in
[verification.md](verification.md#m43-gates-exchange) and
[M81](verification.md#m81-mail-gates-d-090).

## Exchange and Microsoft 365 mailboxes

`ExchangeMailbox` (`libs/mail_exchange`, M81,
[D-090](decisions.md#d-090-qindamail-rules-categories-flags-search-folders-automatic-replies))
reaches what an Exchange mailbox keeps beside its calendar: automatic
replies, the master category list, the server's inbox rules (listed
read-only) and the categories and follow-up flag of one message or event.
`service` picks the protocol — `graph` (`baseUrl`, bearer only) or `ews`
(`url`, `userName`, `serverVersion`; basic or bearer) — and the adapter has
the providers' surface (`setSecret`, `clearSecret`, `hasSecret`, `busy`,
`timeoutMs`, `allowInsecureAuth`; one request id answered once on a later
turn), so `AccountRuntime::run` signs it in like the calendar adapters. The
transport rules of [Credentials and transport](#credentials-and-transport)
apply unchanged (`HttpTransport`, redaction, https or loopback only).

| Invokable → signal | Graph v1.0 | EWS |
| --- | --- | --- |
| `readAutoReplies` / `writeAutoReplies(settings)` → `autoRepliesFinished` | `GET`/`PATCH /me/mailboxSettings` `automaticRepliesSetting` (`status` disabled / alwaysEnabled / scheduled, `externalAudience` none / contactsOnly / all, UTC `scheduled…DateTime`) | `GetUserOofSettings` / `SetUserOofSettings` (`OofState`, `ExternalAudience` None / Known / All, `Duration`) |
| `listCategories` → `categoriesFinished` | `GET /me/outlook/masterCategories` (`preset0`…`preset24`) | `GetUserConfiguration` `CategoryList` on the calendar folder (the XML's `color` 0…24) |
| `addCategory(name, preset)` → `categoriesFinished` | `POST /me/outlook/masterCategories` (409 = already there) | refused with a reason: Outlook owns that list |
| `listServerRules` → `serverRulesFinished` | `GET /me/mailFolders/inbox/messageRules` | `GetInboxRules` |
| `setItemState(item)` → `itemStateFinished` | message: `GET /me/messages?$filter=internetMessageId eq '<…>'`, then `PATCH` `categories` and `flag` (`flagStatus`, `startDateTime`/`dueDateTime` in the item's zone, `completedDateTime`); event: `PATCH /me/events/{id}` `categories` | message: `FindFolder` for the mailbox, `FindItem` by `message:InternetMessageId`, `UpdateItem` `item:Categories` and `item:Flag` (Exchange 2013+); event: `UpdateItem` by `ItemId` |
| `pullItemStates(days)` → `itemStateFinished` (`items`) | `GET /me/messages?$select=internetMessageId,categories,flag&$filter=receivedDateTime ge …` (paged, at most 20 pages) | `FindItem` over the inbox's recent items with `Categories` and `Flag` |

Settings are `{enabled, scheduled, start, end (ISO UTC), internalMessage,
externalMessage (plain text), externalAudience none | contacts | all}`
(`auto_reply.h` in `office_mail` validates the same map). Messages are found
by their Internet message id because QindaMail reads Microsoft 365 and
Exchange mail over IMAP; the `folder` hint (the IMAP mailbox name) narrows
EWS's search. When the `mailbox` token cannot be had because its scopes
were never granted, `AccountRuntime::run` answers with the token result's
`consentRequired` (see [OAuth](#oauth-20)) and QindaMail keeps the tags on
this computer until the next sign-in. Tests: `tst_mail_exchange_mailbox`
over the existing Graph and EWS fakes (`graph_fake_mailbox.cpp`,
`ews_fake_mailbox.cpp`).

## QindaMail (app)

`apps/qindamail` (M44, [D-042](decisions.md#d-042-qindamail-is-a-document-less-window-over-one-mail-service-per-process)):
binary `qindamail`, desktop id `org.qindaqt.QindaMail`
(`Exec=qindamail %U`, MIME `message/rfc822`, `application/vnd.ms-outlook`,
`application/vnd.ms-outlook-pst` (defined by the suite's
`data/mime/qindaoffice.xml`), `application/mbox`, `text/vcard`,
`text/x-vcard`, `text/calendar`, `x-scheme-handler/mailto`,
`x-scheme-handler/webcal`). M64 added the People workspace, HTML reading,
printing and message files, M76 Outlook data files, contact groups and
CardDAV: [mail-interchange.md](mail-interchange.md) ([D-072](decisions.md#d-072-mail-essentials-contacts-safe-html-message-files),
[D-085](decisions.md#d-085-qindamail-outlook-data-files-contact-groups-carddav)). Static QML module
`QindaOffice.Mail.App` (its QML imports `QindaOffice.Shell`,
`QindaOffice.Mail`, `QindaTK`); namespace `QindaOffice::Apps::Mail`.

```
MailHost (one per main window, DocumentHost without a document)
   └─ MailService (one per process: MailService::shared())
        ├─ LocalProductivityStore   $XDG_DATA_HOME/qindaoffice/mail/store.json
        ├─ LocalAttachmentStore     $XDG_DATA_HOME/qindaoffice/mail/attachments
        ├─ folders.json             last IMAP LIST per account (offline folder pane)
        ├─ view.json (config dir)   conversations, collapsed folders, hidden calendars
        ├─ AccountStore, MailSecrets, AutoConfig          (office_mail)
        ├─ ContactStore             $XDG_DATA_HOME/qindaoffice/mail/contacts.json (M64)
        ├─ PstImporter              File ▸ Import ▸ Outlook Data File (M76)
        ├─ MailOrganizer            rules, categories, flags, search folders, automatic replies (M81, below)
        └─ AccountRuntime per account
             ├─ ImapMailProvider per mailbox | Pop3MailProvider
             ├─ SmtpMailProvider
             ├─ CalDavProvider | IcsCalendarProvider | EwsCalendarProvider |
             │  GraphCalendarProvider per enabled calendar
             ├─ CardDavProvider (accounts with contacts, M76)
             ├─ ImapExtras, SmtpRedirector, ManageSieveProvider, ExchangeMailbox (M81)
             └─ OAuth2Session (OAuth accounts)
```

### Account runtime

`AccountRuntime::run(service, provider, start, done)` starts every remote
request. Before it, the credential is applied to the provider's write-only
`setSecret`: a keyring secret (`<accountId>/<imap|pop3|smtp|caldav|ews|ics|carddav>`;
SMTP with `sameCredentialsAsIncoming` reads the incoming key; CardDAV
without its own key reads the incoming, else the CalDAV one) or an OAuth
access token (`ensureToken(resource)` → `applyAccessToken`; resources `mail`
for IMAP/POP3/SMTP, `calendar` for Google CalDAV and Graph, `ews` for EWS,
`contacts` for Google CardDAV, `mailbox` for Microsoft 365's mailbox
settings and tags). M81 adds the services `sieve` (ManageSieve, signed in
with the IMAP credential: same keyring key, same OAuth token) and
`graphmail` (`ExchangeMailbox` over Graph, resource `mailbox`); an EWS
mailbox runs as `ews` with the calendar's credential. `run(…, affectsStatus
= false)` keeps an optional request (tags, keywords, header blocks,
automatic replies, a rule's copy) from marking the whole account `error`
or `needsSignIn`; a rule's redirect is a send and counts like one.
An authentication rejection (`authRejected`, or the providers' refusal texts:
IMAP `AUTHENTICATIONFAILED`, POP3 `-ERR [AUTH]`, SMTP 535, HTTP 401/403) is
retried once with a forced token refresh or a fresh keyring read; a second one,
a missing password, a dead refresh token or an unconfigured OAuth client sets
the account's `status` to `needsSignIn` (the request never reaches the server
without a credential). Account `status` is `idle | syncing | offline | error |
needsSignIn`. `syncAccount` lists IMAP mailboxes, then syncs INBOX and the
folder on screen, POP3 downloads, every calendar syncs and the address
books sync both ways (M76; a conflict raises the "Contacts changed in two
places" notice); each account has a
timer (`syncIntervalMinutes`, 0 = manual), and the service syncs all accounts
at start and when the network comes back (`QNetworkInformation`).

### Folder keys

| Key | Row |
| --- | --- |
| `all:inbox` | unified inboxes, first row |
| `acct:<accountId>` | account header (`isAccount`, not selectable, collapsible) |
| `acct:<accountId>:<mailbox>` | a folder: the IMAP mailbox name as records carry it in `remoteMailbox` (decoded UTF-8, hierarchy by the LIST delimiter); POP3 `INBOX`, `Archive` (`localMailbox`); local `Inbox`, `Drafts`, `Outbox`, `Sent`, `Archive`, `Trash` |

Special folders come from SPECIAL-USE first, then the account's
`specialFolders`; siblings sort inbox, drafts, outbox, sent, archive, …, junk,
trash. Deleting in an IMAP folder moves to Trash (a real delete in Trash);
archive is `UID MOVE` to the archive mailbox; POP3 read/flag/archive are local
edits and delete also removes the server copy; local mail goes to Trash first.

### The local account and the Sent copy

"On this computer" (id `local`, not in `accounts.json`) always exists: its
Inbox holds opened `.eml` files, Drafts and Outbox are the store's `drafts`
(Outbox = `queued: true`, sent when the network is back), Sent holds local
copies. After an accepted send the copy is filed once: Gmail and Microsoft 365
file it themselves; other IMAP accounts APPEND `composeMessage(draft)` to their
Sent mailbox with `\Seen`; POP3 accounts (or a failed APPEND, with a warning)
keep it in the local Sent folder. Only then is the draft removed. An offline
send keeps the draft queued; a rejected one keeps it with `lastError`.

### Drafts and mailto

Drafts keep `to`/`cc`/`bcc` as address-list strings, `subject`, plain `body`,
`richDocumentJson` (composer), `attachments`, `inReplyTo` (bare id),
`references` and `accountId`. Replies use `Re:` unless a reply prefix (Re, RE,
Re[n], AW, SV, Antw) is present, forwards `Fwd:` unless Fwd/Fw/WG/Tr/RV is;
the signature goes above the quotation; reply-all drops every own address.
A forward copies the attachment bytes it can read into its own ids.
`mailtoDraft(url)` follows RFC 6068 (addresses and `to`, `cc`, `bcc`,
`subject`, `body`, `in-reply-to`; other headers ignored). `qindamail
mailto:…`, a `webcal:` URL, an `.ics` or `.eml` path — on the command line or
handed to the running instance — reach `MailHost::openWithoutDocument`.
`qindamail --attach <path> [--attach <path> …] [mailto:…]` (M48, D-048)
opens a composer whose draft has the files imported into the attachment
store (`attachFiles`; unreadable ones are named in `missingAttachments`).
`main()` folds the options and the mailto: URL into one private
`qindamail-compose:` positional (`src/launch_arguments.h`), so the hand-off
to a running instance carries it too. A mailto: URL never attaches a file,
and the `qindamail-compose:` scheme is not registered as a URL handler.

### QML API

`MailHost` (`host` in `MailWindow.qml`): `service`, `preferencesPageUrl`
(RW), `view` (`mail|calendar|tasks`, RW), `calendarMode`
(`day|workweek|week|month`, RW), `readingPaneVisible`; `compose(draft)`;
signals `composeRequested(draft)`, `accountDialogRequested(accountId)` (empty
= Add Account), `eventDialogRequested()`, `calendarImportRequested()`,
`calendarExportRequested()`, `calendarSubscribeRequested()`,
`searchRequested()`, `moveRequested()`, `taskNewRequested()`. Commands (ids
listed in `mail_host.h`): mail*, cal*, `taskNewAction`; radio groups
`mailView` and `calendarMode`.

For a `needsSignIn` account, `FolderPane` emits
`signInRequested(accountId)`; only its owning `MailWorkspace` routes that
intent through the window's public `MailHost.accountDialogRequested`.
The existing account dialog opens that account's password/app-password or
browser path, displays its public `lastError` (including missing-client
guidance), and retains rejected sign-in/save results. OAuth
`operationFinished` replies are correlated to that dialog's returned
request ID; dismissal or opening another account invalidates late replies.
Opening the dialog itself never authorizes or syncs, and other windows
sharing the service receive no dialog broadcast.

`MailService` (`host.service`):

| Kind | Names |
| --- | --- |
| properties | `accounts` [{id, kind, displayName, emailAddress, color, enabled, status, lastError, lastSync, hasMail, hasCalendar, canSend, usesOAuth}], `accountStore`, `autoConfig`, `secrets`, `store`, `attachmentStore`, `busy`, `statusText`, `online`, `autoSync` (RW), `defaultAccountId`, `ownAddresses`, `sendableAccounts` [{id, displayName, emailAddress}], `folderModel`, `currentFolderKey` (RW), `currentFolderName`, `unreadTotal`, `messageModel`, `searchText` (RW), `filter` (RW: all/unread/flagged/attachments), `threaded` (RW), `selectedMessageId` (RW), `selectedMessage`, `markReadDelayMs` (RW, 500; <0 never), `calendars` [{id, accountId, name, color, visible, readOnly, service}], `writableCalendars`, `events`, `eventsError`, `taskColumns` |
| folders / messages | `toggleFolderExpanded(key)`, `folderTargets(accountId)` → [{key, name, depth, iconName, specialUse, accountId}], `conversation(threadId)`, `messageById(id)`, `markRead(ids, read)`, `setFlagged(ids, flagged)`, `archive(ids)`, `deleteMessages(ids)`, `moveTo(ids, folderKey)`, `fetchAttachment(messageId, attachmentId)`, `openAttachment(…)`, `saveAttachmentAs(…, url)`, `importMessageFile(path)` |
| sync | `syncAll()`, `syncAccount(id)`, `syncFolder(key)` |
| drafts | `newDraft(accountId)`, `replyDraft(messageId, replyAll)`, `forwardDraft(messageId)`, `mailtoDraft(url)`, `attachFiles(draft, paths)`, `saveDraft(draft)` → {ok, id, error}, `discardDraft(id)` → bool, `send(draft)` → requestId |
| accounts | `addAccount(map, password)`, `updateAccount(map, password)`, `removeAccount(id)` (→ AccountStore results; remove adds `purgedRecords`; M76: the account's synced contacts stay, unbound), `signIn(id)`, `testAccount(requestId, map, password)` (M76: a `carddav` step and `addressBooks`), `discoverCalendars(requestId, url, userName, password)`, `discoverAddressBooks(requestId, url, userName, password)` (M76), `accountById(id)` |
| calendar | `setCalendarVisible(id, visible)`, `projectRange(startDate, endDate)` (local dates, half-open), `createEvent(map)`, `updateEvent(map)` (with `occurrenceId` + `scope: "occurrence"` an override), `deleteEvent(eventId, occurrenceId, scope)`, `applyChange(requestId, eventId, proposal)`, `eventById(id)`, `importIcs(url, calendarId)`, `exportIcs(url, calendarId)`, `subscribeFeed(url, name, color)`, `recurrencePreview(eventId, rule)` |
| tasks | `addTask(title)` → id, `moveTask(requestId, taskId, toStatus, index)`, `updateTask(map)`, `deleteTask(id)` |
| signals | `operationFinished(requestId, state, message)` (message actions, sync, sign-in, attachments, iCalendar, tasks), `sendFinished(requestId, result)` (+ `draftId`, `sentCopy` server/imap/local, `queued`), `eventChangeFinished(requestId, state, message)`, `accountTestFinished(requestId, result)` (+ `steps`, `mailboxes`, `calendars`), `calendarsDiscovered(requestId, result)`, `addressBooksDiscovered(requestId, result)` (M76), `notify(title, text, variant)` and the `…Changed` notifiers |

Request-id answers always arrive on a later event-loop turn. `folderModel`
roles: `key, accountId, name, path, depth, specialUse, iconName, unread, total,
selectable, isAccount, expanded` (+ `get(row)`, `indexOfKey(key)`).
`messageModel` roles: `messageId, threadId, threadCount, threadMessageIds,
sender, senderAddress, recipients, subject, snippet, date, dateText, dayGroup,
unread, flagged, hasAttachments, accountId, accountColor, isDraft` (+ `count`,
`get(row)`, `indexOfMessage(id)`). Account and calendar colours are user data
(`""` when unset: the QML picks a theme role).

Tests: `ctest -R qindamail`; gates in
[verification.md](verification.md#m44-gates-qindamail-core--appsqindamail-c).

## Organizing mail (M81)

M81 ([D-090](decisions.md#d-090-qindamail-rules-categories-flags-search-folders-automatic-replies))
brings Outlook's everyday mail handling to QindaMail, under Outlook's names.
The value code — conditions, rules, categories, follow-up flags, search
folders, automatic-reply settings, the Sieve vacation block and the
ManageSieve client — is in `office_mail` (no Quick, headless tests); the
Exchange side is `ExchangeMailbox` in `mail_exchange`
([above](#exchange-and-microsoft-365-mailboxes)); the app's
`MailOrganizer` (`MailService.organizer`, `apps/qindamail/src/mail_organizer*.cpp`)
keeps the state, runs the rules and talks to the servers through the
account runtimes:

```
MailService
   └─ MailOrganizer             organizer.json beside view.json (schema qindaoffice.mail.organizer/1)
        ├─ rules, categories {listKey → list}, searchFolders, baselines,
        │  keywordSupport, ignoredThreads, autoReplies (unknown keys kept)
        └─ through AccountRuntime::run (affectsStatus = false but for a redirect's send):
             ImapExtras        keywords, UID STORE ±FLAGS, header blocks, UID COPY (imap_extras.cpp)
             SmtpRedirector    a redirect's prepared bytes (smtp_redirector.cpp)
             ManageSieveProvider   automatic replies on IMAP servers (office_mail)
             ExchangeMailbox   Graph (`graphmail`) or EWS (`ews`) mailboxes (mail_exchange)
```

Message records gain `categories` (names), `followUp` (below),
`importance` (`low | normal | high`, from `Importance`, `X-Priority` or
`Priority` when the source was read, else set by a rule) and `tagsPending`
(a change not yet on the server); IMAP records also `remoteCategories`
(what the server had at the last look). Events, tasks and contacts carry
`categories` too.

### Rules and Alerts

`mail_rules.h` / `mail_criteria.h`: a rule is `{id, name, enabled,
conditions, exceptions, actions}`; rules run in their order (Rules and
Alerts ▸ Run earlier / Run later), turned-off ones are skipped.

| Conditions (and exceptions) | Value |
| --- | --- |
| `from`, `sentTo` (To or Cc), `subject`, `body`, `subjectOrBody`, `header` | words or addresses, any one matches (case-insensitive substrings) |
| `sentOnlyToMe`, `toMe`, `toOrCcMe` | — (against every account's address) |
| `hasAttachment` | — |
| `importance` | `low \| normal \| high` |
| `size` | `min`, `max` KB (0 = open) |
| `received` | `after`, `before` (`yyyy-MM-dd`, inclusive local dates) |
| `account` | account ids |
| `category` (search folders too) | names; empty = any category |
| `flagged`, `unread`, `olderThan` (`days`) | search folders only |

A message matches a rule when every condition matches and no exception
does. `header` reads the header block: the stored source's when there is
one, else — only when a rule needs it — one `UID FETCH BODY.PEEK[HEADER]`
for the message (IMAP).

| Actions | |
| --- | --- |
| `moveTo`, `copyTo` | `folderKey`; the first move wins and a later rule's move becomes a copy (Outlook files the message in both places); copies stay in the message's account (IMAP `UID COPY`, else a second local record with its own source and attachment bytes) |
| `delete`, `deletePermanently` | to the account's Trash / expunged; delete beats a move and deleting permanently beats both |
| `markRead`, `flag` (`today`…`noDate`), `clearFlag`, `assignCategory` (names), `clearCategories`, `setImportance` | edits of the record, sent to the server like a manual change |
| `forward` | a forward draft through `MailService.send` (the Outbox when offline); never back to the sender |
| `redirect` | the original bytes with a `Resent-From/To/Date/Message-ID` block (RFC 5322 §3.6.6) in an Outbox draft with `redirectOf` and its own copy of the source, delivered by `SmtpRedirector` when online |
| `notify` | a desktop notification (freedesktop `Notify`, `suppress-sound`: Outlook's "display a desktop alert", no sound) with the text, or the rule's name |
| `stop` | stop processing more rules |

Forward and redirect addresses are checked as the composer checks them
(`Mime::parseAddressList`) when the rule is saved. One message's plan is
carried out in a fixed order: tags, read state, forwards, redirects,
alerts, copies, then the move or delete.

**On arrival** the rules run on what a sync of an account's INBOX (or a
POP3 download) added, once that mailbox has a *baseline*: the first
download of a mailbox and a download after a UIDVALIDITY reset are old mail
and never trigger rules (`AGENT-GUARD` in `mail_organizer.h`). Arrivals are
queued and handled one message at a time. **Run Rules Now** (Rules ▸ Run
Rules Now…, or from Rules and Alerts) runs the chosen rules — turned-off
ones too — on a folder (and its subfolders), on all, read or unread
messages, and reports how many it checked and how many a rule acted on.
**Create Rule** opens the editor with the selected message's sender checked
and its subject, the account and "sent to me" offered. Rules export and
import as JSON (`{"schema": "qindaoffice.mail.rules/1", "rules": [...]}`);
imported rules get fresh ids, go after the others, and come in turned off
when they have a problem (an unknown folder, a bad address).
Exchange and Microsoft 365 **server rules** are listed read-only at the
foot of Rules and Alerts with a note; they are changed in Outlook on the web.

### Categories

`mail_categories.h`: a category is `{name, color, preset}`; names compare
case-insensitively. The **master list** is per mailbox: Microsoft 365 and
Exchange accounts use the server's list (Graph `masterCategories`, EWS's
`CategoryList`; refreshed after each INBOX sync, new names added to Graph;
EWS's list and renames or deletions on either are Outlook's to make), every
other account and this computer share the local list, which starts with
Outlook's six colour categories. Outlook's 25 preset colours map to Graph
`preset0`…`preset24` and EWS colour indexes. Categories are assigned to
messages (Home ▸ Categorize, the reading pane's chips, Color Categories…),
events and tasks (their dialogs' Categories field) and contacts (People ▸
Categorize); the message list filters by category and search takes
`category:<name>`. **Server mapping**: IMAP keeps a category as a keyword
(`keywordFor`: spaces become `_`, anything an RFC 3501 atom cannot hold
goes into a modified-UTF-7 `&…-` run, read back exactly; `$`-keywords such
as `$Forwarded` are never categories) when the mailbox's PERMANENTFLAGS
allow new keywords — otherwise the user is told once and the categories
stay local. Exchange and Microsoft 365 messages (read over IMAP) are found
by Internet message id and get `categories` through Graph or EWS; events
on Graph or EWS calendars too (`categoriesPending` until the server has
them). CalDAV events and tasks, POP3 mail and this computer keep them
locally (QindaTK's VEVENT/VTODO mapping has no `CATEGORIES`).

### Follow-up flags

`follow_up.h`: `followUp` is `{status flagged | complete, preset, start,
due, completed, column}`; `flagged` mirrors status `flagged`. Today,
Tomorrow, This Week (due the last work day of the week, start two days
from today but not after it), Next Week (the first to the last work day of
next week), No Date, Custom… (start and due, Ctrl+Shift+G), Mark Complete
and Clear Flag, on the selection; the Quick Click flag in the list sets
Today or clears. The list shows when it is due ("Today", "Tomorrow", a
weekday, a date, "Overdue", "Done"); the reading pane shows "Follow up.
Start by …. Due by …." with Mark Complete and Clear Flag. **Server mapping**: IMAP `\Flagged` (status only); Graph `flag`
(`flagStatus`, `startDateTime`, `dueDateTime`, `completedDateTime`) and EWS
`item:Flag` keep the dates and the completion. **The task board** shows
flagged mail beside the tasks (`mailflag:<messageId>` cards: To do / In
progress while flagged, Done when complete); moving a card to Done marks
the flag complete, back from Done flags it again, deleting the card clears
the flag.

### Search folders

`search_folders.h`: a saved search `{id, name, preset, match all | any,
conditions, accounts}` listed under **Search Folders** in the folder tree
(`searches` header row, `search:<id>` rows) and evaluated over the store
whenever the folder is shown, so it stays live. Presets (Outlook's New
Search Folder list): Unread Mail, For Follow Up, Unread or For Follow Up,
Important Mail, Mail from Specific People, Mail Sent Directly to Me, Mail
with Attachments, Categorized Mail, Large Mail (100 KB by default), Old
Mail (7 days), Mail with Specific Words, and a custom
folder from the rule conditions plus flagged, unread and older-than. Search
folders never list Trash, Junk, Drafts or the Outbox. Folder ▸ Customize
This Search Folder edits one as a custom folder; Delete Search Folder
removes the search, not the mail.

### Automatic replies

Accounts ▸ Automatic Replies… (Outlook's Out of Office), per account:

| Account | How | What is kept |
| --- | --- | --- |
| Microsoft 365 (Graph) | `mailboxSettings.automaticRepliesSetting` (OAuth resource `mailbox`) | on or scheduled, inside and outside replies, outside audience none / contacts / all |
| Exchange (EWS) | `GetUserOofSettings` / `SetUserOofSettings` | the same |
| IMAP with ManageSieve (RFC 5804; `sieve` in the account, else the IMAP host on 4190 with STARTTLS) | a Sieve `vacation` block in the active script | one reply for everyone, a subject, once per sender per day; scheduled with `date` / `relational` (`currentdate :value "ge"/"le"`) |
| POP3, CalDAV-only, this computer | — | says plainly that the server cannot; the dialog's OK stays off |

The Sieve block (`sieve_script.h`) is QindaMail's alone: a require line
marked `# QindaMail automatic reply` first, then `# BEGIN … # END QindaMail
automatic reply` (with the settings as JSON in a comment, for reading back)
after the script's own leading `require`s; turning the reply off restores
the user's script byte for byte. The script is checked with `CHECKSCRIPT`
(when the server has it) before `PUTSCRIPT`; with no active script QindaMail creates and activates
`qindamail` (and removes it again when the reply goes off and nothing else
is in it). A vacation another program put in the script is left alone and
the dialog says so. Credentials: the IMAP keyring key or OAuth token
(`PLAIN`, `LOGIN`, `XOAUTH2`, `OAUTHBEARER`); never in a result or a log.

### Conversations

Clean Up Conversation moves to Trash the read, unflagged, uncategorised
messages of the selected conversation whose text a later message quotes;
Ignore Conversation (Ctrl+Delete) moves the conversation to Trash and does
the same with what arrives in it later (up to 1 000 ignored conversations
remembered); on an ignored conversation the command reads Stop Ignoring
Conversation.

### Commands (M81)

Twenty new persistent ids, none renamed or reused; every one is in the
menu tree and on the ribbon (the squeeze gate holds at 520 px):

| Id | Menu | Ribbon |
| --- | --- | --- |
| `mailRulesAction`, `mailCreateRuleAction`, `mailRunRulesAction` | Message ▸ Rules | Home ▸ Move ▸ Rules; Folder ▸ Rules |
| `mailCategorizeAction`, `mailCategoriesAction`, `mailClearCategoriesAction` | Message ▸ Categorize | Home ▸ Tags ▸ Categorize; People ▸ Manage (Categorize) |
| `mailFlagTodayAction` … `mailFlagNoDateAction`, `mailFlagCustomAction` (Ctrl+Shift+G), `mailFlagCompleteAction`, `mailFlagClearAction` | Message ▸ Follow Up | Home ▸ Tags ▸ Follow Up |
| `mailNewSearchFolderAction`, `mailEditSearchFolderAction`, `mailDeleteSearchFolderAction` | Folder | Folder ▸ Search folders |
| `mailAutoRepliesAction` | Accounts | Accounts ▸ Out of office |
| `mailCleanUpAction`, `mailIgnoreAction` (Ctrl+Delete) | Message | Home ▸ Delete |

### QML API (M81)

`MailService.organizer` (`MailOrganizer`, uncreatable): properties
`revision` (bumped on every change of rules, categories or search folders),
`rules`, `searchFolders`, `serverRules`, `running`. Rules: `newRule()`,
`ruleFromMessage(id)` → `{rule, suggestions}`, `validateRule(rule)`,
`describeRule(rule)`, `saveRule(rule)` → `{ok, id, errors}`,
`deleteRule`, `copyRule`, `moveRule(id, index)`, `setRuleEnabled`,
`exportRules(url, ids)`, `importRules(url)`, `runRulesNow(ruleIds,
folderKey, includeSubfolders, which)` → request id answered by
`rulesRunFinished(requestId, {state, processed, matched, error})`,
`folderLabel(key)`. Categories: `categoriesFor(accountId)`,
`listKeyFor(accountId)`, `categoryColor`, `categoryPalette()`,
`addCategory`, `updateCategory`, `deleteCategory`, `categoriesOf(kind, id)`,
`accountOf(kind, id)`, `setCategories(kind, ids, names)`,
`toggleCategory(kind, ids, name, on)` (kind `message | event | task |
contact`; answered by `tagsFinished`). Flags: `followUp(ids, preset)`,
`followUpCustom(ids, start, due)`, `followUpOf(id)`. Search folders:
`searchFolderPresets()`, `newSearchFolder(preset, argument)`,
`searchFolderByKey(key)`, `saveSearchFolder(folder)` → `{ok, id, key, errors}`,
`deleteSearchFolder(idOrKey)`. Conversations: `cleanUpConversation(id)`,
`ignoreConversation(id)`, `isIgnored(id)`. Automatic replies:
`autoReplyAccounts()` → `[{id, name, address, method graph | ews | sieve |
none, reason}]`, `readAutoReplies(accountId)`, `writeAutoReplies(accountId,
settings)` → `autoRepliesFinished(requestId, {ok, state, available, method,
settings, foreignVacation, error})`, `refreshServerRules()`. Every request
id is answered once, on a later turn. `MailHost` adds the signals
`rulesRequested`, `ruleEditRequested(rule, suggestions)`,
`runRulesRequested`, `categorizeRequested(kind, ids, accountId)`,
`categoriesRequested(…)`, `followUpCustomRequested(ids)`,
`searchFolderRequested(key)` and `autoRepliesRequested`. `messageModel`
roles gain `categoryChips` [{name, color}], `followUpStatus`, `followUpDue`
and `importance`; `MessageListModel.categoryFilter` (RW; a name, `"*"` = any
category, `""` = off) narrows the list.

Tests: `tst_mail_rules`, `tst_mail_organize`, `tst_mail_sieve`
(`office_mail`), `tst_mail_exchange_mailbox`, `tst_qindamail_rules`,
`tst_qindamail_organize`, `tst_qindamail_organizer_ui`; gates in
[verification.md](verification.md#m81-mail-gates-d-090).
