<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaNote as an AI partner (M59, D-065)

QindaNote can listen during a session, organise the notes as it goes, answer
questions from the current page and its transcript, and prepare follow-ups
for review. A local CLI/MCP agent can also read and edit the running page
without screenshots. The application owns the notebook state, validation,
grants and undo; the model proposes wording or actions.

## Start with your technical work

In **AI → Live Notes**, choose the audio source and notes model. Press Start;
in the setup choose the session kind, then **Use suggested structure for this
kind**. This is opt-in: it preserves your topic, focus, exclusions and custom
instructions, and does not replace your saved preferences automatically.

| Situation | Suggested structure and behavior | Useful after the session |
|---|---|---|
| Technical discussion or design review | Goals/constraints, options/tradeoffs, decisions/rationale, evidence to verify. Proposed designs remain proposals until agreed. | Meeting recap; ask “Which decision was actually agreed, and why?” |
| Project planning or status | Scope, progress, milestones/dependencies, risks/blockers and decisions. Completed work stays separate from commitments. Missing owners and dates stay unspecified. | Project next steps; questions for the next discussion; reviewed tasks |
| Troubleshooting or incident | Symptoms/environment, reproduction, hypotheses, attempts/results, verified fixes, verification/rollback, plus a chronological timeline. Preserve commands and versions exactly. | Troubleshooting brief; ask “What failed, what worked, and what remains unverified?” |

Example: you discuss an audio problem. Rebooting failed; firmware 2.3 restored
audio once; suspend verification is still pending. The useful output is a
record of both attempts, an explicitly limited result, and a follow-up to
verify suspend. The model is instructed to leave the root cause unknown and
not invent a repair command. Check important claims against the cited
transcript time or recording.

The existing **Focus on**, **Leave out**, prep material, earlier-page context,
detail level, custom sections and style choices remain available. Flag an
important moment with **F9**, use **Catch me up**, and correct unclear wording
in **Session Review**. See [live notes](live-notes.md) for speech/model setup,
recordings, speaker labels and exports.

## Broader situations

The fourteen stable kinds include the original general, lecture, meeting,
interview, tutorial and talk/podcast/video choices, plus technical discussion,
planning, troubleshooting, retrospective, one-to-one/coaching, personal
appointment, workshop/conference and community/volunteer planning.

Retrospectives separate observations from interpretations and capture agreed
experiments. Coaching notes distinguish advice from commitments. Workshops
separate demonstrations from claims and keep named resources. Community
planning distinguishes proposals from decisions and does not assign people
without agreement. Personal appointments record actual instructions and
questions without adding new clinical or legal advice; their suggested setup
turns off kept recording and screen capture, which the human can opt back into.

