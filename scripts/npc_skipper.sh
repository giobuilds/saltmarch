#!/usr/bin/env bash
# npc_skipper.sh -- an NPC faction as a feed participant
#                   (MMO_PLAN Phase 4).
#
# Appends scripted voyages to its own file in the shared feed folder on
# a timer. To every client it is indistinguishable from a slow human
# player — which is the architectural claim this script exists to
# prove, and it gives a populated ocean to develop against.
#
# Personality: a tramp freighter working a fixed circuit of the
# archipelago (0 -> 1 -> 3 -> 2 -> 0), one leg per interval, carrying a
# rotating good. Runs forever; ctrl-C to retire the skipper.
#
# Usage:
#   ./scripts/npc_skipper.sh <shared-dir> [name]
#
# The voyage lines match src/feed.h's format exactly. departure_tick is
# 0 for an NPC (it has no sim clock); ghosts are rendered from
# departure_unix_ms, so it is never read.

set -euo pipefail

SHARED=${1:?usage: npc_skipper.sh <shared-dir> [name]}
NAME=${2:-Old-Meridian}
INTERVAL=${NPC_INTERVAL:-45}

mkdir -p "$SHARED"

ID=$(( ($(printf '%s' "$NAME" | cksum | cut -d' ' -f1) % 4000000000) + 1 ))
CIRCUIT=(0 1 3 2)   # the tramp route, as island indices
LEG=0

OUT="$SHARED/$NAME.jsonl"
TMP="$SHARED/.$NAME.tmp"

# Fresh handshake each launch (truncate: an NPC has no history worth
# keeping, and it stops the file growing without bound).
printf '{"hello":%d,"name":"%s"}\n' "$ID" "$NAME" > "$TMP"
mv "$TMP" "$OUT"

echo "npc_skipper: '$NAME' (id $ID) working the circuit in $SHARED every ${INTERVAL}s"

while :; do
    FROM=${CIRCUIT[$(( LEG      % 4 ))]}
    TO=${CIRCUIT[$((  (LEG + 1) % 4 ))]}
    NOW_MS=$(( $(date +%s) * 1000 ))
    CARGO_SLOT=$(( LEG % 6 ))   # rotate through the six goods

    # cargo array: 35 units of the rotating good, 7 slots (RES_COUNT).
    CARGO="["
    for s in 0 1 2 3 4 5 6; do
        [ "$s" -gt 0 ] && CARGO="$CARGO,"
        if [ "$s" -eq "$CARGO_SLOT" ]; then CARGO="${CARGO}35"; else CARGO="${CARGO}0"; fi
    done
    CARGO="$CARGO]"

    {
        cat "$OUT"
        printf '{"player":%d,"ship":0,"from":%d,"to":%d,"departure_tick":0,"cargo":%s,"departure_unix_ms":%d}\n' \
               "$ID" "$FROM" "$TO" "$CARGO" "$NOW_MS"
    } > "$TMP"
    mv "$TMP" "$OUT"

    LEG=$(( LEG + 1 ))
    sleep "$INTERVAL"
done
