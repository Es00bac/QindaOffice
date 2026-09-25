#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Local, live gallery of QindaOffice development screenshots.

Adapted from QindaTK's tools/scripts/validation_gallery.py. Scans the
QindaOffice checkout and its QindaOffice-* worktrees, the QindaTK lane
worktrees of the current plan (docs/plans/ribbon-and-mail.md), and
$QQO_VALIDATION_DIR (default ~/.local/share/qindaoffice-validation-gallery/inbox),
where scripted grabs can be dropped. Every capture is archived by content
hash, so a later rebuild that overwrites a PNG keeps the earlier one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
from pathlib import Path
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


HERE = Path(__file__).resolve().parent
PAGE = HERE / "validation-gallery" / "index.html"
WORKSPACE = HERE.parents[1]
DATA = Path.home() / ".local/share/qindaoffice-validation-gallery"
INBOX = Path(os.environ.get("QQO_VALIDATION_DIR", str(DATA / "inbox")))
# QindaTK worktrees whose captures belong to this project's milestones.
TOOLKIT_LANES = ("QindaTK-ribbon-v2", "QindaTK-mail-pop3", "QindaTK-mail-protocols",
                 "QindaTK-calendar-grid")
ARCHIVE = DATA / "archive"
MANIFEST = DATA / "manifest.json"
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif"}
SKIP_DIRS = {
    ".git", ".rcc", ".qt", "CMakeFiles", "_deps", "node_modules",
    "qmlcache", "autogen", "qt68-sdk", "qt68-stage", "qt611-stage",
    "validation-gallery", "__pycache__", "third_party",
}
CAPTURE_NAMES = ("screenshot", "capture", "preview", "grab", "compact", "comfortable")
IMAGE_ID = re.compile(r"[0-9a-f]{64}")


def roots() -> list[Path]:
    """QindaOffice and its worktrees (including ones created after launch),
    this plan's QindaTK lanes, and the inbox."""
    found = [path for path in WORKSPACE.glob("QindaOffice*")
             if (path.name == "QindaOffice" or path.name.startswith("QindaOffice-"))
             and path.is_dir() and (path / ".git").exists()]
    found += [WORKSPACE / name for name in TOOLKIT_LANES if (WORKSPACE / name / ".git").exists()]
    INBOX.mkdir(parents=True, exist_ok=True)
    found.append(INBOX)
    return sorted(found, key=lambda path: path.name)


def fresh_filter(root: Path):
    """Git-tracked captures that are unchanged are history, not this work:
    a checkout (or worktree) carries hundreds of committed evidence images.
    Returns a predicate over paths relative to `root`."""
    if root == INBOX or not (root / ".git").exists():
        return lambda relative: True

    def listing(*flags: str) -> set[str]:
        try:
            out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", *flags],
                                 capture_output=True, check=True, timeout=20).stdout
        except (OSError, subprocess.SubprocessError):
            return set()
        return {entry.decode() for entry in out.split(b"\0") if entry}
    tracked, modified = listing(), listing("-m")
    return lambda relative: relative not in tracked or relative in modified


def is_capture(root: Path, path: Path) -> bool:
    if path.suffix.lower() not in IMAGE_SUFFIXES:
        return False
    if root == INBOX:
        return True
    relative = path.relative_to(root)
    parts = {part.lower() for part in relative.parts[:-1]}
    return bool({"screenshots", "evidence", "build"} & parts) or any(
        word in path.stem.lower() for word in CAPTURE_NAMES
    )


def complete_image(data: bytes, suffix: str) -> bool:
    if suffix == ".png":
        return data.startswith(b"\x89PNG\r\n\x1a\n") and data.endswith(
            b"\x00\x00\x00\x00IEND\xaeB`\x82"
        )
    if suffix in {".jpg", ".jpeg"}:
        return data.startswith(b"\xff\xd8") and data.endswith(b"\xff\xd9")
    if suffix == ".webp":
        return data.startswith(b"RIFF") and data[8:12] == b"WEBP" and len(data) >= 12
    if suffix == ".gif":
        return data.startswith((b"GIF87a", b"GIF89a")) and data.endswith(b"\x3b")
    return False


