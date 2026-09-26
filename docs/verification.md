<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Verification

## Build gates (every milestone)

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Every test runs under `QT_QPA_PLATFORM=offscreen` and, for the QtQuick
windows, `QT_QUICK_BACKEND=software` (pinned via ctest `ENVIRONMENT`; each
test's `main` also sets both defensively). A build against a QindaTK
development prefix needs `-DCMAKE_PREFIX_PATH=<QindaTK>/install`.

The UI tests also write what a reviewer looks at: the structural fixtures
in `tests/qml_fixtures/` (`QQO_UPDATE_FIXTURES=1` rewrites them) and, with
`QQO_GRAB_DIR=<dir>`, a PNG of each window at 1280x800 and 520x700 — saved
*before* the fixture comparison, so a failing layout is still captured.
`QQO_GRAB_PREFIX` prefixes the file names.

**Validation gallery.** `tools/validation_gallery.py` (default
`http://127.0.0.1:8788/`) is a live page of every capture of the current
work: untracked or modified PNGs in the checkout, its `QindaOffice-*`
worktrees and the plan's QindaTK lane worktrees, plus
`~/.local/share/qindaoffice-validation-gallery/inbox/`. Grab straight into
it with `QQO_GRAB_DIR=~/.local/share/qindaoffice-validation-gallery/inbox
QQO_GRAB_PREFIX=M40-`; captures are archived by content hash, so later
runs never overwrite earlier evidence.

## On-desktop manual checklist (run inside a QindaQt session)

From the M0 acceptance criteria. **Verified 2026-09-14** on the live QindaQt
session (compositor queried over `org.qindaqt.Compositor1`; evidence in git
history):

- [x] Windows map as ordinary top-levels with the right identity:
      `applicationId=org.qindaqt.QindaWrite`, title `Untitled — QindaWrite`.
- [x] One process owns `org.qindaqt.QindaWrite` on the session bus; a second
      launch hands off over D-Bus and (with no paths) opens a second
      ordinary window in the primary process.
- [x] Global menu: each window exports a live `com.canonical.dbusmenu`
      endpoint at `/org/qindaqt/QindaOffice/QindaWrite/Menu/<n>` on the
      app's bus connection (identical announcement mechanism to QQTerm).
- [x] `gtk-launch org.qindaqt.QindaWrite` launches the Portage-installed
      binary (launcher path end-to-end).
- [x] Packaging: `emerge -1 gui-apps/qindaoffice` from the `qindaqt` overlay
      builds and installs all four apps + desktop files + icons + MIME
      (M10 acceptance; +charts USE flag drives QQO_WITH_CHARTS and pulls
      dev-qt/qtcharts).
- [x] M10 gates: 385-row function oracle green; chart part lands in the
      xlsx package (series range + title in chart XML); the paged view
      answers QAccessible queries (Document role, cursor/selection);
      TemplateLoader surfaces the bundled starters and a picked template
      opens detached from its path.

Still interactive-only (pointer/focus on a busy live desktop; the
app-side requirements above are the parts the suite owns):

- [ ] Meta+Shift-drag the two windows together — they group as **tabs**;
      drag a tab to a screen edge for a **split** (compositor-side; the
      desktop's own `compositor.hybrid-pointer-nested` harness covers the
      gesture).
- [ ] Eyeball the panel menu against the focused window, and the launcher
      Office category with the suite icons.
- [ ] Dirty a document, `Ctrl+W` — Save/Discard/Cancel consent; kill -9
      mid-edit and relaunch — the Recover dialog offers the journal entry.

## Milestone-specific gates

### M40 gates (the ribbon, D-041)

- **Ribbon gate** (`tests/support/ribbon_gate.h`, one test per app,
  `ribbonReachesEveryCommandAndSqueezes`) at a desktop width and at 520 px:
  every command of the menu tree is reachable from the ribbon — a button
  or a dropdown entry — unless the app lists it as menu-only with a reason
  (Quit, Find Next/Previous, About, Restore Previous, and the colour
  dialogs the colour buttons open); every icon name the layout asks for
  exists in QindaTK; every item of every tab, contextual tabs forced on,
  is visible and inside the ribbon. This replaces the strips' squeeze
  tests: the ribbon squeezes (large → small → icon) and then wraps, it
  never scrolls or hides a command (QindaTK D-297).
- **Live binding**: Write's Bold button toggles the entry and shows its
  state; Calc's Undo follows the entry's enabled state; Note's tools stay
  one radio group; Write's Table and Show's Shape tabs appear only in
  their context.
- **Shell**: `tst_office_window` builds a ribbon from ids (an unknown id is
  skipped, never drawn), flips View ▸ Compact Ribbon, and opens Help ▸
  Search Commands… (Alt+Q) — the palette lists the menu tree's commands by
  section and activates by id.
- **Pixels reviewed**: all four apps at 1280 and 520, dark preset
  (validation gallery, `M40-*`).
- `--list-commands` on every app prints id, label, shortcut, icon and menu
  path — the inventory the ribbon layouts were written against.

Added per milestone in the milestone commit (formula tables, xlsx
round-trips, fodp→pptx conversion, provider smoke tests, pen-input checks,
presenter-view-on-second-screen, reduced-motion, packaging).

### M22 pen gates (automated: `tst_stroke_edit`, `tst_note_input`, `tst_note_ui`)

- office_ink: a pen outline follows its pressure width with round caps;
  the highlighter outline ignores pressure (constant width, flat caps);
  the stroke-mode eraser removes whole strokes and reports misses (no
  phantom undo step); the partial eraser splits a stroke at the gap and
  drops fragments under two samples; a pen stroke crossing a highlighter
  reads as the pen colour (highlighter under ink, one translucent fill).
- App: the eraser acts DURING the drag (stroke gone before release); one
  undo step per eraser gesture and none for an empty swipe; a held barrel
  MiddleButton borrows the eraser and reverts on release; a 20-sample
  stroke repaints only segment-sized rects (never the whole page); the
  page stays white under a dark palette (D-004 pin); P/H/E switch tools
  and type into a focused find field instead of switching; three quick
  edits coalesce into one debounced page write; the strip is icon-only
  for the shipped glyphs and keeps every command visible squeezed to a
  third of the window width.

Manual (QindaQt desktop, tablet):

- [ ] Pressure looks right (thin at a light touch, swelling under
      pressure); Settings ▸ Preferences ▸ Notes ▸ "Pen pressure changes
      width" off → uniform width; smoothing Off/Light/Strong felt.
- [ ] No lag at 5 000 strokes; a palm resting on the screen does nothing.
- [ ] Highlighter sits under pen ink; the eraser ring cursor tracks
      radius × zoom; the stylus eraser end and the barrel button erase
      while held and revert on release.
- [ ] Dark theme: the page is white, ink readable; strip icons legible at
      16 px on light and dark toolbars.
- [ ] Single-letter shortcuts switch tools; with the navigator TREE
      focused a letter switches tools instead of keyboard-searching (the
      tree does not take ShortcutOverride — accepted, D-026); Esc returns
      to Pen; Ctrl+wheel and the zoom combo agree.

### M11 UI completeness (automated: `tst_write_ui`)

- Main + formatting toolbars exist with persistent named actions
  (objectNames from the shell's `ActionId` table); Format/Insert menus
  exist after `finalizeAppMenuHooks()` and share the same action objects.
- Bold/italic/underline/strikethrough toggle and re-read from the cursor;
  font family/size combos apply and sync; alignment, bullet/numbered
  lists (toggle + convert), indent clamp at 0.
- Insert Table (2x3 bordered) and Insert Columns (1xN borderless) land as
  real `QTextTable`s; find wraps and reports, replace-all counts.
- Edit actions follow control availability; a pristine document has zero
  undo steps (Qt `setPageSize` undo quirk cleared); zoom actions move the
  view; the paper palette stays white-on-dark under any theme.

### M15 additions (automated: `tst_qindanote`)

- Pen width (1-24 px) and pen color flow into newly inked strokes
  (mouse-fallback inking verified stroke width/color).
- Page background choice (blank/ruled/grid/dotted) edits the envelope and
  reports pageEdited.
- Zoom (50-200%) scales the page rect and repositions text blocks while
  inked coordinates stay in page space (identical samples at 1x and 2x).

### M14 additions (automated: `tst_write_ui`, `tst_write_view`)

- Table menu + toolbar section: row/column insert+delete, merge, split -
  all persistent named actions enabled only inside a table; deletion keeps
  the cursor in the table.
- Page Setup dialog edits header/footer ({page}/{pages} tokens); the
  exported PDF carries header and per-page footer text on every page
  (poppler-verified); HTML save/reload round-trips page size, margin and
  header/footer metas.

### M13 additions (automated: `tst_show_ui`, `tst_show_model`)

- Shapes toolbar + Insert/Shape menus share persistent named actions
  (objectNames from the shell ActionId table).
- Bold/italic/fill/stroke mutate through the canvas with undoable
  before/after payloads; two undos walk a fill then a bold back.
- The shape clipboard round-trips through the JSON codec with a fresh id
  and a +12 pt offset; cut removes the source; paste selects the new shape.
- Insert actions land shapes through the normal undo path; editing actions
  follow selection and clipboard shape; Select All selects every shape.

### M12 additions (automated: `tst_calc_ui`)

- Undo/redo restores values AND re-evaluates formulas (dependency index
  rebuilt by the snapshot restore); sheet insert is undoable and the tab
  bar follows `structureChanged`.
- Rich clipboard round-trips formulas exactly; plain TSV pastes numbers,
  booleans, text and formulas; cut clears in one undo step.
- Bold/italic/underline, alignment, font family/size, text/fill colors and
  the number-format combo write `CellStyle`s through one-undo-step
  `StylePatch`es; the UI state re-reads the cursor cell.
- The find bar walks matches row-major with wrap; F3/Shift+F3 drive it.

### M11 on-desktop checklist (interactive)

- [ ] Under the QindaQt dark theme a .docx opens as a white page with dark
      text (no "white boxes on dark"), matching PDF export.
- [ ] Toolbar icons resolve from the Breeze theme (offscreen runs show the
      text fallback by design); `Ctrl+F` opens the find bar, Esc closes it.

### M1 additions (automated)

- `tst_single_instance` — two helper processes over a private session bus:
  primary acquires, secondary hands off and exits 0. Skips with an explicit
  message when the environment cannot spawn a bus or denies Qt name
  ownership (some sandboxed/elevated contexts).
- `tst_libre_office_converter` — live txt→pdf conversion against the
  installed soffice; skipped when the conversion pipeline is unavailable.
  Every soffice child — the converter's and the ones tests start through
  `tests/support/soffice_env.h` — runs with `SAL_USE_VCLPLUGIN=svp` and
  without `QT_QPA_PLATFORM`: a QindaQt session exports
  `SAL_USE_VCLPLUGIN=qt6`, and with the tests' offscreen platform soffice
  aborted before converting, which turned the live cases into failures or
  silent skips. Layout probes compare against laid-out line heights, never
  a literal, because the session's platform theme (`qindaqt`) picks the
  default font even under `offscreen`.
- `tst_office_application` — canonical-path uniqueness (duplicate open
  activates), pristine-untitled reuse, recents bookkeeping, 32-window bound.
- `tst_recovery_journal` / `tst_restore_inventory` / `tst_recent_files` /
  `tst_atomic_file_store` / `tst_plain_text_document` — store bounds,
  debounce, round trips, optimistic-revision inputs.

### M1 manual checklist (live desktop)

- [ ] Open a file from the file manager: it lands in a new ordinary window
      (or a pristine untitled one); opening the same file twice activates
      the existing window.
- [ ] Edit, close: Save/Discard/Cancel consent appears; Discard clears the
      recovery entry; Save clears it after writing.
- [ ] Kill the app mid-edit; relaunch: a Recover dialog offers the unsaved
      document; Restore brings it back dirty, Discard forgets it.
- [ ] Settings ▸ Restore Previous Documents (off by default); with it on,
      the previous session's paths reopen at launch.
- [ ] Second launch (`qindawrite file`) from a terminal while one instance
      runs: paths hand off over D-Bus; `--new-instance` bypasses.

### M16 shell-reachability audit

Automated by `tst_document_window`, `tst_calc_window`, `tst_recovery_journal`
and `tst_office_ai`.

Rationale: the milestone suites drive documents directly, so a command can
be fully wired and still be unreachable from the UI. This pass dumps every
window-scoped `QAction` of the four real windows under
`QT_QPA_PLATFORM=offscreen` (enabled state, objectName, shortcut) and
asserts the reachability contract. See D-011.

- A document installed through `setDocumentHost()` enables Save and Save
  As; before this pass they were disabled in all four apps, so File ▸ Save,
  the toolbar Save button and Ctrl+S did nothing.
- Close Window is `Ctrl+W` and Quit is `Ctrl+Q` (AGENTS.md rule 1), asserted
  as literal chords — `QKeySequence::Close`/`::Quit` resolve to Ctrl+F4 and
  the "Exit" media key under the keyboard scheme the qindaqt platform theme
  yields.
- Every menu action in QindaWrite/QindaCalc/QindaShow/QindaNote carries a
  non-empty objectName (rule 5); the QindaNote tool strip has the toolbar
  objectName `QMainWindow::saveState()` requires.
- QindaWrite has exactly one `&AI` menu (the inline actions live in the
  shell's AI menu next to Assistant…, as in QindaShow).
- An empty `recoverySnapshot()` is not written as a journal entry, so the
  stub engines (QindaCalc, QindaNote) cannot produce a Recover prompt whose
  restore always fails.
- `ModelCatalog::configFor()` resolves every id `modelIds()` hands out, and
  a corrupt `providers.json` is kept and reported instead of being replaced
  by the seed.
- QindaCalc: undo back to the saved position clears the dirty mark (dirty
  follows the `QUndoStack` clean index); chart insert is one undo command.

### M17 capability completeness

Automated by `tst_qindanote`, `tst_calc_window`, `tst_calc_io` and
`tst_office_ai`. Rationale in D-012: a declared capability that is a stub is
a defect, and the stubs were hiding a data-loss bug. Live-LibreOffice cases
skip with a message where `soffice` is unavailable.

- QindaNote: saving a notebook after opening ONE page leaves every other
  page's file intact (`save()` writes only loaded pages; Save As
  materialises unloaded ones from the source first).
- QindaNote: Edit ▸ Redo replays an undone stroke, follows the view's real
  history depth, and a fresh edit ends the redo branch.
- QindaNote: `recoverySnapshot()` round-trips sections, page titles, ink and
  page background; an untitled notebook recovers entirely from memory, a
  saved one re-attaches to its directory so unopened pages stay on disk.
- QindaCalc: `recoverySnapshot()` round-trips formulas (re-parsed and
  recomputed), typed values (the text `007` stays text), styles set on EMPTY
  cells, extra sheets, named ranges and chart specs.
- QindaCalc: an `.ods` saves and reopens through the LibreOffice bridge with
  its formula recomputed; `mimeTypes()` matches the `.desktop` MimeType list.
- calc_io: a named range survives the xlsx round trip, and `=SUM(NAME)` still
  evaluates after import (names are read before the cells, so the dependency
  index sees them).
- office_ai: `configFor()` resolves every id `modelIds()` hands out; a
  corrupt `providers.json` is moved to `.corrupt` and the picker still works.

### M44 gates (QindaMail core — `apps/qindamail` C++)

- **UI** (`tst_qindamail_ui`): the real `MailWindow.qml` over a seeded store —
  the inbox lists the messages with day headings, a click fills the
  reading pane (subject, attachment chip), the ribbon gate holds in the
  Mail, Calendar and Tasks workspaces at 1280 and 520 px (a command counts
  as reachable when some workspace's ribbon reaches it; Save/Save As and
  the edit commands are menu-only in a document-less window), the
  composer window and the account dialog open, structural fixtures
  `qindamail_{1280,520}.txt`. Grabs reviewed: `M44-iter4-*` in the
  validation gallery (inbox, calendar week with the now line, tasks,
  composer, account kinds, 520 px).

Automated by `tst_qindamail_drafts`, `tst_qindamail_service`,
`tst_qindamail_accounts`, `tst_qindamail_calendar`, `tst_qindamail_host` and
`tst_qindamail_flow` (`ctest -R qindamail`). Each test runs a `MailService`
over its own temp tree with an in-memory keyring, `autoSync` off and the
network watcher off; the only servers are the tests' own POP3/SMTP fakes on
127.0.0.1. Rationale in D-042; reference in [mail.md](mail.md#qindamail-app).

- [x] Drafts: address lists, Re:/Fwd: rules (Re, RE, Re[2], AW / Fwd, FW),
      quoting, reply / reply-all (own addresses in any case and duplicates
      removed; a reply to one's own message goes to its recipients), forward
      header block, In-Reply-To and References.
- [x] `mailto:` — every example of RFC 6068 §6 (percent-encoded `%`, `?`,
      `&`, quotes, UTF-8 subject, IDN domain to ACE, `to=` merged with the
      path, `+` kept), other schemes refused.
- [x] Message list rules: dateText (time today, weekday this week, date
      before), day groups, multi-word search, unread / flagged / attachment
      filters.
- [x] The local account and its folders exist with no account; folder
      counts (per folder, all inboxes, `unreadTotal`); collapse/expand;
      account headers not selectable; conversations, filters and search
      through the service; the conversations setting survives a restart.
- [x] Selecting an unread message marks it read after `markReadDelayMs`,
      and not when the selection moves on first; `selectedMessage` carries
      `accountId`, `to`, `bodyTruncated`, `attachments`.
- [x] Local actions (flag, archive, move, delete → Trash → gone) answer
      `operationFinished` on a later turn; cross-account moves and unknown
      ids are refused.
- [x] Reply / forward / mailto drafts from the service; a forward owns
      copies of the attachment bytes (discarding it never deletes the
      message's); drafts persist; sending from the local account is refused
      and keeps the draft.
- [x] An opened `.eml` lands once in the local Inbox with its attachment
      bytes; opening an attachment writes only store bytes to a private file.
- [x] Accounts: add writes the password to the keyring only (never to
      `accounts.json`); remove wipes every secret, purges the account's
      cached messages/events and keeps its drafts; secret-named fields are
      refused.
- [x] Protocol objects per kind (gmail, imap preset, microsoft365, pop3,
      exchange): host, port, security, user, mechanism, the secret key SMTP
      shares, the calendar provider type and URL, the OAuth resource, the
      sent-mail rule; one IMAP provider per mailbox.
- [x] Credentials before every request: a stored password reaches the
      provider (a closed loopback port answers `offline`); no stored password
      is `needsSignIn` without a connection; the auth-rejection
      classification (IMAP, POP3, SMTP 535, HTTP 401, not TLS policy).
- [x] Calendar: create / update / delete (series, occurrence, following),
      the projection of a weekly rule with an exception clipped to the
      requested days, grid `shift-local` / `resize-local` in occurrence,
      series and following scopes (a second split included), refusals,
      visibility (persisted), recurrence preview, `.ics` export → import
      round trip (rule and EXDATE kept, re-import idempotent).
- [x] Tasks: add, move between columns, update, delete, persisted.
- [x] Host: 30 command ids with shipped Lucide icons, unique shortcuts, the
      `mailView` and `calendarMode` radio groups, enabled state from view and
      selection, the panel export accepted by the desktop's validator with
      every exported id resolving and every command in a menu, `mailto:` and
      `.ics` paths handed to the window, `MailWindow.qml` instantiates.
- [x] Flow against local servers: POP3 sync with the keyring password, a
      reply sent over SMTP and filed in the local Sent folder (draft gone),
      a wrong password retried once and then `needsSignIn` (cleared by a new
      password), an unreachable SMTP server leaves the message in the
      Outbox; no password in results, `store.json` or `accounts.json`.
- [ ] Lead lane: QindaMail fixtures and grabs (1280/520), ribbon squeeze,
      the launcher card.
- [ ] On the desktop: a real IMAP account (and Gmail/Microsoft 365 with a
      registered client ID, WP-089/WP-090), `qindamail mailto:…` from a
      browser, a `webcal:` link, the panel menu.

### M43 gates (mail accounts, autoconfig, OAuth, secrets — `office_mail`)

Automated by `tst_mail_accounts`, `tst_mail_autoconfig` and `tst_mail_oauth`
(`ctest -R mail`). Offline: every server is a `FakeHttpServer` on
127.0.0.1 and the keyring is in memory (`QQO_MAIL_SECRET_BACKEND=memory` in
the ctest environment). Rationale in D-043; reference in `docs/mail.md`.

- [x] `jsonRoundTripKeepsUnknownFields`: unknown keys at the root, in an
      account, in `incoming` and in a calendar entry survive load → update →
      save; the typed fields round-trip exactly.
- [x] `validation` (12 rows): kind, address, host, port, clear text to a
      non-loopback host, `xoauth2` without a provider, a calendar on POP3,
      `http:` CalDAV, Microsoft with a password, sync interval, and a
      secret-named key nested or at the top — each refused with its field
      path, and nothing written.
- [x] `noSecretOnDisk`: with a password in the (in-memory) keyring, the
      written `accounts.json` contains neither the password nor the word.
- [x] `removeWipesSecrets`: removing an account removes every service's
      secret for it and leaves another account's alone.
- [x] `corruptFileIsMovedAside`, `newerSchemaIsReadOnly`: never overwrite
      what cannot be read or what a newer build wrote.
- [x] `presets` (14 rows) and `everyPresetMakesAValidAccount`; `draftFor`.
- [x] `clientRegistryOrder`: account → user file → system file; a
      secret borrowed for a matching ID; a broken file reported; the
      "no client ID" text with the Gmail app-password hint.
- [x] `secretsInMemory`: `check` never returns the value, `applySecret`
      reaches `setSecret`, unknown services and bad ids refused, a missing
      secret service reported (`unavailable`) with nothing stored.
- [x] `qmlTypesRegistered`: `import QindaOffice.Mail` creates all four
      types and wires them.
- [x] `parseGmail` / `parseOutlook` / `parseFastmail` /
      `parsePlaceholdersAndSkipPlainText` over ISPDB-shaped fixtures
      (`tests/office_mail/fixtures`), `parseRejectsGarbage` (5 rows).
- [x] `presetNeedsNoNetwork`, `fallbackOrder` (preset → autoconfig →
      well-known → ISPDB, the address sent only to step 2),
      `firstSourceWins`, `invalidDocumentFallsThrough`, `nothingFound`,
      `unreachableIsOffline`, `httpsOnly`, `oversizedAndSlowAnswers`,
      `cancelAndBadInput`.
- [x] `pkceMatchesRfc7636AppendixB` (the RFC's verifier → challenge
      vector) and verifier/state shape.
- [x] `authorizationRequest`, `fullFlowMicrosoft`,
      `googleOneTokenAndClientSecret`: the test plays the browser; the
      fake token server checks code, verifier against the challenge,
      redirect URI and client; the refresh token lands in the keyring and
      the loopback port closes.
- [x] `secondResourceSameRefreshToken`: the Graph token comes from a
      refresh-token grant with `offline_access Calendars.ReadWrite` and the
      same refresh token (`+/=` intact); a rotated refresh token is stored.
- [x] `stateMismatchRejected` (no code exchange), `deniedConsent` (nothing
      reflected into the page), `refreshFromKeyringThenCached`,
      `concurrentRefreshesShareOneGrant`, `invalidGrantNeedsReauthorization`
      (token forgotten and wiped), `serverTroubleIsOffline`,
      `timeoutAndCancel`, `applyAccessToken`, `configureFromRegistry`,
      `xoauth2Format` (Google's published example).
- [x] `tokensNeverLogged`: every message of a full flow with all debug
      categories enabled, and every result, is free of the tokens.
- [ ] On the desktop (needs a registered client ID; QindaTK WP-089/WP-090):
      sign in to a real Gmail and Microsoft 365 account from the account
      dialog (M44), confirm the browser page, the keyring entry (service
      `qindaoffice-mail` in Seahorse or KWalletManager) and a mail sync
      with the token.

### M43 gates (Exchange)

Automated by `tst_mail_exchange_mapping`, `tst_mail_exchange_ews`,
`tst_mail_exchange_ews_writes`, `tst_mail_exchange_graph` and
`tst_mail_exchange_graph_writes` (`ctest -R exchange`), against in-process
EWS and Graph endpoints on 127.0.0.1 — no real network. Rationale in D-044;
behaviour in [mail.md](mail.md#exchange-and-microsoft-365-calendars).

- [x] Initial sync maps a single meeting (organizer, attendees, join URL),
      a read-only all-day event, a weekly Denver series across the 8 March
      2026 DST change with one deleted and one moved occurrence, and a
      second-Tuesday series (`recurrenceRaw`); the toolkit projection puts
      the series at 16:00Z, then 15:00Z, the moved instance at its new time
      and nothing on the deleted day (EWS and Graph).
- [x] A second sync, and a sync after reloading the store file into a new
      store and adapter, fetches and writes nothing.
- [x] Graph pages every collection through `@odata.nextLink`; an exception
      moved outside the window is an override, not a deletion.
- [x] A local edit is written with the stored `ChangeKey` /
      `If-Match: <etag>` and only the changed field; a server edit plus a
      local edit is a conflict with nothing overwritten; `resolveConflict`
      `local` writes over the recorded version (a further server edit gives
      412 and a fresh conflict), `remote` adopts the server copy.
- [x] Local create (EWS `CreateItem` / Graph `POST` with `transactionId`)
      in Europe/Zurich sends the Windows zone and keeps the record's zone;
      `removeRemote` deletes conditionally; a server deletion removes the
      record, or makes a conflict when it was edited here.
- [x] Occurrence edits: a moved occurrence is `UpdateItem` on
      `OccurrenceItemId` `InstanceIndex` 4 / `PATCH` of the instance; an
      exclusion is `DeleteItem` #5 / `DELETE` of the instance; restoring a
      deleted occurrence is a conflict with a reason.
- [x] 401 → `rejected` with `authRejected`; 429/503 `Retry-After` and EWS
      `BackOffMilliseconds` → `offline`, `retryable`, `retryAfterSeconds`;
      refused connection → `offline`; plain `http:` to a non-loopback host
      and a foreign `@odata.nextLink` → `rejected` before the token is sent.
- [x] The password, `user:password` base64 and the tokens appear in no
      result, transcript, captured log line (Qt network categories and the
      adapter's own category enabled) or saved store/recovery file.
- [x] Windows↔IANA mapping (preferred zone kept), DST wall times, pattern
      mapping incl. Exchange's month-end semantics, Retry-After forms, XML
      DTD/depth refusal (`tst_mail_exchange_mapping`).
- [ ] Against a real Exchange 2016+/Exchange Online mailbox (WP-089/WP-090):
      `InstanceIndex` addressing, the Graph `$filter`, all-day events created
      in Outlook, attendee notifications from Graph edits.

### M48 gates (Mercury and Stripe connections)

Automated by `ctest -R 'books_connect|qindabooks_connections'` against a
fake Mercury + Stripe API on 127.0.0.1 (`tests/books_connect/
fake_bank_api.h`, the documented response shapes; the fake checks the
Bearer credential and echoes a refused key in its error, as Stripe does),
an in-memory keyring, `TZ=UTC`, "today" 2026-03-31. No real network.
Rationale in D-048; the model in
[books.md](books.md#bank-and-payment-connections-mercury-stripe).

- [x] `tst_books_connect_mercury`: v3 storage — the company uuid is
      stable, `Connections::save` refuses token-shaped values and unknown
      providers, a payment link is current only at the open balance; a
      refused token (401) is not stored and not echoed; the accepted one
      is stored under `<uuid>/mercury`, sent as `Bearer`, never in the
      file; linking creates "Mercury Checking ••1234" or reuses a bank
      account (not an expense account), unlinks; sync over three pages
      (offset paging, `start` 90 days back, `order=asc`) imports the
      three `sent` transactions and skips pending/failed, dates on
      `postedAt`, payee/memo/FITID/cents exact; a second sync imports 0
      and starts from the cursor; the pending one imports once when it
      posts; rules suggest on synced lines; http to a non-loopback host
      is refused before any request; disconnect removes the token and
      keeps the feed; a lost token reports "needs its API token again";
      no token in captured logs.
- [x] `tst_books_connect_stripe`: a refused key is not stored and the
      echoed key is redacted; connect creates "Stripe (USD)" (bank) and
      "Merchant Fees" (expense), payouts default to the Mercury-linked
      account, reconnecting creates nothing new; a six-transaction
      history over three pages (`starting_after`, `expand[]=data.source`):
      charge with invoice metadata → invoice Paid and the gross line
      matched, fees categorized to Merchant Fees (3.20 + 1.75 + 15.00 +
      0.05), a plain sale, refund and dispute left New, the payout booked
      as a transfer and matched, the Mercury "STRIPE TRANSFER" deposit
      matched to it — **Mercury first and Stripe first** — never income;
      re-sync imports and applies nothing; after reviewing the rest the
      clearing account = Stripe available + pending (35.00) and the trial
      balance balances with no integrity problems; another company's
      invoice metadata is left for review with a note; a payment link
      sends `unit_amount` = open balance, `currency`, `product_data[name]`,
      metadata and `payment_intent_data[metadata]` (invoice, company uuid,
      number), `restrictions[completed_sessions][limit]=1`, form-encoded,
      with Idempotency-Keys per invoice + amount; a second request for the
      same amount reuses the link without a request; after a partial
      payment a new link for the new balance with a new key.
- [x] `tst_books_io_forms::payOnlineLinkIsPrinted`: a stale link is not
      printed; a current one prints "Pay online: <url>" under PAYMENT
      INSTRUCTIONS (PDF text via Poppler).
- [x] `tst_qindabooks_connections`: the QML API end to end —
      `connections()` states, `connectMercury` (refused, then accounts
      with `balanceText`), `linkMercuryAccount`/`mercuryLinks`,
      `connectStripe`/`stripeSettings`/`saveStripeSettings` (a non-bank
      clearing account refused), `createPaymentLink` →
      `paymentLinkFinished` and `document(id).paymentLinkUrl`,
      `emailDocument` launches `qindamail --attach <pdf> mailto:…` with
      "Pay online: <url>" in the body, `syncConnections` →
      `connectionSyncFinished` (status `syncing` meanwhile; a second
      request fails fast with its own id), `disconnect` removes the
      keyring entry; no token in any result, log or the company file; a
      company without connections never asks the keyring.
- [x] `tst_qindabooks_host`: `booksSyncBankAction` (F5, `refresh-cw`,
      "Sync Mercury & Stripe") and `booksConnectionsAction` (`link`,
      emits `connectionsRequested`) exist, first in the Banking menu;
      `tst_qindabooks_menu_export` passes the validator with them.
- [x] `tst_qindamail_host::attachHandOff`: `--attach` (both spellings,
      repeatable, relative paths made absolute) folds into one
      `qindamail-compose:` token; the draft has the mailto: fields and the
      files imported into the attachment store (a PDF's bytes and MIME
      type); a missing file is named in `missingAttachments`; a mailto:
      `attach=` is ignored.

Remaining for the lead: `ConnectionsDialog.qml` (a registered stub), the
two commands on the ribbon's Banking tab (the ribbon gate exempts them
until then — remove the exemption in `tst_qindabooks_ui` with that
change), and the on-desktop checks: connect a real read-only Mercury token
and a Stripe test-mode restricted key, sync, pay a test-mode payment link
with Stripe's test card and see the invoice turn Paid on the next sync.

### M50 gates (QindaDiagram, D-050)

Automated by `ctest -R diagram` (headless, temp dirs and XDG dirs).
Rationale in D-050; the model and API in [diagram.md](diagram.md).
QindaDiagram builds against a QindaTK prefix that has D-302
(`ShapePalette` tiles, category menu, drag reports — QindaTK branch
`work/diagram-shape-palette`).

- [x] `tst_diagram_model` (12): ids never reused (a hand-edited file's
      next id is skipped); the catalogue has every stencil the brief names
      in seven categories, each with a non-empty outline and a thumbnail;
      paper names both ways; case-insensitive shape data; the `.qdiagram`
      round trip keeps every field and unknown members on the root, a page
      and a shape (written back out); newer, foreign, malformed and
      page-less files refused; atomic save/load (a missing directory
      fails with a message); clipboard fragments carry connectors between
      copied shapes only; `DrawingDocument` — one undo step per real edit,
      no step for a no-op, merged nudges, dirty follows the clean state,
      recovery is exact and dirty; save/load resets the stack; themes
      restyle colours and fonts, keep widths and label sizes.
- [x] `tst_diagram_ops` (13): deleting a shape frees its connectors in
      place; a lane carries the shape inside it and a free end follows
      its shape, locked shapes stay; align/distribute (equal gaps,
      outermost stay); rotate/flip about the selection; z-order in both
      directions; one-level groups expand selections; duplicate and paste
      re-glue copies (ports kept) and free foreign ends; shape/text/
      connector patches, format painter, data; find (text and data) and
      replace; pages (add, duplicate with fresh ids, move, rename, the
      last page stays) and fit-to-contents; glue picks a port, then the
      outline, else free.
- [x] `tst_diagram_output` (10): orthogonal routes are Manhattan, leave
      along their port and detour round a blocker; dynamic glue faces the
      other end; straight meets the outline; curved is a cubic; line jumps
      hop the later connector only; layered layout ranks a flowchart with
      a loop down one axis and re-glues bottom → top, left-to-right too;
      the org chart from CSV (quoted names, data columns, an unknown
      manager reported, a hanging team of four); draw.io import (lane
      children in page coordinates, HTML labels, `UserObject` data, edge
      style/dash/labels, a free end; compressed and non-draw.io files
      refused); PNG (page size, selection with transparency), SVG, the
      paged print painter, bounds including icon captions; the stencil
      sheet grab.
- [x] `tst_qindadiagram_host` (9): 101 `diagram…` commands with labels
      and icons, no shared shortcut; enablement and the Shape/Connector
      ribbon contexts follow the selection; the tool radio group mirrors
      the service; pages, paper and orientation checks, themes; styles,
      grow font, format painter (refused without a source), data; the
      canvas adapter's proxies, a move and a resize each one undo step,
      stale edits refused; copy in one window, paste in another (glue
      kept), Copy as Picture; CSV and draw.io import through the service;
      PNG/SVG export; the print callback's PDF; save and reopen.
- [x] `tst_qindadiagram_menu_export` (2): the exported tree — File Edit
      View Insert Format Arrange Design AI Settings Help — passes the
      desktop's validator with and without a selection; every `diagram…`
      command appears exactly once and every exported id resolves and
      fires (90+ pulled).
- [x] `tst_qindadiagram_ui` (6): the real `DiagramWindow.qml` over three
      seeded drawings — a volunteer-onboarding flowchart across three
      swimlanes, a nonprofit's org chart built from CSV, an office network
      in a container — at 1320 and 520 px (drawers below 900 px, minimap
      over the network page); a decision's inspector (geometry, fill,
      line, effects, quick styles) and a server's shape data; typed search
      across stencils; a panel drop lands (centre-snapped) where released;
      the connector tool glues both ends; arrow-key nudges merge and undo;
      F2 edits text in place and Escape stores it; the ribbon gate at 1320
      and 520 (the assistant menu-only: nothing to offer a drawing yet);
      structural fixtures `qindadiagram_{1280,520}.txt`.
      Grabs reviewed: `M50-iter1…4-*` (flowchart, org chart, network at
      1320/520, inspector, shape data, stencil search and the Network
      stencil, new drawing, text editing, ribbons, the stencil sheet).

Remaining for the lead: the launcher card; the on-desktop checklist
(drag shapes with a real pointer and tablet, print a page and preview
it, open two drawings side by side and paste between them, the panel
menu, dark and light themes).
### M51 gates (QindaPlan)

Automated by `ctest -R "plan"` (headless; the app tests pin "today" to
Monday 30 March 2026 and seed the "Spring fundraiser gala": 4 phases, 36
subtasks, 7 milestones, 6 resources with one overallocation, a baseline
and three days of slippage). Rationale in D-051; the model in
[plan.md](plan.md).

- [x] `tst_plan_calendar` (7): working-time arithmetic on the standard
      calendar (a day ends 17:00 the same day, lunch, weekends, negative
      spans), exceptions and a resource calendar over its base (holidays
      inherited, a working Sunday, a vacation), degenerate calendars;
      duration, date and money text both ways.
- [x] `tst_plan_schedule` (15): hand-computed dates for FS chains, parallel
      paths with slack, SS/FF/SF with lag, lead and 50% lag, all eight
      constraints (an FNLT conflict reported and honoured, negative slack
      upstream), ALAP by free slack, summaries and summary links, milestones,
      inactive/manual/started tasks, cycles and dangling links ignored with
      issues, the link checks, resource calendars, work and integer cost;
      2,000 tasks schedule in ~30 ms (< 1 s gate).
- [x] `tst_plan_edit` (9): sheet fields as text with Project's typing
      semantics, the predecessor and resource-name languages, outline
      edits, link/unlink, % complete and actuals, manual/auto, respect
      links, Gantt moves, Update Project, assignments.
- [x] `tst_plan_file` (8): byte-identical `.qplan` round trip, unknown
      fields kept at every level, newer version refused, repairs with
      warnings, atomic save, the task clipboard, CSV.
- [x] `tst_plan_resources` (9): per-day overallocation and the usage table
      (days and weeks), leveling within slack (finish unmoved) and beyond
      it, started and pinned tasks untouched, baselines and variances, the
      four reports (late vs slipped).
- [x] `tst_plan_mspdi` (6): a Project 2016-shaped file imported field by
      field and scheduled (both exception forms, all link types with lag,
      lead and %, a resource calendar, material, baseline, manual and
      inactive tasks), export → import identical, well-formed schema order,
      malformed input refused with a line number.
- [x] `tst_plan_output` (6): Gantt pagination (pages wide × row pages,
      hidden subtasks), Gantt and report PDFs, a report table that repeats
      its header across pages, the Timeline PNG.
- [x] `tst_qindaplan_service` (10): the gala schedules as documented
      (gala night Thu 14 May, three days behind its baseline, inside its
      Finish No Later Than; Marcus overallocated from 7 April); sheet edits
      and the blank-row commit as single undo steps with dirty tracking;
      ribbon operations; assign/level/clear; calendars, baselines, Update
      Project, reports; `.qplan`, MSPDI, CSV, PNG and PDF out and back; the
      Gantt/sheet/calendar/usage projections; a Gantt drag previewed and
      committed through the engine and undone.
- [x] `tst_qindaplan_menu_export` (4): the panel export passes the
      desktop's validator with and without a project; every `plan…Action`
      appears exactly once and every exported command resolves and fires.
- [x] `tst_qindaplan_ui` (7): the real `PlanWindow.qml` at 1280 and 520 px
      — Gantt (critical bars, summaries, milestones, baselines, today and
      status markers, the Timeline strip), in-place sheet editing committed
      through the engine, resource sheet, usage with the overallocation
      banner, calendar, overview report; Task Information opens on the
      selected task; the ribbon gate at 1320 and 520 px (no menu-only
      commands beyond the shared ones); structural fixtures
      `qindaplan_{1280,520}.txt`.
- [x] Grabs looked at (validation gallery, `M51-*`): the Gantt with the
      critical path and baselines, sheet editing, resource sheet, usage
      with the overallocation, calendar, overview report and Task
      Information at 1280 and 520; the printed Gantt, reports and Timeline
      (`M51-iter*-output-*`); the toolkit's Gantt example (`M51-tk-*`).
- [ ] On the desktop: print the Gantt two pages wide to a real printer;
      open an exported `.xml` in Microsoft Project and in ProjectLibre;
      import a real Project-saved XML with resource calendars.

### M49 gates (QindaBase — `libs/base_engine`, `apps/qindabase`)

Automated by `ctest -R "base_|qindabase"` (headless; temp dirs and XDG
dirs per test, so backups never touch the user's). Rationale in D-049;
the file, types and API in [qindabase.md](qindabase.md).

- [x] `tst_base_types` (3): type ids and declared types round-trip; foreign
      affinities map; currency parses and prints exactly (grouping,
      parentheses, too many decimals refused, INT64_MIN, 0.10 + 0.20 =
      $0.30, QML doubles, zero-decimal currencies); every type parses what
      people type and refuses the rest with a reason.
- [x] `tst_base_file` (10): the `.qbase` identity (application_id at
      offset 68 = "QBAS", journal DELETE); a plain SQLite file opens
      byte-for-byte untouched until consent, then keeps its user_version;
      non-databases refused; orphans reported by the integrity check;
      backups never over a file, automatic backups named and kept; compact;
      settings and unknown keys survive; captured changes undo and redo a
      cascaded delete exactly; a changed record refuses a stale undo;
      whole-database images restore; statement splitting.
- [x] `tst_base_schema` (9): create/read back (captions, defaults, Date(),
      indexes); refused designs (clashes, reserved names, two AutoNumbers,
      bad defaults, dropping a referenced table); a table rename keeps
      relationships; renames and added fields by plain ALTER; type changes
      through the rebuild convert exactly (REAL 19.99 → 1999 cents, text
      that does not fit kept and counted in the preview, date-time → date
      backed up) and undo/redo as images; required/unique checked against
      the data; dropping a field backs up and keeps enforcement; a
      relationship refused while an orphan exists, then SET NULL works;
      a foreign file's declared types survive a rebuild without adding
      system tables.
- [x] `tst_base_records` (6): sort, filter (every operator, OR filters via
      the app), truncation; typed cell edits with named refusals, lookups
      by display name, multi-cell steps; required fields on new records;
      delete impact (blocked / cascades) and undo of a cascade; tab-separated
      paste (quotes, updates + appends, one step, bad cell names its row);
      exact totals (sum $158.46, average $39.62, count, max date).
- [x] `tst_base_query` (7): criteria convert by type (> 20 on currency =
      2000), Between/In/Like/Is Null/Not, bare words, quotes stay literal;
      Or rows and cross-field criteria; joins + totals + Where + HAVING,
      money expressions in minor units; parameters typed by their field;
      hand-written SQL inspected (DDL and PRAGMA refused); update/delete/
      append previewed, refused on a changed count, run and undone;
      unknown design members round-trip.
- [x] `tst_base_io` (5): saved objects (case-blind clash, rename, delete +
      undo, a table rename rewrites their sources); CSV detection (`;`,
      BOM, quoted delimiters, leading-zero codes, currency, dates, Yes/No)
      and all-or-nothing import, skip-bad-rows, keyed ids, no half-imported
      table; every template builds and its queries run; form mirror and
      report snapshot shapes; Access import through fake mdb-tools, and
      the message when they are missing.
- [x] `tst_qindabase_service` (7): templates and navigation; table design
      plan (readable SQL, bookkeeping summarised) → apply → undo/redo; a new
      table and a relationship; renames carry forms and reports along;
      queries (select into the datasheet, parameters asked, update with a
      confirmed count and undo, saved drafts, SQL console); forms and
      reports open as toolkit design files (DocumentManager), records saved
      through the form exactly, wizards; CSV import as one step, CSV and
      XLSX export of what is shown; maintenance; plain SQLite consent.
- [x] `tst_qindabase_host` (5): 52 command ids with icons and labels, no
      shared shortcut, all disabled without a database; enablement follows
      the view and selection, contextual tabs; Undo/Redo over recorded
      steps with the text of the step, a stale step clears the history
      with a message, the designer's history while one is open; a dirty
      design dirties the window and Save flushes it; Save As copies and
      switches; a report is the print source (PDF export), leaving it
      clears the source.
- [x] `tst_qindabase_menu_export` (2): the exported tree passes the
      desktop's validator with and without a database — File Edit View
      Records Create External Data Design Database Tools AI Settings Help —
      every `base` command appears exactly once, every exported id resolves
      and fires.
- [x] `tst_qindabase_ui` (10): the real `BaseWindow.qml` over the Donors &
      Donations starter seeded with 40 donors, 4 campaigns and 120
      donations: the start gallery and an untitled database; the datasheet
      (lookup names, totals row, an edit undone from the Edit menu, filter by
      selection, the PivotTable); the table designer with the relationship
      and a pending plan; the query designer with a join and totals, run;
      a form over 120 records; a report (120 rows, printable, PDF); the
      relationships diagram and the SQL console; the plan dialog, CSV import
      proposal, report wizard; the ribbon gate at 1280 and 520 px (Replace
      menu-only); structural fixtures `qindabase_{1280,520}.txt`.
      Grabs reviewed: `M49-iter*` (start, home, datasheet, pivot, table
      design, query design and result, form, report and design,
      relationships, SQL, plan dialog, import, report wizard; 1280 and
      520). `M49-iter3-tkbranch-*` are the same run against QindaTK
      `work/base-db-controls` (money in reports, aligned form labels, the
      schema badge as a file name).
- [ ] When QindaTK `work/base-db-controls` lands: rerun
      `tst_qindabase_ui` with `QQO_UPDATE_FIXTURES=1` — the toolkit adds
      `formStatus`, `formType_*` and `schemaDatabase` object names, so
      `qindabase_1280.txt` changes at its FormDesigner lines (every other
      QindaBase gate already passes against that branch).

Still on the desktop: open a real Access file with mdb-tools installed;
print a report to a real printer; two databases side by side.

### M47 gates (the QindaBooks app)

- **UI** (`tst_qindabooks_ui`): the real `BooksWindow.qml` over a seeded
  company (customers, products, four invoices — one overdue, one paid —
  a bill, an expense, an imported bank statement): the dashboard's
  figures, every workspace renders, the invoice editor opens with totals
  the service computed, the ribbon gate at 1320 and 520 px (edit commands
  and Save As menu-only), structural fixtures `qindabooks_{1280,520}.txt`.
  Grabs reviewed: `M47-iter2-*` (dashboard, sales, expenses, banking
  review feed, accounting, reports, invoice editor).

Automated by `ctest -R qindabooks` (headless, temp dirs and XDG dirs,
"today" pinned to 2026-03-31). Rationale in D-047; the API in
[books.md](books.md#qindabooks-app).

- [x] `tst_qindabooks_service`: create from a template (one company per
      window: a second create is refused), templates and currencies;
      settings round trip incl. fiscal July, cash basis, prefixes, notes,
      accent (bad month/currency refused); money text both ways (locale
      form, parentheses, excess decimals refused); customers/vendors
      (address, deactivate), items (`price` text, `type`), a bank account
      with an opening balance, payment/expense/income account lists,
      system accounts cannot be deactivated, classes, tax rates reusing
      their agency; an invoice with a 10% discount and 10% tax on the
      taxable line (900.00 − 90.00 + 36.00 = 846.00, due date from Net
      30, one totals entry per input row) saved as INV-1001, read back,
      listed/filtered/searched, edited in place (940.00); drafts only for
      invoices, draft → posted, estimate → invoice (estimate closed), void
      (zero balance, audit rows); bills (overdue status), an expense on
      the card (edit refused), two bills paid in one run (one payment per
      vendor, 50.00 left); a payment across two invoices (paid +
      partial, 200.00 open); unbalanced journal entry refused with the
      difference, balanced posted; transfer; deposit from Undeposited
      Funds; lock date refuses an earlier invoice; `changed()` fires on a
      commit and not on a refusal.
- [x] `tst_qindabooks_banking`: CSV import (3 lines, re-import 0 + 3
      duplicates), categorize, exclude/restore, a rule applied and
      accepted, rule edit replaces, a recorded payment offered and
      accepted as a match; OFX card statement; a CSV the detector cannot
      read returns `needsMapping` with its header, and imports with an
      explicit mapping; reconciliation refuses to finish at 800.00
      difference, finishes at zero, register shows reconciled lines with
      deposit/payment text and running balance, report, undo; P&L map
      (section/account/total rows, `total:net_income`, cash basis, month
      columns) with account drill-down to the invoice, Balance Sheet
      balances, A/R aging `party:<id>` drill-down, unknown report and
      reversed period refused; range presets for a calendar and a July
      fiscal year (two different "todays"); KPIs equal the P&L, trends
      with `…Chart` doubles, expense mix, 7 ratios (net margin 73.3%);
      report PDF/CSV, invoice PDF, IIF, journal CSV, customer list CSV
      imported into another company; timestamped backup and Backup…
      never over a file; e-mail launches `qindamail --attach <pdf>
      mailto:` with the subject (M48: the PDF is attached); monthly recurring from
      Jan 31 generates 3 invoices once.
- [x] `tst_qindabooks_host`: all 40 command ids (38 + the two M48 ones) exist with icons and
      labels, Ctrl+I and Ctrl+1…6, New reads "New Company…"; no shortcut
      is shared; commands disabled until a company exists and the setup
      request fires once; the workspace radio group keeps exactly one
      checked from the menu and from `currentView`; every command emits
      its request; Save As switches the window and the service to the
      copy (original untouched) and Save is a successful no-op; report
      and document print sources export PDFs (twelve month columns
      prefer landscape), clearing disables Print, Print Document prints
      the selection; the (stub) `BooksWindow.qml` instantiates.
- [x] `tst_qindabooks_menu_export`: the exported tree (with and without a
      company) passes the desktop's validator — File Edit View Sales
      Expenses Banking Accounting Reports AI Settings Help — every books
      command appears exactly once, every exported id resolves and fires.

Remaining for the lead's QML merge: fixtures and grabs of the real
window at 1280/520, the ribbon over these ids (squeeze), the launcher
card, and the on-desktop checklist (open two companies side by side,
print an invoice and a report, e-mail an invoice into QindaMail).

### M45/M46 gates (bookkeeping engine and I/O)

Automated by `ctest -R books` (headless, temp dirs). Rationale in D-045
and D-046; the worked example is in [books.md](books.md).

M45 — `books_engine`, `books_document`:

- [x] `tst_books_money`: 0.1 + 0.2 = 0.30 exactly; overflow detected
      (never wraps); INT64_MIN prints; JPY/KWD exponents; half-up vs
      half-even vs toward/away from zero; allocation sums exactly and each
      part is within one minor unit; locale parse (grouping, symbol,
      parentheses, trailing minus, German decimal comma) refuses excess
      fraction digits; **no `double`/`float`/`qreal` in the engine
      sources** (scanned).
- [x] `tst_books_company`: create/reopen (one file, no `-wal`); refuse a
      foreign file, a newer schema ("newer version"), a missing file;
      migrate a v1 file to v2 with a `.pre-v2.bak` copy and an audit row;
      `VACUUM INTO` backup never overwrites; settings, counters, logo;
      currency fixed once posted; all four templates name every system
      role and balance (also with opening balances); nonprofit functional
      groups; chart rules (duplicate, cross-class parent, delete with
      postings refused, deactivate only at zero balance, inactive account
      refused).
- [x] `tst_books_ledger`: balanced/unbalanced/one-line/unknown-account/
      currency-mismatch; lock date refused, override without reason
      refused, with reason stored + audited; void = zero effect with lines
      and trail kept; edit = void + repost with the same document id and a
      failed edit changing nothing; triggers refuse UPDATE/DELETE of
      posted lines, un-void and audit edits; opening balances.
- [x] `tst_books_sales`: estimate → invoice (10% discount, 6% + 1.25%
      two-agency tax on 1,800.00 = 130.50, total 2,065.50) → partial
      payment → over-applications refused → overpayment credit → credit
      applied → taxed credit memo → two refunds: A/R 0, Sales Tax Payable
      123.25 (State 102.00, City 21.25), statement of 7 lines closing at 0;
      drafts, edits under payments, voided payment reopens the invoice;
      sales receipt to Undeposited Funds; recurring Jan 31 monthly → Feb 28,
      Mar 31, Apr 30, re-run creates nothing.
- [x] `tst_books_purchases`: bills with vendor terms, vendor credit
      applied, two bills paid in one run (one payment per vendor), card
      charge and check, edit below payments refused.
- [x] `tst_books_banking`: transfers and registers (bank and card sign);
      deposits from Undeposited Funds, no double deposit, void returns
      them; bank feed dedup (hash + occurrence keeps two identical
      coffees, FITID beats text), rules by priority/direction/amount,
      accept suggestions, match suggestions ranked by date and payee,
      exclude/restore/categorize as Check; reconciliation refuses to
      finish at a non-zero difference, report numbers, reconciled lines
      block void, next session starts at the reconciled balance, undo.
- [x] `tst_books_reports` (fixture Q1 2026): P&L accrual (net −200.00)
      and cash (−720.00); months, classes, prior year and prior period
      with % change; Balance Sheet balances on both bases at four dates;
      net income rolls into Retained Earnings next year; cash flow ties to
      the change in cash on both bases and for February alone; trial
      balance debits = credits (11,720.00); aging exactly 30 days late in
      1–30, 31 in 31–60, 91 in 91+; balances, open invoices/unpaid bills
      as of dates; sales by customer/item on both bases; expenses by
      vendor/category; sales tax liability; budget vs actual; GL, journal,
      transaction list; cash recognition of a 3-way split payment is exact.
- [x] `tst_books_analysis`: the seven ratios against hand arithmetic,
      unavailable ratios, expense mix, monthly trends with month-end cash,
      trailing-average forecast.
- [x] `tst_books_perf`: 50,000 journal lines; P&L and Balance Sheet each
      < 1 s (measured ~0.5 s and ~0.07 s in a Debug build).
- [x] `tst_books_document`: never dirty, own commits refresh the byte
      revision, Save no-op, Save As copies and switches (and replaces an
      existing target), recovery refused, untitled scratch company dirty
      until saved.

M46 — `books_io`: see the list below (tests `tst_books_io_*`).

- [x] `tst_books_io_forms` (8): invoice PDF is one page and its Poppler
      text holds company, number, customer, address, total, balance due,
      discount/tax labels and "Page 1 of 1"; an 80-line invoice runs to 5
      pages with the header repeated and every line printed once;
      estimate, sales receipt, credit memo, statement (aging strip); void
      refused; a QPrinter through `asPrintCallback` paints the same page.
- [x] `tst_books_io_reports` (10): P&L PDF text; CSV round trip of P&L,
      Balance Sheet, Trial Balance, A/R aging detail to the minor unit;
      12-month P&L goes landscape; multi-page journal repeats its header;
      CSV quoting and delimiter detection.
- [x] `tst_books_io_bank_import` (28): amount edge cases, date-format
      detection (day > 12, month names, 2-digit years), both CSV fixtures,
      explicit mapping with `invertSign`, per-row errors, OFX 1.x SGML,
      QFX 2.x XML credit card, QIF; each fixture imported twice through
      `Banking::importLines` adds nothing the second time.
- [x] `tst_books_io_export` (7): journal CSV balances; list CSV
      export → import round trip; IIF headers, account types and every
      TRNS…ENDTRNS block summing to zero; timestamped backups keep the
      newest N and each opens clean.
- [x] Grabs looked at (validation gallery inbox, `M46-books-*.png`):
      invoice, invoice page 2, statement, Profit and Loss, Balance Sheet.

- [ ] On the desktop (M47 app lane): print an invoice to a real printer
      and compare with its PDF; open an IIF export in an accountant's
      QuickBooks.

### M39 gates (floating images) and the parity close-out

Automated by `tst_calc_io_images`, `tst_calc_window` and `tst_calc_sample`.
Rationale in D-040.

- [x] `imagesRoundTrip`: a real JPEG survives byte-for-byte (the point of
      not using QXlsx's insertImage), with its format, anchor and extent;
      the media part, the content-type default, the drawing relationship
      and the `<xdr:pic>` anchor are package-inspected.
- [x] `picturesAndChartsShareOneDrawing`: a sheet with both gets one
      drawing part holding both anchors, and both come back on re-read.
- [x] `imagesFollowStructuralEdits`: anchors move with inserted rows and
      columns, ignore edits on other sheets, and go with a removed sheet.
- [x] `imagesInsertPaintAndRecover`: Insert ▸ Image… lands a picture at
      the cursor as ONE undo step, the grid paints it (the pixel inside
      its rect is the image's own colour), and the recovery payload
      brings the bytes back.

The close-out for the whole parity run (M32–M39), sample-gated:

- [x] `sampleSurvivesOurOwnSaveAndReopen`: the author's workbook, saved by
      us and read back, is still the author's workbook — all 11 sheets,
      106,464 cells, 106,402 styles, 8 conditional formats, 9 validations,
      6 tables, 4 charts, 6 series, the frozen panes, the print areas, the
      active tab and the recomputed Forecast value, with zero error cells
      on both sides. This gate is what caught the blank-but-styled cells
      the writer had been dropping (2,114 of them).
- [x] `whatWeWriteOpensInLibreOffice`: our package converted by headless
      LibreOffice and read back — the external judge that our own reader
      and writer do not merely share a bug.
- [ ] On the desktop, with real Excel: open the saved file and confirm the
      look. The one check no test here can make.

### M38 gates (array arithmetic)

Automated by `tst_calc_functions` and `tst_calc_window`. Rationale in D-038.

- [x] `arrayArithmeticBroadcasts`: the sample's own shape
      (`SUMPRODUCT(A1:A5,(1+0.1)^A1:A5)`), every operator broadcasting a
      scalar against a range and a range against a range, the
      `--(A1:A5>2)` coercion idiom, row-against-column stretching, and
      `TEXTJOIN` over a computed array.
- [x] Same gate, negative half: a shape mismatch is `#VALUE!`, an array in
      scalar position is `#VALUE!` unless it is 1x1, and an error element
      is the whole aggregate's answer.
- [x] `fixtureRows` stays green — the 300+ LibreOffice oracle rows are the
      check that broadcasting did not change any scalar answer.
- [x] `opensARealExcelWorkbook`: the sample workbook now reports **zero**
      error cells across all eleven sheets (was 12, pinned since M32).
- [ ] On the desktop: open the sample, read Forecast B26:C28 against the
      values Excel shows.

Recalculation cost (D-039), same milestone:

- [x] `orderingTheDirtySetStaysCheap`: 3000 formulas packed into one row
      band, all reading ranges that cover it — the shape where the sort
      used to rebuild the same candidate set once per dirty cell. Budget
      3s; measured 10.5s before the fix and ~1.1s after, so the gate
      separates the two worlds with room on both sides.
- [x] `undoBackToTheSavedStateClearsDirty`: undo and redo each land the
      right cell value, which is what makes skipping the push-time redo
      safe.
- [x] `sampleRulesEnforceAndImport` (sample-gated): one edit to
      `Controls!B5` in the 106k-cell workbook, start to finish, in 7.8s
      of test — it used to trip QtTest's 300-second watchdog.

### M37 gates (view & print state)

Automated by `tst_calc_io_view` and `tst_calc_window`. Rationale in D-037.

- [x] `viewStateRoundTrips`: frozen panes (`state="frozen"`, xSplit/
      ySplit), hidden sheets (`state="hidden"`), the active tab, the
      selection cell and page setup (landscape/fitToWidth) survive.
- [x] `printNamesAreLocalDefinedNames`: `_xlnm.Print_Area/Titles` write
      with `localSheetId`, read into per-sheet fields, and never enter
      the formula name table.
- [x] `freezeToggleIsUndoableAndRecovered`: the freeze toggle lands on
      the sheet (undo clears, redo restores) and the recovery payload
      carries it.
- [x] `hiddenSheetsLeaveTheTabStrip`: a hidden sheet drops out of the
      strip, activating a view index reaches the right engine sheet, and
      hiding the active sheet activates a visible one.
- [x] `printHonorsThePrintArea`: the exported PDF (read back through
      Poppler) contains rows inside the area and none outside it.
- [ ] On the desktop: freeze row 1, hide a sheet, save, reopen — the
      pane, the tab and the print layout come back as left.

### M36 gates (Excel tables + the custom chart renderer)

Automated by `tst_calc_io_tables`, `tst_calc_structure` and
`tst_calc_window`. Rationale in D-036.

- [x] `tablesRoundTrip`: the table part, content-types, rels and the
      `<tableParts>` element are package-inspected; the reimported table
      keeps name, range, style, header and column names.
- [x] `tablesFollowStructuralEdits`: row insert above/inside moves and
      grows a table; removing its span drops it.
- [x] `sampleTablesImportAndBand` (QQO_SAMPLE_XLSX): six tables import by
      name; the Orders header paints dark and a stripe row differs from
      its unbanded neighbour on screen.
- [x] `chartWindowsRender` (QQO_SAMPLE_XLSX): a chart window paints real
      series pixels (several distinct colours in the plot area) — the
      QtCharts ChartView path segfaulted under QGuiApplication before the
      custom ChartItem existed.
- [ ] On the desktop: insert a chart through the dialog (its preview is
      the same ChartItem) and open the sample — four chart windows render
      live data and stay put on edits.

### M35 gates (chart import + StackedBar export)

Automated by `tst_calc_io_charts`, `tst_calc_structure` and
`tst_calc_window`. Rationale in D-035.

- [x] `chartImportReadsKindsAndGrouping`: a StackedBar chart part reads
      back as StackedBar with title, anchor and both series' cat/val refs.
- [x] `stackedBarExportWritesGrouping`: the rangeText path's package
      carries `<c:grouping val="stacked"/>` (post-pass).
- [x] `chartsSurviveRoundTrip`: a series-authored chart generates its
      part + drawing anchor (content-types, rels, `<drawing r:id>`
      inspected) and reimports with kind, title and series intact.
- [x] `chartSeriesFollowTheEdit`: row insert moves series refs and the
      anchor; removing the data band drops the series and then the chart.
- [x] `sampleChartsImport` (QQO_SAMPLE_XLSX): four charts land — two on
      Dashboard, two on Forecast — with titles and six series; the host
      exposes all four for the chart windows.
- [ ] On the desktop: the Dashboard and Forecast chart windows render
      their series live (ChartView over the imported refs).

### M34 gates (rules & annotations round-trip)

Automated by `tst_calc_io_rules`, `tst_calc_rules` and `tst_calc_window`.
Rationale in D-034.

- [x] `condFormatsRoundTrip`: a cellIs rule with a dxf survives
      (package-inspected dxf fill as `bgColor`), evaluates after reimport,
      and keeps stopIfTrue.
- [x] `expressionAndDataBarSurvive`: the formula text round-trips, reparses
      and evaluates shifted; the bar colour and live fraction survive.
- [x] `validationsRoundTrip`: list (with message) and whole-number rules
      enforce after the round trip; unmappable operators/styles are
      skipped with a count, never approximated.
- [x] `autoFilterRoundTrip`: the element lands after `</sheetData>`,
      re-imports active with its allowed values, and hides the same rows.
- [x] `commentsRoundTrip`: the comments part, rels and content-type
      override exist; author/text return at the right cells.
- [x] Engine: expression rules shift per cell, data bars scale over the
      range, stopIfTrue truncates, merge is first-wins-per-field (the M21
      later-wins test flipped, D-034).
- [x] `sampleRulesEnforceAndImport` (QQO_SAMPLE_XLSX): 8 conditional
      formats and 9 validations arrive; Controls!B5 rejects "Bogus" with a
      reason and accepts "Upside"; the Monthly data bar measures live.
- [ ] On the desktop: data bars paint on Monthly/Product Analysis/
      Customer Analysis; the "REORDER"/"FOLLOW UP" rows colour; a comment
      hover shows the imported text.

### M33 gates (full style round-trip)

Automated by `tst_calc_io_styles` and `tst_calc_window`. Rationale in D-033.

- [x] `fullStyleRoundTripsOnValueFormulaAndEmptyCells`: every CellStyle
      field (custom number format, bold/italic/underline/strikeout, font
      size/family, fg/bg, H+V alignment, wrap, border mask) survives on
      all three cell kinds; the package carries the fill as `fgColor`
      (the colour external consumers paint).
- [x] `customNumberFormatsRoundTrip`: the sample workbook's own codes
      (`0.0%;[Red](0.0%);–`, `$#,##0.00;[Red]($#,##0.00);–`) serialize.
- [x] `themeColorsResolveToRgb`: a `theme="4" tint="-0.25"` colour
      resolves through the package's own palette.
- [x] `tst_calc_window::sampleDashboardKeepsItsDarkTitle` is GREEN (was
      red by design at M32): the sample's navy banner fill imports.
- [x] `recoverySnapshotRoundTripsTheWholeWorkbook` covers strikeout and
      vAlign; old payloads decode vAlign as centre.
- [x] `largeSheetStaysResponsive` stays green with workbook-wide style
      dedup (`formatKey()`) and the hoisted format cache.
- [ ] On the desktop: the sample workbook shows fills, wraps and banner
      colours on every sheet (visual pass over the M32 state).

### M32 gates (sheet tabs render; worker-thread loads)
Automated by `tst_calc_window` (QindaOffice) and QindaTK's
`qindatk_qml::ControlsStructure`. Rationale in D-032.

- [x] QindaTK `test_tab_strip_accepts_cpp_sequence_model`: a C++
      `QStringList` sequence (`Qt.application.arguments`) yields tab
      delegates — fails without the QindaTK 7fdc60c fix, passes with it;
      QindaTK `ctest --preset dev` stays 14/14.
- [x] `tst_calc_window::allTabsReachableAtSmallWidths`: 13 long-named
      sheets at 900 px — the strip overflows, and every tab scrolls into
      the viewport when activated.
- [x] `tst_calc_window::windowResponsiveAfterSampleLoad`
      (`QQO_SAMPLE_XLSX`): the window retitles, shows one tab per sheet
      (11), and paints (grab pixel check) after an awaited worker-thread
      load; wall time logged.
- [x] `tst_calc_window::opensARealExcelWorkbook` counts error cells by
      `isErr` (the literal text cell `#` is not an error) and pins the 12
      known array-arithmetic `#VALUE!` cells until M38 tightens it to 0.
- [x] `tst_calc_window::sampleDashboardKeepsItsDarkTitle`: red by design
      (QEXPECT_FAIL) until M33 lands cell fills — Dashboard!A1:P2 is a
      navy merged banner whose fill the importer drops today.
- [x] The structural fixtures `qindacalc_{1280,520}.txt` record
      `QQuickTabButton#tab_0` — the first fixture with a visible tab.
- [ ] On the QindaQt desktop with the packaged QindaTK: the installed
      `qindacalc` opens the sample workbook with all eleven tabs shown
      and responsive while loading (needs the `dev-libs/qindatk` rebuild
      carrying 7fdc60c).

### M31 gates (launcher, Widgets removal, packaging, rules)

- [x] `tests/qindalauncher/tst_launcher`: the catalog (four distinct apps
      with role and blurb, which app owns a suffix, the four recent lists
      merged newest-first with missing files skipped and an unparsable list
      survived without being rewritten), the controller's published apps
      and recents, and the real `LauncherWindow.qml` — a card per app whose
      accessible name a screen reader can read, the recent row present, the
      empty state hidden, the Open button there. `QQO_GRAB_DIR` saves the
      window (reviewed: four mascot cards, the recent document, the Open
      button).
- [x] `ctest --preset dev` green (45 tests) with no Qt Widgets UI in the
      suite: no `Qt6::Widgets` link outside `third_party`, and
      `grep -rn QApplication apps libs tests` is empty. `libQt6Widgets.so`
      is still loaded because QtPrintSupport links it (D-031).
- [x] `libs/office_shell` and `tests/office_shell` deleted; the
      `QQO_WIDGETS_APPS` option, the `QQO_WIDGETS_AI` define, the Widgets
      assistant panel and the Widgets proposal dialog gone with them.
- [x] `AGENTS.md` rewritten for the QtQuick suite (rules 3, 4, 8 restated;
      the QML rules and the headless-first verification section added).
- [x] The INSTALLED binaries run, not just the build tree: every executable
      over `office_quick` carries an RPATH to the QindaQt.Tokens backing
      library (`$ORIGIN`-relative first, as the desktop's own QML apps are
      linked, then the absolute directory). `objdump -x /usr/bin/qindacalc |
      grep RUNPATH` must name it, and `<prefix>/bin/<app> --version` must
      answer instead of dying on a missing `libqindaqt_tokens_qml.so`.
- [ ] Packaging (operator, needs root and a clean tree):
      `tools/make-dist.sh 0.1.0_p<date>-r5 /var/cache/distfiles`, then
      `ebuild qindaoffice-0.1.0_p<date>-r5.ebuild manifest` in the overlay
      and `emerge -1 dev-libs/qindatk gui-apps/qindaoffice`; all five
      binaries (`qindawrite`, `qindacalc`, `qindashow`, `qindanote`,
      `qindaoffice`), their desktop files and the QindaTK QML module
      installed.
- [ ] On the QindaQt desktop, the whole suite: the panel menu drives each
      app end to end; portal file dialogs open; the tablet inks in
      QindaNote; the QindaShow player takes the second screen; light and
      dark themes and comfortable density all read correctly.

### M30 gates (QindaShow on QindaTK)

- [x] `tests/qindashow`: `tst_qindashow` (fodp round trip, pptx through
      LibreOffice when present, the canvas item mutating the model with
      its undo payloads and topmost-first hit-testing, the host wiring
      deck / notes / transition / undo and reopening a saved deck, the
      controller revealing builds before slides and reporting the build
      duration, outline parsing, deck PDF, the text bridge),
      `tst_show_ui` (named strips and menus, styling undoable, the shape
      clipboard, insert commands, edit commands following the selection,
      find across titles / shapes / notes with case sensitivity, the
      canvas painting the page from a window grab, align against the
      slide, bring-to-front in one step, move slide, Replace sweeping the
      deck in one step, a sorter thumbnail per slide, the player window
      advancing through the shared controller and the editor landing on
      the slide the show ended on, pristine host, structural fixtures at
      1280 and 520), `tst_show_design` (masters paint and supply default
      type only, a build animation changes what is drawn mid-reveal,
      snapping pulls an edge to the slide centre and the toggle follows
      the canvas, grouping survives the fodp round trip, a multi-shape
      gesture is one undo step, masters round-trip through the dialog
      seam, animation entries follow the selection), `tst_show_menu_export`
      (validator accepts the ten menus; every command resolves; one AI
      menu with the inline actions, deck generation and the seven
      animation entries).
- [x] `QQO_GRAB_DIR=<dir> tst_show_ui` saves 1280×800 and 520×700 PNGs;
      reviewed before the commit (sorter thumbnail, page centred on the
      canvas, both strips, notes panel, "Slide 1 of 1   Shapes: 1").
- [ ] On the QindaQt desktop: the panel menu drives QindaShow end to end;
      F5 opens the player on this screen and the presenter view on the
      second one; Escape ends both.

### M29 gates (QindaWrite on QindaTK)

- [x] `tests/qindawrite`: `tst_write_control` (the editing engine on its
      own: keys, mouse hit and drag, Shift-select, clipboard round trip,
      undo, input-method preedit/commit, pinned format), `tst_write_view`
      (3-page document paginates identically on screen and in the PDF
      through Poppler; typing through the window reaches the document;
      zoom is the viewport's transform; styles survive HTML; accessible
      text over the item; header/footer in the PDF), `tst_write_ui`
      (named strips, menu tree, char/paragraph formatting through the
      entries, font combos, tables and columns, find/replace with wrap and
      one-step undo, edit commands follow the control, paper palette
      pinned and white in a window grab, zoom commands, table operations,
      page setup round trip, the 29-command strip wraps at 560 px, pristine
      host, structural fixtures at 1280 and 520), `tst_write_typography`,
      `tst_write_review` (footnotes, TOC, tracked changes through the
      window's key path, ruler markers through the host and `Tk.Ruler`,
      footnote dialog round trip), `tst_write_menu_export` (validator
      accepts the twelve menus; every command resolves; one AI menu with
      the inline actions), `tst_write_formats` and the pagination probe on
      `QGuiApplication`.
- [x] `QQO_GRAB_DIR=<dir> tst_write_ui` saves 1280×800 and 520×700 PNGs;
      reviewed before the commit (ruler numbered from the text column,
      markers on the margins, paper white, strip wrapped in three rows).
- [x] QindaTK `tst_qml`: `test_ruler_markers_drag_and_tabs`.
- [ ] On the QindaQt desktop: the panel menu drives QindaWrite end to end;
      Wayland text-input composes into the page (preedit visible).

### M28 gates (QindaCalc on QindaTK)

- [x] `tests/qindacalc`: `tst_calc_window` (document, host, recovery, ods
      bridge, pristine host; `QQO_SAMPLE_XLSX=<path>` runs the real-workbook
      diagnostic), `tst_calc_ui` (named strips, undo/redo recalculates,
      clipboard, formatting through the entries, find/replace with wrap,
      the cursor outline on the cursor cell from a window grab, the strip
      wraps at 560 px, D-015 grid commands, sheets, freeze panes,
      structural fixtures at 1280 and 520), `tst_calc_rules_ui`
      (validation through the dialog seam, conditional formatting as one
      undo step, autofilter hides rows, comments survive undo and
      recovery), `tst_calc_menu_export` (validator accepts the ten menus;
      every command resolves; the template ids are in the tree).
- [x] `tests/calc_io`: `absolutePartNamesStillOpen` (a package rewritten
      to absolute targets opens with its styles); `tests/calc_engine`
      fixture rows for `SUMIFS`, `RANK.EQ`, `FORECAST.LINEAR`, `SUBTOTAL`,
      `LET`.
- [x] `Northstar_Excel_Demo.xlsx` (11 sheets, 95 k cells, 10 k formulas)
      opens in ~10 s with 12 `#VALUE!` cells (array arithmetic in
      `SUMPRODUCT`, D-028 limitation); it did not open before.
- [ ] On the QindaQt desktop: the panel menu drives QindaCalc end to end;
      a wheel over the grid scrolls, Ctrl+wheel zooms.

### M27 gates (QindaTK conversion, foundation + QindaNote)

Build with the QindaTK checkout on the prefix path:

```
cmake --preset dev -DCMAKE_PREFIX_PATH=<QindaTK>/install
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

QtQuick tests run under `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`
(pinned via ctest `ENVIRONMENT`).

- [x] `tests/office_quick`: `tst_action_model` (entries, radio groups,
      trigger by id), `tst_menu_export` (the desktop's validator accepts the
      exported tree; every exported command resolves and fires in the
      model), `tst_document_host` (async open/save/close flows), `tst_office_window`
      (the real `OfficeWindow.qml` over a host: bands, menu bar from the
      tree, tool strip buttons follow their entries, View ▸ Show Menu Bar),
      `tst_print_service` (PDF from the render callback, Poppler preview of
      the same PDF, printer model).
- [x] `tests/qindanote`: `tst_qindanote` (engine + host: reopen, lazy pages
      never blanked, recovery, structure commands through the host, find
      across unopened pages, replace all), `tst_note_blocks` (layout,
      placed blocks, checklist state and nesting, merged typing undo, image
      bytes, tap-to-type), `tst_note_input` (pen through
      `QWindowSystemInterface`: inks with pressure in document units, touch
      ignored, undo/redo, live eraser as one undo step, barrel-button
      eraser, zoom is a view transform, tool shortcuts lose to a text
      field, paper white under the dark preset, debounced page writes),
      `tst_note_ui` (icon-only addressable strip, slots follow the tool,
      wrap instead of hide, navigator opens pages, assistant panel follows
      AI ▸ Assistant…, structural fixtures at 1280 and 520),
      `tst_note_menu_export` (hard-asserts V5).
- [x] `tests/office_ai`: `assistantControllerSendsAndStreams` over the mock
      SSE server (no Widgets).
- [x] The three Widgets apps still build and pass behind `QQO_WIDGETS_APPS=ON`.
- [x] **Verified 2026-09-17 on the live QindaQt session (desktop host):**
      QindaNote from the build tree exports
      `/org/qindaqt/QindaOffice/QindaNote/Menu/1` on the session bus with
      all 8 menus (98 items, radio states correct); `busctl --user call
      org.qindaqt.QindaNote <path> com.canonical.dbusmenu Event isvu -- <id>
      clicked s "" 0` toggles Show Navigator (toggle-state 1→0), New Page
      enables Move Page Up, and Quit on a pristine window exits the process
      (V5 fixed end to end). The desktop reports `appearance.theme =
      qinda-dusk`; `DesktopAppearance` publishes it (log line
      "QindaQt tokens applied").
- [ ] Manual, needs a tablet at the desk: pen with pressure and tilt, palm
      ignored, barrel button erases; paper white under the dark theme
      (offscreen: `NoteInputTest::pageIsPaperUnderTheDarkPreset`).

Fixtures: `tests/qml_fixtures/qindanote_{1280,520}.txt` are structural
dumps (named items and visibility, no text or geometry);
`QQO_UPDATE_FIXTURES=1 ctest -R tst_note_ui` rewrites them after an
intentional chrome change.

### M21 the next tier, and the suite's launcher

Automated by `tst_calc_rules`, `tst_calc_rules_ui`, `tst_write_review`,
`tst_show_design`, `tst_note_blocks`, `tst_launcher` and the pristine-window
cases now in all four app suites. Rationale in D-018b (open reuse), D-019
(Calc rules), D-020 (Write document features), D-021 (Show design layer),
D-022 (Note blocks) and D-023 (the launcher).

**Opening.** A freshly created window of every app is pristine, so File ▸
Open replaces its blank document instead of opening beside it; the window
the Open came FROM is preferred; a window that has been typed into is never
displaced.

**QindaCalc.** A blank cell satisfies no comparison and an empty input is
never refused. Two conditional formats compose field by field. A refused
value emits `inputRejected` rather than vanishing. A filtered row leaves
the grid entirely (the row below moves up into its place) and the header
row is never hidden. Comments survive undo AND the recovery payload. All
four rule kinds follow an inserted row.

**QindaWrite.** Footnote markers number by document order and renumber when
one is inserted in the middle; a note prints on its marker's page; an
orphaned note is pruned. The contents lists headings with the page numbers
the view paints, never lists itself, and Update replaces the table in
place. A tracked insertion is rejectable and a tracked deletion is
acceptable, exactly inverse. The ruler's markers describe the cursor's
paragraph. Enter after a heading starts body text, and a Heading 1 at the
very start does not push a blank page ahead of itself.

**QindaShow.** A master really paints the background and supplies defaults
only to runs that never chose. A half-played animation draws something that
is neither the background nor the final fill. Snapping takes a candidate
inside the threshold and nothing outside it, and does nothing when off. A
group survives the fodp round trip. A multi-shape gesture is one undo step.

**QindaNote.** Three kinds of block stack without overlapping, each as tall
as its kind needs. A checklist item's text and tick reach the envelope. An
image block carries its bytes, not a path.

**Launcher.** Four distinct apps with distinct binaries and desktop ids;
every suffix routes to the app that opens it and a suffix-less path to
QindaNote; recents merge the four lists, skip files that are gone, and
survive a `recent.json` that is not JSON without rewriting it; each card
carries an accessible name and description.

### M21 packaging gate (Gentoo)

The ebuild is the only supported install path, and it must build with no
network access at all — Portage's default `FEATURES` include
`network-sandbox`. Verify before tagging a snapshot:

- [ ] `tools/make-dist.sh <version> <distdir>` exits non-zero on a dirty
      tree. A tarball that matches no commit is a tarball nobody can
      reproduce.
- [ ] The tarball contains `third_party/QXlsx/QXlsx/CMakeLists.txt`. The
      submodule must be VENDORED, not referenced: `src_unpack` cannot fetch
      it under `network-sandbox`.
- [ ] `ebuild <ebuild> clean unpack` leaves the tree at `${WORKDIR}/${P}`
      and its `.dist-commit` names the commit the archive was cut from.
- [ ] `emerge -1 gui-apps/qindaoffice` runs with **no** `MAKEOPTS` or `-j`
      override; Portage's configured job and load limits are the limits.
- [ ] After merge, all five binaries exist (`qindawrite`, `qindacalc`,
      `qindashow`, `qindanote`, `qindaoffice`), and the five desktop files
      and the hicolor SVGs are installed.
- [ ] `packaging/gentoo/gui-apps/qindaoffice/` and the `qindaqt` overlay
      hold the SAME ebuild. The in-repo copy is the source; a drift means
      the overlay is packaging something the tree does not describe.
- [ ] The install image contains **no** `.a`, `.so*`, `.h` or `.cmake`
      files (D-025). Check against an eclass-like configure, not the dev
      preset — `cmake.eclass` forces `BUILD_SHARED_LIBS=ON`, so a build
      that is static locally can still ship a shared library under
      Portage:

      ```
      cmake -S . -B /tmp/ic -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64
      cmake --build /tmp/ic && DESTDIR=/tmp/img cmake --install /tmp/ic
      find /tmp/img \( -name '*.a' -o -name '*.so*' -o -name '*.h' \
                     -o -name '*.cmake' \) | wc -l     # must be 0
      ```
- [ ] `ldd` on the installed `qindacalc` names no `libQXlsxQt6`: the
      vendored copy must be linked in, not planted in `/usr/lib64`.
- [ ] Two machines cutting the SAME commit produce byte-identical
      archives. The Manifest pins one hash and `pkg_nofetch` tells the
      operator to run `make-dist.sh`, so a non-reproducible tarball means
      the second machine's own archive is rejected as a Manifest
      mismatch:

      ```
      sha512sum <distdir>/qindaoffice-<pvr>.tar.xz          # on each host
      grep SHA512 .../gui-apps/qindaoffice/Manifest
      ```

      A plain `tar -cJf` does NOT satisfy this — readdir order, the
      creation times of `.dist-commit` and the staging directories, the
      embedded uid/gid, and multi-threaded xz all vary per machine.

### M21 manual checklist (live desktop)

- [ ] `gtk-launch org.qindaqt.QindaOffice` opens the launcher with four
      mascots; clicking a card starts that app, and closing the launcher
      leaves it running.
- [ ] QindaCalc: colour a range by rule, put a list validation on a column
      and try to type something else, filter a header row, hover a
      commented cell.
- [ ] QindaWrite: insert two footnotes and check the numbers on the page
      and in the PDF; build a contents list and re-paginate; record a few
      changes and accept half of them; drag the ruler's indent markers.
- [ ] QindaShow: give the deck a master, animate a bullet and play it,
      drag a shape near the slide centre and watch the guide, group two
      shapes and move them.
- [ ] QindaNote: add a checklist and an image, then print — the page prints
      what the screen showed, in the same places.

### M20 office-suite completeness

Automated by `tst_preferences`, `tst_calc_structure`, `tst_calc_ui`,
`tst_write_ui`, `tst_write_typography`, `tst_show_ui` and `tst_qindanote`. Rationale in D-014
(settings, status bar, dead entries), D-015 (QindaCalc structure), D-016
(QindaShow canvas and arrange), D-017 (QindaNote structure and layout) and
D-018 (QindaWrite typography).

**Shell.** Settings ▸ Preferences… is enabled and opens a dialog whose OK
writes through and whose Cancel does not; the `Preferences` store bounds
what it reads back and survives a reopen. Every window has a status bar from
construction with a PERMANENT right-hand field a transient `showMessage()`
cannot displace. View ▸ Show Toolbars / Show Status Bar apply and persist.
The File menu ends with Quit however many app entries were inserted.
Auto-save is off by default and, when on, skips (with a status message)
rather than putting a dialog on screen for a document with no path or a file
changed on disk.

**calc_engine.** Inserting rows moves cells AND rewrites every formula that
points at them — including cross-sheet ones that name the edited sheet — and
a straddling range grows with the insert; deleting turns a reference into
the gap into `#REF!` while a straddling range shrinks; styles on EMPTY
cells, explicit geometry, merges, named ranges and chart anchors all follow.
Renaming a sheet requalifies every reference to it; deleting one breaks only
its own references and leaves the rest working after the index rebuild; the
last sheet cannot be deleted. Sort moves rows with their formulas (relative
references shift by the row delta), keeps the header row and sinks blanks in
both directions. `recalcAll()` refreshes cells off the diagonal (it used to
read the packed key transposed and skip them).

**QindaCalc.** The grid starts on A1, not on a default-constructed
`{-1,-1}`; the status field reads the address, Sum, Average and Count of the
selection. Delete clears the selection in one undo step. A header click
selects the whole row or column bounded by the used range. Insert/Delete
Rows are one undo step each and the dependents re-evaluate. Sort Ascending
keeps a text header on top. AutoSum totals the contiguous run above the
cursor. Merge works from the selection and Unmerge from any cell the merge
covers. A sheet rename keeps cross-sheet formulas working. With a pane
frozen, row 1 stays visible after scrolling to the bottom and still
hit-tests as row 1 — painting and clicking walk the same band lists.

**QindaWrite.** The font-size combo is never blank. Superscript reaches the
selection's char format; line spacing writes `ProportionalHeight` and the
menu's check mark re-reads it from the document. The word count follows the
document and narrows to the selection, and appears in the status field.
Insert Date puts text in and Insert Horizontal Rule leaves a block carrying
`BlockTrailingHorizontalRulerWidth` (so it survives the HTML round trip).

**QindaShow.** The canvas paints a white page (an empty slide used to render
as a flat dark-grey view). Align Centre with one shape selected centres it
on the SLIDE and is one undo step. Bring to Front is one command whatever
the distance. Move Slide Up reorders the deck and follows the selection.
A deck-wide Replace is one undo step however many slides it touched.

**QindaNote.** A fresh window opens on a real, writable page and says so in
the status field. The page view no longer forces the window tall — a 900x500
window stays 900x500. Deleting a page removes its file as well as its index
entry. Move Page reorders within the section and refuses past either end.

### M19 the rest of the standard commands

Automated by `tst_calc_ui`, `tst_show_ui` and `tst_qindanote`. Print,
Print Preview, View ▸ Zoom and Edit ▸ Find/Replace were created disabled by
`createStandardActions()` and wired only in QindaWrite, so in the other three
apps they were menu entries the app did not implement.

- QindaShow prints through the same pure `renderSlide` the PDF export uses;
  QindaNote through `note_page_render.cpp`, a page renderer with no widget or
  palette in it, covering the whole notebook (pages never opened are read
  from disk first); QindaCalc paginates its used range by rows, never
  splitting a row across pages.
- Zoom works in all four and is presentation state only — it never touches
  stored column widths, row heights or the slide model, so it cannot dirty a
  document. QindaCalc's grid geometry and hit-testing both go through
  `colWidthPx()`/`rowHeightPx()`, which is what keeps painting and clicking
  agreeing at any zoom.
- QindaCalc's find bar gained Replace and Replace All. Replace edits the
  cell INPUT, never the formatted display text, and a whole Replace All
  sweep is ONE undo step however many cells it touched.
- QindaShow's find searches slide titles, shape text and speaker notes and
  selects the matching slide. QindaNote's searches page titles and typed
  text blocks, reading pages that were never opened first — a search that
  skipped them would report "not found" for text that is really there.

### M18 toolbar reachability

Automated by `tst_calc_ui` and `tst_write_ui`. Rationale in D-013: a command
behind an extension chevron is a command the user never sees.

- Squeezing the window to a third of its width grows the format strip a row
  instead of dropping commands; every strip command still has a visible
  button inside the strip (QindaCalc: 8 commands; QindaWrite: 16+).
- Every strip command carries a non-empty objectName.
- The cursor outline is painted on the cursor cell, not one header offset
  down and to the right of it.

### M20 manual checklist (live desktop)

- [ ] Settings ▸ Preferences…: set a document font and a default zoom; a
      newly opened window of the same app uses both. Turn off Show Toolbars
      and open a second window — its toolbars are off too.
- [ ] Turn auto-save on at 1 minute, edit a saved document and wait: the
      status bar reports the auto-save and the window title loses its dirty
      mark, with no dialog at any point.
- [ ] QindaCalc: right-click the grid and the sheet tab; drag a column
      boundary and double-click it; freeze at B2 and scroll — the first row
      and column stay put; select a numeric block and read Sum/Average/Count.
- [ ] QindaShow: the slide is a white page centred in the canvas at any
      window size, and the current slide's thumbnail shows the slide (not a
      blue rectangle).
- [ ] QindaNote: shrink the window to half the screen height — the page
      scrolls instead of the window refusing to shrink.
- [ ] Panel global menu: activate the new commands by objectName (Insert
      Rows, Sort Ascending, Bring to Front, New Section, Word Count).

### M16 manual checklist (live desktop)

- [ ] `Ctrl+S` saves in each of the four apps; `Ctrl+W` closes the window
      and `Ctrl+Q` closes the current window only.
- [ ] Panel global menu: activate *New from Template*, *Export PDF*,
      *Bring forward* and the AI inline actions from the panel — each
      resolves by objectName instead of being inert.
- [ ] Launcher and task switcher show the suite icons (the hicolor SVGs are
      installed by `make install` / the ebuild) — four mascots, one family.
- [ ] QindaCalc: type a name into the Name Box with a range selected — it
      defines the name; typing it again jumps back to that range; Ctrl+Z
      removes the definition.
- [ ] QindaNote: open a multi-page notebook, view one page, Ctrl+S, reopen —
      the pages you never opened still have their ink.
- [ ] Formatting strips show ICONS, not words: the suite's own
      hicolor/scalable/actions set resolves under the QindaQt icon theme
      (which inherits hicolor and carries none of the format family).
- [ ] Narrow any app window until the strip wraps — no "»" chevron appears
      and no command disappears.
- [ ] QindaCalc with a chart open: type into cells — the chart re-renders
      in place; its window does not disappear, move or jump to the front.
