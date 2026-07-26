#!/usr/bin/env bash
#
# host-smoke.sh -- end-to-end check of the dedicated server
#                  (MMO_PLAN Phase 6).
#
# tests/test_server.c covers the protocol and the persistence rules over
# the in-memory transport, deterministically. This covers the two things
# that test cannot: real sockets, and the two binaries actually being
# able to talk to each other. It asserts, in one run:
#
#   1. the server starts, listens, and ticks on its own clock;
#   2. a real client connects over TCP and is given an identity and the
#      world;
#   3. the server keeps ticking after that client leaves;
#   4. the checkpoint it writes replays to the same hash in
#      saltmarch_replay -- i.e. the server's world is an ordinary
#      (seed, log) world, not a private format.
#
# Usage: ci/host-smoke.sh <build-dir> [port]

set -uo pipefail

BUILD="${1:?usage: host-smoke.sh <build-dir> [port]}"
PORT="${2:-7788}"
EXE=""
[ -x "$BUILD/saltmarch_host" ]     && EXE=""
[ -x "$BUILD/saltmarch_host.exe" ] && EXE=".exe"

HOST_BIN="$BUILD/saltmarch_host$EXE"
GAME_BIN="$BUILD/saltmarch$EXE"
REPLAY_BIN="$BUILD/saltmarch_replay$EXE"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

WORLD="$WORK/world.smlog"
SRVLOG="$WORK/server.log"
CLILOG="$WORK/client.log"

fail() { printf '  FAIL: %s\n' "$1"; FAILURES=$((FAILURES + 1)); }
pass() { printf '  ok:   %s\n' "$1"; }
FAILURES=0

echo "== host smoke test: $HOST_BIN (port $PORT) =="

for BIN in "$HOST_BIN" "$GAME_BIN" "$REPLAY_BIN"; do
    if [ ! -x "$BIN" ]; then
        echo "  FAIL: missing binary: $BIN"
        exit 1
    fi
done

# 200 ticks = 20 seconds of world time; the run ends on its own, so a
# hung server cannot stall CI and no `timeout` (GNU-only) is needed.
"$HOST_BIN" --world "$WORLD" --seed 4242 --port "$PORT" \
            --ticks 200 --checkpoint-seconds 0 >"$SRVLOG" 2>&1 &
HOST_PID=$!

# Give it a moment to bind before connecting.
sleep 2

if ! kill -0 "$HOST_PID" 2>/dev/null; then
    fail "server exited immediately"
    sed 's/^/  | /' "$SRVLOG"
    exit 1
fi
pass "server started and is listening"

# ---- adversarial probes over a real socket -------------------
# tests/test_net_hardening.c covers what the in-memory transport can
# reach. These two cannot be expressed there: both are about a
# connection MISbehaving, and the mem transport has neither a socket to
# hold open nor a way to say "connect, then don't". Skipped rather than
# failed where bash has no /dev/tcp.
#
# The protocol version is read out of the header so this cannot rot
# into silently testing a version mismatch instead of what it means to.
PROTO=$(sed -n 's/^#define NET_PROTO_VERSION *\([0-9]*\)u*.*/\1/p' \
        "$(dirname "$0")/../src/net.h")
RAW_OK=0

# A connection that connects and says nothing, held open for the rest
# of the run. It should be dropped at the handshake deadline instead of
# occupying one of NET_MAX_PEERS slots until the world ends.
if [ -n "$PROTO" ] && exec 9<>"/dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
    RAW_OK=1
else
    echo "  skip: no /dev/tcp — raw socket probes not run"
fi

# A real client, headless. --as 1 claims the founding island, which is
# the identity a fresh world leaves unheld on a dedicated server.
SDL_VIDEODRIVER=dummy "$GAME_BIN" --join "127.0.0.1:$PORT" --as 1 \
    >"$CLILOG" 2>&1 &
CLI_PID=$!
sleep 6
kill -TERM "$CLI_PID" 2>/dev/null
wait "$CLI_PID" 2>/dev/null

# One connection, two introductions. Each HELLO used to be answered
# with a fresh identity, another copy of the whole command log, and
# another starting island — so a single socket could take the entire
# archipelago by repeating itself.
if [ "$RAW_OK" -eq 1 ]; then
    if exec 8<>"/dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
        # [type=1][len=8 LE][proto u32 LE][resume u32 LE = PLAYER_NONE].
        # Written straight to the fd in three pieces rather than built
        # in a variable: the frame is mostly NUL bytes, and a shell
        # variable cannot hold those — collecting it first sends a
        # short, corrupt frame and tests the length guard by accident.
        send_hello() {
            printf '\x01\x08\x00\x00\x00'          >&8
            printf "\\x$(printf '%02x' "$PROTO")"  >&8
            printf '\x00\x00\x00\x00\x00\x00\x00'  >&8
        }
        send_hello 2>/dev/null || true
        send_hello 2>/dev/null || true
        sleep 2
        exec 8<&- 2>/dev/null || true
    fi
fi

wait "$HOST_PID" 2>/dev/null
HOST_RC=$?
exec 9<&- 2>/dev/null || true

echo "--- server output ---"
sed 's/^/  | /' "$SRVLOG"
echo "---------------------"

if [ "$HOST_RC" -eq 0 ]; then
    pass "server ran its ticks and exited cleanly"
else
    fail "server exited with rc=$HOST_RC"
fi

if grep -q "client joined as player" "$SRVLOG"; then
    pass "client connected over TCP and was given an identity"
else
    fail "server never logged a join"
    sed 's/^/  | /' "$CLILOG"
fi

if grep -q "world installed at tick" "$CLILOG"; then
    pass "client received and installed the server's world"
else
    fail "client never installed a world"
    sed 's/^/  | /' "$CLILOG"
fi

if grep -q "stopping at tick 200" "$SRVLOG"; then
    pass "server kept ticking through the client's departure"
else
    fail "server did not reach its tick target"
fi

if [ "$RAW_OK" -eq 1 ]; then
    if grep -q "never introduced itself" "$SRVLOG"; then
        pass "a connection that never says hello is dropped"
    else
        fail "a silent connection held its slot for the whole run"
    fi

    if grep -q "said hello twice" "$SRVLOG"; then
        pass "a second HELLO on one connection is refused"
    else
        fail "one connection could introduce itself twice"
    fi
fi

if [ -f "$WORLD" ]; then
    pass "server wrote a checkpoint"
    if "$REPLAY_BIN" --replay "$WORLD" >"$WORK/replay.log" 2>&1; then
        pass "the checkpoint replays to the same hash"
    else
        fail "the checkpoint failed to replay:"
        sed 's/^/  | /' "$WORK/replay.log"
    fi
else
    fail "no checkpoint written"
fi

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "HOST SMOKE TEST PASSED"
    exit 0
fi
echo "HOST SMOKE TEST FAILED ($FAILURES check(s))"
exit 1
