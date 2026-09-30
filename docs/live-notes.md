<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Live notes (QindaNote, M52, D-057)

QindaNote can take notes by itself while something is being said: a
lecture in the room, a meeting or call, a YouTube video or anything else
playing on the desktop. It listens and transcribes. A language model keeps
one set of well-formatted notes on the page, revising them as more
information arrives. The notes are one **markdown block** on a notebook
page, so they print, export to PDF, take ink and can be edited like
anything else on the page.

M59 adds technical discussion, project planning, troubleshooting and broader
session structures, transcript-based read-only answers, reviewed follow-up
drafts and local CLI/MCP access. See [QindaNote AI](qindanote-ai.md) for examples
and the human workflow.

## Using it

**AI ▸ Live Notes** (Ctrl+Shift+L), or the **Live** ribbon tab, opens the
panel.

1. Optionally name the session ("Biology 101 — lecture 4"). The name
   becomes the page title and a hint to the model.
2. Choose what to listen to:
   - **Microphone** — a lecture in the room.
   - **Desktop audio** — whatever the desktop is playing, such as a video.
   - **Both** — a call you take part in.
3. Press **Start live notes**. The **New live notes session** setup opens
   first, pre-filled with last time's answers:
   - **What is it?**: the topic.
   - **Kind**: the six original choices plus Technical discussion, Project
     planning, Troubleshooting, Retrospective, One-to-one/coaching, Personal
     appointment, Workshop/conference, and Community/volunteer planning.
     **Use suggested structure for this kind** applies an opt-in layout and
     section list. Each has its own emphasis: a
     meeting leads with decisions and owners; a tutorial becomes numbered
     steps with exact commands.
   - **Focus on**: what you care about. It gets the most space.
   - **Leave out**: what you don't care about. It is never noted.
   - **Include**: timestamps, key quotes, action items, open questions,
     a definitions glossary, and **Who said what**. With the ElevenLabs
     engine, speakers are told apart (Speaker 1, Speaker 2, …, and names
     once the conversation reveals them). The text then arrives about
     every 45 s instead of live (D-060).
   - **Instructions**: anything else, in your own words.

   Untick *Show this before every session* to start straight away. The
   list-checks button on the panel reopens the setup; changes during a
   session apply from the next update. Then The current page is used if it is empty;
   otherwise a new page is made. A level meter shows the source is alive,
   and *Heard* shows the latest transcript lines with the words still being
   recognised.
4. The notes appear on the page after the first minute or so and keep
   improving. **Pause** stops recording without ending the session.
   **Stop** lets the last words land and runs one final revision over the
   whole transcript.

**Detail** sets how much the notes keep: Key points, **Concise** (the
default: short fragments, merged points, roughly one line per minute of
speech), Balanced, Detailed, or Comprehensive. Moving the slider during a
session rewrites the notes at the new level straight away. After a
session, ask the assistant (AI ▸ Assistant, **Edit the document** on) to
"condense this page to key points"; it edits the page itself, as one undo
step (D-058).

You can edit the notes while a session runs, and your edits win. A
revision never lands while the notes block is open in the editor. If you
changed the notes while the model was writing, its reply is dropped, and
your version goes into the next request. Each revision is one undo step.

The transcript is kept on the block (the `transcript` field, not painted).
**Copy transcript** puts it on the clipboard.

### What the notes look like

The model is asked for GitHub-flavoured Markdown with a fixed frame — a
`#` title, a `## Summary` of the whole session so far, italic annotations
in parentheses (*(speaker corrects the earlier figure)*, *(unclear at
03:12: …?)*), and a final `## Action items` checklist — and, inside it, the
organisation and typesetting the session setup chose (M57, below).

### Organised, typeset notes (M57, D-063)

**Organize** (session setup) decides the notes' shape. The layouts live in
`libs/office_ai/src/live_notes_layouts.*`:

| Layout | Shape |
|---|---|
| By subject (default) | one `##` per subject; a point made later is filed in the section it belongs to, not appended |
| Chronological | the M52 shape: sections in the order things came up |
| Learning objectives | "Explain why …" goals, each with explanation, example and a *Check yourself* callout |
| Study and retention | Cornell `| Cue | Notes |` tables, a takeaway per topic, memory aids, `## Review questions` |
| Meeting minutes | `## Decisions` first, then topics, risks, open questions |
| Argument map | thesis, claims with evidence and counterpoints, objections, assessment |
| Questions and answers | a `###` per question the session answers, grouped by theme |
| Story guide | characters, plot so far, world and lore, themes, memorable lines, predictions — for shows, films and books |
| My own sections | the user's list, one per line ("Recipes — anything cooked"), in order, plus `## Other` |

