#!/usr/bin/env bash
#
# sanitize.sh -- the runtime half of "zero warnings is the bar".
#
# -Wall -Wextra -Wpedantic -Wshadow -Wconversion is a compile-time
# argument about types. This is a runtime argument about memory and
# arithmetic, and it exists because this project is specifically exposed
# to a class of bug that no amount of warning flags can see:
#
#   * snapshot.c decodes bytes off disk and off a socket, net.c parses
#     frames from a peer it does not trust, and account.c parses a file
#     an admin edits by hand. "It did not crash" and "it read nothing
#     out of bounds" are different claims and only one is checkable.
#   * the world is HASHED. A hash taken over uninitialised or overrun
#     memory can be perfectly stable within one run and differ across
#     machines — the failure CI is least able to see, because each
#     platform only ever compares against itself.
#
# Two passes, because no single sanitizer covers both:
#
#   address,undefined  everything, including the SDL client and a real
#                      TCP server. Catches overruns, use-after-free,
#                      leaks, misaligned access, signed overflow.
#   memory             the SDL-FREE half only. MemorySanitizer needs
#                      every linked object instrumented and SDL is not,
#                      so it can only run where SDL does not reach —
#                      which happens to be exactly where hashed world
#                      state lives.
#
# The first thing this found, on its first run: game_load and the join
# path both formed a `const Command *` over a save file and a network
# frame at an offset just past a snapshot of arbitrary length. Undefined
# behaviour that happens to work on x86 — and both are the paths a
# hostile peer reaches.
#
# Usage: ci/sanitize.sh [address|memory|all]   (default all)
#
# Needs clang and its sanitizer runtimes. Fedora ships them in
# compiler-rt; gcc's live in the separate libasan/libubsan packages, so
# clang is the fewer moving parts.

set -uo pipefail

MODE="${1:-all}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

FAILURES=0
fail() { printf '\n  FAIL: %s\n' "$1"; FAILURES=$((FAILURES + 1)); }
pass() { printf '\n  ok:   %s\n' "$1"; }

CC_BIN="${CC:-clang}"
if ! command -v "$CC_BIN" >/dev/null 2>&1; then
    echo "sanitize: $CC_BIN not found" >&2
    exit 1
fi

run_address() {
    local dir="$root/build-asan"

    echo "== address,undefined =="
    cmake -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_C_COMPILER="$CC_BIN" \
          -DSALTMARCH_SANITIZE=address,undefined >/dev/null || {
        fail "configure"; return; }
    cmake --build "$dir" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null || {
        fail "build"; return; }

    # The whole headless suite, then the two replays, then the client
    # and the server for real. A sanitizer only ever reports what the
    # program actually executed, so coverage here IS the tool.
    if CC="$CC_BIN" SALTMARCH_BUILD_DIR="$dir" ./tests/run.sh >/dev/null 2>&1
    then pass "the headless suite is clean under address,undefined"
    else fail "the headless suite reported something — rerun without >/dev/null"
    fi

    if "$dir/saltmarch_replay" --record "$dir/san.smlog" --seed 12345 \
           >/dev/null 2>&1 &&
       "$dir/saltmarch_replay" --replay "$dir/san.smlog" >/dev/null 2>&1
    then pass "the determinism fixture records and replays clean"
    else fail "the determinism fixture tripped a sanitizer"
    fi

    if "$dir/saltmarch_replay" --record-ui "$dir/sanui.smlog" --seed 777 \
           >/dev/null 2>&1 &&
       "$dir/saltmarch_replay" --replay "$dir/sanui.smlog" --verify-ui \
           >/dev/null 2>&1
    then pass "the recorded UI session re-drives clean"
    else fail "the UI replay tripped a sanitizer"
    fi

    if ./ci/smoke-test.sh "$dir/saltmarch" 5 >/dev/null 2>&1
    then pass "the client starts, runs and exits clean"
    else fail "the client tripped a sanitizer (or failed its smoke test)"
    fi

    if ./ci/host-smoke.sh "$dir" >/dev/null 2>&1
    then pass "the server survives its own adversarial probes clean"
    else fail "the host smoke test tripped a sanitizer (or failed)"
    fi
}

run_memory() {
    local dir="$root/build-msan"

    echo
    echo "== memory (SDL-free only) =="
    cmake -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_C_COMPILER="$CC_BIN" \
          -DSALTMARCH_SANITIZE=memory >/dev/null || { fail "configure"; return; }
    # The client links SDL and cannot be instrumented through, so only
    # the SDL-free targets are built here. Asking for the game binary
    # would fail honestly rather than usefully.
    cmake --build "$dir" -j"$(nproc 2>/dev/null || echo 4)" \
          --target saltmarch_sim saltmarch_net saltmarch_ui saltmarch_replay \
          >/dev/null || { fail "build"; return; }

    if CC="$CC_BIN" SALTMARCH_BUILD_DIR="$dir" SALTMARCH_SDL_FREE_ONLY=1 \
       ./tests/run.sh >/dev/null 2>&1
    then pass "the SDL-free tests read no uninitialised memory"
    else fail "MemorySanitizer found an uninitialised read"
    fi

    # The one that matters most: a hash computed over uninitialised
    # bytes is the desync nobody can see from inside one machine.
    if "$dir/saltmarch_replay" --record "$dir/msan.smlog" --seed 12345 \
           >/dev/null 2>&1 &&
       "$dir/saltmarch_replay" --replay "$dir/msan.smlog" >/dev/null 2>&1
    then pass "sim_hash reads nothing it was never given"
    else fail "MemorySanitizer found an uninitialised read in the sim"
    fi
}

case "$MODE" in
    address) run_address ;;
    memory)  run_memory ;;
    all)     run_address; run_memory ;;
    *) echo "usage: $0 [address|memory|all]" >&2; exit 2 ;;
esac

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "SANITIZERS CLEAN"
    exit 0
fi
echo "SANITIZERS FOUND $FAILURES PROBLEM(S)"
exit 1