The questions-and-review design draws on Cornell's
[note-taking system](https://lsc.cornell.edu/how-to-study/taking-notes/cornell-note-taking-system/).
Technical structures are adapted from the concrete agenda/action patterns in
GitLab's [engineering meetings](https://handbook.gitlab.com/handbook/engineering/devops/create/engineering-managers/meetings/)
and [retrospectives](https://handbook.gitlab.com/handbook/engineering/careers/management/group-retrospectives/).
These are design references, not a claim that choosing a profile guarantees
model accuracy or improved learning.

## Ask the built-in assistant

Open **AI → Assistant**. With editing off, **Page + speech** supplies the
current page's typed blocks and stored transcript; **Selection** and **No
context** are still available. Nothing is sent until Send. With editing on,
the current page and its transcript are supplied automatically.

Try “What did we actually agree?”, “Which fixes failed?”, “What evidence
supports the recommendation?” or “Which questions should we resolve next?”
The context identifies the page and block numbers, labels transcript
evidence, and requests citations to recorded times. It explicitly identifies
handwriting and image content as unread. Long sources have omission markers
and keep both their beginning and end; a model must not infer missing facts.

**Edit the document** lets the assistant rewrite/insert typed blocks, rename
the page, or update checklist items through the same engine as local agents.
The whole batch validates before mutation and becomes one native undo step,
including its title. An invalid target refuses the whole batch. A changed
page, newer content, unfinished typing/drawing, an active recording session,
or a visible modal prevents a stale reply from overwriting work. **Undo
change** refuses to undo newer human edits. Rewriting a page preserves ink,
images, stored transcripts and recording references; deleting a block that
holds session evidence is refused.

## Review follow-ups before accepting

Stop live notes, open **AI → Session Review** (or **Live → Review**), and
choose one of six **Follow-up drafts**:

| Kind | Result |
|---|---|
| Meeting recap | Summary, explicit decisions/rationale, alternatives, commitments and open questions |
| Project next steps | Status, remaining work, dependencies, blockers and separately labelled suggestions |
| Troubleshooting brief | Reproduction, investigation trail, observed results, verification/rollback and unknowns |
| Questions for next time | Unresolved questions and explicit commitments; suggested questions labelled as suggestions |
| Study page | Supported flashcards and practice questions, with fewer cards when evidence is sparse |
| Tasks for QindaMail | Outstanding commitments with source-supported owners/deadlines |

Press **Prepare draft**. Read the preview, then **Add as new page**, **Send
reviewed tasks**, or **Discard**. Generation leaves the source untouched.
Page drafts are added immediately after the source page, and you stay on
that source; select the new page in the navigator to edit it. Repeated Accept
does not duplicate it. The source is checked again at acceptance. A reply
after navigation or a human edit is discarded and asks for a fresh draft.
Discard cancels a running draft request; model failures release the busy state.

Tasks are previewed before any handoff file is created or QindaMail starts.
“Tomorrow” is anchored to the recorded session date, not the day an old note
is read. With no reliable date, relative deadlines remain unscheduled;
malformed model dates are kept as unverified review notes, without a calendar
date. Review the entire task draft before accepting. If you need a correction,
discard, correct the source, and regenerate; individual preview rows are not
editable yet. Only accepting creates the local iCalendar handoff. QindaMail
imports tasks and dated calendar entries; this does not email attendees.
If QindaMail is unavailable, the UI reports the saved file instead.

At narrow window widths, opening the review/assistant/live panel hides the
other side panels so its controls fit. Their existing View/AI commands bring
them back. The DockModel remains the placement authority.

## Work from a terminal or another assistant

In **AI**, grant **Allow Local Agents to Read This Document** or **Allow Local
Agents to Edit This Document**. Each normal launch starts disconnected.
Alternatively launch `qindanote --agent-read /path/to/notebook` or
`qindanote --agent-edit /path/to/notebook`. Grants last only for that process;
Disconnect revokes access immediately. The user-only Unix socket is local and
shared cooperatively by programs running as the same OS user.

```sh
qinda-agent sessions
qinda-agent documents
qinda-agent inspect DOCUMENT_ID
qinda-agent actions DOCUMENT_ID
qinda-agent invoke DOCUMENT_ID note.edit \
  --args '{"edits":[{"op":"set_checked","block":1,"checked":true}]}' \
  --if-revision REVISION_FROM_INSPECT --request-id verify-audio-1
```

The snapshot contains notebook section/page titles, current page ID/title,
typed blocks, checklist state, transcript/session metadata, dirty state,
zoom/scroll, selection, unfinished editor input, active modal names, live
session state and follow-up preview state. Block indices refer to the
inspected revision. Mutations return a real receipt and a named `undo_ref`.
Retry an uncertain delivery with the same request ID and identical arguments;
inspect again for current state. `note.undo` can undo that named latest change
without undoing newer human work.

`qinda-agent mcp` exposes the same operations to compatible assistants such
as Codex or Claude Code through their MCP configuration. Jev or another model
can plan calls. No model is part of the local transport itself. See the
[agent interface](agent-interface.md) for socket discovery, access semantics,
revision/retry rules and configuration.

## Current limits and implementation boundaries

This provider reads/edits the **current page**, with notebook title inventory
for orientation; it does not silently read every page. At most 1,000 page
titles, 500 current blocks and 512 KiB of block fields are reported. Omitted
fields and shortened titles/drafts have explicit flags. It does not interpret
ink or image bytes, navigate pages, save/export, start capture, change models,
or send tasks through RPC. Those need their own typed workflow contracts.

Follow-ups use the selected notes model and are ephemeral until accepted.
Every model/provider still needs a working configuration. The grounded prompt
and revision guards reduce specific errors; they cannot prove that a model's
wording is factually correct. Review citations, owners and dates. Multiple
session sources survive a whole-page rewrite, although one merged block
cannot select between several recordings in the current player.

[Synthetic model-review evidence](verification/qindanote-ai-20260929.json)
records the observed outputs and refinements, including initial mistakes.

Code boundaries: `note_evidence.*` builds bounded evidence; `note_tools.*`
validates atomic edits; `note_agent_{state,actions}.cpp` implements the SDK;
`note_session_drafts.cpp` owns inference/review/handoff lifecycle;
`live_notes_workflows.cpp` and `live_notes_followup.*` own model instructions.
All heavy verification runs on qinda. No laptop model needs to run to use the
CLI or a notes model hosted on qinda.
