#!/usr/bin/env bash
# tests/run.sh -- build and run the headless behaviour tests.
#
# Follows the project's verification convention: link the game's own
# compiled .o files (everything except main.c, which owns SDL_AppMain)
# into a small C program that asserts real behaviour, headless. Requires
# the game to have been built first (cmake --build build).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
builddir="$root/build"
linkfile="$builddir/CMakeFiles/saltmarch.dir/link.txt"
simlib="$builddir/libsaltmarch_sim.a"
netlib="$builddir/libsaltmarch_net.a"
uilib="$builddir/libsaltmarch_ui.a"

if [ ! -f "$linkfile" ] || [ ! -f "$simlib" ]; then
    echo "build objects not found; run: cmake -B build && cmake --build build" >&2
    exit 1
fi

# Client objects (minus main.c, which owns SDL_AppMain) plus the sim
# library, which since MMO_PLAN Phase 6 is a separate static archive.
#
# The object list is read out of CMake's own link line rather than
# globbed off disk: after the Phase 6 split, an incremental build leaves
# the pre-split game.c.o etc. sitting in saltmarch.dir/, and a glob would
# happily link those stale duplicates. link.txt is by definition what the
# current configuration actually builds.
objs=$(tr ' ' '\n' < "$linkfile" | grep '\.c\.o$' | grep -v '/main\.c\.o$' \
       | sed "s|^|$builddir/|")
sdlflags=$(pkg-config --cflags --libs sdl3 sdl3-ttf 2>/dev/null || echo "-lSDL3 -lSDL3_ttf")

status=0
for src in "$root"/tests/test_*.c; do
    name=$(basename "$src" .c)
    echo "=== $name ==="

    # Two tests are deliberately linked WITHOUT SDL, because the link
    # itself is the assertion:
    #   test_headless  -- the sim archive alone (MMO_PLAN Phase 6): the
    #                     simulation is separable from the client.
    #   test_ui_kit    -- sim + UI archives (UI_PLAN Phase 0): layout and
    #                     hit-testing are pure functions that never reach
    #                     for a font metric or a renderer.
    #   test_exchange  -- the same, for the exchange screen (Phase 1):
    #                     it drives the REAL layout and hit-test at goods
    #                     counts the game does not have yet.
    # Handing either of them the client objects and SDL would make the
    # test meaningless -- it would still link the day that stops being
    # true.
    case "$name" in
        test_headless) link_objs="";        link_sdl="" ;;
        test_ui_kit)   link_objs="$uilib";  link_sdl="" ;;
        test_exchange) link_objs="$uilib";  link_sdl="" ;;
        test_hud)      link_objs="$uilib";  link_sdl="" ;;
        test_vitals)   link_objs="$uilib";  link_sdl="" ;;
        test_confirm)  link_objs="$uilib";  link_sdl="" ;;
        test_fx_reject) link_objs="$uilib"; link_sdl="" ;;
        test_intent)   link_objs="$uilib";  link_sdl="" ;;
        test_escrow)   link_objs="$uilib";  link_sdl="" ;;
        test_charter)  link_objs="";        link_sdl="" ;;
        *)             link_objs="$objs $netlib $uilib"; link_sdl="$sdlflags" ;;
    esac

    # shellcheck disable=SC2086
    cc -std=c99 -Wall -Wextra -I"$root/src" "$src" $link_objs "$simlib" \
       $link_sdl -lm -o "$root/build/$name"
    # Headless: no window/audio device needed for these.
    if ! SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$root/build/$name"; then
        status=1
    fi
    echo
done

exit $status
