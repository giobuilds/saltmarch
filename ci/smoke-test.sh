#!/usr/bin/env bash
#
# smoke-test.sh -- launch the built game headlessly and assert it is
# actually alive and usable, not merely that it compiled.
#
# This exists because a compile check is not enough for this project.
# The font path used to be Fedora-specific and a missing font was
# non-fatal, so the game would build, start, run, and render no text at
# all -- a state any build-only CI would have called success. Hence the
# "Fonts loaded" assertion below: it is the guard against that class of
# silent, green failure returning.
#
# Runs under SDL_VIDEODRIVER=dummy so it needs no display, which is what
# makes it usable on every CI runner.
#
# Usage: ci/smoke-test.sh <path-to-binary> [seconds]

set -uo pipefail

BIN="${1:?usage: smoke-test.sh <binary> [seconds]}"
SECONDS_TO_RUN="${2:-5}"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

fail() { printf '  FAIL: %s\n' "$1"; FAILURES=$((FAILURES + 1)); }
pass() { printf '  ok:   %s\n' "$1"; }
FAILURES=0

echo "== smoke test: $BIN (${SECONDS_TO_RUN}s, headless) =="

if [ ! -x "$BIN" ]; then
    echo "  FAIL: binary not found or not executable: $BIN"
    exit 1
fi

# The game runs until quit, so a clean exit is never expected -- the
# question is "was it still alive after N seconds?", and we have to kill
# it ourselves to find out.
#
# This deliberately does NOT use `timeout`. That is GNU coreutils, and
# macOS ships a BSD userland without it (Homebrew provides it only as
# `gtimeout`); Git Bash on Windows is no safer a bet. Since this script
# runs on all three CI platforms, the wait is done with a poll loop over
# `kill -0`, which needs nothing beyond POSIX sh builtins.
#
# Polling rather than one flat sleep also means a crash is noticed at the
# second it happens, so ALIVE_FOR below reports where it died.
SDL_VIDEODRIVER=dummy "$BIN" >"$LOG" 2>&1 &
GAME_PID=$!

ALIVE_FOR=0
SURVIVED=1
while [ "$ALIVE_FOR" -lt "$SECONDS_TO_RUN" ]; do
    sleep 1
    if kill -0 "$GAME_PID" 2>/dev/null; then
        ALIVE_FOR=$((ALIVE_FOR + 1))
    else
        SURVIVED=0
        break
    fi
done

if [ "$SURVIVED" -eq 1 ]; then
    # Still running, as it should be. Ask nicely, then insist -- a hung
    # process must not stall the CI job.
    kill -TERM "$GAME_PID" 2>/dev/null
    sleep 1
    kill -KILL "$GAME_PID" 2>/dev/null
fi
wait "$GAME_PID" 2>/dev/null
RC=$?

echo "--- captured output (rc=$RC) ---"
sed 's/^/  | /' "$LOG"
echo "--------------------------------"

if [ "$SURVIVED" -eq 1 ]; then
    pass "survived ${SECONDS_TO_RUN}s without exiting"
else
    fail "died after ${ALIVE_FOR}s (rc=$RC) — crashed or aborted on its own"
fi

# A binary that runs and says NOTHING is a different failure from one
# that runs and says the wrong thing, and the checks below cannot tell
# them apart: they would report six missing lines and no reason.
#
# It is also the case `kill -0` is worst at. A child that has exited but
# not been reaped is a zombie, and a zombie answers `kill -0`
# successfully — so "survived" above can be true of a process that died
# in the first millisecond. An empty log is the tell.
if [ ! -s "$LOG" ]; then
    fail "produced NO output at all (rc=$RC) — it did not reach world"
    fail "generation, so this is a startup failure rather than a"
    fail "regression in anything the checks below name"
fi

# Guard against the silent-failure class described above.
if grep -q "Fonts loaded:" "$LOG"; then
    pass "font loaded (text will render)"
else
    fail "no 'Fonts loaded:' line — the game would draw no text at all"
fi

if grep -q "Ready\." "$LOG"; then
    pass "reached the ready state"
else
    fail "never logged its ready line — init did not complete"
fi

# World generation is the first real subsystem to run; if the
# archipelago is missing, something broke well before the game loop.
for ISLAND in Saltford Brinehold Tidefast Marrowbay; do
    if grep -q "Island '$ISLAND' generated" "$LOG"; then
        pass "generated $ISLAND"
    else
        fail "$ISLAND was not generated"
    fi
done

if grep -q "ERROR" "$LOG"; then
    fail "logged an ERROR:"
    grep "ERROR" "$LOG" | sed 's/^/        /'
else
    pass "no errors logged"
fi

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "SMOKE TEST PASSED"
    exit 0
fi
echo "SMOKE TEST FAILED ($FAILURES check(s))"
exit 1
