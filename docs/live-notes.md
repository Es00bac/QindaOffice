<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Live notes (QindaNote, M52, D-057)

QindaNote can take notes by itself while something is being said: a
lecture in the room, a meeting or call, a YouTube video or anything else
playing on the desktop. It listens and transcribes. A language model keeps
one set of well-formatted notes on the page, revising them as more
information arrives. The notes are one **markdown block** on a notebook
page, so they print, export to PDF, take ink and can be edited like
anything else on the page.

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
   - **Kind**: General, Lecture or class, Meeting, Interview, Tutorial or
     how-to, or Talk, podcast or video. Each has its own emphasis: a
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

The model is asked for GitHub-flavoured Markdown with a fixed shape:

- a `#` title, then a `## Summary` of the whole session so far;
- one `##` section per topic, stamped with its start time (`[12:40]`);
- **bold** key terms and `**Term** — definition` lines;
- `==highlights==` for the few takeaways that matter most, painted as a
  yellow marker stroke;
- `> "quotes"` for exact wording;
- italic annotations in parentheses — *(speaker corrects the earlier
  figure)*, *(unclear: …?)*;
- a final `## Action items` checklist (`- [ ]`) for tasks and deadlines.

Editing the block shows its Markdown source. Closing the editor paints it
again.

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
  Every sixth update, and the final one, rewrites from the whole
  transcript while that still fits.
- **FULL and TAIL.** Short notes are rewritten whole. Once they pass about
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
  Ollama and LM Studio. Each preset maps OpenCode's `npm` wire package onto
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
15–22 s. Leave the gate off unless a re-test on an idle machine, or Jev,
does better.

It listens on `127.0.0.1:8000` (`/etc/laya/serve.env`, which also takes
`LAYA_API_KEY` if you expose it). In QindaNote, open **Live Notes ▸
Settings ▸ Gate ▸ Laya (self-hosted)**; the defaults are
`http://127.0.0.1:8000` and model `english`. TypeSafe **Jev** is the hosted
alternative (a key from typesafe.ai, `$JEV_API_KEY`).

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
