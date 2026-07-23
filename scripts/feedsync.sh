#!/usr/bin/env bash
# feedsync.sh -- out-of-process transport for the shared voyage feed
#                (MMO_PLAN Phase 4).
#
# The game only ever touches two local files: it appends its own
# departures to feed_out.jsonl and reads ghosts from feed_in.jsonl.
# THIS script is the network. The default transport is the dumbest
# thing that can possibly work — a shared folder (Syncthing, NFS, a
# USB stick...) — merged with plain cp/cat. Swap the folder for curl
# against any HTTP endpoint or rsync over ssh without touching the
# game: that swappability is the point of keeping transport out of
# process.
#
# Usage:
#   ./scripts/feedsync.sh <shared-dir> <player-name> [game-dir]
#
#   <shared-dir>   directory every participant can read/write
#   <player-name>  must match SALTMARCH_PLAYER used to launch the game
#   [game-dir]     where feed_out/feed_in.jsonl live (default: cwd)
#
# Every interval: upload our feed_out as <player-name>.jsonl (write to
# a temp name, then mv -- rename-into-place keeps readers from ever
# seeing a half-written file), and concatenate every OTHER player's
# file into feed_in.jsonl (same rename-into-place on the way down).
# The game tolerates a partial trailing line regardless, but not
# handing it one is still politer.

set -euo pipefail

SHARED=${1:?usage: feedsync.sh <shared-dir> <player-name> [game-dir]}
NAME=${2:?usage: feedsync.sh <shared-dir> <player-name> [game-dir]}
GAMEDIR=${3:-.}
INTERVAL=${FEEDSYNC_INTERVAL:-15}

mkdir -p "$SHARED"

echo "feedsync: '$NAME' <-> $SHARED every ${INTERVAL}s (ctrl-C to stop)"

while :; do
    # Up: our departures, atomically.
    if [ -f "$GAMEDIR/feed_out.jsonl" ]; then
        cp "$GAMEDIR/feed_out.jsonl" "$SHARED/.$NAME.tmp"
        mv "$SHARED/.$NAME.tmp" "$SHARED/$NAME.jsonl"
    fi

    # Down: everyone else's, concatenated, atomically.
    tmp="$GAMEDIR/.feed_in.tmp"
    : > "$tmp"
    for f in "$SHARED"/*.jsonl; do
        [ -e "$f" ] || continue                       # empty dir
        [ "$(basename "$f")" = "$NAME.jsonl" ] && continue  # not our own
        cat "$f" >> "$tmp"
    done
    mv "$tmp" "$GAMEDIR/feed_in.jsonl"

    sleep "$INTERVAL"
done
