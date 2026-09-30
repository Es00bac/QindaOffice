<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Live agent access — Calc and Note (M58/M59, D-064/D-065)

QindaCalc now exposes its **running document**, not a separate file copy, over
the QindaTK agent service. A CLI or MCP client can discover an open workbook,
read its live state, edit inputs, see recalculated values and undo its named
edit. The provider calls the same `CalcTools` / `SpreadsheetDocument` code as
the built-in assistant. No screen capture or coordinate clicking is involved.

M58 includes the reusable toolkit service and a complete Calc vertical slice;
M59 adds QindaNote. Write, Show and the other suite applications still need
their own domain providers; their local-agent access commands stay disabled.
Existing built-in assistants continue working. No model subscription or cloud
API is required for the local service itself.

## Human controls

Each supported document window starts **disconnected**. In **AI**, choose **Allow Local
Agents to Read This Document** or **Allow Local Agents to Edit This Document**.
The checked action records the grant. **Disconnect Local Agents** revokes it
immediately, including access to cached edit receipts. These session controls
also appear in Alt+Q command search and the exported global menu. They are
deliberately menu-only, like the shared session/restore controls.

Alternatively launch `qindacalc --agent-read budget.xlsx` or
`qindacalc --agent-edit budget.xlsx`. The flags explicitly grant documents in
that new process, including subsequently opened windows; they bypass handoff
to an existing process. Choose only one flag. Grants are never persisted or
increased by RPC. A normal subsequent launch starts disconnected.

The endpoint lives under `$XDG_RUNTIME_DIR/qindatk-agent/`, in a private
user-owned directory, with a user-only Unix socket. Other programs under the
same OS user share this cooperative grant: this is not per-agent isolation.
It is not a TCP endpoint. Granting the local service does not select a model
or send anything to one. A connected assistant chooses what context to send
using its own model/provider settings.

## Try the CLI

`qinda-agent` is installed by `dev-libs/qindatk` (Python standard library).
Open/grant the workbook in Calc, then run:

```sh
qinda-agent sessions
qinda-agent documents
qinda-agent inspect DOCUMENT_ID
qinda-agent actions DOCUMENT_ID
qinda-agent invoke DOCUMENT_ID calc.set_cells \
  --args '{"cells":{"B5":"2"}}' \
  --if-revision REVISION_FROM_INSPECT --request-id lighting-42
```

Discovery spans running application endpoints and routes the document UUID to
its granted owner. An explicit `--socket ACTUAL_ENDPOINT` selects one process.
The client reports refused or partial requests with a nonzero exit code.

For a budget with `B5=1`, `C5=2250`, `D5=B5*C5`, another line of 5,382 and
`D11=SUM(D5:D10)`, the edit returns a real application receipt showing
`D11.value=9882`. Its `undo_ref` names that single undo step:

```sh
qinda-agent inspect DOCUMENT_ID
qinda-agent invoke DOCUMENT_ID calc.undo \
  --args '{"undo_ref":"REF_FROM_RECEIPT"}' \
  --if-revision CURRENT_REVISION --request-id undo-lighting-42
```

The result is again 7,632. Agent edits use the native undo stack. A named undo
is refused after any intervening content mutation, so it cannot accidentally
undo a person's newer edit. Ordinary Edit → Undo remains available.

## What the agent actually reads

The snapshot reports the document title, dirty/loading state, active sheet,
sheet list, selection and cursor, focused object, draft input, visible modal
objects, filter and validation rules. Each populated cell includes its input,
typed engine value (or an error), display string, number format and whether it
is on screen. The view reports zoom/scroll and visible rows/columns taken from
the **same bands the grid paints**, including frozen panes and filtered rows.

On-screen cells get priority. Snapshots include at most 3,000 populated cells
and roughly 1 MiB of cell JSON. `truncated` / `omitted_cells` report missing
cells; values are omitted whole rather than silently shortened. The agent
must not infer facts about omitted cells. Large-sheet range queries and
paged snapshots are follow-up work. The current snapshot is structured
workflow context, not automatic introspection of every QML control.

