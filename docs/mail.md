<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Mail accounts (`office_mail`)

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

Adapter bookkeeping: `remoteAccount`, `remoteBaseline` (the mapped fields
at the last sync, so a write sends only what changed and never rewrites an
HTML body, attendees or categories), `syncDigest` and `syncState`
(`synced | conflict`). Display-only fields are never written back.

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
`tst_mail_exchange_graph_writes`; gates in
[verification.md](verification.md#m43-gates-exchange).
