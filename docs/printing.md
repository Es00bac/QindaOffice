<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Printing

File ▸ Print (Ctrl+P) opens the suite's QindaTK dialog. The printer list is
the configured system queues reported by Qt PrintSupport/CUPS. Connect and
switch on a printer, add it in system printer settings, then choose
**Refresh printers** without closing the document or restarting the app.
The desktop package owns the CUPS service, discovery and driver/filter
installation; Office does not install drivers or create queues itself.

The initial selection follows the system default, or the first available
queue if there is no default. Refresh preserves the selected queue by name.
If that queue disappears, Print stays disabled until another printer is
chosen. An empty list explains setup and PDF export. Print also checks the
exact selected queue immediately before rendering, so a removed queue
cannot silently fall back to a different printer.

The dialog offers copies, all pages or an inclusive page range, and
orientation. An inverted range is rejected. QindaWrite applies the range
to its original page stack, including mail merges and envelopes/labels,
while retaining document page numbers and headers/footers. Starting after
the last page is an error; an end beyond the last page stops at the last
page. The first selected Word section supplies its paper before painting
starts. Later pages keep their own section paper as before. Unless the
orientation was explicitly changed, a range follows its first section's
orientation.

Print, Print Preview and Export PDF share each application's renderer.
`PrintJob` supplies the same configured range to preview and PDF renderers;
the renderer is responsible for observing it. QindaWrite's range behavior is
covered here; the other Office renderers still need their own range support.
QindaWrite additionally supplies an optional original-page-to-paper
callback. It runs on the GUI thread with the same document lifetime as the
render callback; an invalid layout means the requested page does not exist.
Other apps keep their existing fixed initial paper API. Render callbacks
borrow the active painter and must leave completion to `PrintJob`.

Success means the print backend accepted the completed job. The status says
“Sent to the printer queue”; it does not claim that paper emerged. Failures
remain in a dismissible message dialog. A disconnected or out-of-paper
device may still accept a queued job: inspect the system printer queue for
delivery status. Backend completion failures, missing printers and invalid
ranges never report success.

## Verification

`tst_print_service` checks explicit queue selection, invalid-range refusal,
render completion, and range-consistent previews. `tst_write_print` uses
the production WriteHost, renderer and QML window for selected text, original
page numbering, a landscape section as the first selected page, out-of-range
refusal, setup/refresh UI and persistent error feedback. By default it uses
a nonexistent private CUPS socket and never submits a print job.

With a configured queue, this read-only opt-in inspects the real dialog and
saves its wide/narrow screenshots without printing:

```sh
QQO_TEST_PRINT_QUEUE='<queue>' QQO_GRAB_DIR=build/print-grabs \
  build/dev/tests/qindawrite/tst_write_print printerDialogReadOnlyOptIn
```

Physical verification is separate. This exact opt-in submits one generated
page through WriteHost and PrintJob; it never opens a user's document:

```sh
QQO_TEST_PRINT_QUEUE='<queue>' \
  build/dev/tests/qindawrite/tst_write_print hardwarePrinterOptIn
```

Both the queue environment variable and the exact hardware test argument
are required. Confirm the generated page and CUPS job state separately;
an automated submission result alone does not prove physical output.