With any other layout the same text box lists sections to keep as well.
**A timeline underneath** adds `## Timeline` below the organised sections:
one terse `- [mm:ss]` line per minute or two — the chronological layer under
the organised one.

**Look** (Plain / Styled / Rich) is how much of the page's typesetting the
model uses. The vocabulary (`apps/qindanote/src/note_markdown_style.*`,
Obsidian-compatible where Obsidian has an equivalent):

- highlights by meaning: `==key==` yellow, `=={term}…==` blue,
  `=={example}…==` green, `=={warn}…==` red, `=={question}…==` violet,
  `=={date}…==` orange;
- `++underline++`, `!!stand-out!!` (bold, larger, red ink), `x^2^`,
  `H~2~O`, `[#tag]` badges; `[mm:ss]` timestamps are small and grey;
- callout boxes, `> [!kind] Title` with the content on `> ` lines: def,
  key, example, warn, tip, question, mnemonic, summary, quote, big (the
  central idea, large and centred), anything else as a note;
- tables are ruled, padded and get a tinted header row.

Plain is headings, bullets, bold, italics and yellow highlights only.
Changing the layout, look or sections mid-session rewrites the notes into
the new shape once (a restyle). **Open in QindaWrite** now hands over HTML
from the rendered page, so the colours and boxes survive; **Export
Markdown** keeps the markup.

**Revisions are edits.** Once there are notes, an update no longer asks for
the whole notes back. The model replies with an edit script
(`libs/office_ai/src/live_notes_edits.*`): `=== APPEND ## Heading`,
`=== REPLACE ## Heading`, `=== NEW UNDER ## Heading`, `=== NEW AFTER`,
`=== NEW`, `=== MOVE ## A TO UNDER ## B`, `=== DELETE`, `=== SUMMARY`,
`=== TITLE`, `=== TIMELINE`, `=== ACTIONS`, or `=== END` when nothing is
worth noting. Headings are matched loosely (level, case, `**`, a trailing
`[mm:ss]` ignored, then a unique prefix). Ambiguous targets cannot delete or
replace either matching section. `=== END` is a successful no-change reply,
including the final pass. A missing target keeps the content as a
new section rather than losing it. This is what lets old sections grow and
get corrected in place, and it is much cheaper for a slow writer: an
update's output is only what changed. When the session stops, one final
edit script files what is left and tidies (merges duplicates, re-files,
condenses, updates the summary) instead of a full rewrite. A model that
answers with whole notes anyway (they start with `#`) is still accepted.

Editing the block shows its Markdown source. Closing the editor paints it
again.

## During and after a session (M56, D-061)

- **This matters (F9):** flags the moment; it will be highlighted. The
  button in the live panel shows how many you flagged.
- **Catch me up:** a few bullets on the last five minutes.
- **Speakers:** with *Who said what* on, type a name next to "Speaker 2".
  The notes switch to the name.
- **Prep material** (setup): attach slides, a syllabus or an agenda.
  **Continue from the earlier pages in this section** gives the
  note-taker the previous sessions' notes, so it refers back instead of
  repeating. Names and jargon from all of it, and from the first three
  minutes of speech, are learned as key terms automatically.
- **Explain jargon** (setup): short AI explanations in teal, marked ⓘ.
- **Capture the screen** (setup, desktop audio, a model that reads
  images): changed slides and code go to the model with the next update.
- **Keep the recording** (setup, on by default): saved beside the
  notebook. Ctrl+click any `[12:40]` in the notes to play from there.
- **Silence:** after 20 s of quiet, audio stops being sent until there is
  sound again. The session stops after 15 minutes of silence
  (Settings ▸ Silence).