class Gallery:
    def __init__(self) -> None:
        DATA.mkdir(parents=True, exist_ok=True)
        ARCHIVE.mkdir(parents=True, exist_ok=True)
        try:
            saved = json.loads(MANIFEST.read_text(encoding="utf-8"))
            self.items = saved.get("items", {})
            self.version = int(saved.get("version", 0))
        except (OSError, ValueError, TypeError):
            self.items = {}
            self.version = 0
        self.stat_cache: dict[str, tuple[int, int, str]] = {}
        self.lock = threading.Lock()
        self.last_scan = 0.0

    def scan(self) -> None:
        with self.lock:
            if time.monotonic() - self.last_scan < 3:
                return
            changed = False
            for root in roots():
                fresh = fresh_filter(root)
                for directory, child_dirs, files in os.walk(root):
                    child_dirs[:] = [name for name in child_dirs if name not in SKIP_DIRS]
                    base = Path(directory)
                    for name in files:
                        path = base / name
                        if not is_capture(root, path) or not fresh(str(path.relative_to(root))):
                            continue
                        try:
                            stat = path.stat()
                            if not path.resolve().is_relative_to(root.resolve()):
                                continue
                            if stat.st_size == 0 or time.time_ns() - stat.st_mtime_ns < 500_000_000:
                                continue
                            key = str(path)
                            cached = self.stat_cache.get(key)
                            if cached and cached[:2] == (stat.st_size, stat.st_mtime_ns):
                                continue
                            data = path.read_bytes()
                            suffix = path.suffix.lower()
                            if not complete_image(data, suffix):
                                continue
                        except OSError:
                            continue
                        digest = hashlib.sha256(data).hexdigest()
                        self.stat_cache[key] = (stat.st_size, stat.st_mtime_ns, digest)
                        if digest in self.items:
                            if stat.st_mtime_ns < self.items[digest]["capturedNs"]:
                                self.items[digest]["capturedNs"] = stat.st_mtime_ns
                                changed = True
                            continue
                        archived = ARCHIVE / f"{digest}{suffix}"
                        temporary = ARCHIVE / f".{digest}.tmp"
                        temporary.write_bytes(data)
                        temporary.replace(archived)
                        self.items[digest] = {
                            "id": digest,
                            "name": path.name,
                            "source": f"{root.name}/{path.relative_to(root)}",
                            "capturedNs": stat.st_mtime_ns,
                            "firstSeenNs": time.time_ns(),
                            "bytes": len(data),
                            "file": archived.name,
                        }
                        changed = True
            self.last_scan = time.monotonic()
            if changed:
                self.version += 1
                temporary = MANIFEST.with_suffix(".tmp")
                temporary.write_text(json.dumps({"version": self.version, "items": self.items}),
                                     encoding="utf-8")
                temporary.replace(MANIFEST)

    def listing(self) -> dict:
        self.scan()
        with self.lock:
            ordered = sorted(self.items.values(),
                             key=lambda item: (item["capturedNs"], item["firstSeenNs"], item["id"]))
            return {"version": self.version, "count": len(ordered), "items": ordered,
                    "roots": [root.name for root in roots()]}

    def image(self, digest: str) -> tuple[bytes, str] | None:
        with self.lock:
            record = self.items.get(digest)
            if not record:
                return None
            path = ARCHIVE / record["file"]
        try:
            return path.read_bytes(), mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        except OSError:
            return None


def handler_for(gallery: Gallery) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def send_bytes(self, body: bytes, content_type: str, cache: str = "no-store") -> None:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", cache)
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            route = urlsplit(self.path).path
            if route in {"/", "/index.html"}:
                self.send_bytes(PAGE.read_bytes(), "text/html; charset=utf-8")
            elif route == "/api/screenshots":
                body = json.dumps(gallery.listing(), separators=(",", ":")).encode()
                self.send_bytes(body, "application/json; charset=utf-8")
            elif route.startswith("/image/") and IMAGE_ID.fullmatch(route[7:]):
                result = gallery.image(route[7:])
                if result:
                    self.send_bytes(*result, cache="public, max-age=31536000, immutable")
                else:
                    self.send_error(404)
            else:
                self.send_error(404)

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8788)
    args = parser.parse_args()
    gallery = Gallery()
    with ThreadingHTTPServer((args.host, args.port), handler_for(gallery)) as server:
        print(f"QindaOffice validation gallery: http://{args.host}:{server.server_port}/", flush=True)
        server.serve_forever(poll_interval=0.2)


if __name__ == "__main__":
    main()
