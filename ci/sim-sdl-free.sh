#!/usr/bin/env bash
#
# sim-sdl-free.sh -- assert the simulation library really is SDL-free.
#
# MMO_PLAN ground rule 3: anything that will live in the headless sim
# library must compile without SDL. Since Phase 6 that library exists
# (libsaltmarch_sim), and the rule is only worth stating if something
# checks it — a single stray SDL_Log in a sim file is the whole point of
# failure, and it would otherwise be caught much later by whoever next
# tries to build the server.
#
# Two independent checks, because each catches what the other misses:
#
#   1. Source grep over the exact file list CMakeLists.txt declares as
#      SALTMARCH_SIM_SOURCES, plus net.c and the server (everything
#      saltmarch_host links). Fails early, names the file and line.
#   2. Symbol scan of the built archive for undefined SDL_* references.
#      Catches SDL reached indirectly, via a header or a macro, which no
#      grep for "SDL" in the .c files would see. Both archives, since a
#      server that cannot avoid SDL is not a server. Skipped when nm is
#      unavailable (MSVC), where check 1 still applies.
#
# Usage: ci/sim-sdl-free.sh [path-to-libsaltmarch_sim.a]

set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
lib="${1:-$root/build/libsaltmarch_sim.a}"
netlib="$(dirname "$lib")/libsaltmarch_net.a"
uilib="$(dirname "$lib")/libsaltmarch_ui.a"
failures=0

fail() { printf '  FAIL: %s\n' "$1"; failures=$((failures + 1)); }
pass() { printf '  ok:   %s\n' "$1"; }

echo "== SDL-free check (sim, net, server, ui kit) =="

# --- 1. the declared sim sources, straight out of CMakeLists.txt ---
# The sim proper, plus net.c and the server: everything the dedicated
# server links must be SDL-free, or saltmarch_host stops building at all.
sources=$(sed -n '/^set(SALTMARCH_SIM_SOURCES/,/^)/p' "$root/CMakeLists.txt" \
          | grep -o 'src/[A-Za-z0-9_]*\.c')
sources="$sources src/net.c server/saltmarch_host.c"
# The UI kit too (UI_PLAN Phase 0): layout and hit-testing are pure
# functions precisely so headless tests can drive them, and the day one
# of them reaches for a font metric is the day that stops being true.
sources="$sources src/ui_kit.c src/ui_snapshot.c"

if [ -z "$sources" ]; then
    fail "could not read SALTMARCH_SIM_SOURCES out of CMakeLists.txt"
    exit 1
fi

hits=0
for src in $sources; do
    [ -f "$root/$src" ] || { fail "$src is listed but does not exist"; continue; }
    # Comment lines are stripped first: several of these files explain in
    # prose WHY they no longer call SDL_Log or SDL_IOStream, and failing
    # the build over the explanation would be a fine way to get the
    # explanations deleted. Check 2 covers anything this misses.
    if sed -E 's,//.*$,,; /^[[:space:]]*[*]/d; /^[[:space:]]*\/\*/d' "$root/$src" \
       | grep -nE '(SDL3/SDL|\bSDL_[A-Za-z_]+)'; then
        fail "$src references SDL"
        hits=$((hits + 1))
    fi
done
[ "$hits" -eq 0 ] && pass "no SDL in $(echo "$sources" | wc -w | tr -d ' ') sim/net/server/ui sources"

# --- 2. the built archive's undefined symbols ---
if ! command -v nm >/dev/null 2>&1; then
    echo "  skip: nm unavailable — source check only"
elif [ ! -f "$lib" ]; then
    fail "sim library not found at $lib (build first)"
else
    for archive in "$lib" "$netlib" "$uilib"; do
        [ -f "$archive" ] || continue
        undef=$(nm --undefined-only "$archive" 2>/dev/null \
                | grep -o 'SDL_[A-Za-z0-9_]*' | sort -u)
        if [ -n "$undef" ]; then
            fail "$(basename "$archive") needs SDL symbols:"
            printf '        %s\n' $undef
        else
            pass "$(basename "$archive") links no SDL symbols"
        fi
    done
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "SIM, NET, SERVER AND UI KIT ARE SDL-FREE"
    exit 0
fi
echo "SDL-FREE CHECK FAILED ($failures)"
exit 1
