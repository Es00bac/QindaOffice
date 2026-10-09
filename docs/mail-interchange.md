<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Mail essentials: contacts, safe HTML, message files (QindaMail, `office_mail`)

M64 / [D-072](decisions.md#d-072-mail-essentials-contacts-safe-html-message-files).
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
| Outlook `.msg` (MS-OXMSG over MS-CFB) | the same paths; converted to RFC 5322 first, then stored like an `.eml` | as `.eml` (Outlook opens it) |
| `.mbox` (mboxo / mboxrd, Thunderbird, Apple Mail, Google Takeout) | File ▸ Import Mailbox, the file manager → a new local folder named after the file | — |
| vCard `.vcf` 2.1 (Outlook), 3.0 (RFC 2426), 4.0 (RFC 6350), many cards per file | File ▸ Import Contacts, People ▸ Import, the file manager | File ▸ Export Contacts: vCard 3.0 (default — Outlook, Google, Apple) or 4.0 |
| Outlook contacts `.csv` (Outlook desktop and Outlook.com exports; Google's CSV too) | File ▸ Import Contacts | — |
| Print / PDF | — | File ▸ Print, Print Preview, Export as PDF (the selected message) |

The desktop file adds `text/vcard`, `text/x-vcard`, `application/vnd.ms-outlook`
and `application/mbox` to `message/rfc822`, so the file manager opens them with
QindaMail.

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
- `.pst` files, CardDAV and contact groups (distribution lists) are not
  supported.

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