- **Session Review** panel (Live tab ▸ Review), for the open page:
  - the player, and the sections with their times;
  - **Check these**: every *(unclear at 1:42: …?)* spot, with Listen and
    a correction box;
  - **Follow-up drafts**: recap, project next steps, troubleshooting brief,
    questions, study material or tasks, shown for review before acceptance;
  - **Study draft**: reviewed flashcards and practice questions become a new page;
  - **Review tasks**: only **Send reviewed tasks** creates the QindaMail
    handoff. Relative dates use the recorded session date, and dated tasks
    also become calendar entries;
  - **Pages by topic**: one page per topic after an index, also automatic
    at Stop if chosen in the setup;
  - **Export**: Markdown, QindaWrite, Anki.
- The **usage** line in the live panel shows audio minutes sent, model
  calls and tokens, gate calls, screens and terms learned.
- Ask the **assistant** about the session ("what did she say about the
  exam?"). It reads the transcript and answers with times.

## The pipeline

```
PcmCapture ──► SpeechRecognizer ──► LiveNotesEngine ──► NoteHost ──► markdown block
 pw-record      ElevenLabs realtime   (office_ai)        (page, undo,
 mic / monitor  local Whisper         ▲       ▲           transcript)
 / both mixed   HTTP transcription    │       │
                                      gate    notes model
                              (Jev / Laya)    (model catalog)
```

| Stage | Where | Leaves the machine? |
| --- | --- | --- |
| Capture | `libs/office_speech/src/pcm_capture.*`: `pw-record` at 16 kHz mono s16le. Desktop audio is the default sink's monitor (`-P '{ stream.capture.sink = true }' --target <sink>`). Both streams are mixed by `PcmMixer`. | no |
| Speech | `ElevenLabsRealtime` (Scribe v2 realtime over a WebSocket, server-side VAD), `WhisperLocal` (faster-whisper through `tools/qindaoffice-whisper`), `HttpTranscription` (any OpenAI-compatible `/audio/transcriptions`, e.g. Groq) | ElevenLabs: yes · Whisper: no · HTTP: unless loopback |
| Gate (optional) | `SystemOneClient` → TypeSafe **Jev** (hosted) or **Laya** (self-hosted, Jev-compatible) | Jev: yes · Laya on this machine: no |
| Notes | `LiveNotesEngine` with any model-catalog row: Ornith on a llama-server, Claude, GPT, Kimi, GLM, DeepSeek, OpenRouter, Ollama, … | per row (a local server: no) |

The panel shows a **Leaves this machine** badge for each stage that does,
before Start. Nothing is recorded or sent until Start.

### Speech engines

- **ElevenLabs realtime** is the same protocol and model Gabbee (the
  desktop's dictation provider) uses. Live notes are continuous, so the
  server's voice-activity detection commits the text, not a push-to-talk
  key. A manual commit follows after 25 s without a pause. The session
  reconnects when ElevenLabs ends it (`session_time_limit_exceeded`) or the
  network drops, replaying up to 30 s of buffered audio.
- **Local Whisper** uses faster-whisper on the CPU with the model Portage
  installs (`app-accessibility/faster-whisper-models`, default
  `distil-small.en`), the same engine as Gabbee's local fallback. Audio is
  cut into utterances by an energy segmenter (`SpeechSegmenter`: 700 ms of
  silence or at most 20 s per clip). On the laptop it transcribed four
  minutes of speech in about 43 s. The panel says so if it falls behind.
- **Transcription API** posts each utterance as a WAV file. It works with
  Groq (`whisper-large-v3-turbo`), OpenAI, or a self-hosted
  OpenAI-compatible Whisper server.

**Key terms** (comma-separated names and jargon) bias every engine.
ElevenLabs receives them as `keyterms`. Whisper and the HTTP engine get
them as a `Glossary:` prompt. The notes model gets them as the correct
spellings, so it can fix mishearings: local Whisper hears "Jev" as "Jeff".

### The notes engine (`libs/office_ai/src/live_notes_engine.*`)

- **Cadence.** An update runs when enough new substantive words have
  arrived (45) and 20 s have passed, or when anything new has waited 75 s.
  Only one runs at a time, so a slow model makes fewer, bigger updates.
  Every sixth update rewrites from the whole transcript while that still
  fits; the others are edit scripts (M57, above), and the final pass is a
  tidying edit script.
- **FULL and TAIL** (with `Options::editMode` off, the M52 path). Short
  notes are rewritten whole. Once they pass about
  9000 characters, the model sees the earlier sections as headings only. It
  returns `## Summary`, the revised last sections and any new ones, and
  `## Action items`. `mergeTail()` splices these back, so a two-hour
  lecture does not cost two hours of output per update.
- **Guards.**
  - `<think>` blocks, code fences and chatty preambles are stripped.
  - A reply that drops more than half the notes is refused.
  - A reply with no sections in TAIL mode is refused.
  - User edits win (see above).
- **The gate.** Each stretch of about 25 words goes to a System One model
  as three typed questions:
  - `substance` (noul): filler or not?
  - `topic` (choice): continues, new topic, or wrap-up?
  - `importance` (score, 0–3).

  Filler does not count towards an update, so ads, music and small talk
  cost no big-model calls. A new topic triggers an update promptly, so the
  previous topic is written up. Important lines reach the model marked ★,
  and topic starts ⤷. A failing gate is switched off for a minute and every
  word counts (fail open). Jev answers in about 0.25 s for a fraction of a
  cent, and Laya is Apache-2.0 at about 420M parameters, so the gate costs
  far less than one extra big-model call.

### Measured on the owner's setup (2026-09-27)

Four minutes of a two-host podcast were played through the pipeline:
local Whisper `distil-small.en`, then Ornith 1.5 35B-A3B on qinda's
llama-server with `{"chat_template_kwargs": {"enable_thinking": false}}`.

- Transcription finished in 43 s.
- Ornith's first update landed 23 s after it was sent.
- The notes were revised three times, including the final full pass.
- The output had the summary, timestamped topic sections, bold terms, two
  highlights, an *(unclear: …)* annotation, a numbered walkthrough and
  action items.

The run is the manual slot `NoteLiveTest::realPipelineEndToEnd`; see
[verification](verification.md#m52-gates-live-notes-d-057).

## Models and keys: borrowed from OpenCode

The model catalog (`ModelCatalog`, `ai-providers.json` per app) gained
what OpenCode does well:

- **Presets from models.dev.** OpenCode's provider catalog is distilled by
  `tools/gen_provider_presets.py` into `libs/office_ai/data/provider_presets.json`,
  compiled in as a resource. It covers 17 hosted providers (Anthropic,
  OpenAI, Moonshot/Kimi, Kimi Code, Z.ai/GLM, DeepSeek, OpenRouter, Groq,
  Cerebras, Together, Fireworks, xAI, Mistral, Google via its
  OpenAI-compatible endpoint, MiniMax, Ollama Cloud) plus llama-server,
  the laptop's offline notes model (D-062), Ollama and LM Studio. Each
  preset maps OpenCode's `npm` wire package onto
  the two protocols office_ai speaks: `openai` (chat/completions) and
  `anthropic` (/v1/messages). **AI ▸ AI Providers…** adds a row from a
  preset, edits it, stores its key, tests it (lists the endpoint's models)
  or removes it.
- **Import from OpenCode** reads `opencode.json(c)` from
  `~/.config/opencode`, `$OPENCODE_CONFIG`, and any
  `~/.config/*/opencode.json`. It adds each custom provider with a
  `baseURL`, such as the llama-server on qinda in
  `~/.config/qinda-llm/opencode.json`, and every provider connected with
  OpenCode's `/connect` that has a preset.
- **Key resolution** (`ApiKeyResolver`) tries these sources in order:
  1. the suite's keyring entry;
  2. the row's reference, `{env:NAME}` or `{file:PATH}` (OpenCode's
     syntax; a literal key there is ignored);
  3. the provider's conventional environment variables;
  4. OpenCode's `auth.json`;
  5. a Secret Service item by attributes.

  The last is how the ElevenLabs key **Gabbee** stores is reused, so
  dictation and live notes share one key. Keys typed into the dialogs go
  to the keyring only.
- **Wire options** per row: `maxTokensField` (OpenAI's reasoning models
  want `max_completion_tokens`), `sendTemperature`, extra `headers`, and an
  extra request `body`. For example, the body can turn off a local
  reasoning model's thinking where speed matters.

### Ornith on qinda

The llama-server on qinda listens on Tailscale only at
`http://qinda.tailf118c.ts.net:8100/v1`, model `ornith`. Its key is in
`~/.config/qinda-llm/api-key` on the laptop. **Import from OpenCode**
picks it up, with the key as `{file:…/api-key}`. For live notes, add
`{"chat_template_kwargs": {"enable_thinking": false}}` as the row's extra
fields.

The OpenCode config names the key as `{env:QINDA_LLM_API_KEY}`, which is
set only in `~/.zshrc`, so apps started from the desktop do not see it and
the server answers 401. Point the row at the file instead:
`{file:~/.config/qinda-llm/api-key}` (or just the path).

### Laya (self-hosted gate)

Laya is packaged in the `qindaqt` overlay as `sci-ml/laya` (the library
and `laya-serve`, USE=serve) and `sci-ml/laya-models` (the checkpoints,
pinned, under `/usr/share/laya`; `english` by default, `multilingual` and
`typed-decisions` optional). Every dependency, including a CPU-only
PyTorch, comes from `::gentoo`.

A Gentoo patch adds `LAYA_MODEL_DIR` to `laya-serve`, so it serves the
installed checkpoints and never downloads anything. The unit sets
`HF_HUB_OFFLINE=1`.

```sh
sudo emerge sci-ml/laya            # pulls sci-ml/laya-models[english]
sudo systemctl enable --now laya-serve.service
```

**Measured 2026-09-27:** with the gate's own questions, the English
checkpoint judged poorly: small talk scored above lecture content, and
every stretch came back as a "new topic". On a busy CPU each request took
15–22 s. Re-tested on the idle laptop the same evening, the judgements were
the same and each request still took 14–20 s with 1.5 GB resident. Leave
the gate off; the engine's own cadence (minimum words and interval) already
decides when to revise.

It listens on `127.0.0.1:8000` (`/etc/laya/serve.env`, which also takes
`LAYA_API_KEY` if you expose it). In QindaNote, open **Live Notes ▸
Settings ▸ Gate ▸ Laya (self-hosted)**; the defaults are
`http://127.0.0.1:8000` and model `english`. TypeSafe **Jev** is the hosted
alternative (a key from typesafe.ai, `$JEV_API_KEY`).

### Offline notes on the laptop (D-062)

Local Whisper plus a small local model makes live notes work with no
network at all. The model is packaged in the `qindaqt` overlay as
`sci-ml/qinda-notes-llm`: Qwen3.5-4B (unsloth UD-Q4_K_XL, 2.9 GB, pinned)
under `/usr/share/gguf`, served by `sci-misc/llama-cpp[vulkan]` on the
integrated GPU. It is **on demand**: `qinda-notes-llm.socket` listens on
`127.0.0.1:8080`, the first request starts llama-server and waits until
the model is loaded (about 4 s from the page cache), and it unloads after
15 idle minutes, so its ~4 GB is held only during a session. Thinking is
turned off server-side (`LLAMA_ARG_CHAT_TEMPLATE_KWARGS` in
`/etc/qinda-notes-llm/serve.env`), so no extra fields are needed.

```sh
sudo emerge sci-ml/qinda-notes-llm
sudo systemctl enable --now qinda-notes-llm.socket
```

In **AI ▸ AI Providers…**, add the preset **Local notes model (this
machine, offline)**; it is model `qwen3.5-4b` at that address.

**Measured 2026-09-27** (qinda-top: Ryzen 7 5825U, Radeon Barcelo,
13 GiB) on the same four-minute podcast as the Ornith run, Whisper
transcribing at the same time:

| | Ornith 1.5 on qinda | Qwen3.5-4B on the laptop |
|---|---|---|
| reading (prompt) | 120–145 tok/s | 73–115 tok/s |
| writing | 18–23 tok/s | 7–10 tok/s |
| first notes on the page | 23 s after sending | 52 s |
| a full rewrite | ~45 s | 65–78 s |
| whole run, 3 revisions | 153 s | 252 s |

Writing speed is the limit (memory bandwidth on a shared-memory iGPU), so
the local model is about half as fast as Ornith per token. It is never
queued behind another job, though. The notes kept the structure (summary,
timestamped sections, bold terms, a quote, a numbered walkthrough, a
highlighted takeaway) and misheard one proper noun. Free memory never fell
below 3.4 GB with the desktop, QindaNote and Whisper running.

## Settings

`$XDG_CONFIG_HOME/<app>/live-notes.json` holds these settings, per app and
never shared (AGENTS.md rule 7):

- source, engine, language, Whisper model;
- HTTP endpoint and model;
- key terms;
- the notes model id;
- gate kind, server and model;
- the last topic.

It holds no keys. The keyring ids are `elevenlabs-api-key`,
`transcription-api-key` and `systemone-api-key` (service `qindaoffice`).
