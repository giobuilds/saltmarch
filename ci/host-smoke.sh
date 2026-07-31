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
#   4. the checkpoint it writes loads in saltmarch_replay -- i.e. the
#      server's world is a file the ordinary tools read, not a private
#      format. Since SERVER.md's log truncation that checkpoint is
#      STATE rather than history, so what is verified is the snapshot's
#      own checksum and stored hash rather than a replay from tick 0;
#      the determinism gate proper runs on --record fixtures, which
#      keep their full logs precisely so it can.
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
        # [type=1][len=44 LE][proto u32][resume u32][account u32]
        # [token 32]. Written straight to the fd in pieces rather than
        # built in a variable: the frame is mostly NUL bytes, and a
        # shell variable cannot hold those — collecting it first sends a
        # short, corrupt frame and tests the length guard by accident.
        #
        # The account id and token are zero, which is "I have no
        # account": this server runs without --accounts, so that is the
        # ordinary case and the probe still tests what it is here for.
        send_hello() {
            printf '\x01\x2c\x00\x00\x00'          >&8
            printf "\\x$(printf '%02x' "$PROTO")"  >&8
            printf '\x00\x00\x00\x00\x00\x00\x00'  >&8
            # account id + 32 zero bytes of token
            for _ in $(seq 1 36); do printf '\x00' >&8; done
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

# ---- authentication (AUTH_PLAN Phase 1) ----------------------
# A second server, this one with an account file and registration
# closed. The assertion is invariant 6: a peer that cannot say who it
# is must be turned away at the handshake, BEFORE any world is sent —
# a peer refused later would already hold every island's stockpile.
#
# XDG_DATA_HOME is redirected so the client writes its token into the
# work directory rather than into whoever is running CI. A test that
# leaves credentials in a developer's home directory is a test that
# has done something worse than fail.
PORT2=$((PORT + 1))
WORLD2="$WORK/auth.smlog"
ACCTS="$WORK/auth.accounts"
SRVLOG2="$WORK/auth-server.log"
CLILOG2="$WORK/auth-client.log"

"$HOST_BIN" --port "$PORT2" --world "$WORLD2" --seed 4242 \
            --accounts "$ACCTS" --registration closed \
            --ticks 120 --checkpoint-seconds 0 >"$SRVLOG2" 2>&1 &
HOST2_PID=$!
sleep 2

if kill -0 "$HOST2_PID" 2>/dev/null; then
    XDG_DATA_HOME="$WORK/pref" SDL_VIDEODRIVER=dummy "$GAME_BIN" \
        --join "127.0.0.1:$PORT2" --as 1 >"$CLILOG2" 2>&1 &
    CLI2_PID=$!
    sleep 4
    kill -TERM "$CLI2_PID" 2>/dev/null
    wait "$CLI2_PID" 2>/dev/null
fi
wait "$HOST2_PID" 2>/dev/null

if grep -q "authenticating against" "$SRVLOG2"; then
    pass "the server authenticates when it has an account file"
else
    fail "the server did not enable authentication"
    sed 's/^/  | /' "$SRVLOG2"
fi

if grep -q "token" "$SRVLOG2"; then
    pass "and minted an account for the world's existing owner, once"
else
    fail "no account was minted for the existing island owner"
fi

if [ -f "$ACCTS" ]; then
    pass "the account sidecar was written"
    if grep -q "^account " "$ACCTS"; then
        pass "...with an account line in it"
    else
        fail "the sidecar has no accounts"
    fi
else
    fail "no account sidecar written"
fi

# --as 1 with no credential: the honour system is what this closes.
if grep -q "registration closed" "$SRVLOG2"; then
    pass "a client with no credential is refused at the handshake"
else
    fail "an unauthenticated client was not refused"
    sed 's/^/  | /' "$SRVLOG2"
fi

if grep -q "world installed at tick" "$CLILOG2"; then
    fail "a refused client received a world anyway"
    sed 's/^/  | /' "$CLILOG2"
else
    pass "...and never received a world"
fi

if [ -f "$WORLD" ]; then
    pass "server wrote a checkpoint"
    if "$REPLAY_BIN" --replay "$WORLD" >"$WORK/replay.log" 2>&1; then
        pass "the checkpoint loads in the ordinary replay tool"
        if grep -q "restored to tick" "$WORK/replay.log"; then
            pass "...as a snapshot, restored rather than replayed"
        else
            fail "the checkpoint was not a snapshot"
            sed 's/^/  | /' "$WORK/replay.log"
        fi
    else
        fail "the checkpoint failed to load:"
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
