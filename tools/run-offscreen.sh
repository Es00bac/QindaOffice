#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the test suite headless (offscreen QPA). The ctest ENVIRONMENT
# property already pins QT_QPA_PLATFORM per test; this wrapper exists for
# direct ./tst_foo invocations from a build tree.
set -e
cd "$(dirname "$0")/.."
export QT_QPA_PLATFORM=offscreen
exec ctest --preset dev "$@"
