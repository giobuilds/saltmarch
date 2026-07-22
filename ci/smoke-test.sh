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

# The game runs until quit, so a clean exit is not expected: timeout
# kills it and returns 124. That 124 IS the success signal -- it means
# the process was still alive. Any other code means it died on its own,
# which for a game loop means it crashed or bailed during init.
SDL_VIDEODRIVER=dummy timeout "${SECONDS_TO_RUN}s" "$BIN" >"$LOG" 2>&1
RC=$?

echo "--- captured output ---"
sed 's/^/  | /' "$LOG"
echo "-----------------------"

if [ "$RC" -eq 124 ]; then
    pass "survived ${SECONDS_TO_RUN}s without exiting (rc=124 = still running)"
else
    fail "exited early with rc=$RC — crashed or aborted during startup"
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
