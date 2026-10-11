<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Mail essentials: contacts, safe HTML, message files (QindaMail, `office_mail`)

M64 / [D-072](decisions.md#d-072-mail-essentials-contacts-safe-html-message-files);
M76 / [D-085](decisions.md#d-085-qindamail-outlook-data-files-contact-groups-carddav):
Outlook data files, contact groups and CardDAV address books.
What an Outlook user needs every day and QindaMail lacked (survey in
[plans/office-parity.md](plans/office-parity.md)): an address book with vCard
and Outlook CSV, recipient autocomplete, HTML mail shown as Outlook shows it
with remote pictures blocked, inline pictures, saving and opening attachments,
printing and PDF, and `.eml` / `.mbox` / Outlook `.msg` files. The accounts
layer and the app's architecture are in [mail.md](mail.md).

## Formats

| Format | Open / import | Save / export |
| --- | --- | --- |
| `.eml` (RFC 5322) | File ▸ Open, the file manager, `qindamail file.eml` → "On this computer ▸ Inbox" | File ▸ Save Message As (F12): the message's original source byte for byte when this computer has it, else written from the stored record |
| Outlook `.msg` (MS-OXMSG over MS-CFB) | the same paths; mail is converted to RFC 5322 first, then stored like an `.eml`; a contact, contact group, appointment or task saved as `.msg` goes to People, the calendar or the task board (M76) | as `.eml` (Outlook opens it) |
| Outlook data file `.pst` (Outlook 97–2002 ANSI and 2003+ Unicode), `.ost` of Outlook 2003–2010 | File ▸ Import ▸ Outlook Data File (.pst)…, the file manager → mail into a local folder tree named after the file, contacts and groups into People, appointments into the calendar, tasks onto the board ([below](#outlook-data-files-pst-ost)) | — |
| `.mbox` (mboxo / mboxrd, Thunderbird, Apple Mail, Google Takeout) | File ▸ Import Mailbox, the file manager → a new local folder named after the file | — |
| vCard `.vcf` 2.1 (Outlook), 3.0 (RFC 2426), 4.0 (RFC 6350), many cards per file; contact groups as 4.0 `KIND:group` + `MEMBER` or Apple's 3.0 `X-ADDRESSBOOKSERVER-KIND` / `-MEMBER` | File ▸ Import Contacts, People ▸ Import, the file manager | File ▸ Export Contacts: vCard 3.0 (default — Outlook, Google, Apple) or 4.0 |
| CardDAV address books (RFC 6352; iCloud, Fastmail, Nextcloud, Google, …) | People ▸ Connect Address Book…, or Contacts (CardDAV) in a mail or calendar account; synced both ways with the account ([below](#address-books-carddav)) | the same sync |
| Outlook contacts `.csv` (Outlook desktop and Outlook.com exports; Google's CSV too) | File ▸ Import Contacts | — |
| Print / PDF | — | File ▸ Print, Print Preview, Export as PDF (the selected message) |

The desktop file adds `text/vcard`, `text/x-vcard`, `application/vnd.ms-outlook`,
`application/vnd.ms-outlook-pst` and `application/mbox` to `message/rfc822`, so
the file manager opens them with QindaMail.

## The address book (People)

`ContactStore` (`libs/office_mail`, QML `ContactStore`, reached as
`MailService.contacts`): one store per process beside the mail store,
`$XDG_DATA_HOME/qindaoffice/mail/contacts.json`, schema
`qindaoffice.mail.contacts/1`, written atomically; pictures are files in
`contact-photos/` next to it, named by their SHA-256. The `AccountStore` rules
hold: unknown fields kept, another schema read-only, an unparseable file moved
to `contacts.json.corrupt`. Bounded: 20 000 contacts, 1 MiB per picture, 64
values per repeated field.

A contact has name parts (prefix, first, middle, last, suffix), a display
name, nickname, typed e-mail addresses, phones, postal addresses and web
pages, company / department / job title, birthday and anniversary, notes,
categories and a picture. vCard properties QindaMail does not model (IMPP,
X-…, KEY, GEO, …) are kept and written back, so a card survives a round trip.

The **People** workspace (rail, View ▸ People, Ctrl+4) lists and searches the
contacts and edits the selected one in place (Save / Revert, New message,
Picture…, Delete). Commands: New Contact (Ctrl+Shift+C), Delete Contact,
Import Contacts…, Export Contacts…, and, on a message, **Add Sender to
Contacts** (Home ▸ People, the reading pane, the Message menu).

**Contact groups** (M76; Outlook's Contact Group, vCard 4.0's `KIND:group`):
New Contact Group (Ctrl+Shift+L; File ▸ New, People) opens an editor with
the group's name, its members — contacts of the address book or plain
addresses, added through the composer's suggestions (a group may hold
groups) — and notes; New message writes to every member. A group is a
contact of kind `group` whose members are `urn:uuid:<uid>` (a contact) or
`mailto:` (an address) URIs; up to 5 000 members, expanded through nested
groups to a depth of 8 with cycles ignored, each address once; a member
whose contact was deleted is shown as "Not in your contacts" and skipped.
In the composer a group is suggested as "Name (group)"; picking it, or
typing its name, puts its members in that field. Since M89 (D-097) a picked
group is known by the suggestion chosen — QindaTK's two-argument token
validator hands the entry over (D-314), the token becomes
`group:<contactId>` and that group's members replace it — so two groups of
one name, or a contact named like a group, are never confused.

**Recipient autocomplete**: the composer's To / Cc / Bcc fields suggest as you
type — contacts first (name, nickname, e-mail; every word must start a word),
then addresses seen in mail (senders and recipients of stored mail and
drafts, by frequency then recency; never your own addresses).
`RecipientCompleter` is the `completionProvider` of QindaTK's `Tk.TokenField`.

### vCard and CSV details

- Reading: folded lines (CRLF/LF + space or tab), vCard 2.1 quoted-printable
  with soft line breaks and `CHARSET=`, 2.1 bare parameters (`TEL;WORK;VOICE`),
  base64 pictures (2.1 `ENCODING=BASE64`, 3.0 `ENCODING=b`, 4.0 `data:` URIs),
  `TYPE=` lists and `PREF`, escapes, Apple's `item1.` groups with `X-ABLabel`,
  a remote `PHOTO` URL kept as text and never fetched; a broken card is
  skipped and counted, the rest of the file still reads.
- Writing: UTF-8, CRLF, folded at 75 octets (never inside a character).
- Outlook CSV: RFC 4180 (quoted commas, doubled quotes, line breaks in
  fields), a BOM, Windows-1252 when the bytes are not UTF-8, the header names
  of Outlook's export (`First Name`, `E-mail Address`, `Business Phone`,
  `Business Street`, `Birthday` as `M/D/YYYY`, `0/0/00` = none, …).
- Import merges: the same UID updates; else the same primary e-mail fills the
  empty fields and adds the new addresses and numbers; else a new contact.
- Groups: `KIND:group` (4.0) and `X-ADDRESSBOOKSERVER-KIND:group` (3.0) are
  read alike, members from `MEMBER` / `X-ADDRESSBOOKSERVER-MEMBER` (Apple's
  `X-CN` parameter as the member's name); other `KIND` values are kept as
  unknown properties. Export writes the form of the chosen version.

## Address books (CardDAV)

A mail account (IMAP, POP3, Gmail) or a CalDAV account can sync its
provider's address books — turn on **Contacts (CardDAV)** in the account
dialog — and People ▸ **Connect Address Book…** sets up a contacts-only
account (kind `carddav`). QindaMail suggests the server from the address
(`contacts.icloud.com`, `carddav.fastmail.com`, Google's CardDAV principal,
else `https://<domain>/`); **Find address books** follows
`/.well-known/carddav` (RFC 6764), the `current-user-principal` and the
`addressbook-home-set` (RFC 6352) and lists the books to sync. The password
is the account's (an app password at iCloud or Fastmail) unless one is
given for the address books; Google uses the account's sign-in (the
`carddav` scope: an existing Google account signs in once more).

Sync runs with the account's (Sync All, F5, and the account's interval):

- **Pull**: RFC 6578 `sync-collection` from the stored token (a server that
  refuses an old token is asked again from the start); without it, the
  book's `getctag` and an ETag listing. Changed cards come by
  `addressbook-multiget` in batches of 50 (GET one by one when the report is
  refused) and are merged into People (unknown vCard properties kept).
- **Push**: a contact changed here is PUT with `If-Match: <etag>`, a new
  one (saved to that book from the contact card's **Save to**) with
  `If-None-Match: *`, a deleted one is DELETEd with `If-Match`.
- **Conflicts — the server wins.** A card changed in both places takes the
  server's version; yours is kept as a copy in the category
  **Sync conflict** and a notice names it. A deletion the server refuses
  (412) brings the card back.
- Groups travel as Apple's vCard 3.0 groups, which iCloud, Nextcloud and
  Fastmail show as groups.
- Removing the account keeps its contacts on this computer, no longer
  synced.

The sync state (each card's href, ETag and a SHA-256 of what was last
synced; each book's token and ctag; deletions not yet sent) is kept in
`contacts.json` under `carddav`. Bounds: 20 000 requests and 16 MiB per
response per sync, 50 000 responses per listing, XML with a DTD refused,
https only (http to a loopback address for local servers), redirects only
within the same site, the password never in a result, a transcript or a
log.

## Reading mail as Outlook shows it

The reading pane renders the message's **source** (below): the best body of a
`multipart/alternative` (the last HTML one, else the last text one), the root
of a `multipart/related`, inline text parts after the body appended as mail
clients show them.

- **HTML** is sanitized before Qt's rich-text engine sees it
  (`html_sanitizer`): tokenized and regenerated, never passed through; an
  element allow-list (scripts, iframes, objects, embeds, forms and their
  controls, SVG/MathML, `base`, `link`, `meta` removed; HTML5 sectioning
  elements become `div`); every `on…` handler and every non-`http(s)`/
  `mailto` link removed (entity- and whitespace-obfuscated `javascript:`
  included); CSS without `url()`, `@import`, `expression()`, `behavior`,
  `-moz-binding`, fixed/absolute positioning and at-rules (Qt abandons a
  sheet at `@media`); hidden content dropped — `display:none` (inline or by
  a simple class/id rule), `mso-hide:all`, the `hidden` attribute — because
  Qt would show it. Tables beyond 64 columns or 10 000 cells, and nesting
  deeper than 6, are flattened (Qt pads every row to the widest and lays out
  nested tables exponentially). The pane shows the first 2 M characters of
  a body and says so.
- **Remote pictures are blocked** by default (tracking pixels): a bar says so
  and offers **Show pictures** (this message, this session) and **Always show
  from this sender** (kept in `view.json`, at most 2 000 senders; the same
  bar turns it off). Only then are the pictures downloaded — http(s), at most
  64 per message and 8 MiB each, 15 s, no cookies, nothing written to disk —
  and the body re-renders; a picture of 2×2 px or less (a tracking pixel)
  is dropped even then. Home ▸ Show Pictures does the same.
- **Inline pictures** (`cid:`) come from the message's related parts; `data:`
  pictures are decoded; attached pictures appear below the body; every
  picture is scaled to the pane's width; only raster formats (PNG, JPEG,
  GIF, BMP, WebP) are decoded, never SVG, and never more than 10 000 px a
  side. **Every image URL the HTML names is registered with the document
  before the HTML is set** — the decoded picture, or an empty image — because
  `QTextDocument` asks its registered resources first and only then its
  loader (for the reading pane's QML TextEdit, a network fetch through the
  QML engine; for the printer's document, a local file). So no URL is ever
  fetched by Qt (D-072 point 3).
- **Plain text** is escaped, keeps its line breaks and gets clickable links
  (`http(s)://`, `www.`, e-mail addresses); quoted lines are set off.
- Mail is shown on **paper** (white page, black ink) in every theme, as it is
  written for one (AGENTS.md rule 3). Text is selectable and copyable; a
  hovered link shows its real target in the status bar; clicking an http(s)
  link opens the desktop's browser, a `mailto:` link opens a composer, any
  other scheme is refused with a notice.
- **Attachments**: open with the desktop's handler (the existing private
  0700 copy), **Save** one (a save dialog), **Save All Attachments…** into a
  folder (existing names get " (2)"…); pictures placed inside the body are not
  listed again.

### Where the source comes from

QindaTK's providers keep only the plain text of a message (the toolkit's
`MailRecord::fromParsed`; follow-up in D-072), so QindaMail keeps the source
itself, in `<dataDir>/sources/<sha256(id)>.eml` (up to 64 MiB a message):

| Message | Source |
| --- | --- |
| opened `.eml` / `.msg`, imported mbox | written at import (`localSource: true` on the record) |
| IMAP | downloaded whole on first display (`sourceCached: true`) by `ImapSourceFetcher`: `UID FETCH <uid> (UID BODY.PEEK[])` — the message stays unread — over QindaTK's own IMAP session (TLS, STARTTLS, PLAIN/LOGIN/XOAUTH2, redaction), with the credential `AccountRuntime::run` applies to every request. The toolkit provider's own downloads accept numbered parts only. |
| POP3, local Sent copies, drafts | none: the stored record (text body, the pictures and invitations already downloaded) |

Until an IMAP source arrives the pane shows the record's text and, after a
moment, "Downloading the full message…". Attachments of a message whose
source is kept are read from it — no second download. A source goes with its
record; files whose record vanished (an expunge applied by a sync) are
pruned when the app starts.

## Printing

File ▸ Print / Print Preview (enabled when a message is selected) and Export
as PDF print the message as Outlook prints a memo: subject, From, Sent, To,
Cc, Attachments, a rule, then the body exactly as the pane renders it (the
same sanitizer and pictures; remote pictures only when shown on screen). The
document is laid out in CSS pixels at 96 dpi and painted through a scaled
painter, so mail's `font-size: 14px` prints at its size; pages are A4/Letter
from the print dialog with 15 mm margins.

## Meeting requests (iMIP)

A message with a `text/calendar; method=REQUEST` part shows a meeting card
above the body — title, when (in the event's time zone), where, organizer,
your answer so far — with **Accept**, **Tentative** and **Decline**
(RFC 6047, the receiving side). Accept and Tentative put the meeting into
the local calendar (marked with its iCalendar UID and your answer; answering
again updates it), Decline takes it out; then a `METHOD:REPLY` (RFC 5546
§3.2.3: the request's UID, SEQUENCE, time and organizer, only you as an
attendee with your PARTSTAT, its VTIMEZONEs) goes to the organizer through
the account the request came to, as "Accepted: …" mail. An invitation
opened from a file, or one no account can answer, only updates the
calendar. A POP3 invitation works too (its calendar part is downloaded with
the message).

**Sending invitations**: the event dialog has an **Invite** field (the same
suggestions as the composer). Saving an event with invitees mails them a
`METHOD:REQUEST` (RFC 5546 §3.2.2: the toolkit's VEVENT mapping plus
ORGANIZER — the sending account — and one `ATTENDEE;ROLE=REQ-PARTICIPANT;
PARTSTAT=NEEDS-ACTION;RSVP=TRUE` per invitee) through the calendar's own
account when it can send, else the default one; the event keeps its
iCalendar UID, invitees and SEQUENCE, so saving again sends "Updated
invitation" with the next SEQUENCE.

Limits: the toolkit's SMTP provider attaches requests and replies as
`invite.ics` / `reply.ics` files — it cannot write a `text/calendar;
method=…` part — so a server that wants a proper iMIP part shows the
attachment instead of acting on it by itself. A received CANCEL shows the
card with **Remove from calendar** (nothing is sent). Sending a CANCEL when
you delete a meeting you organize, and COUNTER, are not done (D-072).

## Message files

- **`.eml`** is stored as is; its id is `local:eml:` + its SHA-256, so the
  same file opened twice is one message.
- **mbox**: a pull reader (`MboxReader`), a few messages per event-loop turn
  so the window stays responsive; separators are `From ` lines at the start or
  after an empty line carrying a `hh:mm` time (every writer puts one there),
  so an unescaped body line "From the start…" does not split a message; every
  `^>+From ` line loses one `>` (mboxrd, also right for mboxo's single level).
  Bounds: 64 MiB per message (larger ones skipped and counted), 100 000
  messages per file.
- **Outlook `.msg`**: an MS-CFB reader (v3 and v4 sector sizes, DIFAT, mini
  stream; every sector number, offset, chain and size checked, cycles
  detected — corrupt or truncated files fail with an error, never a crash),
  the MS-OXMSG mapping (subject, From / Sender with SMTP addresses — Exchange
  X.500 addresses are never used as an address —, recipients by type, dates,
  plain and HTML bodies, compressed RTF per MS-OXRTFCP with the HTML
  de-encapsulated per MS-OXRTFEX when that is the only body, attachments with
  names, MIME types and Content-IDs, embedded messages as attached
  messages), then the RFC 5322 writer. The writer is deterministic (content-
  derived boundaries and Message-ID), so importing the same `.msg` twice is
  one message, and its output opens in Outlook.

## Outlook data files (.pst, .ost)

File ▸ Import ▸ **Outlook Data File (.pst)…** (also a `.pst` or `.ost`
opened from the file manager or `qindamail file.pst`) brings a whole Outlook
file across. A dialog shows the folder being read, a progress bar and the
counts as they grow; **Cancel import** stops at once and keeps what is in;
the summary lists folders, messages, contacts, groups, appointments, tasks
and skipped items with the first warnings, and **Show imported mail** opens
the folder. The window stays usable meanwhile (one import at a time per
process).

| In the file | Goes to |
| --- | --- |
| mail, posts, meeting mail, reports in any folder | a local folder tree under "On this computer", named after the file (`Outlook`, `Archive (2)` when the name is taken) and mirroring the file's folders from its top of the information store; read and flag state kept; converted by the `.msg` mapping (bodies, compressed RTF, attachments, embedded messages, recipients) |
| contacts (`IPM.Contact`) | People: names, e-mail addresses (SMTP only, never Exchange X.500), phones, postal addresses, company, birthday and anniversary, notes, categories, the picture |
| contact groups (`IPM.DistList`) | People as groups: members that are contacts of the same file stay linked to them, others by address |
| appointments (`IPM.Appointment`) | the local calendar, in their own time zone (Windows names mapped to IANA, the system zone when the item names none), all-day as dates, recurring ones with their rule and deleted occurrences, location, categories, organizer and attendees |
| tasks (`IPM.Task`) | the task board: To do / Doing / Done from the task's status and percent complete, start, due and completed dates, priority, categories, notes |
| notes, journal entries | skipped and counted ("Notes and journal entries are not imported") |

- **Again without duplicates.** Every item's identity is the file's record
  key plus its node id (contacts and groups get a version-5 UUID from it,
  mail its content id), and the record key remembers the local folder it
  went to (`view.json` "outlookImports"), so importing the same file again
  replaces what it brought.
- **Streaming.** The folder tree is read first, then the items in slices of
  about 40 ms; nothing is held for the whole file but its folder list.
  Message sources are written without an fsync each and synced once before
  the store is saved; People is written once at the end. A 10 000-message
  file took 46 s on qinda (the gate's bound is 180 s; reading it alone
  takes 1.2 s): most of that is the toolkit store's per-record upsert
  (D-085).
- **The reader** (`libs/office_mail/src/pst_*`, MS-PST): Unicode (64-bit)
  and ANSI (32-bit) files, no encoding, the permute ("compressible")
  encoding and the cyclic ("high") encoding, data trees of any depth,
  subnodes, the heap-on-node, BTH, property and table contexts, named
  properties. Everything read is bounded and checked — offsets and sizes
  against the file, block sizes and CRCs against the index, signatures,
  B-tree levels (16 at most), cycles in any tree, 64 MiB per node, 1 048 576
  subnodes, 2 000 000 table rows, 8 levels of embedded messages, 1 000
  attachments and 10 000 recipients per message — so a damaged file fails
  with an error naming the problem ("checksum mismatch", "loops back on
  itself", "truncated", …) instead of crashing, and an item that cannot be
  read is skipped and counted. Folders nest 64 deep at most, 100 000 of
  them.
- **Refused by name**: the 4 KiB-page `.ost` of Outlook 2013 and later
  ("Outlook 2013 …"), and files encrypted with Information Protection. A
  `.pst` password is not asked for: Outlook's password does not encrypt
  the file.

## Limits

- Qt's rich-text engine is not a browser: CSS layout (floats, flex, grid,
  `max-width`), web fonts and background images are not rendered; tables,
  colours, fonts, lists, quotes and pictures are.
- POP3 messages show text only (the toolkit keeps no HTML); an IMAP message
  above 64 MiB too.
- The composer already writes formatted mail (QindaTK's `Tk.FormatBar`:
  font, size, bold, italic, underline, strike, colours, alignment, bulleted
  and numbered lists, links; sent as multipart/alternative) and per-account
  plain-text signatures; pictures inside the body need the toolkit's MIME
  builder to write multipart/related (D-072 follow-up).
- Outlook data files: a changed occurrence of a recurring appointment keeps
  the series' time (deleted occurrences are honoured); a group's members are
  read from `PidLidDistributionListMembers` / `…OneOffMembers`, not the
  newer `PidLidDistributionListStream`; folders without mail are not
  created; notes and journal entries are skipped; meeting mail arrives as
  mail without its calendar part.
- CardDAV writes vCard 3.0 (Apple's group form) and syncs when the account
  syncs (no push); a contact changed in two places keeps the server's
  version and a copy of yours.

## API (M76 additions)

`MailService`: `outlookImport` (`PstImporter`: `running`, `phase` idle |
scanning | importing | done | failed | cancelled, `fileName`,
`currentFolder`, `total`, `processed`, `progress`, `counts`, `summary`,
`lastError`; `start(url)` → requestId ("" while one runs), `cancel()`;
`finished(requestId, summary)` with {state, file, folder, folderKey,
folders, messages, contacts, groups, events, tasks, skipped, total, error,
warnings}), `importOutlookItemFile(path)` → {handled, ok, view, id, error}
(a `.msg` contact, group, appointment or task; `handled` false for mail),
`discoverAddressBooks(requestId, url,
userName, password)` → `addressBooksDiscovered(requestId, {ok, state,
error, principalUrl, homeSetUrl, addressBooks [{url, name}]})`.

`ContactStore` (office_mail): contact maps gain `kind`, `isGroup`,
`members` [{uri, name, address, displayName, kind contact | group | address
| missing, contactId}], `memberCount`, `syncAccountId`; `add()` takes
`addressBookAccount` + `addressBookUrl` (a new contact for that CardDAV
book); `expandGroup(id)` → {ok, name, recipients ["Name <address>"],
count, missing, error}; `contactForMember(uri)`; `complete()` offers groups
({type: "group", group: true, contactId, memberCount}). C++:
`importContacts(list)`, `syncSnapshot`/`applySync`/`forgetSync`
(`CardDavProvider`'s local side), `kConflictCategory`.

`CardDavProvider` (QML type of `QindaOffice.Mail`): `accountId`, `url`,
`userName`, `addressBooks` [{id, url, name, enabled}], `store`,
`timeoutMs`, `allowInsecureAuth`, `hasSecret`, `busy`, `lastError`;
`setSecret(secret, basic | bearer)`, `clearSecret()`, `discover(requestId,
url)` → `discoveryFinished`, `sync(requestId)` → `syncFinished(requestId,
{ok, state, error, created, updated, deleted, pushed, conflicts,
conflictNames, books})`. `AccountStore`: `contactsUrlFor(address)`,
`addressBooks()` → [{accountId, accountName, id, url, name}].

`MailHost`: signals `outlookImportRequested()`, `contactGroupNewRequested()`,
`addressBookRequested()`; `.pst`/`.ost` handed to the app start the import.
QML types of `QindaOffice.Mail.App`: `PstImportDialog`, `ContactGroupEditor`,
`AddressBookSection`; `RecipientCompleter.expand(label)` → the members of a
group suggestion, `groupIdFor(label)`.

## API (M64 additions)

`MailService` (`host.service`, [mail.md](mail.md#qml-api) for the rest):

| Kind | Names |
| --- | --- |
| property | `contacts` (`ContactStore`) |
| message files | `importMessageFile(path)` (`.eml`, `.msg`; an mbox goes to `importMailbox`), `importMailbox(url)` → requestId, `saveMessageAs(messageId, url)` → requestId, `saveAllAttachments(messageId, folderUrl)` → requestId |
| reading | `openLink(url)` → bool (http/https only), `showPictures(messageId)`, `picturesShown(messageId)`, `setSenderPicturesAllowed(address, allowed)`, `senderPicturesAllowed(address)` |
| people | `addSenderToContacts(messageId)` → {ok, id, existed, error}, `recipientSuggestions(query, limit = 8)` → [{label, value, valid}] |
| meetings | `meetingOf(messageId)` → {method, uid, title, startLocal, endLocal, zone, allDay, location, organizer(Name), attendees, myAddress, myStatus, eventId, canReply}, `respondToMeeting(messageId, accepted \| tentative \| declined)` → requestId, `sendInvitations(eventId, invitees)` → requestId |
| C++ only | `storedSource(id)`, `loadSource(id, done)`, `contentOf(id)` (`MessageContent`), `renderPolicy(id)`, `fetchRemoteImages(id)`, `setRemoteFetcher(fn)` (tests: no network), `setUrlOpener(fn)` |
| signals | `sourceLoaded(messageId)`, `picturesChanged(messageId)` (empty id: every message of a sender), `meetingChanged(messageId)` |

`MailHost`: `selectedContactId` (RW, the People selection), `exportMessagePdf(url)`,
`suggestedFileName(suffix)`, `suggestedSaveUrl(fileName)`; signals
`contactNewRequested()`, `contactDeleteRequested(id)`,
`contactsImportRequested()`, `contactsExportRequested()`,
`mailboxImportRequested()`, `messageSaveRequested()`,
`attachmentsSaveRequested()`, `pdfExportRequested()`; the `people` view.

QML types of `QindaOffice.Mail.App`: `MessageView` (`service`, `messageId`,
`document` = a TextEdit's `textDocument`, `maxImageWidth`; `loading`,
`formatted`, `remoteBlocked`, `picturesShown`, `senderTrusted`,
`senderAddress`, `hiddenAttachmentIds`, `pictureCount`, `remoteImageCount`,
`truncated`, `meeting`;
`showPictures()`, `setSenderTrusted(bool)`, `plainText()`) and
`RecipientCompleter` (`service`, `limit`; `request(query)` → id, later
`finished(id, {candidates})`). QML type of `QindaOffice.Mail`: `ContactStore`.