Actions are typed/discovered: `calc.set_cells`, `calc.set_range`, `calc.clear`
and `calc.undo`. Writes are active-sheet scoped, bounded to 20,000 cells and
checked for worksheet bounds and validation before any mutation. They are
disabled during loading, an unfinished grid/formula input or a visible modal
dialog. Input semantics remain Calc's: a string beginning `=` goes through
its parser; inspect the reported value/error rather than assuming a valid
formula or a successful numeric result. Edits mark the open workbook dirty;
saving still uses the normal File menu. This first provider does not expose
save, export, sheet switching or arbitrary command activation.

## QindaNote's live page (M59)

`qindanote --agent-read /path/to/notebook` or `--agent-edit` grants a running
notebook. `inspect` reports its section/page title inventory and current page
blocks, checklist state, stored transcript/session metadata, dirty state,
viewport, selection/editor draft, drawing, modals, live capture state and
follow-up draft. Ink and image contents are explicitly unread. Snapshots cap
1,000 page titles, 500 blocks and 512 KiB of block fields; omissions and
shortened fields have explicit flags.

`note.edit` accepts one `edits` array: `replace_block`, `insert_block`,
`delete_block`, `replace_page`, `rename_page`, or `set_checked`. Up to 256
operations, 256 KiB per batch, 65,536 characters per text, 512 per new title,
and 500 resulting blocks. Block numbers always refer to the read snapshot.
The entire batch validates before mutation; title and content share one undo
step. Ink/images and session evidence survive rewrites; deletion of a session
evidence block is refused. `note.undo` accepts the receipt's named `undo_ref`.
Newer human work invalidates that reference. The built-in Note assistant uses
the same tools and revision checks, including transcript context in read mode.

Edits are disabled during unfinished typing/drawing, live capture or a modal.
This first provider does not navigate pages, expose all page contents, save,
export, start recording or hand off tasks. Follow-ups are human-reviewed UI
drafts; edit permission is not permission to send them. See
[QindaNote AI](qindanote-ai.md) for the human workflow and concrete use cases.

## Concurrency, receipts and the built-in assistant

Every mutation/load/undo advances a generation. The opaque revision combines
that with a hash of current document/view/draft state. A request must echo the
revision it read. A human edit, changed active sheet, selection, viewport or
draft invalidates an older request; the agent refreshes and reasons again.

Choose a unique `request_id` for a new action. If delivery is uncertain, retry
the **same ID with identical parameters**. It returns the stored receipt
without another mutation. Replays include a `current_revision`; the original
receipt result is historical, so inspect again for current facts. Receipts
are session-local, capped at 1,024 without eviction; activity keeps 256
metadata events. Restarting/crashing ends the deduplication session.

Calc's built-in assistant also captures the live revision before inference.
If editing is turned off or the revision changes before the reply returns,
the reply is shown with a refusal and no edits applied. Its Undo change
checks content/history and unfinished input, so clicking Undo can change focus
without blocking the undo. CalcTools validates the entire proposed
batch, so the built-in and external paths obey worksheet rules alike. Other
apps can supply `DocumentTools::revisionToken()` and an `undoRevisionToken()`
that excludes harmless focus/viewport changes when they adopt the contract.

## MCP and application adoption

Configure a compatible MCP client to start `qinda-agent mcp`. It can initialize
before any app opens. Tools discover endpoints, list granted documents,
inspect them, discover action schemas, invoke actions and poll one endpoint's
activity. Resources expose granted snapshots. `qinda_events` accepts an
`endpoint` from discovery; sequence numbers belong to that process. A fixed
`qinda-agent --socket ACTUAL_ENDPOINT mcp` is also supported. Client-specific
configuration belongs to that client's settings. Jev or another model can
plan calls; validation, grants and the final result stay in the application.

Provider implementation: `apps/qindacalc/src/calc_agent_{state,actions}.cpp`.
The suite integration lives in `office_quick/src/agent_runtime.*` and
`document_host_agent.cpp`. Toolkit contract:
`QindaTK/docs/agent-interface.md`, CMake target `QindaTK::agent`.
For source development against a system toolkit that predates this SDK:

```sh
cmake --preset dev -DQQO_AGENT_SOURCE_DIR=/path/to/QindaTK
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

This builds the SDK as a source dependency; it does not install home binaries.
Production uses the Portage-installed target. The next providers should expose
stable domain IDs, bounded live state, typed actions over existing engines,
explicit grants and revision-aware undo. Export/send/publish actions need
their own real consent and error contracts rather than reusing cell-edit
permissions. Cross-document transactions, asynchronous jobs and cancellation
are not supplied by this first synchronous service.
