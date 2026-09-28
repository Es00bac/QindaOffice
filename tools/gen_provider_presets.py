#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerates libs/office_ai/data/provider_presets.json from the models.dev
catalog (the one OpenCode reads: https://models.dev/api.json).

Usage: tools/gen_provider_presets.py <api.json> > libs/office_ai/data/provider_presets.json

AGENT-NOTE (D-057): the suite does not download the 5 MB catalog at run
time; this script distils it into a small, reviewed preset table that is
compiled in as a resource. Each preset maps OpenCode's `npm` wire package
onto the two protocols office_ai speaks: "openai" (chat/completions) and
"anthropic" (/v1/messages). Base URLs the catalog omits (the SDK default)
come from DEFAULT_API below.
"""

import json
import sys

PROVIDERS = [
    "anthropic", "openai", "moonshotai", "kimi-code-plan-global", "zai", "zai-coding-plan",
    "deepseek", "openrouter", "groq", "cerebras", "togetherai", "fireworks-ai", "xai",
    "mistral", "google", "minimax", "ollama-cloud",
]
DEFAULT_API = {
    "anthropic": "https://api.anthropic.com",
    "openai": "https://api.openai.com/v1",
    "groq": "https://api.groq.com/openai/v1",
    "cerebras": "https://api.cerebras.ai/v1",
    "togetherai": "https://api.together.xyz/v1",
    "xai": "https://api.x.ai/v1",
    "mistral": "https://api.mistral.ai/v1",
    # Gemini's OpenAI-compatible endpoint, so no third protocol is needed.
    "google": "https://generativelanguage.googleapis.com/v1beta/openai",
}
OPENROUTER_MAKERS = ["anthropic/", "openai/", "google/", "moonshotai/", "z-ai/", "deepseek/",
                     "x-ai/", "qwen/"]
MODELS_PER_PROVIDER = 8

LOCAL = [
    {"id": "llama-server", "name": "llama.cpp server (self-hosted)", "kind": "openai",
     "baseUrl": "http://127.0.0.1:8080/v1", "env": [], "local": True,
     "doc": "https://github.com/ggml-org/llama.cpp/tree/master/tools/server",
     "hint": "Any llama-server, also one on another machine over Tailscale "
             "(http://<host>:<port>/v1). Its --api-key goes in as a file reference: "
             "{file:~/.config/<name>/api-key}.",
     "models": []},
    # AGENT-CONTRACT: the laptop's offline notes model, QindaGentoo
    # sci-ml/qinda-notes-llm (socket 127.0.0.1:8080, alias NOTES_ALIAS).
    {"id": "qinda-notes-llm", "name": "Local notes model (this machine, offline)", "kind": "openai",
     "baseUrl": "http://127.0.0.1:8080/v1", "env": [], "local": True,
     "doc": "https://github.com/Es00bac/QindaGentoo/tree/master/sci-ml/qinda-notes-llm",
     "hint": "sci-ml/qinda-notes-llm: Qwen3.5-4B on llama.cpp, loaded on first use and "
             "unloaded when idle. Enable once: systemctl enable --now qinda-notes-llm.socket",
     "models": [{"id": "qwen3.5-4b", "name": "Qwen3.5-4B", "context": 32768, "output": 8192,
                 "reasoning": False}]},
    {"id": "ollama", "name": "Ollama (this machine)", "kind": "openai",
     "baseUrl": "http://127.0.0.1:11434/v1", "env": [], "local": True,
     "doc": "https://ollama.com", "models": []},
    {"id": "lmstudio", "name": "LM Studio (this machine)", "kind": "openai",
     "baseUrl": "http://127.0.0.1:1234/v1", "env": [], "local": True,
     "doc": "https://lmstudio.ai", "models": []},
]


def kind_of(provider):
    return "anthropic" if provider["npm"] == "@ai-sdk/anthropic" else "openai"


def base_of(pid, provider, kind):
    api = provider.get("api") or DEFAULT_API.get(pid, "")
    api = api.rstrip("/")
    if kind == "anthropic" and api.endswith("/v1"):
        api = api[:-3]  # office_ai's Anthropic provider appends /v1/messages
    return api


def pick_models(pid, provider):
    models = [m for m in provider["models"].values()
              if m.get("status") != "deprecated"
              and "text" in m.get("modalities", {}).get("output", ["text"])]
    models.sort(key=lambda m: m.get("release_date", ""), reverse=True)
    if pid == "openrouter":
        chosen = []
        for maker in OPENROUTER_MAKERS:
            chosen += [m for m in models if m["id"].startswith(maker)][:2]
        models = chosen
    else:
        models = models[:MODELS_PER_PROVIDER]
    return [{"id": m["id"], "name": m.get("name", m["id"]),
             "context": m.get("limit", {}).get("context", 0),
             "output": m.get("limit", {}).get("output", 0),
             "reasoning": bool(m.get("reasoning"))} for m in models]


def main():
    catalog = json.load(open(sys.argv[1]))
    presets = []
    for pid in PROVIDERS:
        provider = catalog.get(pid)
        if provider is None:
            continue
        kind = kind_of(provider)
        presets.append({"id": pid, "name": provider["name"], "kind": kind,
                        "baseUrl": base_of(pid, provider, kind), "env": provider["env"],
                        "doc": provider.get("doc", ""), "local": False,
                        "models": pick_models(pid, provider)})
    out = {"_generated": "tools/gen_provider_presets.py from models.dev api.json; do not "
                         "edit by hand (vendored data, exempt from the line budget)",
           "presets": presets + LOCAL}
    json.dump(out, sys.stdout, indent=1, ensure_ascii=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
