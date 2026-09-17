#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the release tarball the Gentoo ebuild consumes.
#
# The repository is not hosted yet, so gui-apps/qindaoffice takes its source
# from a distfile rather than a URL (see the ebuild's RESTRICT). This script
# is what produces that distfile, so anyone with a checkout can reproduce it
# byte-for-byte from a given commit.
#
# AGENT-CONTRACT: the tarball must carry third_party/QXlsx VENDORED. It is a
# git submodule, and Portage builds with FEATURES=network-sandbox — an
# ebuild that fetched it at build time would fail on any machine with the
# default sandbox on.
#
# Usage: tools/make-dist.sh <version> [output-dir]
#   tools/make-dist.sh 0.1.0_p20260917 /var/cache/distfiles

set -eu

version="${1:?usage: make-dist.sh <version> [output-dir]}"
outdir="${2:-$PWD/dist}"
name="qindaoffice-${version}"

repo=$(git rev-parse --show-toplevel)
cd "$repo"

if [ -n "$(git status --porcelain)" ]; then
    echo "make-dist: the tree has uncommitted changes; the tarball would not" >&2
    echo "           match any commit. Commit or stash first." >&2
    exit 1
fi

if [ ! -f third_party/QXlsx/QXlsx/CMakeLists.txt ]; then
    echo "make-dist: third_party/QXlsx is empty — run:" >&2
    echo "           git submodule update --init --recursive" >&2
    exit 1
fi

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
mkdir -p "$staging/$name"

# The tracked tree of HEAD, then the submodule's own tracked tree on top.
git archive HEAD | tar -x -C "$staging/$name"
git -C third_party/QXlsx archive HEAD | tar -x -C "$staging/$name/third_party/QXlsx"

# Record what this tarball is, so an installed tree can be traced back.
git rev-parse HEAD > "$staging/$name/.dist-commit"

# AGENT-GUARD: every one of these flags is load-bearing for the byte-for-byte
# promise above, and a plain `tar -cJf` breaks it. The Manifest pins ONE hash,
# so a second machine cutting the same commit must produce the same bytes or
# Portage rejects its archive — which is what pkg_nofetch tells an operator to
# go and do. Verified: this laptop and the desktop produce identical archives;
# a plain tar differed in both size and hash.
#
#   --sort=name + LC_ALL=C  readdir order is filesystem-dependent
#   --mtime=@<commit>       .dist-commit and the staging dirs are created NOW
#   --owner/--group/--numeric-owner   uid, gid and user names get embedded
#   --pax-option            drops atime/ctime, and keeps the PaxHeaders names
#                           from carrying the staging path
#   xz -T1                  multi-threaded xz splits the stream into blocks,
#                           so the output depends on the CPU count
commit_epoch=$(git log -1 --pretty=%ct HEAD)

mkdir -p "$outdir"
LC_ALL=C tar -C "$staging" \
    --sort=name \
    --mtime="@$commit_epoch" \
    --owner=0 --group=0 --numeric-owner \
    --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime \
    -cf - "$name" \
    | xz -T1 -9 > "$outdir/$name.tar.xz"
echo "$outdir/$name.tar.xz"
